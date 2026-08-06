[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Elf,

    [Parameter(Mandatory = $true)]
    [string]$Source,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [Parameter(Mandatory = $true)]
    [string]$FirmwareOutput,

    [Parameter(Mandatory = $true)]
    [string]$Objcopy,

    [Parameter(Mandatory = $true)]
    [string]$PlatformPath,

    [Parameter(Mandatory = $true)]
    [string]$CitoolCli,

    [Parameter(Mandatory = $true)]
    [string]$ServiceUrl,

    [Parameter(Mandatory = $true)]
    [string]$ProjectResources,

    [Parameter(Mandatory = $true)]
    [ValidateSet('ci1302', 'ci1303', 'ci1306')]
    [string]$Chip,

    [Parameter(Mandatory = $true)]
    [ValidateSet('aec', 'null', 'cwsl_aec', 'cwsl')]
    [string]$Algorithm,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$MaxUserCodeSize
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-ArduinoSource {
    param([Parameter(Mandatory = $true)][string]$Path)

    $candidates = [System.Collections.Generic.List[string]]::new()
    $candidates.Add($Path)
    if (-not [System.IO.Path]::HasExtension($Path)) {
        $candidates.Add($Path + '.ino')
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Arduino source file not found. Tried: $($candidates -join ', ')"
}

function Get-ArduinoAssetRoot {
    param([Parameter(Mandatory = $true)][string]$SourcePath)

    $sourceDirectory = Split-Path -Parent $SourcePath
    $temporaryDirectory = Split-Path -Parent $sourceDirectory
    if ((Split-Path -Leaf $sourceDirectory) -ieq 'sketch' -and
        (Split-Path -Leaf $temporaryDirectory) -ieq '.temp') {
        return (Split-Path -Parent $temporaryDirectory)
    }
    return $sourceDirectory
}

if ($env:OS -ne 'Windows_NT') {
    throw 'CI13XX post-build packaging is currently supported on Windows only.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'CI13XX post-build packaging requires 64-bit Windows because ci-tool-kit.exe is x64.'
}

$elfPath = (Resolve-Path -LiteralPath $Elf).Path
$sourcePath = Resolve-ArduinoSource -Path $Source
$assetRoot = Get-ArduinoAssetRoot -SourcePath $sourcePath
$objcopyCandidate = $Objcopy
if (-not (Test-Path -LiteralPath $objcopyCandidate -PathType Leaf) -and
    (Test-Path -LiteralPath ($objcopyCandidate + '.exe') -PathType Leaf)) {
    $objcopyCandidate += '.exe'
}
$objcopyPath = (Resolve-Path -LiteralPath $objcopyCandidate).Path
$citoolCandidate = $CitoolCli
if (-not (Test-Path -LiteralPath $citoolCandidate -PathType Leaf) -and
    (Test-Path -LiteralPath ($citoolCandidate + '.exe') -PathType Leaf)) {
    $citoolCandidate += '.exe'
}
$citoolPath = (Resolve-Path -LiteralPath $citoolCandidate).Path
$platformRoot = (Resolve-Path -LiteralPath $PlatformPath).Path
$toolKit = Join-Path $platformRoot 'tools\sdk\bin\ci-tool-kit.exe'
$secondCore = Join-Path $platformRoot ("tools\sdk\bin\libbnpu_core_alg_pro_{0}.a" -f $Algorithm)
$mergeUserFileEntries = Join-Path $platformRoot 'tools\merge_user_file_entries.ps1'
$projectResourcesBase = (Resolve-Path -LiteralPath $ProjectResources).Path
$projectResourcesRoot = if ($Algorithm -in @('cwsl_aec', 'cwsl')) {
    (Resolve-Path -LiteralPath (Join-Path $projectResourcesBase 'cwsl')).Path
}
else {
    $projectResourcesBase
}

foreach ($required in @($toolKit, $secondCore, $mergeUserFileEntries, $citoolPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing post-build packaging asset: $required"
    }
}
$resourceFiles = [ordered]@{
    ASR = Join-Path $projectResourcesRoot 'asr.bin'
    DNN = Join-Path $projectResourcesRoot 'dnn.bin'
    Voice = Join-Path $projectResourcesRoot 'voice.bin'
    UserFile = Join-Path $projectResourcesRoot 'user_file.bin'
}
foreach ($resource in $resourceFiles.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $resource.Value -PathType Leaf)) {
        throw "Missing project $Algorithm profile $($resource.Key) resource: $($resource.Value)"
    }
}

$outputFullPath = [System.IO.Path]::GetFullPath($Output)
$firmwareOutputFullPath = [System.IO.Path]::GetFullPath($FirmwareOutput)
if ($outputFullPath -eq $firmwareOutputFullPath) {
    throw 'User-code and complete-firmware output paths must be different.'
}
$outputDirectory = Split-Path -Parent $outputFullPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $firmwareOutputFullPath) | Out-Null

