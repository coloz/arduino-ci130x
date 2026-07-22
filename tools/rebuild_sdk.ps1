[CmdletBinding()]
param(
    [string]$SdkPath = (Join-Path $PSScriptRoot '..\..\CI13XX_SDK_ASR_ALG_V2.7.12'),

    [string]$ToolchainBin = $env:CHIPINTELLI_GCC_BIN,

    [ValidateSet('ci1306', 'ci1302', 'ci1303')]
    [string]$Variant = 'ci1306',

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

function Write-MultiVariantUserConfig {
    param(
        [string]$Source,
        [string]$Destination
    )

    $content = [IO.File]::ReadAllText($Source)
    $boardSelection = '#if (USE_CI_D02GS01J_BOARD == 1)'
    $chipBranches = '#elif (USE_CI_D12GS01J_BOARD == 1)'
    if (-not $content.Contains($boardSelection) -or -not $content.Contains($chipBranches)) {
        throw "Unable to adapt SDK user_config.h for Arduino variants: $Source"
    }

    # Arduino owns UART2. The SDK voice-module protocol must not initialize it
    # during startup; HardwareSerial::begin() configures the peripheral and pads
    # only when the sketch explicitly opens Serial2.
    $messageUartEnabled = '#define MSG_COM_USE_UART_EN            1'
    $messageUartDisabled = '#define MSG_COM_USE_UART_EN            0'
    if (-not $content.Contains($messageUartEnabled) -and -not $content.Contains($messageUartDisabled)) {
        throw "Unable to disable the SDK message UART in: $Source"
    }
    $content = $content.Replace($messageUartEnabled, $messageUartDisabled)

    # Arduino owns UART0 as Serial. Disable the vendor SDK logger so it cannot
    # configure the same peripheral for 921600 baud or block before setup().
    $logUartPattern = '(?m)^#define CONFIG_CI_LOG_UART\s+HAL_UART0_BASE[^\r\n]*\r?$'
    $logUartMatches = [regex]::Matches($content, $logUartPattern)
    if ($logUartMatches.Count -ne 1) {
        throw "Unable to disable the SDK UART0 logger in: $Source"
    }
    $content = [regex]::Replace(
        $content,
        $logUartPattern,
        '#define CONFIG_CI_LOG_UART             0  // UART0 is owned by Arduino Serial.',
        1)

    # CI1302/CI1303 reference applications normally use the internal RC. Keep
    # the default overridable so Arduino's board menu can support custom boards
    # that really do fit a 12.288 MHz external crystal.
    $arduinoClockSelection = @(
        '// Arduino board options may override this macro.',
        '#ifndef USE_EXTERNAL_CRYSTAL_OSC',
        '#if ((CI_CHIP_TYPE == 1302) || (CI_CHIP_TYPE == 1303) || (CI_CHIP_TYPE == 1312) || (CI_CHIP_TYPE == 1311))',
        '#define USE_EXTERNAL_CRYSTAL_OSC        0',
        '#else',
        '#define USE_EXTERNAL_CRYSTAL_OSC        1',
        '#endif',
        '#endif'
    ) -join "`r`n"
    # Match directives rather than the vendor's encoded trailing comment.
    $clockPattern = '(?ms)^#if \(\(CI_CHIP_TYPE == 1312\) \|\| \(CI_CHIP_TYPE == 1311\)\)\r?\n#define USE_EXTERNAL_CRYSTAL_OSC\s+0\r?\n#else\r?\n#define USE_EXTERNAL_CRYSTAL_OSC\s+1[^\r\n]*\r?\n#endif'
    $clockMatches = [regex]::Matches($content, $clockPattern)
    if ($clockMatches.Count -ne 1) {
        throw "Unable to make the SDK clock source selectable in: $Source"
    }
    $content = [regex]::Replace($content, $clockPattern, $arduinoClockSelection, 1)

    $selectionReplacement = @'
#if defined(CI_CHIP_CI1302)
#undef USE_CI_D02GS01J_BOARD
#undef USE_CI_D02GS02S_BOARD
#undef USE_CI_D06GT01D_BOARD
#define USE_CI_D02GS01J_BOARD       0
#define USE_CI_D02GS02S_BOARD       1
#define USE_CI_D06GT01D_BOARD       0
#elif defined(CI_CHIP_CI1303)
#undef USE_CI_D02GS01J_BOARD
#undef USE_CI_D02GS02S_BOARD
#undef USE_CI_D06GT01D_BOARD
#define USE_CI_D02GS01J_BOARD       0
#define USE_CI_D02GS02S_BOARD       0
#define USE_CI_D06GT01D_BOARD       0
#endif

#if defined(CI_CHIP_CI1303)
#define USE_CI_D03GS01J_BOARD       0
#define USE_CI_D03GS02S_BOARD       1
#else
#define USE_CI_D03GS01J_BOARD       0
#define USE_CI_D03GS02S_BOARD       0
#endif

#if (USE_CI_D02GS01J_BOARD == 1)
'@
    $branchReplacement = @'
#elif (USE_CI_D03GS01J_BOARD == 1)
#define CI_CHIP_TYPE                1303    //flash:4MB,SSOP24
#define BOARD_PORT_FILE             "CI-D03GS01J.c"
#elif (USE_CI_D03GS02S_BOARD == 1)
#define CI_CHIP_TYPE                1303    //flash:4MB,SSOP24
#define BOARD_PORT_FILE             "CI-D03GS02S.c"
#elif (USE_CI_D12GS01J_BOARD == 1)
'@

    $content = $content.Replace($boardSelection, $selectionReplacement.TrimEnd("`r", "`n"))
    $content = $content.Replace($chipBranches, $branchReplacement.TrimEnd("`r", "`n"))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    [IO.File]::WriteAllText($Destination, $content, [Text.UTF8Encoding]::new($false))
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
$variantPath = Join-Path $platformRoot "variants\$Variant"
if (-not (Test-Path -LiteralPath $variantPath -PathType Container)) {
    throw "Arduino variant directory is missing: $variantPath"
}
$variantProfiles = @{
    ci1306 = @{ Chip = 'CI1306'; Board = 'CI-D06GT01D'; Flash = '4 MB' }
    ci1302 = @{ Chip = 'CI1302'; Board = 'CI-D02GS02S'; Flash = '2 MB' }
    ci1303 = @{ Chip = 'CI1303'; Board = 'CI-D03GS02S'; Flash = '4 MB' }
}
$profile = $variantProfiles[$Variant]
if ($SkipBuild -and $Variant -ne 'ci1306') {
    throw '-SkipBuild is not supported for CI1302/CI1303 because it cannot verify which board profile produced the existing SDK objects.'
}
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
        Write-Host "Building CI13XX V2.7.12 $Variant USE_NULL integration objects with $jobs jobs ..."
        # The vendor project defaults to -flto. GNU ld --wrap cannot interpose
        # calls that GCC resolves internally between LTO units, which would
        # bypass both the Arduino scheduler hook and the ASR result hook. Build
        # the 138 source-available SDK objects without LTO; binary-only vendor archives keep
        # their original GCC 9.2 LTO payload. The vendor Makefile does not
        # track command-line flag changes, so remove its generated build tree
        # before every profile build.
        # Do not use make -B: it incorrectly treats generated source_file.mk
        # as an always-outdated prerequisite and eventually fails with "No
        # rule to make target" after compiling the object set.
        $resolvedProjectBuild = [IO.Path]::GetFullPath($projectBuild)
        $sdkPrefix = $sdkRoot.TrimEnd('\') + '\'
        if (-not $resolvedProjectBuild.StartsWith($sdkPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean SDK build directory outside SDK root: $resolvedProjectBuild"
        }
        if (Test-Path -LiteralPath $resolvedProjectBuild -PathType Container) {
            Get-ChildItem -LiteralPath $resolvedProjectBuild -Force | Remove-Item -Recurse -Force
        }
        else {
            New-Item -ItemType Directory -Force -Path $resolvedProjectBuild | Out-Null
        }

        # The generated-source rule starts with a shell comment that the
        # vendor Windows make attempts to execute as a process. Invoke the
        # shipped Lua generator directly after cleaning the build tree.
        $lua = Join-Path $sdkRoot 'tools\build-tools\bin\lua.exe'
        $makefileGenerator = Join-Path $sdkRoot 'utils\generate_makefile.lua'
        Push-Location $projectRoot
        try {
            & $lua $makefileGenerator 'source_file.prj' $sdkRoot
            if ($LASTEXITCODE -ne 0) {
                throw "SDK source-file generation failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }

        # Every Arduino variant supplies the user_config.h wrapper used for SDK
        # builds, including CI1306. This keeps the packaged configuration (where
        # the SDK message UART is disabled) authoritative for every source build.
        $includePath = $variantPath.Replace('\', '/')
        $optimization = "-Os -I`"$includePath`""
        & $make -C $projectRoot -j $jobs 'LTO_OPTION=' "O_OPTION=$optimization" 'build/offline_asr_alg_pro_sample.elf'
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
$sourceOutput = Join-Path $stagingOutput 'src'
$linkerOutput = Join-Path $stagingOutput 'ld'
$binaryOutput = Join-Path $stagingOutput 'bin'
New-Item -ItemType Directory -Force -Path $libOutput, $includeOutput, $preservedIncludeOutput, $sourceOutput, $linkerOutput, $binaryOutput | Out-Null

# Package the exact 138 translation units from the validated vendor project.
# Files included textually by those units are copied as source dependencies but
# are deliberately absent from source_file.prj, so they are not compiled twice.
$sourceManifest = Join-Path $projectRoot 'source_file.prj'
Copy-RequiredFile -Source $sourceManifest -DestinationDirectory $stagingOutput
$sourceRelativePaths = @(
    Get-Content -LiteralPath $sourceManifest | ForEach-Object {
        if ($_ -match '^\s*source-file:\s*(.+?)\s*$') {
            $matches[1].Trim().Replace('/', '\')
        }
    }
)
if ($sourceRelativePaths.Count -ne 138) {
    throw "Expected 138 compiled SDK sources in $sourceManifest, found $($sourceRelativePaths.Count)"
}

$sourceDependencies = @(
    'components\cmd_info\command_file_reader_v2.c',
    'components\cmd_info\command_info_v2.c',
    'driver\boards\CI-D02GS02S.c',
    'driver\boards\CI-D03GS02S.c',
    'driver\boards\CI-D06GT01D.c'
)
$packagedSourcePaths = @($sourceRelativePaths + $sourceDependencies | Sort-Object -Unique)
foreach ($relativePath in $packagedSourcePaths) {
    $source = Join-Path $sdkRoot $relativePath
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required SDK source is missing: $source"
    }
    $destination = Join-Path $sourceOutput $relativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

# Arduino C++ global constructors may allocate memory before main(). Initialize
# the ROM/newlib dispatch table at the end of the C runtime _init() hook, before
# __libc_init_array starts those constructors. The vendor application initializes
# it later from hardware_default_init(), so preserve that order outside Arduino.
$arduinoRuntimeInitPath = Join-Path $sourceOutput 'startup\ci130x_init.c'
$arduinoRuntimeInitContent = [IO.File]::ReadAllText($arduinoRuntimeInitPath)
$runtimeInitPattern = '(?ms)(void _init\(\)\s*\{.*?)(\r?\n\})'
$runtimeInitMatches = [regex]::Matches($arduinoRuntimeInitContent, $runtimeInitPattern)
if ($runtimeInitMatches.Count -ne 1) {
    throw "Unable to move ROM initialization ahead of Arduino constructors: $arduinoRuntimeInitPath"
}
$arduinoRuntimeInitContent = [regex]::Replace(
    $arduinoRuntimeInitContent,
    $runtimeInitPattern,
    {
        param($match)
        return $match.Groups[1].Value + @'


#if defined(CI_ARDUINO_CORE)
    maskrom_lib_init();
#endif
'@ + $match.Groups[2].Value
    },
    1)
[IO.File]::WriteAllText($arduinoRuntimeInitPath, $arduinoRuntimeInitContent, [Text.UTF8Encoding]::new($false))

# The vendor demo aborts before starting FreeRTOS when its informational PLL
# tolerance check fails. Internal-RC boards can legitimately exceed that fixed
# 10 MHz window, so Arduino builds report the condition but must still reach
# setup()/loop(). Keep the vendor behavior for non-Arduino SDK builds.
$arduinoMainPath = Join-Path $sourceOutput 'projects\offline_asr_alg_pro_sample\app\app_main\main.c'
$arduinoMainContent = [IO.File]::ReadAllText($arduinoMainPath)
$pllFatalPattern = '(?m)^(\s*)mprintf\("PLL config err!\\n"\);\r?\n\1while\(1\);'
$pllFatalMatches = [regex]::Matches($arduinoMainContent, $pllFatalPattern)
if ($pllFatalMatches.Count -ne 1) {
    throw "Unable to adapt the SDK PLL check for Arduino: $arduinoMainPath"
}
$arduinoMainContent = [regex]::Replace(
    $arduinoMainContent,
    $pllFatalPattern,
    {
        param($match)
        $indent = $match.Groups[1].Value
        return @(
            ($indent + 'mprintf("PLL config err!\n");'),
            ($indent + '#if !defined(CI_ARDUINO_CORE)'),
            ($indent + 'while(1);'),
            ($indent + '#endif')
        ) -join "`r`n"
    },
    1)

# The upstream sample main automatically starts its ASR/audio application and
# configures board peripherals before an Arduino sketch runs. Arduino must have
# neutral GPIO ownership by default, so use a minimal platform/FreeRTOS entry
# point and expose the vendor init task as an explicit opt-in API.
$vendorMainPattern = '(?ms)^int main\(void\)\r?\n\{.*?^\}\s*$'
$vendorMainMatches = [regex]::Matches($arduinoMainContent, $vendorMainPattern)
if ($vendorMainMatches.Count -ne 1) {
    throw "Unable to replace the SDK sample main for Arduino: $arduinoMainPath"
}
$arduinoMainReplacement = @'
#if defined(CI_ARDUINO_CORE)
static TaskHandle_t g_arduino_sdk_init_task = NULL;

int ci_arduino_sdk_start(void)
{
    if (g_arduino_sdk_init_task != NULL)
    {
        return 1;
    }
    return (xTaskCreate(task_init, "init task", 280, NULL, 4,
                        &g_arduino_sdk_init_task) == pdPASS) ? 1 : 0;
}
#endif

int main(void)
{
    #if defined(CI_ARDUINO_CORE)
    /* Match the proven vendor bare-metal bring-up. The ROM/newlib dispatch
     * table was initialized from _init(), before C++ constructors. Do not run
     * the sample platform_init(), watchdog, task_init(), audio or ASR paths. */
    extern void SystemInit(void);
    SystemInit();
    init_platform();

    scu_set_dma_mode(DMAINT_SEL_CHANNEL1);
    scu_set_device_reset(HAL_GDMA_BASE);
    scu_set_device_reset_release(HAL_GDMA_BASE);

    vTaskStartScheduler();
    #else
    hardware_default_init();
    platform_init();
    #if !USE_BLE
    welcome();
    #endif
    xTaskCreate(task_init, "init task", 280, NULL, 4, NULL);
    vTaskStartScheduler();
    #endif

    while(1){}
}
'@
$arduinoMainContent = [regex]::Replace(
    $arduinoMainContent,
    $vendorMainPattern,
    $arduinoMainReplacement.TrimEnd("`r", "`n"),
    1)
[IO.File]::WriteAllText($arduinoMainPath, $arduinoMainContent, [Text.UTF8Encoding]::new($false))

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
$binaryOnlyArchives = @(
    $sdkLibraries +
    'libir_data.a' +
    'libOnMicroBLE.a' +
    'libcias_crypto.a'
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

$sourceUserConfig = Join-Path $sdkRoot 'projects\offline_asr_alg_pro_sample\app\app_main\user_config.h'
Write-MultiVariantUserConfig -Source $sourceUserConfig -Destination (Join-Path $includeOutput 'user_config.h')
Write-MultiVariantUserConfig -Source $sourceUserConfig -Destination (Join-Path $preservedIncludeOutput 'projects\offline_asr_alg_pro_sample\app\app_main\user_config.h')

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

$sourceHashInput = @($packagedSourcePaths | ForEach-Object {
    $sourcePath = Join-Path $sourceOutput $_
    "$_ $((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash)"
}) -join "`n"
$sourceHashBytes = [Text.Encoding]::UTF8.GetBytes($sourceHashInput)
$sourceHasher = [Security.Cryptography.SHA256]::Create()
try {
    $sourcePayloadHash = ([BitConverter]::ToString($sourceHasher.ComputeHash($sourceHashBytes))).Replace('-', '')
}
finally {
    $sourceHasher.Dispose()
}

$manifest = @"
Generated from: CI13XX_SDK_ASR_ALG_V2.7.12
Project: projects/offline_asr_alg_pro_sample
Validated variant: $Variant
Board: $($profile.Board) / $($profile.Chip) / $($profile.Flash)
Algorithm profile: USE_NULL=1, NO_ASR_FLOW=0
SDK message UART: disabled; Arduino HardwareSerial owns UART2
Compiler: riscv-nuclei-elf-gcc 9.2.0
Compiler executable SHA256: $compilerHash
Compiled SDK source count: $($sourceRelativePaths.Count)
Packaged SDK source/dependency count: $($packagedSourcePaths.Count)
SDK source payload SHA256: $sourcePayloadHash
Header count: $($headers.Count)
SDK sources compile as non-LTO objects (required for scheduler and ASR link interposition)
Vendor archives: original GCC 9.2.0 LTO/ABI payload
Binary-only archive count: $($binaryOnlyArchives.Count)
Binary-only archives: $($binaryOnlyArchives -join ', ')

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
