[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ToolchainRoot,

    [string]$PlatformRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$InputDirectory,

    [string]$OutputDirectory,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'Vendor LTO libraries must be materialized with the validated Windows GCC that created them.'
}

$platformPath = (Resolve-Path -LiteralPath $PlatformRoot).Path
$toolchainPath = (Resolve-Path -LiteralPath $ToolchainRoot).Path
if ((Split-Path -Leaf $toolchainPath) -ne 'gcc_fix_raissrc') {
    $toolchainPath = Join-Path $toolchainPath 'gcc_fix_raissrc'
}
$gcc = Join-Path $toolchainPath 'bin\riscv-nuclei-elf-gcc.exe'
$ar = Join-Path $toolchainPath 'bin\riscv-nuclei-elf-ar.exe'
$ranlib = Join-Path $toolchainPath 'bin\riscv-nuclei-elf-ranlib.exe'
$readelf = Join-Path $toolchainPath 'bin\riscv-nuclei-elf-readelf.exe'
foreach ($tool in @($gcc, $ar, $ranlib, $readelf)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required GCC tool was not found: $tool"
    }
}

$expectedCompilerHash = '84B0FFB1FB194CC41FCFA96FB01D65B3A6289147041CF8BC76DB60BD05FBCB6D'
$compilerHash = (Get-FileHash -LiteralPath $gcc -Algorithm SHA256).Hash
if ($compilerHash -ne $expectedCompilerHash) {
    throw "Unexpected GCC executable SHA-256: $compilerHash"
}

