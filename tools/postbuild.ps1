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
    [string]$Nm,

    [Parameter(Mandatory = $true)]
    [string]$PlatformPath,

    [Parameter(Mandatory = $true)]
    [string]$CitoolCli,

    [Parameter(Mandatory = $true)]
    [string]$ProjectResources,

    [Parameter(Mandatory = $true)]
    [ValidateSet('ci1302', 'ci1303', 'ci1306')]
    [string]$Chip
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-ElfSymbolValue {
    param(
        [hashtable]$Symbols,
        [string]$Name
    )

    if (-not $Symbols.ContainsKey($Name)) {
        throw "ELF does not export required CI13XX memory symbol: $Name"
    }
    return [uint64]$Symbols[$Name]
}

function Format-ByteSize {
    param([uint64]$Size)
    return ('{0} bytes ({1:N2} KiB)' -f $Size, ($Size / 1KB))
}

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
$nmCandidate = $Nm
if (-not (Test-Path -LiteralPath $nmCandidate -PathType Leaf) -and
    (Test-Path -LiteralPath ($nmCandidate + '.exe') -PathType Leaf)) {
    $nmCandidate += '.exe'
}
$nmPath = (Resolve-Path -LiteralPath $nmCandidate).Path
$citoolCandidate = $CitoolCli
if (-not (Test-Path -LiteralPath $citoolCandidate -PathType Leaf) -and
    (Test-Path -LiteralPath ($citoolCandidate + '.exe') -PathType Leaf)) {
    $citoolCandidate += '.exe'
}
$citoolPath = (Resolve-Path -LiteralPath $citoolCandidate).Path
$platformRoot = (Resolve-Path -LiteralPath $PlatformPath).Path
$toolKit = Join-Path $platformRoot 'tools\sdk\bin\ci-tool-kit.exe'
$secondCore = Join-Path $platformRoot 'tools\sdk\bin\libbnpu_core_alg_pro_null.a'
$projectResourcesRoot = (Resolve-Path -LiteralPath $ProjectResources).Path

foreach ($required in @($toolKit, $secondCore, $citoolPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing post-build packaging asset: $required"
    }
}

$nmOutput = @(& $nmPath -n --defined-only $elfPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "nm failed with exit code $LASTEXITCODE`n$($nmOutput -join [Environment]::NewLine)"
}
$elfSymbols = @{}
foreach ($line in $nmOutput) {
    $text = [string]$line
    if ($text -match '^\s*([0-9A-Fa-f]+)\s+\S\s+(\S+)\s*$') {
        $elfSymbols[$Matches[2]] = [Convert]::ToUInt64($Matches[1], 16)
    }
}

$sramStart = Get-ElfSymbolValue $elfSymbols 'SRAM_START_ADDR'
$startAddress = Get-ElfSymbolValue $elfSymbols 'START_ADDR'
$sramEnd = Get-ElfSymbolValue $elfSymbols 'SRAM_END_ADDR'
$bssStart = Get-ElfSymbolValue $elfSymbols '__bss_start'
$bssEnd = Get-ElfSymbolValue $elfSymbols '__bss_end'
$stackSize = Get-ElfSymbolValue $elfSymbols 'STACK_SIZE'
$stackPointer = Get-ElfSymbolValue $elfSymbols '_sp'
$freeRtosHeapStart = Get-ElfSymbolValue $elfSymbols '__FREERTOSHEAP_START'
$freeRtosHeapEnd = Get-ElfSymbolValue $elfSymbols '__FREERTOSHEAP_END'
$configuredFreeRtosHeapSize = Get-ElfSymbolValue $elfSymbols 'SYS_HEAP_SIZE'
$cHeapStart = Get-ElfSymbolValue $elfSymbols 'heap_start'
$cHeapEnd = Get-ElfSymbolValue $elfSymbols 'heap_end'
$minimumCHeapSize = Get-ElfSymbolValue $elfSymbols 'CI_MIN_C_HEAP_SIZE'