$stagingRoot = Join-Path $outputDirectory (([System.IO.Path]::GetFileNameWithoutExtension($outputFullPath)) + '.ci13xx')
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
$staging = Join-Path $stagingRoot 'user_code'
New-Item -ItemType Directory -Path $staging | Out-Null

$resourceMacroPattern = '^\s*#define\s+(?:VOICEMP3|VOICE|COMMAND)[0-9]+\s+'
$hasResourceMacros = [bool](Select-String `
        -LiteralPath $sourcePath `
        -Pattern $resourceMacroPattern `
        -Encoding UTF8 `
        -Quiet)
if ($hasResourceMacros) {
    $generatedResources = Join-Path $stagingRoot 'generated_resources'
    New-Item -ItemType Directory -Path $generatedResources | Out-Null
    foreach ($resource in $resourceFiles.GetEnumerator()) {
        Copy-Item -LiteralPath $resource.Value -Destination (Join-Path $generatedResources ([System.IO.Path]::GetFileName($resource.Value)))
    }

    Write-Host "CI13XX source resource macros found; generating resources through ci-service."
    & $citoolPath generate `
        --source $sourcePath `
        --asset-root $assetRoot `
        --service-url $ServiceUrl `
        --chip $Chip `
        --output $generatedResources
    if ($LASTEXITCODE -ne 0) {
        throw "citool-cli generate failed with exit code $LASTEXITCODE"
    }

    $resourceFiles = [ordered]@{
        ASR = Join-Path $generatedResources 'asr.bin'
        DNN = Join-Path $generatedResources 'dnn.bin'
        Voice = Join-Path $generatedResources 'voice.bin'
        UserFile = Join-Path $generatedResources 'user_file.bin'
    }
    foreach ($resource in $resourceFiles.GetEnumerator()) {
        if (-not (Test-Path -LiteralPath $resource.Value -PathType Leaf)) {
            throw "citool-cli generate did not create the expected $($resource.Key) resource: $($resource.Value)"
        }
    }
}
else {
    Write-Host 'CI13XX source resource macros not found; using the sketch resource set.'
}

$effectiveUserFile = $resourceFiles.UserFile
$userFileEntries = Join-Path $projectResourcesRoot 'user_file_entries'
if (Test-Path -LiteralPath $userFileEntries) {
    if (-not (Test-Path -LiteralPath $userFileEntries -PathType Container)) {
        throw "Project user_file_entries path is not a directory: $userFileEntries"
    }
    $effectiveUserFile = Join-Path $stagingRoot 'user_file.bin'
    & $mergeUserFileEntries `
        -BaseUserFile $resourceFiles.UserFile `
        -EntriesDirectory $userFileEntries `
        -Output $effectiveUserFile
}

$hostImage = Join-Path $staging '[0]code.bin'
$algorithmImage = Join-Path $staging '[1]code.bin'

& $objcopyPath -O binary $elfPath $hostImage
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed with exit code $LASTEXITCODE"
}
$hostImageSize = (Get-Item -LiteralPath $hostImage).Length
Write-Host "CI13XX host image [0]code.bin: $hostImageSize bytes"

Copy-Item -LiteralPath $secondCore -Destination $algorithmImage -Force

& $toolKit merge user-file -i $staging
if ($LASTEXITCODE -ne 0) {
    throw "ci-tool-kit merge user-file failed with exit code $LASTEXITCODE"
}

$mergedImage = Join-Path $staging 'user_code.bin'
if (-not (Test-Path -LiteralPath $mergedImage -PathType Leaf)) {
    throw "ci-tool-kit did not create the expected image: $mergedImage"
}
$mergedImageSize = (Get-Item -LiteralPath $mergedImage).Length
if ($mergedImageSize -gt $MaxUserCodeSize) {
    throw "Merged user_code.bin is $mergedImageSize bytes, exceeding the vendor user-code/SRAM limit ($MaxUserCodeSize bytes)."
}

Copy-Item -LiteralPath $mergedImage -Destination $outputFullPath -Force
Write-Host "CI13XX user-code image: $outputFullPath ($mergedImageSize / $MaxUserCodeSize bytes)"

& $citoolPath compose `
    --chip $Chip `
    --user-code $outputFullPath `
    --user-code-capacity $MaxUserCodeSize `
    --asr $resourceFiles.ASR `
    --dnn $resourceFiles.DNN `
    --voice $resourceFiles.Voice `
    --user-file $effectiveUserFile `
    --output $firmwareOutputFullPath `
    --force
if ($LASTEXITCODE -ne 0) {
    throw "citool-cli compose failed with exit code $LASTEXITCODE"
}

& $citoolPath inspect $firmwareOutputFullPath
if ($LASTEXITCODE -ne 0) {
    throw "citool-cli inspect failed with exit code $LASTEXITCODE"
}
Write-Host "CI13XX complete firmware: $firmwareOutputFullPath"
