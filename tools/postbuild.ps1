[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Elf,

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
    [string]$ProjectResources,

    [Parameter(Mandatory = $true)]
    [ValidateSet('ci1302', 'ci1303', 'ci1306')]
    [string]$Chip,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [long]::MaxValue)]
    [long]$MaxUserCodeSize
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'CI13XX post-build packaging is currently supported on Windows only.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'CI13XX post-build packaging requires 64-bit Windows because ci-tool-kit.exe is x64.'
}

$elfPath = (Resolve-Path -LiteralPath $Elf).Path
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
$secondCore = Join-Path $platformRoot 'tools\sdk\bin\libbnpu_core_alg_pro_null.a'
$mergeUserFileEntries = Join-Path $platformRoot 'tools\merge_user_file_entries.ps1'
$projectResourcesRoot = (Resolve-Path -LiteralPath $ProjectResources).Path

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
        throw "Missing project $($resource.Key) resource: $($resource.Value)"
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
