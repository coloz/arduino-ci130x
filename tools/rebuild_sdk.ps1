[CmdletBinding()]
param(
    [string]$SdkPath = (Join-Path $PSScriptRoot '..\..\CI13XX_SDK_ASR_ALG_V2.7.12'),

    [string]$ToolchainBin = $env:CHIPINTELLI_GCC_BIN,

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'The V2.7.12 SDK build and packaging tools are currently supported on Windows only.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The CI13XX Arduino package requires 64-bit Windows because ci-tool-kit.exe is x64.'
}

function Resolve-ToolchainBin {
    param([string]$RequestedPath)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        $candidates.Add($RequestedPath)
    }

    $candidates.Add((Join-Path $PSScriptRoot 'riscv-nuclei-elf-gcc\gcc_fix_raissrc\bin'))
    $candidates.Add((Join-Path $env:TEMP 'riscv-nuclei-elf-gcc-9.2.0\gcc_fix_raissrc\bin'))

    $arduinoToolRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\chipintelli\tools\riscv-nuclei-elf-gcc\9.2.0'
    $candidates.Add((Join-Path $arduinoToolRoot 'gcc_fix_raissrc\bin'))

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $expanded = [Environment]::ExpandEnvironmentVariables($candidate)
        $compiler = Join-Path $expanded 'riscv-nuclei-elf-gcc.exe'
        if (Test-Path -LiteralPath $compiler -PathType Leaf) {
            return (Resolve-Path -LiteralPath $expanded).Path
        }
    }

    throw @'
Nuclei GCC 9.2.0 was not found. Pass -ToolchainBin with the directory that
contains riscv-nuclei-elf-gcc.exe, or set CHIPINTELLI_GCC_BIN.
'@
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$DestinationDirectory
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required SDK file is missing: $Source"
    }

    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
    Copy-Item -LiteralPath $Source -Destination $DestinationDirectory -Force
}

$sdkRoot = (Resolve-Path -LiteralPath $SdkPath).Path.TrimEnd('\')
$toolchain = Resolve-ToolchainBin -RequestedPath $ToolchainBin
$compiler = Join-Path $toolchain 'riscv-nuclei-elf-gcc.exe'
$compilerVersion = @(& $compiler --version)
if ($LASTEXITCODE -ne 0 -or ($compilerVersion -join "`n") -notmatch '(?m)\b9\.2\.0\b') {
    throw "CI13XX SDK V2.7.12 requires Nuclei GCC 9.2.0: $compiler"
}
$compilerHash = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash
$platformRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$projectRoot = Join-Path $sdkRoot 'projects\offline_asr_alg_pro_sample\project_file'
$projectBuild = Join-Path $projectRoot 'build'
$objectRoot = Join-Path $projectBuild 'objs'
$make = Join-Path $sdkRoot 'tools\build-tools\bin\make.exe'

foreach ($requiredDirectory in @($projectRoot, (Join-Path $sdkRoot 'libs'))) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required SDK directory is missing: $requiredDirectory"
    }
}

if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $make -PathType Leaf)) {
        throw "SDK make.exe is missing: $make"
    }

    $oldPath = $env:PATH
    try {
        $env:PATH = "$toolchain;$oldPath"
        $jobs = [Math]::Max(1, [Environment]::ProcessorCount)
        Write-Host "Building CI13XX V2.7.12 USE_NULL integration objects with $jobs jobs ..."
        # The vendor project defaults to -flto. GNU ld --wrap cannot interpose
        # calls that GCC resolves internally between LTO units, which would
        # bypass both the Arduino scheduler hook and the ASR result hook. Build
        # the 138 open SDK objects without LTO; proprietary vendor archives keep
        # their original GCC 9.2 LTO payload. The vendor Makefile does not
        # track command-line flag changes, so start with its clean target.
        # Do not use make -B: it incorrectly treats generated source_file.mk
        # as an always-outdated prerequisite and eventually fails with "No
        # rule to make target" after compiling the object set.
        & $make -C $projectRoot clean
        if ($LASTEXITCODE -ne 0) {
            throw "SDK make clean failed with exit code $LASTEXITCODE"
        }

        & $make -C $projectRoot -j $jobs 'LTO_OPTION=' 'build/offline_asr_alg_pro_sample.elf'
        if ($LASTEXITCODE -ne 0) {
            throw "SDK make failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        $env:PATH = $oldPath
    }
}

