[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BaseUserFile,

    [Parameter(Mandatory = $true)]
    [string]$EntriesDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Output
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$headerSize = 2
$entrySize = 10
$alignment = 16

function Get-AlignedOffset {
    param([Parameter(Mandatory = $true)][long]$Value)

    return [long]([Math]::Floor(($Value + $alignment - 1) / $alignment) * $alignment)
}

function Get-UserFileEntries {
    param([Parameter(Mandatory = $true)][byte[]]$Buffer)

    if ($Buffer.Length -lt $headerSize) {
        throw 'Base user_file.bin is too small.'
    }

    $count = [BitConverter]::ToUInt16($Buffer, 0)
    $tableEnd = [long]$headerSize + ([long]$count * $entrySize)
    if ($tableEnd -gt $Buffer.LongLength) {
        throw 'Base user_file.bin has a truncated entry table.'
    }

    $entries = [System.Collections.ArrayList]::new()
    $seenIds = @{}
    for ($index = 0; $index -lt $count; $index++) {
        $entryOffset = $headerSize + ($index * $entrySize)
        $id = [BitConverter]::ToUInt16($Buffer, $entryOffset)
        $dataOffset = [BitConverter]::ToUInt32($Buffer, $entryOffset + 2)
        $dataSize = [BitConverter]::ToUInt32($Buffer, $entryOffset + 6)
        $idKey = [string]$id

        if ($seenIds.ContainsKey($idKey)) {
            throw "Base user_file.bin contains duplicate ID $id."
        }
        $dataEnd = [uint64]$dataOffset + [uint64]$dataSize
        if ([uint64]$dataOffset -lt [uint64]$tableEnd -or $dataEnd -gt [uint64]$Buffer.LongLength) {
            throw "Base user_file.bin entry $id points outside the container."
        }
        if ($dataSize -gt [int]::MaxValue) {
            throw "Base user_file.bin entry $id is too large to process."
        }

        $data = [byte[]]::new([int]$dataSize)
        if ($dataSize -ne 0) {
            [Array]::Copy($Buffer, [long]$dataOffset, $data, 0L, [long]$dataSize)
        }
        [void]$entries.Add([PSCustomObject]@{
            Id = [int]$id
            Data = $data
            Source = 'base user_file.bin'
        })
        $seenIds[$idKey] = $true
    }

    return ,$entries
}

$basePath = (Resolve-Path -LiteralPath $BaseUserFile -ErrorAction Stop).Path
$entriesPath = (Resolve-Path -LiteralPath $EntriesDirectory -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $basePath -PathType Leaf)) {
    throw "Base user-file path is not a file: $basePath"
}
if (-not (Test-Path -LiteralPath $entriesPath -PathType Container)) {
    throw "User-file entries path is not a directory: $entriesPath"
}