if (-not $InputDirectory) {
    $InputDirectory = Join-Path $platformPath 'tools\sdk\lib'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $platformPath 'tools\sdk\lib-cross-host'
}
$inputPath = (Resolve-Path -LiteralPath $InputDirectory).Path
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$sdkPath = [IO.Path]::GetFullPath((Join-Path $platformPath 'tools\sdk')).TrimEnd('\') + '\'
if (-not $outputPath.StartsWith($sdkPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must be inside tools/sdk: $outputPath"
}
if (Test-Path -LiteralPath $outputPath) {
    if (-not $Force) {
        throw "Output directory already exists; pass -Force to replace it: $outputPath"
    }
    Remove-Item -LiteralPath $outputPath -Recurse -Force
}

$archives = @(Get-ChildItem -LiteralPath $inputPath -Recurse -File -Filter '*.a' | Sort-Object FullName)
if ($archives.Count -eq 0) {
    throw "No vendor archives were found below $inputPath"
}

$stageRoot = Join-Path $env:TEMP ("ci13xx-native-libs-{0}-{1}" -f $PID, [guid]::NewGuid().ToString('N'))
$stageOutput = Join-Path $stageRoot 'output'
$stageWork = Join-Path $stageRoot 'work'
New-Item -ItemType Directory -Force -Path $stageOutput, $stageWork | Out-Null

$manifestLines = [Collections.Generic.List[string]]::new()
$manifestLines.Add('format=ci13xx-cross-host-native-archives-v2')
$manifestLines.Add("compiler.sha256=$($compilerHash.ToLowerInvariant())")
$manifestLines.Add('compiler.target=riscv-nuclei-elf')
$manifestLines.Add('compiler.version=9.2.0')
$manifestLines.Add('materialization.flags=-march=rv32imafc -mabi=ilp32f -mcmodel=medlow -msmall-data-limit=8 -msave-restore -mfdiv -Os -fsigned-char -ffunction-sections -fdata-sections -fno-common -fno-delete-null-pointer-checks -fno-unroll-loops -fshort-enums -flto -r -nostdlib -flinker-output=nolto-rel')
$membersWithoutCode = @(
    'libOnMicroBLE.a/exe_host_smp.o',
    'libOnMicroBLE.a/exe_ll_sec.o'
)

try {
    foreach ($archive in $archives) {
        $relativeArchive = $archive.FullName.Substring($inputPath.TrimEnd('\').Length).TrimStart('\')
        $relativeUnix = $relativeArchive.Replace('\', '/')
        $members = @(& $ar t $archive.FullName)
        if ($LASTEXITCODE -ne 0 -or $members.Count -eq 0) {
            throw "Unable to list archive members: $($archive.FullName)"
        }
        $duplicates = @($members | Group-Object | Where-Object Count -gt 1)
        if ($duplicates.Count -ne 0) {
            throw "Archive has duplicate member names and cannot be converted safely: $($archive.FullName)"
        }

        $archiveWork = Join-Path $stageWork ([guid]::NewGuid().ToString('N'))
        $sourceMembers = Join-Path $archiveWork 'source'
        $nativeMembersRoot = Join-Path $archiveWork 'native'
        New-Item -ItemType Directory -Force -Path $sourceMembers, $nativeMembersRoot | Out-Null
        Push-Location $sourceMembers
        try {
            & $ar x $archive.FullName
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to extract archive: $($archive.FullName)"
            }
        }
        finally {
            Pop-Location
        }

        $nativeMembers = @()
        $materializedCount = 0
        $passthroughCount = 0
        foreach ($member in $members) {
            $sourceMember = Join-Path $sourceMembers $member
            $nativeMember = Join-Path $nativeMembersRoot $member
            $sourceSections = @(& $readelf -W -S $sourceMember)
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to inspect source archive member: $sourceMember"
            }
            $sourceHasLto = $sourceSections -match '\.gnu\.lto_'
            if ($sourceHasLto) {
                & $gcc @(
                '-march=rv32imafc',
                '-mabi=ilp32f',
                '-mcmodel=medlow',
                '-msmall-data-limit=8',
                '-msave-restore',
                '-mfdiv',
                '-Os',
                '-fsigned-char',
                # Materializing an LTO member without these flags collapses all
                # generated code and data into monolithic .text/.data sections.
                # Linux/macOS then cannot garbage-collect optional CWSL/TTS
                # functions whose source-side callbacks are compiled out.
                '-ffunction-sections',
                '-fdata-sections',
                '-fno-common',
                '-fno-delete-null-pointer-checks',
                '-fno-unroll-loops',
                '-fshort-enums',
                '-flto',
                '-r',
                '-nostdlib',
                '-flinker-output=nolto-rel',
                $sourceMember,
                '-o',
                $nativeMember
                )
                if ($LASTEXITCODE -ne 0) {
                    throw "Unable to materialize $member from $($archive.FullName)"
                }
                $materializedCount++
            }
            else {
                # Assembly and any other already-native members must not be
                # relinked: doing so embeds the randomized staging path in an
                # ELF FILE symbol and makes otherwise identical archives vary.
                Copy-Item -LiteralPath $sourceMember -Destination $nativeMember
                $passthroughCount++
            }
            $sections = @(& $readelf -W -S $nativeMember)
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to inspect materialized object: $nativeMember"
            }
            if ($sections -match '\.gnu\.lto_') {
                throw "Materialized object still contains GCC LTO bytecode: $nativeMember"
            }
            $memberKey = "$relativeUnix/$member"
            if ($sourceHasLto -and
                -not ($sections -match '\.text\.[^\s]+\s+PROGBITS') -and
                $memberKey -notin $membersWithoutCode) {
                throw "Materialized code is missing function sections: $nativeMember"
            }
            $nativeMembers += $nativeMember
        }

        $outputArchive = Join-Path $stageOutput $relativeArchive
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outputArchive) | Out-Null
        & $ar qcD $outputArchive @nativeMembers
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to create materialized archive: $outputArchive"
        }
        & $ranlib -D $outputArchive
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to index materialized archive: $outputArchive"
        }

        $sourceHash = (Get-FileHash -LiteralPath $archive.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $outputHash = (Get-FileHash -LiteralPath $outputArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifestLines.Add("archive=$relativeUnix members=$($members.Count) materialized=$materializedCount passthrough=$passthroughCount source.sha256=$sourceHash output.sha256=$outputHash")
        Write-Host "Materialized $relativeUnix ($materializedCount LTO, $passthroughCount native members)"
    }

    [IO.File]::WriteAllLines(
        (Join-Path $stageOutput 'BUILD-MANIFEST.txt'),
        $manifestLines,
        [Text.UTF8Encoding]::new($false)
    )
    Copy-Item -LiteralPath $stageOutput -Destination $outputPath -Recurse
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}

Write-Host "Cross-host vendor libraries ready: $outputPath"
