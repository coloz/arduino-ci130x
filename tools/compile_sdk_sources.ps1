[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PlatformPath,

    [Parameter(Mandatory = $true)]
    [string]$BuildPath,

    [Parameter(Mandatory = $true)]
    [string]$Compiler,

    [Parameter(Mandatory = $true)]
    [string]$VariantPath,

    [string]$BuildExtraFlags = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function ConvertTo-ResponseArgument {
    param([string]$Value)

    # GCC's response-file parser treats backslashes as escapes. Use forward
    # slashes for Windows paths so D:\sdk\src.c is not reduced to D:sdksrc.c.
    if ($Value -match '^[A-Za-z]:\\') {
        $Value = $Value.Replace('\', '/')
    }
    if ($Value.Contains('"')) {
        return $Value
    }
    return '"' + $Value + '"'
}

function Get-SourceFingerprint {
    param(
        [string[]]$Files,
        [string[]]$Arguments
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($argument in $Arguments) {
        $lines.Add("arg:$argument")
    }
    foreach ($file in $Files | Sort-Object) {
        $item = Get-Item -LiteralPath $file
        $lines.Add("file:$($item.FullName):$($item.Length):$($item.LastWriteTimeUtc.Ticks)")
    }

    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
    }
}

function Start-CompilerBatch {
    param(
        [string]$CompilerPath,
        [string]$WorkingDirectory,
        [string]$ResponseFile
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $CompilerPath
    $startInfo.Arguments = '@"' + $ResponseFile + '"'
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Unable to start SDK compiler batch: $ResponseFile"
    }

    return [PSCustomObject]@{
        Process = $process
        ResponseFile = $ResponseFile
    }
}

$platformRoot = (Resolve-Path -LiteralPath $PlatformPath).Path.TrimEnd('\')
$buildRoot = [IO.Path]::GetFullPath($BuildPath).TrimEnd('\')
$variantRoot = (Resolve-Path -LiteralPath $VariantPath).Path.TrimEnd('\')
$compilerCandidate = $Compiler
if ($env:OS -eq 'Windows_NT' -and
    -not (Test-Path -LiteralPath $compilerCandidate -PathType Leaf) -and
    (Test-Path -LiteralPath ($compilerCandidate + '.exe') -PathType Leaf)) {
    $compilerCandidate += '.exe'
}
$compilerPath = (Resolve-Path -LiteralPath $compilerCandidate).Path
$sdkRoot = Join-Path $platformRoot 'tools\sdk'
$sourceRoot = Join-Path $sdkRoot 'src'
$includeRoot = Join-Path $sdkRoot 'include'
$manifest = Join-Path $sdkRoot 'source_file.prj'
$objectRoot = Join-Path $buildRoot 'ci13xx_sdk_objects'
$linkResponse = Join-Path $buildRoot 'ci13xx_sdk_objects.rsp'
$stampFile = Join-Path $objectRoot 'build.fingerprint'

foreach ($requiredFile in @($compilerPath, $manifest)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required SDK source-build file is missing: $requiredFile"
    }
}
foreach ($requiredDirectory in @($sourceRoot, $includeRoot, $variantRoot)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required SDK source-build directory is missing: $requiredDirectory"
    }
}

$sourceRelativePaths = @(
    Get-Content -LiteralPath $manifest | ForEach-Object {
        if ($_ -match '^\s*source-file:\s*(.+?)\s*$') {
            $matches[1].Trim().Replace('/', '\')
        }
    }
)
if ($sourceRelativePaths.Count -ne 138) {
    throw "Expected 138 compiled SDK sources in $manifest, found $($sourceRelativePaths.Count)"
}
$includeRelativePaths = @(
    Get-Content -LiteralPath $manifest | ForEach-Object {
        if ($_ -match '^\s*include-path:\s*(.+?)\s*$') {
            $matches[1].Trim().Replace('/', '\').TrimEnd('\')
        }
    }
)
if ($includeRelativePaths.Count -ne 88) {
    throw "Expected 88 SDK include entries in $manifest, found $($includeRelativePaths.Count)"
}

$sources = @($sourceRelativePaths | ForEach-Object {
    $path = Join-Path $sourceRoot $_
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Packaged SDK source is missing: $path"
    }
    (Resolve-Path -LiteralPath $path).Path
})

$duplicateNames = @(
    $sources |
        Group-Object { [IO.Path]::GetFileNameWithoutExtension($_) } |
        Where-Object Count -gt 1
)
if ($duplicateNames.Count -ne 0) {
    throw "SDK source basenames must be unique: $($duplicateNames.Name -join ', ')"
}

$commonArguments = @(
    '-march=rv32imafc',
    '-mabi=ilp32f',
    '-mcmodel=medlow',
    '-msmall-data-limit=8',
    '-msave-restore',
    '-mfdiv',
    '-Os',
    '-fsigned-char',
    '-ffunction-sections',
    '-fdata-sections',
    '-fno-common',
    '-fno-delete-null-pointer-checks',
    '-fno-unroll-loops',
    '-fshort-enums',
    '-w',
    '-g',
    '-MMD',
    '-MP',
    '-DASR_CODE_VERSION=2',
    '-DCORE_ID=0',
    '-DCI_ARDUINO_CORE=1',
    '-DCI_CONFIG_FILE=\"user_config.h\"'
)
if (-not [string]::IsNullOrWhiteSpace($BuildExtraFlags)) {
    $commonArguments += @($BuildExtraFlags.Trim() -split '\s+')
}
$preservedIncludeRoot = Join-Path $includeRoot 'sdk'
$commonArguments += @('-I', $variantRoot)
foreach ($relativePath in $includeRelativePaths) {
    $preservedPath = Join-Path $preservedIncludeRoot $relativePath
    $commonArguments += @('-I', $preservedPath)
}

$fingerprintFiles = @($sources)
$fingerprintFiles += @(Get-ChildItem -LiteralPath $includeRoot -Recurse -File -Filter '*.h' | Select-Object -ExpandProperty FullName)
$fingerprintFiles += @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Select-Object -ExpandProperty FullName)
$fingerprintFiles += @(Get-ChildItem -LiteralPath $variantRoot -File | Select-Object -ExpandProperty FullName)
$fingerprint = Get-SourceFingerprint -Files @($fingerprintFiles | Sort-Object -Unique) -Arguments $commonArguments

$expectedObjectNames = @($sources | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) + '.o' })
$cacheValid = Test-Path -LiteralPath $stampFile -PathType Leaf
if ($cacheValid) {
    $cacheValid = ([IO.File]::ReadAllText($stampFile).Trim() -eq $fingerprint)
}
if ($cacheValid) {
    foreach ($objectName in $expectedObjectNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $objectRoot $objectName) -PathType Leaf)) {
            $cacheValid = $false
            break
        }
    }
}

