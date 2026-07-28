[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '..\..\CI130X_SDK_ALG_V2.7.14'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\recursos\cwsl'),
    [string]$CiToolKit = (Join-Path $PSScriptRoot 'sdk\bin\ci-tool-kit.exe'),
    [string]$LamePath,
    [ValidateSet('standard', 'cwsl')]
    [string]$Profile = 'cwsl'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-RequiredFile {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Description`: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Invoke-NativeTool {
    param([string]$Executable, [string[]]$Arguments, [string]$Description)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

function Assert-Output {
    param([string]$Path, [long]$Size, [string]$Sha256)
    $file = Get-Item -LiteralPath $Path
    if ($file.Length -ne $Size) {
        throw "Unexpected generated resource size for $Path`: expected $Size, got $($file.Length)"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $Sha256) {
        throw "Unexpected generated resource SHA-256 for $Path`: expected $Sha256, got $actualHash"
    }
}

$sdkPath = (Resolve-Path -LiteralPath $SdkRoot).Path
$firmwareRoot = Join-Path $sdkPath 'projects\offline_asr_alg_pro_sample\firmware'
if (-not (Test-Path -LiteralPath $firmwareRoot -PathType Container)) {
    throw "The SDK does not contain the official V2.7.14 offline-ASR sample firmware tree: $firmwareRoot"
}
$ciToolPath = Resolve-RequiredFile -Path $CiToolKit -Description 'vendor ci-tool-kit.exe'

