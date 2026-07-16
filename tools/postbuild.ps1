[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Elf,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [Parameter(Mandatory = $true)]
    [string]$Objcopy,

    [Parameter(Mandatory = $true)]
    [string]$PlatformPath,

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
$platformRoot = (Resolve-Path -LiteralPath $PlatformPath).Path
$toolKit = Join-Path $platformRoot 'tools\sdk\bin\ci-tool-kit.exe'
$secondCore = Join-Path $platformRoot 'tools\sdk\bin\libbnpu_core_alg_pro_null.a'

foreach ($required in @($toolKit, $secondCore)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing SDK packaging asset: $required. Run tools\rebuild_sdk.ps1 first."
    }
}

$outputFullPath = [System.IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $outputFullPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$stagingRoot = Join-Path $outputDirectory (([System.IO.Path]::GetFileNameWithoutExtension($outputFullPath)) + '.ci13xx')
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
$staging = Join-Path $stagingRoot 'user_code'
New-Item -ItemType Directory -Path $staging | Out-Null

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
    throw "Merged user_code.bin is $mergedImageSize bytes, exceeding the Arduino baseline partition ($MaxUserCodeSize bytes)."
}

Copy-Item -LiteralPath $mergedImage -Destination $outputFullPath -Force
Write-Host "CI13XX user-code image: $outputFullPath ($mergedImageSize / $MaxUserCodeSize bytes)"