if (-not $cacheValid) {
    $fullObjectRoot = [IO.Path]::GetFullPath($objectRoot)
    $buildPrefix = $buildRoot + '\'
    if (-not $fullObjectRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean SDK object directory outside Arduino build path: $fullObjectRoot"
    }
    if (Test-Path -LiteralPath $fullObjectRoot -PathType Container) {
        Get-ChildItem -LiteralPath $fullObjectRoot -Force | Remove-Item -Recurse -Force
    }
    else {
        New-Item -ItemType Directory -Force -Path $fullObjectRoot | Out-Null
    }

    $cSources = @($sources | Where-Object { [IO.Path]::GetExtension($_) -ceq '.c' })
    $assemblySources = @($sources | Where-Object { [IO.Path]::GetExtension($_) -ceq '.S' })
    if ($cSources.Count -ne 136 -or $assemblySources.Count -ne 2) {
        throw "Expected 136 C and 2 assembly SDK sources; found $($cSources.Count) C and $($assemblySources.Count) assembly"
    }

    $batchCount = [Math]::Min([Math]::Max(1, [Environment]::ProcessorCount), 12)
    $batches = @()
    for ($index = 0; $index -lt $batchCount; $index++) {
        $batch = @()
        for ($sourceIndex = $index; $sourceIndex -lt $cSources.Count; $sourceIndex += $batchCount) {
            $batch += $cSources[$sourceIndex]
        }
        if ($batch.Count -ne 0) {
            $batches += ,$batch
        }
    }
    $batches += ,$assemblySources

    Write-Host "Compiling $($sources.Count) CI13XX SDK sources in $($batches.Count) parallel batches ..."
    $running = @()
    for ($batchIndex = 0; $batchIndex -lt $batches.Count; $batchIndex++) {
        $batch = @($batches[$batchIndex])
        $isAssembly = [IO.Path]::GetExtension($batch[0]) -ceq '.S'
        $arguments = @($commonArguments)
        if ($isAssembly) {
            $arguments += @('-x', 'assembler-with-cpp')
        }
        else {
            $arguments += '-std=gnu11'
        }
        $arguments += '-c'
        $arguments += $batch

        $responseFile = Join-Path $objectRoot ("compile-{0:D2}.rsp" -f $batchIndex)
        $responseArguments = @($arguments | ForEach-Object { ConvertTo-ResponseArgument -Value $_ })
        [IO.File]::WriteAllLines($responseFile, $responseArguments, [Text.UTF8Encoding]::new($false))
        $running += Start-CompilerBatch -CompilerPath $compilerPath `
            -WorkingDirectory $objectRoot `
            -ResponseFile $responseFile
    }

    $failed = $false
    foreach ($entry in $running) {
        $entry.Process.WaitForExit()
        $entry.Process.Refresh()
        $exitCode = $entry.Process.ExitCode
        if ($exitCode -ne 0) {
            $failed = $true
            Write-Error "SDK compiler batch failed ($($entry.ResponseFile)) with exit code $exitCode" -ErrorAction Continue
        }
    }
    if ($failed) {
        throw 'One or more CI13XX SDK source compiler batches failed.'
    }

    $actualObjects = @(Get-ChildItem -LiteralPath $objectRoot -File -Filter '*.o')
    if ($actualObjects.Count -ne 138) {
        throw "Expected 138 compiled SDK objects, found $($actualObjects.Count) in $objectRoot"
    }
    [IO.File]::WriteAllText($stampFile, $fingerprint, [Text.UTF8Encoding]::new($false))
}
else {
    Write-Host 'Using cached CI13XX SDK source objects.'
}

$objectPaths = @($expectedObjectNames | ForEach-Object { Join-Path $objectRoot $_ })
$linkArguments = @($objectPaths | ForEach-Object { ConvertTo-ResponseArgument -Value $_ })
[IO.File]::WriteAllLines($linkResponse, $linkArguments, [Text.UTF8Encoding]::new($false))
Write-Host "CI13XX SDK source objects ready: $($objectPaths.Count)"
