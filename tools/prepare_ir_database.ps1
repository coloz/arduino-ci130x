[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$PlatformPath,
    [Parameter(Mandatory = $true)][string]$Chip
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$databaseName = '[50000]ir_data_2024_08_16.bin'
$databaseSize = 70716L
$databaseHash = 'F7E3680B45F9ABE56C6D3E16D1BBFFD7336E286D0B0DABED20E1D2B4854B97D0'
$manifestName = '.chipintelli-ir-database.json'
$guidance = 'Declare #define CHIPINTELLI_IR_DATABASE 0 or 1 exactly once, unconditionally at the top level of the main sketch; conditional definitions, aliases and #undef are not supported.'

function Resolve-IRSource {
    param([string]$Path)
    $candidates = @($Path)
    if (-not [IO.Path]::HasExtension($Path)) { $candidates += @(($Path + '.ino'), ($Path + '.cpp')) }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw "Arduino source file not found. Tried: $($candidates -join ', ')"
}

function Test-IRDatabaseEnabled {
    param([string]$Path)
    $text = [string](Get-Content -LiteralPath $Path -Raw -Encoding UTF8)
    # Translation phase 2 precedes comment handling, including continued //.
    $text = [regex]::Replace($text, '\\\r?\n', '')
    $pattern = '//[^\n]*|/\*[\s\S]*?\*/|R"(?<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)\k<delimiter>"|"(?:\\[\s\S]|[^"\\\r\n])*"|''(?:\\[\s\S]|[^''\\\r\n])*'''
    $text = [regex]::Replace($text, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{
        param($match)
        # `1 "extra"` must not become a supported declaration after masking.
        $marker = if ($match.Value.StartsWith('//') -or $match.Value.StartsWith('/*')) { ' ' } else { '@' }
        return [regex]::Replace($match.Value, '[^\r\n]', $marker)
    })
    $depth = 0
    $value = $null
    foreach ($line in ($text -split '\r?\n')) {
        $directive = [regex]::Match($line, '^\s*#\s*([A-Za-z_][A-Za-z_0-9]*)(.*)$')
        if (-not $directive.Success) { continue }
        $keyword = $directive.Groups[1].Value
        $rest = $directive.Groups[2].Value
        if ($keyword -cin @('if', 'ifdef', 'ifndef')) { $depth++ }
        elseif ($keyword -ceq 'endif') { $depth = [Math]::Max(0, $depth - 1) }
        elseif ($keyword -cin @('define', 'undef') -and
                $rest -cmatch '^\s+CHIPINTELLI_IR_DATABASE\b') {
            $declaration = [regex]::Match($rest, '^\s+CHIPINTELLI_IR_DATABASE\s+([01])\s*$')
            if ($keyword -cne 'define' -or $depth -gt 0 -or $null -ne $value -or -not $declaration.Success) {
                throw $guidance
            }
            $value = $declaration.Groups[1].Value -ceq '1'
        }
    }
    return $value -eq $true
}

function Test-OfficialIRDatabase {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    # PowerShell treats our dot-prefixed staging files as hidden on Unix.
    if ((Get-Item -LiteralPath $Path -Force).Length -ne $databaseSize) { return $false }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ceq $databaseHash
}

function Assert-IRPath {
    param([string]$Path, [string]$Root)
    $absolute = [IO.Path]::GetFullPath($Path)
    $boundary = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    if (-not $absolute.StartsWith($boundary + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "IR resource path escapes its project: $Path"
    }
    $current = $absolute
    while ($current -ne $boundary) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "IR resource path must not be a link: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Test-IRManagedManifest {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "IR managed manifest is not a file: $Path" }
    try {
        $metadata = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
        $properties = @($metadata.PSObject.Properties.Name)
        if ($properties.Count -ne 4 -or
            $metadata.schemaVersion -ne 1 -or $metadata.name -cne $databaseName -or
            $metadata.size -ne $databaseSize -or $metadata.sha256 -cne $databaseHash) {
            throw 'Unexpected metadata'
        }
    }
    catch { throw "Invalid IR managed manifest; preserving files: $Path" }
    return $true
}

function Publish-IRDatabase {
    param([string]$Database, [string]$Target, [string]$Manifest)
    $directory = [IO.Path]::GetDirectoryName($Target)
    $temporary = Join-Path $directory ('.chipintelli-ir-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    $receipt = Join-Path $directory ('.chipintelli-ir-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::Copy($Database, $temporary, $false)
        if (-not (Test-OfficialIRDatabase -Path $temporary)) { throw "Official IR database changed while copying: $Database" }
        if (-not (Test-Path -LiteralPath $Manifest)) {
            $metadata = [ordered]@{ schemaVersion = 1; name = $databaseName; size = $databaseSize; sha256 = $databaseHash }
            [IO.File]::WriteAllText($receipt, (($metadata | ConvertTo-Json) + "`n"), [Text.UTF8Encoding]::new($false))
            [IO.File]::Move($receipt, $Manifest)
        }
        # Receipt first: interrupted publication remains managed and repairable.
        # File.Move on the same volume is atomic and refuses existing targets.
        [IO.File]::Move($temporary, $Target)
    }
    finally {
        foreach ($path in @($temporary, $receipt)) {
            if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
        }
    }
}

try {
    $sourcePath = Resolve-IRSource -Path $Source
    $enabled = Test-IRDatabaseEnabled -Path $sourcePath
    if ($enabled -and $Chip -ieq 'ci1302') {
        throw 'CHIPINTELLI_IR_DATABASE=1 requires CI1303/CI1306 (4 MB); the CI1302 standard resource layout has insufficient capacity. Remove the macro or use 0 for Raw/NEC.'
    }
    $database = Join-Path $PlatformPath "libraries/ChipIntelliIR/examples/AirConditioner/recursos/user_file_entries/$databaseName"
    if ($enabled -and -not (Test-OfficialIRDatabase -Path $database)) {
        throw "Official IR database is missing or fails the 70716-byte/SHA-256 check: $database"
    }
    $sketch = [IO.Path]::GetDirectoryName($sourcePath)
    $roots = @([pscustomobject]@{ Root = $sketch; Entries = (Join-Path $sketch 'recursos/user_file_entries') })
    $parent = [IO.Path]::GetDirectoryName($sketch)
    if ([IO.Path]::GetFileName($sketch) -ceq 'sketch' -and [IO.Path]::GetFileName($parent) -ceq '.temp') {
        $project = [IO.Path]::GetDirectoryName($parent)
        $packagePath = Join-Path $project 'package.json'
        if ((Test-Path -LiteralPath $packagePath -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $project 'project.abi') -PathType Leaf)) {
            $isAily = $false
            try {
                $package = Get-Content -LiteralPath $packagePath -Raw -Encoding UTF8 | ConvertFrom-Json
                $nameProperty = $package.PSObject.Properties['name']
                $isAily = $null -ne $nameProperty -and $nameProperty.Value -is [string] -and -not [string]::IsNullOrWhiteSpace($nameProperty.Value)
            }
            catch { $isAily = $false }
            if ($isAily) {
                $roots = @([pscustomobject]@{ Root = $project; Entries = (Join-Path $project 'src/recursos/user_file_entries') }) + $roots
            }
        }
    }

    $plans = @()
    foreach ($destination in $roots) {
        $entries = $destination.Entries
        $target = Join-Path $entries $databaseName
        $manifest = Join-Path $entries $manifestName
        if (-not $enabled -and -not (Test-Path -LiteralPath $manifest)) { continue }
        foreach ($path in @($entries, $target, $manifest)) { Assert-IRPath -Path $path -Root $destination.Root }
        if ((Test-Path -LiteralPath $entries) -and -not (Test-Path -LiteralPath $entries -PathType Container)) {
            throw "IR entries path is not a directory: $entries"
        }
        $managed = Test-IRManagedManifest -Path $manifest
        $hasDatabase = $false
        $matchingFiles = 0
        if ($enabled -and (Test-Path -LiteralPath $entries -PathType Container)) {
            foreach ($entry in (Get-ChildItem -LiteralPath $entries -Force)) {
                $id = [regex]::Match($entry.Name, '^\[(\d+)\]')
                if ($id.Success -and $id.Groups[1].Value.TrimStart('0') -ceq '50000') {
                    Assert-IRPath -Path $entry.FullName -Root $destination.Root
                    if ($entry.Extension -ine '.bin' -or -not (Test-OfficialIRDatabase -Path $entry.FullName)) {
                        throw "IR resource ID 50000 conflicts with the official database; preserving user file: $($entry.FullName)"
                    }
                    $hasDatabase = $true
                    $matchingFiles++
                }
            }
            if ($matchingFiles -gt 1) { throw "Duplicate IR resource ID 50000 in $entries; keep one official .bin file." }
        }
        $plans += [pscustomobject]@{ Target = $target; Manifest = $manifest; Managed = $managed; HasDatabase = $hasDatabase }
    }
    # Preflight both aily destinations before changing either one.
    foreach ($plan in $plans) {
        if ($enabled) {
            if ($plan.HasDatabase) {
                Write-Host "CI13XX IR database already present: $([IO.Path]::GetDirectoryName($plan.Target))"
                continue
            }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($plan.Target)) | Out-Null
            Publish-IRDatabase -Database $database -Target $plan.Target -Manifest $plan.Manifest
            Write-Host "CI13XX optional IR database prepared: $($plan.Target)"
        }
        elseif ($plan.Managed) {
            if (Test-OfficialIRDatabase -Path $plan.Target) {
                Remove-Item -LiteralPath $plan.Target -Force
                Write-Host "CI13XX disabled managed IR database removed: $($plan.Target)"
            }
            elseif (Test-Path -LiteralPath $plan.Target) {
                Write-Host "CI13XX modified IR database preserved as user-owned: $($plan.Target)"
            }
            Remove-Item -LiteralPath $plan.Manifest -Force
        }
    }
}
catch {
    Write-Error "CI13XX IR database preparation failed: $($_.Exception.Message)"
    exit 1
}