if (-not $LamePath) {
    $LamePath = @(
        (Join-Path $sdkPath 'tools\lame.exe'),
        (Join-Path (Split-Path -Parent $sdkPath) 'CI13XX_SDK_ASR_ALG_V2.7.12\tools\lame.exe')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $LamePath) {
    throw 'Vendor lame.exe was not found. Pass -LamePath from a complete CI130X SDK tools directory.'
}
$lameExe = Resolve-RequiredFile -Path $LamePath -Description 'vendor lame.exe'
[void](Resolve-RequiredFile -Path (Join-Path (Split-Path -Parent $lameExe) 'libmp3lame.dll') `
    -Description 'vendor libmp3lame.dll')

$asrSource0 = Resolve-RequiredFile -Path (Join-Path $firmwareRoot 'asr\[0]asr_chinese_CI1306_V00874.dat') `
    -Description 'official V2.7.14 offline-ASR model 0'
$asrSource1 = Resolve-RequiredFile -Path (Join-Path $firmwareRoot 'asr\[1]asr_chinese_CI1306_V00874.dat') `
    -Description 'official V2.7.14 offline-ASR model 1'
$dnnSource = Resolve-RequiredFile -Path (Join-Path $firmwareRoot 'dnn\[0]G3-NLP-CH-S-PRO-V00874.fefixbin458') `
    -Description 'official V2.7.14 offline-ASR DNN model'
$cmdInfoSource = Resolve-RequiredFile -Path (Join-Path $firmwareRoot 'user_file\cmd_info\[60000]{cmd_info}.xlsx.bin') `
    -Description 'official V2.7.14 offline-ASR cmd_info resource'
$voiceSource = Join-Path $firmwareRoot 'voice\src'
$voiceSources = @(Get-ChildItem -LiteralPath $voiceSource -File | Sort-Object Name)
if ($voiceSources.Count -ne 130) {
    throw "Expected 130 official V2.7.14 voice source files, found $($voiceSources.Count)."
}

$tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$stagingRoot = Join-Path $tempRoot ("ci130x-{0}-resources-{1}-{2}" -f $Profile, $PID, [guid]::NewGuid().ToString('N'))
$stagingRoot = [System.IO.Path]::GetFullPath($stagingRoot)
if (-not $stagingRoot.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a staging directory outside TEMP: $stagingRoot"
}

try {
    $asrWork = Join-Path $stagingRoot 'asr'
    $dnnWork = Join-Path $stagingRoot 'dnn'
    $userFileWork = Join-Path $stagingRoot 'user_file'
    $voiceWork = Join-Path $stagingRoot 'voice'
    foreach ($directory in @($asrWork, $dnnWork, $userFileWork, $voiceWork)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    Copy-Item -LiteralPath $asrSource0, $asrSource1 -Destination $asrWork
    Copy-Item -LiteralPath $dnnSource -Destination $dnnWork
    Copy-Item -LiteralPath $cmdInfoSource -Destination $userFileWork
    Invoke-NativeTool $ciToolPath @('merge', 'asr-file', '-i', $asrWork) "$Profile asr.bin generation"
    Invoke-NativeTool $ciToolPath @('merge', 'nn-file', '-i', $dnnWork, '-a', $asrWork) "$Profile dnn.bin generation"
    Invoke-NativeTool $ciToolPath @('merge', 'user-file', '-i', $userFileWork) "$Profile user_file.bin generation"

    foreach ($source in $voiceSources) {
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($source.Name)
        if ($source.Name.StartsWith('[65535]')) {
            Copy-Item -LiteralPath $source.FullName -Destination (Join-Path $voiceWork ($baseName + '.txt'))
        }
        else {
            Invoke-NativeTool $lameExe `
                @('--silent', '--cbr', '-b16', '-t', '--resample', '16000', $source.FullName,
                    (Join-Path $voiceWork ($baseName + '.mp3'))) `
                "MP3 conversion for $($source.Name)"
        }
    }
    Invoke-NativeTool $ciToolPath @('ID3-editor', '--input-dir', $voiceWork) "$Profile voice ID tagging"
    Invoke-NativeTool $ciToolPath @('merge', 'user-file', '-i', $voiceWork, '-o', $voiceWork) "$Profile voice.bin generation"

    $generated = [ordered]@{
        'asr.bin' = @((Join-Path $asrWork 'asr.bin'), 20038, '08624F47C63858A4F57F31BA7E2CFA3C9E981B03C868047817FFDA6522082B3E')
        'dnn.bin' = @((Join-Path $dnnWork 'dnn.bin'), 1410376, '18F90809B641B02D3F2E9E10DC3D496FFE720D370410EC048B89691B56C19991')
        'voice.bin' = @((Join-Path $voiceWork 'voice.bin'), 379741, 'C1948B068DEEBB58A455A27CA4B9941D331E613DAF2BAEB99422587EF9D58318')
        'user_file.bin' = @((Join-Path $userFileWork 'user_file.bin'), 12321, '6359ADF0DA19A774BBE1E094BC2AC94E67272096A8C10FA9B0565514127661FD')
    }
    $outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    foreach ($item in $generated.GetEnumerator()) {
        Assert-Output -Path $item.Value[0] -Size $item.Value[1] -Sha256 $item.Value[2]
        $destination = Join-Path $outputRoot $item.Key
        Copy-Item -LiteralPath $item.Value[0] -Destination $destination -Force
        Write-Host ("Generated {0}: {1} bytes, SHA-256 {2}" -f $destination, $item.Value[1], $item.Value[2])
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        profile = $Profile
        mode = 'vendor-sample'
        source = 'CI130X_SDK_ALG_V2.7.14/projects/offline_asr_alg_pro_sample/firmware'
        vendorSpokenControlIdsIncluded = $true
        resources = [ordered]@{
            'asr.bin' = [ordered]@{ size = 20038; sha256 = '08624f47c63858a4f57f31ba7e2cfa3c9e981b03c868047817ffda6522082b3e' }
            'dnn.bin' = [ordered]@{ size = 1410376; sha256 = '18f90809b641b02d3f2e9e10dc3d496ffe720d370410ec048b89691b56c19991' }
            'voice.bin' = [ordered]@{ size = 379741; sha256 = 'c1948b068deebb58a455a27ca4b9941d331e613daf2baeb99422587ef9d58318' }
            'user_file.bin' = [ordered]@{ size = 12321; sha256 = '6359adf0da19a774bbe1e094bc2ac94e67272096a8c10fa9b0565514127661fd' }
        }
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 4
    $manifestPath = Join-Path $outputRoot 'manifest.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $manifestPath,
        $manifestJson + [Environment]::NewLine,
        $utf8NoBom)
    Write-Host "Generated $Profile resource manifest: $manifestPath"
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        $verifiedStagingRoot = [System.IO.Path]::GetFullPath($stagingRoot)
        if ($verifiedStagingRoot.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verifiedStagingRoot -Recurse -Force
        }
    }
}

Write-Host "$Profile resources match CI130X SDK ALG V2.7.14 offline_asr_alg_pro_sample outputs."