$objects = @(Get-ChildItem -LiteralPath $objectRoot -File -Filter '*.o' | Sort-Object Name)
if ($objects.Count -ne 138) {
    throw "Expected 138 SDK objects for the validated profile, found $($objects.Count) in $objectRoot"
}

$objdump = Join-Path $toolchain 'riscv-nuclei-elf-objdump.exe'
foreach ($object in $objects) {
    $sections = @(& $objdump -h $object.FullName)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect rebuilt SDK object: $($object.FullName)"
    }
    if (($sections -join "`n") -match '\.gnu\.lto_') {
        throw @"
SDK object $($object.Name) still contains LTO IR. Arduino scheduler/ASR
interposition would be bypassed. Rebuild with LTO_OPTION empty before
generating the payload.
"@
    }
}

$sdkOutput = Join-Path $platformRoot 'tools\sdk'
$stagingOutput = Join-Path $platformRoot 'tools\sdk.new'
if (Test-Path -LiteralPath $stagingOutput) {
    Remove-Item -LiteralPath $stagingOutput -Recurse -Force
}

$libOutput = Join-Path $stagingOutput 'lib'
$includeOutput = Join-Path $stagingOutput 'include'
$preservedIncludeOutput = Join-Path $includeOutput 'sdk'
$linkerOutput = Join-Path $stagingOutput 'ld'
$binaryOutput = Join-Path $stagingOutput 'bin'
New-Item -ItemType Directory -Force -Path $libOutput, $includeOutput, $preservedIncludeOutput, $linkerOutput, $binaryOutput | Out-Null

$archiver = Join-Path $toolchain 'riscv-nuclei-elf-gcc-ar.exe'
$sdkArchive = Join-Path $libOutput 'libci13xx_sdk.a'
Write-Host "Archiving $($objects.Count) SDK objects ..."
& $archiver crs $sdkArchive @($objects.FullName)
if ($LASTEXITCODE -ne 0) {
    throw "gcc-ar failed with exit code $LASTEXITCODE"
}

$archiveMembers = @(& $archiver t $sdkArchive)
if ($LASTEXITCODE -ne 0 -or $archiveMembers.Count -ne 138) {
    throw "SDK archive verification failed: expected 138 members, found $($archiveMembers.Count)"
}

$sdkLibraries = @(
    'libasr_dis_deepse_v2.a',
    'libnlp.a',
    'libnewlib_port.a',
    'libfreertos_port.a',
    'libdsu.a',
    'libflash_encrypt.a',
    'libcwsl_v2.a',
    'libtts.a',
    'libcikd_pro_cwsl_dis_frm.a'
)
foreach ($library in $sdkLibraries) {
    Copy-RequiredFile -Source (Join-Path $sdkRoot "libs\$library") -DestinationDirectory $libOutput
}

Copy-RequiredFile -Source (Join-Path $sdkRoot 'projects\offline_asr_alg_pro_sample\app\app_ir\ir_src\libir_data.a') -DestinationDirectory $libOutput
Copy-RequiredFile -Source (Join-Path $sdkRoot 'components\ci_ble\stack\libOnMicroBLE.a') -DestinationDirectory $libOutput
Copy-RequiredFile -Source (Join-Path $sdkRoot 'components\ci_ble\stack\libcias_crypto.a') -DestinationDirectory $libOutput

Copy-RequiredFile -Source (Join-Path $sdkRoot 'projects\offline_asr_alg_pro_sample\lds\ci130x_alg_pro_null.lds') -DestinationDirectory $linkerOutput
Copy-RequiredFile -Source (Join-Path $sdkRoot 'utils\common.lds') -DestinationDirectory $linkerOutput

