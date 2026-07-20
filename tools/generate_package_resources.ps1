[CmdletBinding()]
param(
    [string]$SourceFirmware = (Join-Path $PSScriptRoot '..\..\CI13XX_SDK_ASR_ALG_V2.7.12\external\firmware_Reference\tts\firmware\Firmware_V2.0.0.bin'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\recursos')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Crc16Ccitt {
    param([byte[]]$Bytes)

    [uint16]$crc = 0
    foreach ($value in $Bytes) {
        $crc = [uint16]($crc -bxor ([uint16]$value -shl 8))
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = [uint16]((([uint32]$crc -shl 1) -bxor 0x1021) -band 0xFFFF)
            }
            else {
                $crc = [uint16](([uint32]$crc -shl 1) -band 0xFFFF)
            }
        }
    }
    return $crc
}

$sourcePath = (Resolve-Path -LiteralPath $SourceFirmware).Path
$firmware = [System.IO.File]::ReadAllBytes($sourcePath)
$tableOffset = 0x2000
$tableSize = 278
$entryOffset = 166
$entrySize = 17

if ($firmware.Length -lt ($tableOffset + $tableSize)) {
    throw "Source firmware is too small for a V2 partition table: $sourcePath"
}
if ($firmware[$tableOffset + 161] -ne 2) {
    throw "Source firmware is not format V2: $sourcePath"
}

[uint16]$checksum = 0
for ($index = 0; $index -lt 276; $index++) {
    $checksum = [uint16](($checksum + $firmware[$tableOffset + $index]) -band 0xFFFF)
}
$storedChecksum = [BitConverter]::ToUInt16($firmware, $tableOffset + 276)
if ($checksum -ne $storedChecksum) {
    throw ("Source partition-table checksum mismatch: stored 0x{0:X4}, calculated 0x{1:X4}" -f $storedChecksum, $checksum)
}

$resources = [ordered]@{
    'asr.bin' = 2
    'dnn.bin' = 3
    'voice.bin' = 4
    'user_file.bin' = 5
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

foreach ($resource in $resources.GetEnumerator()) {
    $entry = $tableOffset + $entryOffset + ($resource.Value * $entrySize)
    $offset = [BitConverter]::ToUInt32($firmware, $entry + 4)
    $size = [BitConverter]::ToUInt32($firmware, $entry + 8)
    $storedCrc = [BitConverter]::ToUInt32($firmware, $entry + 12)
    $end = [uint64]$offset + [uint64]$size
    if ($offset -eq [uint32]::MaxValue -or $size -eq 0 -or $end -gt $firmware.Length) {
        throw "Invalid $($resource.Key) partition range in $sourcePath"
    }

    $bytes = New-Object byte[] $size
    [Array]::Copy($firmware, [long]$offset, $bytes, 0, [long]$size)
    $actualCrc = Get-Crc16Ccitt -Bytes $bytes
    if ([uint32]$actualCrc -ne $storedCrc) {
        throw ("{0} CRC mismatch: stored 0x{1:X4}, calculated 0x{2:X4}" -f $resource.Key, $storedCrc, $actualCrc)
    }

    $destination = Join-Path $outputRoot $resource.Key
    [System.IO.File]::WriteAllBytes($destination, $bytes)
    Write-Host ("Generated {0}: {1} bytes, CRC16 0x{2:X4}" -f $destination, $bytes.Length, $actualCrc)
}

Write-Host 'The full firmware template is not copied; citool-cli embeds the validated CI130X FW_V2 Bootloader.'