if ($sramStart -ne $startAddress -or $sramStart -ge $sramEnd) {
    throw 'Invalid CI13XX host SRAM start/end symbols in ELF.'
}
if ($bssStart -lt $sramStart -or $bssEnd -lt $bssStart) {
    throw 'Invalid CI13XX BSS range in ELF.'
}
if ($stackPointer -ne $freeRtosHeapStart -or $freeRtosHeapStart -lt $stackSize) {
    throw 'Invalid CI13XX stack/FreeRTOS heap boundary in ELF.'
}
$staticEnd = $freeRtosHeapStart - $stackSize
# The vendor script exports __bss_end after an ALIGN(8) expression that can be
# up to seven bytes beyond the zero-length .no_init/.stack output-section
# boundary reported by ld.  Accept only that known alignment skew.
if ($staticEnd -lt $sramStart -or $bssStart -gt $staticEnd -or $bssEnd -gt ($staticEnd + 7)) {
    throw 'Invalid CI13XX static/BSS/no-init boundary in ELF.'
}
if ($freeRtosHeapEnd -lt $freeRtosHeapStart -or
    ($freeRtosHeapEnd - $freeRtosHeapStart) -ne $configuredFreeRtosHeapSize) {
    throw 'Invalid CI13XX fixed FreeRTOS heap range in ELF.'
}
if ($cHeapStart -ne $freeRtosHeapEnd -or $cHeapEnd -ne $sramEnd -or $cHeapEnd -lt $cHeapStart) {
    throw 'Invalid CI13XX C/newlib heap range in ELF.'
}

$loadableImageSize = $bssStart - $sramStart
$staticFootprint = $staticEnd - $sramStart
$zeroInitFootprint = $staticFootprint - $loadableImageSize
$freeRtosHeapSize = $freeRtosHeapEnd - $freeRtosHeapStart
$cHeapSize = $cHeapEnd - $cHeapStart
if ($cHeapSize -lt $minimumCHeapSize) {
    throw "ELF leaves $(Format-ByteSize $cHeapSize) for the C/newlib heap; the selected board option requires at least $(Format-ByteSize $minimumCHeapSize)."
}

Write-Host ('CI13XX SRAM layout: loadable={0}, BSS/no-init/alignment={1}, stack={2}, FreeRTOS heap={3}, C/newlib heap={4} (minimum {5})' -f `
    (Format-ByteSize $loadableImageSize),
    (Format-ByteSize $zeroInitFootprint),
    (Format-ByteSize $stackSize),
    (Format-ByteSize $freeRtosHeapSize),
    (Format-ByteSize $cHeapSize),
    (Format-ByteSize $minimumCHeapSize))
if ($cHeapSize -lt 32KB) {
    Write-Warning 'Less than 32 KiB remains for C/newlib malloc. Avoid allocation-heavy Arduino String, ASR decoder, or audio workloads, or select a larger heap reserve.'
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

$hostImage = Join-Path $staging '[0]code.bin'
$algorithmImage = Join-Path $staging '[1]code.bin'

& $objcopyPath -O binary $elfPath $hostImage
if ($LASTEXITCODE -ne 0) {
    throw "objcopy failed with exit code $LASTEXITCODE"
}
$hostImageSize = (Get-Item -LiteralPath $hostImage).Length
if ([uint64]$hostImageSize -ne $loadableImageSize) {
    throw "objcopy host image is $hostImageSize bytes, but the ELF loadable SRAM range is $loadableImageSize bytes."
}
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

Copy-Item -LiteralPath $mergedImage -Destination $outputFullPath -Force
Write-Host "CI13XX user-code image: $outputFullPath ($mergedImageSize bytes; Flash capacity is calculated from all final partition images)"

& $citoolPath compose `
    --chip $Chip `
    --user-code $outputFullPath `
    --asr $resourceFiles.ASR `
    --dnn $resourceFiles.DNN `
    --voice $resourceFiles.Voice `
    --user-file $resourceFiles.UserFile `
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