Copy-RequiredFile -Source (Join-Path $sdkRoot 'libs\libbnpu_core_alg_pro_null.a') -DestinationDirectory $binaryOutput
Copy-RequiredFile -Source (Join-Path $sdkRoot 'tools\ci-tool-kit.exe') -DestinationDirectory $binaryOutput
Copy-RequiredFile -Source (Join-Path $sdkRoot 'tools\code_program.exe') -DestinationDirectory $binaryOutput

# Keep the source hierarchy for qualified includes and also provide a flat
# include directory because the original project uses 88 separate -I entries.
$headers = @(Get-ChildItem -LiteralPath $sdkRoot -Recurse -File -Filter '*.h' | Sort-Object FullName)
foreach ($header in $headers) {
    $relativePath = $header.FullName.Substring($sdkRoot.Length).TrimStart('\')
    $preservedDestination = Join-Path $preservedIncludeOutput $relativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preservedDestination) | Out-Null
    Copy-Item -LiteralPath $header.FullName -Destination $preservedDestination -Force
    Copy-Item -LiteralPath $header.FullName -Destination (Join-Path $includeOutput $header.Name) -Force
}

# Resolve the three duplicate basenames according to the include precedence of
# the validated offline_asr_alg_pro_sample build.
Copy-Item -LiteralPath (Join-Path $sdkRoot 'system\ci_assert.h') -Destination (Join-Path $includeOutput 'ci_assert.h') -Force
Copy-Item -LiteralPath (Join-Path $sdkRoot 'components\ci_cwsl_v2\cwsl_manage.h') -Destination (Join-Path $includeOutput 'cwsl_manage.h') -Force
Copy-Item -LiteralPath (Join-Path $sdkRoot 'components\ci_cwsl_v2\cwsl_template_manager.h') -Destination (Join-Path $includeOutput 'cwsl_template_manager.h') -Force

Copy-RequiredFile -Source (Join-Path $sdkRoot 'README.md') -DestinationDirectory $stagingOutput
Move-Item -LiteralPath (Join-Path $stagingOutput 'README.md') -Destination (Join-Path $stagingOutput 'UPSTREAM-SDK-README.md') -Force

$hashedArtifacts = @(Get-ChildItem -LiteralPath $libOutput, $linkerOutput, $binaryOutput -File | Sort-Object FullName)
$artifactHashLines = @($hashedArtifacts | ForEach-Object {
    $relative = $_.FullName.Substring($stagingOutput.Length).TrimStart('\')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    "$relative SHA256=$hash"
}) -join "`r`n"

$manifest = @"
Generated from: CI13XX_SDK_ASR_ALG_V2.7.12
Project: projects/offline_asr_alg_pro_sample
Board: CI-D06GT01D / CI1306 / 4 MB
Algorithm profile: USE_NULL=1, NO_ASR_FLOW=0
Compiler: riscv-nuclei-elf-gcc 9.2.0
Compiler executable SHA256: $compilerHash
SDK object count: 138
Header count: $($headers.Count)
Open SDK objects: non-LTO (required for scheduler and ASR link interposition)
Vendor archives: original GCC 9.2.0 LTO/ABI payload

Payload artifact hashes:
$artifactHashLines

This payload is profile-specific. Do not substitute a linker script, BNPU image,
algorithm library or preprocessor profile independently.
"@
Set-Content -LiteralPath (Join-Path $stagingOutput 'BUILD-MANIFEST.txt') -Value $manifest -Encoding UTF8

if (Test-Path -LiteralPath $sdkOutput) {
    Remove-Item -LiteralPath $sdkOutput -Recurse -Force
}
Move-Item -LiteralPath $stagingOutput -Destination $sdkOutput

$payloadFiles = Get-ChildItem -LiteralPath $sdkOutput -Recurse -File
$payloadSize = ($payloadFiles | Measure-Object -Property Length -Sum).Sum
Write-Host ("SDK payload generated: {0} files, {1:N1} MiB" -f $payloadFiles.Count, ($payloadSize / 1MB))
Write-Host $sdkOutput
