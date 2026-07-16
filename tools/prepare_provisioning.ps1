[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FirmwareDirectory,

    [Parameter(Mandatory = $true)]
    [string]$UserCode,

    [string]$SdkPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [ValidateSet('ci1306', 'ci1302', 'ci1303')]
    [string]$Variant = 'ci1306',

    [string]$HardwareVersion,

    [switch]$LaunchTool
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'CI13XX provisioning preparation requires 64-bit Windows.'
}

if ([string]::IsNullOrWhiteSpace($SdkPath)) {
    $SdkPath = Join-Path $PSScriptRoot '..\..\CI13XX_SDK_ASR_ALG_V2.7.12'
}
$sdkRoot = (Resolve-Path -LiteralPath $SdkPath).Path
$packTool = Join-Path $sdkRoot 'tools\PACK_UPDATE_TOOL.exe'
if (-not (Test-Path -LiteralPath $packTool -PathType Leaf)) {
    throw "Missing official PACK_UPDATE_TOOL.exe: $packTool"
}

$firmwareRoot = (Resolve-Path -LiteralPath $FirmwareDirectory).Path
$userCodePath = (Resolve-Path -LiteralPath $UserCode).Path
$files = [ordered]@{
    User = $userCodePath
    ASR = Join-Path $firmwareRoot 'asr\asr.bin'
    DNN = Join-Path $firmwareRoot 'dnn\dnn.bin'
    Voice = Join-Path $firmwareRoot 'voice\voice.bin'
    UserFile = Join-Path $firmwareRoot 'user_file\user_file.bin'
}
foreach ($entry in $files.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw "Missing $($entry.Key) partition image: $($entry.Value). Run the upstream partition merge step first."
    }
}

function Get-FlashAlignedSize {
    param([long]$Length)
    return [long]([Math]::Ceiling($Length / 4096.0) * 4096)
}

# V2.7.12 components/ota/flash_update.h defines USERCODE_MAX_SIZE as 448 KiB.
$userCodePartitionSize = 448KB
$userCodeSize = (Get-Item -LiteralPath $userCodePath).Length
if ($userCodeSize -gt $userCodePartitionSize) {
    throw "User-code container is $userCodeSize bytes; the Arduino baseline maximum is $userCodePartitionSize bytes."
}

$profiles = @{
    ci1302 = [ordered]@{ board = 'CI-D02GS02S'; chip = 'CI1302'; flashSize = 2MB }
    ci1303 = [ordered]@{ board = 'CI-D03GS02S'; chip = 'CI1303'; flashSize = 4MB }
    ci1306 = [ordered]@{ board = 'CI-D06GT01D'; chip = 'CI1306'; flashSize = 4MB }
}
$profile = $profiles[$Variant]
if ([string]::IsNullOrWhiteSpace($HardwareVersion)) {
    if ($Variant -eq 'ci1306') {
        $HardwareVersion = '2.0.0'
    }
    else {
        throw "Pass -HardwareVersion with the actual $($profile.board) hardware version from the product definition."
    }
}

$partitionFiles = @()
foreach ($entry in $files.GetEnumerator()) {
    $item = Get-Item -LiteralPath $entry.Value
    $partitionFiles += [ordered]@{
        role = $entry.Key
        path = $item.FullName
        size = $item.Length
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        reservedSize = if ($entry.Key -eq 'User') {
            $userCodePartitionSize
        } else {
            Get-FlashAlignedSize $item.Length
        }
    }
}

$manifest = [ordered]@{
    sdk = 'CI13XX_SDK_ASR_ALG_V2.7.12'
    arduinoVariant = $Variant
    board = $profile.board
    chip = $profile.chip
    flashSize = $profile.flashSize
    firmwareFormat = 'FW_V2'
    factoryId = 100
    productId = 100
    hardwareName = $profile.board
    hardwareVersion = $HardwareVersion
    firmwareName = 'CI13XX_Arduino'
    firmwareVersion = '0.0.1'
    partitionVersion = 100
    userCodePartitionSize = $userCodePartitionSize
    code2Enabled = $false
    nvDataReservedSize = 16KB
    generatedAt = (Get-Date).ToUniversalTime().ToString('o')
    inputs = $partitionFiles
}

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$manifestPath = Join-Path $outputRoot 'provisioning-manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Provisioning manifest: $manifestPath"
Write-Host "Open PACK_UPDATE_TOOL and select: CI130X series -> $($profile.chip) -> Firmware packaging."
Write-Host "Use every metadata value from the manifest, FW_V2, $($profile.flashSize / 1MB) MB flash, Code2 disabled, and these inputs/reservations:"
foreach ($entry in $partitionFiles) {
    Write-Host ("  {0,-8} 0x{1:X} bytes reserved  {2}" -f $entry.role, $entry.reservedSize, $entry.path)
}
Write-Host 'After packaging, record the full firmware SHA-256 and flash it once with the tool firmware-upgrade page.'

if ($LaunchTool) {
    Start-Process -FilePath $packTool -WorkingDirectory (Split-Path -Parent $packTool) -WindowStyle Normal
}