$outputPath = [IO.Path]::GetFullPath($Output)
if ([string]::Equals($basePath, $outputPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must not overwrite the base user_file.bin.'
}

$baseBytes = [IO.File]::ReadAllBytes($basePath)
$entries = Get-UserFileEntries -Buffer $baseBytes
$baseIndexById = @{}
for ($index = 0; $index -lt $entries.Count; $index++) {
    $baseIndexById[[string]$entries[$index].Id] = $index
}

$overlayIds = @{}
$overlays = [System.Collections.ArrayList]::new()
$entryFiles = @(Get-ChildItem -LiteralPath $entriesPath -File -Filter '*.bin')
foreach ($file in $entryFiles) {
    if ($file.Name -notmatch '^\[(?<id>[0-9]+)\].*\.bin$') {
        throw "User-file entry name must start with a numeric [id]: $($file.Name)"
    }

    [uint32]$parsedId = 0
    if (-not [uint32]::TryParse($Matches.id, [ref]$parsedId) -or $parsedId -gt [uint16]::MaxValue) {
        throw "User-file entry ID must be between 0 and 65535: $($file.Name)"
    }
    $id = [int]$parsedId
    $idKey = [string]$id
    if ($overlayIds.ContainsKey($idKey)) {
        throw "Duplicate user-file overlay ID $id in '$($overlayIds[$idKey])' and '$($file.Name)'."
    }

    $data = [IO.File]::ReadAllBytes($file.FullName)
    if ($data.Length -eq 0) {
        throw "User-file overlay entry is empty: $($file.Name)"
    }
    [void]$overlays.Add([PSCustomObject]@{
        Id = $id
        Data = $data
        Source = $file.FullName
    })
    $overlayIds[$idKey] = $file.Name
}

$replaced = 0
$added = 0
foreach ($overlay in @($overlays | Sort-Object -Property Id, Source)) {
    $idKey = [string]$overlay.Id
    if ($baseIndexById.ContainsKey($idKey)) {
        $entries[$baseIndexById[$idKey]] = $overlay
        $replaced++
    }
    else {
        if ($entries.Count -ge [uint16]::MaxValue) {
            throw 'The merged user-file entry count exceeds 65535.'
        }
        $baseIndexById[$idKey] = $entries.Count
        [void]$entries.Add($overlay)
        $added++
    }
}

# Match ci-tool-kit's canonical layout. The current SDK uses a linear lookup,
# but other vendor readers assume the file table is sorted by numeric ID.
$entries = @($entries | Sort-Object -Property Id)

$tableEnd = [long]$headerSize + ([long]$entries.Count * $entrySize)
$nextDataOffset = Get-AlignedOffset -Value $tableEnd
$layout = [System.Collections.ArrayList]::new()
foreach ($entry in $entries) {
    $dataEnd = [uint64]$nextDataOffset + [uint64]$entry.Data.LongLength
    if ([uint64]$nextDataOffset -gt [uint32]::MaxValue -or $dataEnd -gt [uint32]::MaxValue) {
        throw "Merged user-file entry $($entry.Id) exceeds the 32-bit container format."
    }
    [void]$layout.Add([PSCustomObject]@{
        Id = $entry.Id
        Data = $entry.Data
        Offset = [uint32]$nextDataOffset
    })
    $nextDataOffset = Get-AlignedOffset -Value ([long]$dataEnd)
}

if ($layout.Count -eq 0) {
    $outputSize = $nextDataOffset
}
else {
    $lastEntry = $layout[$layout.Count - 1]
    $outputSize = [long]$lastEntry.Offset + $lastEntry.Data.LongLength
}
if ($outputSize -gt [int]::MaxValue) {
    throw 'Merged user_file.bin is too large to materialize.'
}

$outputBytes = [byte[]]::new([int]$outputSize)
for ($index = 0; $index -lt $outputBytes.Length; $index++) {
    $outputBytes[$index] = 0xFF
}
[BitConverter]::GetBytes([uint16]$layout.Count).CopyTo($outputBytes, 0)
for ($index = 0; $index -lt $layout.Count; $index++) {
    $entry = $layout[$index]
    $entryOffset = $headerSize + ($index * $entrySize)
    [BitConverter]::GetBytes([uint16]$entry.Id).CopyTo($outputBytes, $entryOffset)
    [BitConverter]::GetBytes([uint32]$entry.Offset).CopyTo($outputBytes, $entryOffset + 2)
    [BitConverter]::GetBytes([uint32]$entry.Data.Length).CopyTo($outputBytes, $entryOffset + 6)
    if ($entry.Data.Length -ne 0) {
        [Array]::Copy($entry.Data, 0L, $outputBytes, [long]$entry.Offset, [long]$entry.Data.Length)
    }
}

$outputDirectory = [IO.Path]::GetDirectoryName($outputPath)
if (-not [string]::IsNullOrEmpty($outputDirectory)) {
    [void][IO.Directory]::CreateDirectory($outputDirectory)
}
[IO.File]::WriteAllBytes($outputPath, $outputBytes)

Write-Host "CI13XX user-file entries: base=$($entries.Count - $added), replaced=$replaced, added=$added, output=$outputPath"
