[CmdletBinding()]
param(
    [string]$SdkPath = (Join-Path $PSScriptRoot '..\..\CI130X_SDK_ALG_V2.7.14'),

    [string]$ToolchainBin = $env:CHIPINTELLI_GCC_BIN,

    [ValidateSet('ci1306', 'ci1302', 'ci1303')]
    [string]$Variant = 'ci1306',

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'The V2.7.14 SDK build and packaging tools are currently supported on Windows only.'
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

function Replace-RequiredLiteral {
    param(
        [string]$Content,
        [string]$OldValue,
        [string]$NewValue,
        [int]$ExpectedCount,
        [string]$Description
    )

    $matches = [regex]::Matches($Content, [regex]::Escape($OldValue))
    if ($matches.Count -ne $ExpectedCount) {
        throw "Unable to $Description; expected $ExpectedCount match(es), found $($matches.Count)."
    }
    return $Content.Replace($OldValue, $NewValue)
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
    # Match the Windows checkout convention so a content-identical rebuild
    # does not leave these generated headers falsely marked as modified.
    $content = $content.Replace("`r`n", "`n").Replace("`n", "`r`n")
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    [IO.File]::WriteAllText($Destination, $content, [Text.UTF8Encoding]::new($false))
}

$sdkRoot = (Resolve-Path -LiteralPath $SdkPath).Path.TrimEnd('\')
$toolchain = Resolve-ToolchainBin -RequestedPath $ToolchainBin
$compiler = Join-Path $toolchain 'riscv-nuclei-elf-gcc.exe'
$compilerVersion = @(& $compiler --version)
if ($LASTEXITCODE -ne 0 -or ($compilerVersion -join "`n") -notmatch '(?m)\b9\.2\.0\b') {
    throw "CI13XX SDK V2.7.14 requires Nuclei GCC 9.2.0: $compiler"
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
        Write-Host "Building CI13XX V2.7.14 $Variant USE_NULL integration objects with $jobs jobs ..."
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

# The closed IR database archive looks up user-file ID 0 during ir_init().
# Arduino keeps ID 0 available for the default TTS dictionary, so add a
# task-scoped compatibility alias that can map only that initialization lookup
# to a sketch-provided physical resource such as ID 50000.
$flashDataSourcePath = Join-Path $sourceOutput 'components\flash_control\flash_control_src\ci_flash_data_info.c'
$flashDataSourceContent = [IO.File]::ReadAllText($flashDataSourcePath)
$flashDataNewline = if ($flashDataSourceContent.Contains("`r`n")) { "`r`n" } else { "`n" }
$oldNvdmReadyOrder =
    ('    set_ci_flash_data_info_init_flag();' + [char]0x20) +
    $flashDataNewline +
    '    cinv_init(partition_table.nv_data_offset, partition_table.nv_data_size);'
$flashDataSourceContent = Replace-RequiredLiteral `
    -Content $flashDataSourceContent `
    -OldValue $oldNvdmReadyOrder `
    -NewValue @'
    cinv_init(partition_table.nv_data_offset, partition_table.nv_data_size);
    /* Publish readiness only after NVDM has created its mutex and scanned the
     * partition. Arduino storage libraries may run concurrently with this
     * initialization task. */
    set_ci_flash_data_info_init_flag();
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "publish NVDM readiness after initialization in $flashDataSourcePath"
$flashDataSourceContent = Replace-RequiredLiteral `
    -Content $flashDataSourceContent `
    -OldValue 'static partition_table_t partition_table = {0};' `
    -NewValue @'
static partition_table_t partition_table = {0};

typedef struct
{
    bool active;
    uint16_t logical_id;
    uint16_t physical_id;
    TaskHandle_t owner;
} userfile_id_alias_t;

/*
 * Some vendor algorithm libraries use a fixed user-file ID internally.  Keep
 * that compatibility shim scoped to the task performing initialization so an
 * unrelated TTS or application lookup can never observe the temporary alias.
 */
static userfile_id_alias_t userfile_id_alias = {false, 0, 0, NULL};
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "add the task-scoped user-file alias state to $flashDataSourcePath"
$userFileLookupPattern = '(?m)^    if \(\(NULL == p_file_addr\) \|\|\(NULL == p_file_size\)\)\r?\n    \{\r?\n        return 1;\r?\n    \}\r?\n\r?\n    if \(get_file_addr\(partition_table\.user_file_offset, file_id, p_file_addr, p_file_size\)\)\r?$'
$userFileLookupMatches = [regex]::Matches($flashDataSourceContent, $userFileLookupPattern)
if ($userFileLookupMatches.Count -ne 1) {
    throw "Unable to scope user-file ID aliases to the current task in $flashDataSourcePath; expected 1 match, found $($userFileLookupMatches.Count)."
}
$userFileLookupReplacement = @'
    if ((NULL == p_file_addr) ||(NULL == p_file_size))
    {
        return 1;
    }

    userfile_id_alias_t alias_snapshot;
    taskENTER_CRITICAL();
    alias_snapshot = userfile_id_alias;
    taskEXIT_CRITICAL();

    if (alias_snapshot.active &&
        (alias_snapshot.logical_id == file_id) &&
        (alias_snapshot.owner == xTaskGetCurrentTaskHandle()))
    {
        file_id = alias_snapshot.physical_id;
    }

    if (get_file_addr(partition_table.user_file_offset, file_id, p_file_addr, p_file_size))
'@.TrimEnd("`r", "`n")
$flashDataSourceContent = [regex]::Replace(
    $flashDataSourceContent,
    $userFileLookupPattern,
    $userFileLookupReplacement,
    1)
$flashDataSourceContent = Replace-RequiredLiteral `
    -Content $flashDataSourceContent `
    -OldValue 'partition_table_t * get_partition_table(void)' `
    -NewValue @'
bool ci_userfile_id_alias_begin(uint16_t logical_id, uint16_t physical_id)
{
    TaskHandle_t owner = xTaskGetCurrentTaskHandle();
    if ((NULL == owner) || (logical_id == physical_id))
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (userfile_id_alias.active)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    userfile_id_alias.logical_id = logical_id;
    userfile_id_alias.physical_id = physical_id;
    userfile_id_alias.owner = owner;
    userfile_id_alias.active = true;
    taskEXIT_CRITICAL();
    return true;
}

void ci_userfile_id_alias_end(void)
{
    TaskHandle_t owner = xTaskGetCurrentTaskHandle();

    taskENTER_CRITICAL();
    if (userfile_id_alias.active && (userfile_id_alias.owner == owner))
    {
        userfile_id_alias.active = false;
        userfile_id_alias.logical_id = 0;
        userfile_id_alias.physical_id = 0;
        userfile_id_alias.owner = NULL;
    }
    taskEXIT_CRITICAL();
}

partition_table_t * get_partition_table(void)
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "add the task-scoped user-file alias API to $flashDataSourcePath"
$flashDataSourceContent = $flashDataSourceContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
[IO.File]::WriteAllText($flashDataSourcePath, $flashDataSourceContent, [Text.UTF8Encoding]::new($false))

# Keep ChipIntelliAudio mute authoritative across SDK-owned volume changes.
# The weak hook preserves the upstream behavior when the Arduino audio library
# is not linked into a sketch.
$audioPlayDeviceSourcePath = Join-Path $sourceOutput 'components\player\audio_play\audio_play_device.c'
$audioPlayDeviceSourceContent = [IO.File]::ReadAllText($audioPlayDeviceSourcePath)
$audioPlayDeviceSourceContent = Replace-RequiredLiteral `
    -Content $audioPlayDeviceSourceContent `
    -OldValue @'
void audio_play_set_vol_gain(int32_t gain)
{
    g_audio_play_gain = gain;
'@.TrimEnd("`r", "`n") `
    -NewValue @'
void audio_play_set_vol_gain(int32_t gain)
{
#if defined(CI_ARDUINO_CORE)
    /* Keep Arduino-level mute authoritative even when an SDK command changes
     * the volume through vol_set() or another internal path. The hook is weak
     * so sketches that do not use ChipIntelliAudio keep the vendor behavior. */
    extern int chipintelli_audio_mute_requested(void) __attribute__((weak));
    if ((chipintelli_audio_mute_requested != NULL) &&
        chipintelli_audio_mute_requested())
    {
        gain = 0;
    }
#endif
    g_audio_play_gain = gain;
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "add the Arduino audio-mute hook to $audioPlayDeviceSourcePath"
$audioPlayDeviceSourceContent = $audioPlayDeviceSourceContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
[IO.File]::WriteAllText($audioPlayDeviceSourcePath, $audioPlayDeviceSourceContent, [Text.UTF8Encoding]::new($false))

# The vendor prompt player invokes completion callbacks while holding its
# non-recursive mutex. Route every mutex release through an Arduino weak hook;
# ChipIntelliAudio records callbacks while locked and drains them from this
# post-unlock hook, avoiding prompt API re-entry deadlocks.
$promptPlayerSourcePath = Join-Path $sourceOutput 'components\cmd_info\prompt_player.c'
$promptPlayerSourceContent = [IO.File]::ReadAllText($promptPlayerSourcePath).Replace("`r`n", "`n")
$promptUnlockPattern = '(?m)^(?<indent>[ \t]*)if \(prompt_player\.semaphore\)\r?\n\k<indent>\{\r?\n\k<indent>    xSemaphoreGive\(prompt_player\.semaphore\);\r?\n\k<indent>\}'
$promptUnlockMatches = [regex]::Matches($promptPlayerSourceContent, $promptUnlockPattern)
if ($promptUnlockMatches.Count -ne 8) {
    throw "Unable to route all prompt-player mutex releases through the Arduino hook in: $promptPlayerSourcePath"
}
$promptPlayerSourceContent = [regex]::Replace(
    $promptPlayerSourceContent,
    $promptUnlockPattern,
    {
        param($match)
        return $match.Groups['indent'].Value + 'prompt_player_unlock();'
    })
$promptPlayerSourceContent = Replace-RequiredLiteral `
    -Content $promptPlayerSourceContent `
    -OldValue @'
    #if USE_AEC_MODULE
    0,          //timer_handle
    #endif
};
'@.TrimEnd("`r", "`n").Replace("`r`n", "`n") `
    -NewValue @'
    #if USE_AEC_MODULE
    0,          //timer_handle
    #endif
};

#if defined(CI_ARDUINO_CORE)
extern void chipintelli_sdk_prompt_unlocked(void) __attribute__((weak));
#endif

/*
 * The vendor player normally invokes completion callbacks while holding this
 * mutex. Arduino callbacks record the event there and are dispatched by the
 * weak hook only after the mutex has been released.
 */
static void prompt_player_unlock(void)
{
    if (prompt_player.semaphore)
    {
        xSemaphoreGive(prompt_player.semaphore);
    }
#if defined(CI_ARDUINO_CORE)
    if (chipintelli_sdk_prompt_unlocked != NULL)
    {
        chipintelli_sdk_prompt_unlocked();
    }
#endif
}
'@.TrimEnd("`r", "`n").Replace("`r`n", "`n") `
    -ExpectedCount 1 `
    -Description "add the post-unlock Arduino prompt hook to $promptPlayerSourcePath"
$promptPlayerSourceContent = $promptPlayerSourceContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
[IO.File]::WriteAllText($promptPlayerSourcePath, $promptPlayerSourceContent, [Text.UTF8Encoding]::new($false))

# ChipIntelliASR.begin() waits for the capture path, not merely for the later
# system-message consumer. Publish readiness immediately after codec/DMA input
# starts so a delayed queue consumer cannot cause a false initialization timeout.
$audioInputSourcePath = Join-Path $sourceOutput 'components\audio_in_manage\audio_in_manage_inner.c'
$audioInputSourceContent = [IO.File]::ReadAllText($audioInputSourcePath)
$audioInputSourceContent = Replace-RequiredLiteral `
    -Content $audioInputSourceContent `
    -OldValue '    xTaskResumeAll();' `
    -NewValue @'
#if defined(CI_ARDUINO_CORE)
    /*
     * ASR begin() is waiting specifically for the capture path.  Notify it
     * here, after the ASR core is up and the input codec/DMA has started,
     * instead of depending solely on the later system-message consumer.
     */
    extern void chipintelli_sdk_notify_ready(void);
    chipintelli_sdk_notify_ready();
#endif
    xTaskResumeAll();
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "publish Arduino capture readiness in $audioInputSourcePath"
$audioInputSourceContent = $audioInputSourceContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
[IO.File]::WriteAllText($audioInputSourcePath, $audioInputSourceContent, [Text.UTF8Encoding]::new($false))

# Expose allocation failure from the vendor system-message initializer. The
# original void API only logged queue/mutex allocation failures and then let
# the rest of the SDK start with invalid handles.
$systemMessageSourcePath = Join-Path $sourceOutput 'projects\offline_asr_alg_pro_sample\app\app_main\system_msg_deal.c'
$systemMessageSourceContent = [IO.File]::ReadAllText($systemMessageSourcePath)
$systemMessageSourceContent = Replace-RequiredLiteral `
    -Content $systemMessageSourceContent `
    -OldValue 'void sys_msg_task_initial(void)' `
    -NewValue 'BaseType_t sys_msg_task_initial(void)' `
    -ExpectedCount 1 `
    -Description "make the SDK system-message initializer report failure in $systemMessageSourcePath"
$systemMessageFunctionEndPattern = '(?ms)(BaseType_t sys_msg_task_initial\(void\)\s*\{.*?)(\r?\n\}\r?\n\r?\n\r?\n/\*\*)'
$systemMessageFunctionEndMatches = [regex]::Matches($systemMessageSourceContent, $systemMessageFunctionEndPattern)
if ($systemMessageFunctionEndMatches.Count -ne 1) {
    throw "Unable to add the SDK system-message initializer result in: $systemMessageSourcePath"
}
$systemMessageSourceContent = [regex]::Replace(
    $systemMessageSourceContent,
    $systemMessageFunctionEndPattern,
    {
        param($match)
        return $match.Groups[1].Value + "`r`n    return (sys_msg_queue != NULL && WakeupMutex != NULL) ? pdPASS : pdFAIL;" + $match.Groups[2].Value
    },
    1)
$systemMessageSourceContent = Replace-RequiredLiteral `
    -Content $systemMessageSourceContent `
    -OldValue '#include "cwsl_manage.h"' `
    -NewValue @'
#include "cwsl_manage.h"
#if defined(CI_ARDUINO_CORE)
extern void chipintelli_sdk_notify_ready(void);

static volatile uint32_t s_arduino_sys_message_count = 0;
static volatile uint32_t s_arduino_asr_message_count = 0;
static volatile uint32_t s_arduino_cmd_info_message_count = 0;
static volatile uint32_t s_arduino_audio_started_message_count = 0;
static volatile uint32_t s_arduino_asr_status_count[6] = {0};

uint32_t ci_arduino_sys_message_count(void)
{
    return s_arduino_sys_message_count;
}

uint32_t ci_arduino_asr_message_count(void)
{
    return s_arduino_asr_message_count;
}

uint32_t ci_arduino_cmd_info_message_count(void)
{
    return s_arduino_cmd_info_message_count;
}

uint32_t ci_arduino_audio_started_message_count(void)
{
    return s_arduino_audio_started_message_count;
}

uint32_t ci_arduino_asr_status_count(uint32_t status)
{
    if (status >= (sizeof(s_arduino_asr_status_count) /
                   sizeof(s_arduino_asr_status_count[0])))
    {
        return 0;
    }
    return s_arduino_asr_status_count[status];
}
#endif
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "declare Arduino SDK readiness reporting in $systemMessageSourcePath"
$audioReadyPattern = '(?ms)(\s*#if !UART_BAUDRATE_CALIBRATE\r?\n\s*\{\r?\n\s*sys_power_on_hook\(\);\r?\n\s*\}\r?\n\s*#endif)(\r?\n\s*break;\r?\n\s*\}\r?\n\s*default:)'
$audioReadyMatches = [regex]::Matches($systemMessageSourceContent, $audioReadyPattern)
if ($audioReadyMatches.Count -ne 1) {
    throw "Unable to publish Arduino readiness after audio input starts in: $systemMessageSourcePath"
}
$systemMessageSourceContent = [regex]::Replace(
    $systemMessageSourceContent,
    $audioReadyPattern,
    {
        param($match)
        return $match.Groups[1].Value + @'

                    #if defined(CI_ARDUINO_CORE)
                    chipintelli_sdk_notify_ready();
                    #endif
'@ + $match.Groups[2].Value
    },
    1)
$systemMessageLoopPattern = '(?m)^        if\(pdPASS == err\)\r?\n        \{\r?$'
$systemMessageLoopMatches =
    [regex]::Matches($systemMessageSourceContent, $systemMessageLoopPattern)
if ($systemMessageLoopMatches.Count -ne 1) {
    throw "Unable to add the Arduino system-message counter in: $systemMessageSourcePath"
}
$systemMessageSourceContent = [regex]::Replace(
    $systemMessageSourceContent,
    $systemMessageLoopPattern,
    {
        param($match)
        return $match.Value + @'

#if defined(CI_ARDUINO_CORE)
            ++s_arduino_sys_message_count;
#endif
'@
    },
    1)
$systemMessageSourceContent = Replace-RequiredLiteral `
    -Content $systemMessageSourceContent `
    -OldValue '                    asr_rev_data = &(rev_msg.msg_data.asr_data);' `
    -NewValue @'
                    asr_rev_data = &(rev_msg.msg_data.asr_data);
#if defined(CI_ARDUINO_CORE)
                    ++s_arduino_asr_message_count;
                    if ((uint32_t)asr_rev_data->asr_status <
                        (sizeof(s_arduino_asr_status_count) /
                         sizeof(s_arduino_asr_status_count[0])))
                    {
                        ++s_arduino_asr_status_count[(uint32_t)asr_rev_data->asr_status];
                    }
#endif
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "count Arduino ASR messages in $systemMessageSourcePath"
$systemMessageSourceContent = Replace-RequiredLiteral `
    -Content $systemMessageSourceContent `
    -OldValue '                    cmd_info_rev_data = &(rev_msg.msg_data.cmd_info_data);' `
    -NewValue @'
                    cmd_info_rev_data = &(rev_msg.msg_data.cmd_info_data);
#if defined(CI_ARDUINO_CORE)
                    ++s_arduino_cmd_info_message_count;
#endif
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "count Arduino command-info messages in $systemMessageSourcePath"
$systemMessageSourceContent = Replace-RequiredLiteral `
    -Content $systemMessageSourceContent `
    -OldValue '                    uint8_t volume;' `
    -NewValue @'
#if defined(CI_ARDUINO_CORE)
                    ++s_arduino_audio_started_message_count;
#endif
                    uint8_t volume;
'@.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "count Arduino audio-start messages in $systemMessageSourcePath"
$systemMessageSourceContent = $systemMessageSourceContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
[IO.File]::WriteAllText($systemMessageSourcePath, $systemMessageSourceContent, [Text.UTF8Encoding]::new($false))

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

# Turn the vendor's asynchronous, fire-and-forget task_init() into an
# observable Arduino startup state. Every resource needed by the default ASR
# flow must exist before begin() reports success.
$taskInitDeclaration = 'static void task_init(void *p_arg)'
$arduinoInitSupport = @'
#if defined(CI_ARDUINO_CORE)
extern void chipintelli_sdk_notify_failed(void);

#define CI_ARDUINO_INIT_FAILED()       \
    do                                 \
    {                                  \
        chipintelli_sdk_notify_failed(); \
        vTaskDelete(NULL);             \
        return;                        \
    } while (0)
#define CI_ARDUINO_REQUIRE_OK(call)     \
    do                                 \
    {                                  \
        if ((call) != RETURN_OK)       \
        {                              \
            CI_ARDUINO_INIT_FAILED();  \
        }                              \
    } while (0)
#define CI_ARDUINO_REQUIRE_PASS(call)   \
    do                                 \
    {                                  \
        if ((call) != pdPASS)          \
        {                              \
            CI_ARDUINO_INIT_FAILED();  \
        }                              \
    } while (0)
#else
#define CI_ARDUINO_INIT_FAILED() return
#define CI_ARDUINO_REQUIRE_OK(call) ((void)(call))
#define CI_ARDUINO_REQUIRE_PASS(call) ((void)(call))
#endif
'@
$arduinoMainContent = Replace-RequiredLiteral `
    -Content $arduinoMainContent `
    -OldValue $taskInitDeclaration `
    -NewValue ($arduinoInitSupport.TrimEnd("`r", "`n") + "`r`n`r`n" + $taskInitDeclaration) `
    -ExpectedCount 1 `
    -Description "add Arduino SDK initialization state reporting to $arduinoMainPath"

$requiredMainCallReplacements = @(
    @{ Old = '    audio_play_init();'; New = '    CI_ARDUINO_REQUIRE_OK(audio_play_init());'; Count = 2; Name = 'check audio player initialization' },
    @{ Old = '    sap_init();'; New = '    CI_ARDUINO_REQUIRE_PASS(sap_init());'; Count = 1; Name = 'check simple audio player initialization' },
    @{ Old = '    xTaskCreate(audio_in_manage_inner_task, "audio_in_manage_inner_task", 300, NULL, 4, NULL);'; New = '    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(audio_in_manage_inner_task, "audio_in_manage_inner_task", 300, NULL, 4, NULL));'; Count = 1; Name = 'check the audio input task' },
    @{ Old = ('    xTaskCreate(doa_out_result_hand_task, "doa_out_result_hand_task", 100, NULL, 4, NULL);' + ' '); New = '    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(doa_out_result_hand_task, "doa_out_result_hand_task", 100, NULL, 4, NULL));'; Count = 1; Name = 'check the DOA task' },
    @{ Old = "`txTaskCreate(nlpTaskManageProcess,`"nlpTaskManageProcess`",480,NULL,4,NULL);"; New = "`tCI_ARDUINO_REQUIRE_PASS(xTaskCreate(nlpTaskManageProcess,`"nlpTaskManageProcess`",480,NULL,4,NULL));"; Count = 1; Name = 'check the NLP task' },
    @{ Old = '    sys_msg_task_initial();'; New = '    CI_ARDUINO_REQUIRE_PASS(sys_msg_task_initial());'; Count = 1; Name = 'check the system-message resources' },
    @{ Old = '    xTaskCreate(UserTaskManageProcess,"UserTaskManageProcess",480,NULL,4,NULL);'; New = '    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(UserTaskManageProcess,"UserTaskManageProcess",480,NULL,4,NULL));'; Count = 1; Name = 'check the user task' },
    @{ Old = '    xTaskCreate(uart_data_handle_task,"uart_data_handle_task", 480, NULL, 4, NULL);'; New = '    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(uart_data_handle_task,"uart_data_handle_task", 480, NULL, 4, NULL));'; Count = 1; Name = 'check the code-switch UART task' }
)
foreach ($replacement in $requiredMainCallReplacements) {
    $arduinoMainContent = Replace-RequiredLiteral `
        -Content $arduinoMainContent `
        -OldValue $replacement.Old `
        -NewValue $replacement.New `
        -ExpectedCount $replacement.Count `
        -Description "$($replacement.Name) in $arduinoMainPath"
}

$initFailurePatterns = @(
    '(?ms)(if \(nlp_module_init\(\) != NLP_STATE_OK\)[^\r\n]*\r?\n\s*\{\r?\n\s*mprintf\("nlp module init error\.\.\.\\r\\n"\);\r?\n\s*)return;[ \t]*',
    '(?ms)(if\(!record_play_test_init\(\)\)[^\r\n]*\r?\n\s*\{\r?\n\s*mprintf\("record_play_test_init init error\.\.\.\\r\\n"\);\r?\n\s*)return;[ \t]*'
)
foreach ($failurePattern in $initFailurePatterns) {
    $failureMatches = [regex]::Matches($arduinoMainContent, $failurePattern)
    if ($failureMatches.Count -ne 1) {
        throw "Unable to adapt an SDK initialization failure path in: $arduinoMainPath"
    }
    $arduinoMainContent = [regex]::Replace(
        $arduinoMainContent,
        $failurePattern,
        {
            param($match)
            return $match.Groups[1].Value + 'CI_ARDUINO_INIT_FAILED();'
        },
        1)
}

$lateUserTaskPattern = '(?ms)    /\*user app[^\r\n]*\*/\r?\n    userapp_initial\(\);\r?\n    /\*[^\r\n]*\*/\r?\n    CI_ARDUINO_REQUIRE_PASS\(sys_msg_task_initial\(\)\);\r?\n    CI_ARDUINO_REQUIRE_PASS\(xTaskCreate\(UserTaskManageProcess,"UserTaskManageProcess",480,NULL,4,NULL\)\);'
$lateUserTaskMatches = [regex]::Matches($arduinoMainContent, $lateUserTaskPattern)
if ($lateUserTaskMatches.Count -ne 1) {
    throw "Unable to move the SDK system-message consumer ahead of producers in: $arduinoMainPath"
}
$arduinoMainContent = [regex]::Replace($arduinoMainContent, $lateUserTaskPattern, '', 1)

$audioInputTaskCall = '    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(audio_in_manage_inner_task, "audio_in_manage_inner_task", 300, NULL, 4, NULL));'
$earlyMessageConsumer = @'
    /* Create the consumer queue/task before audio or protocol producers can
     * publish their first message. */
    CI_ARDUINO_REQUIRE_PASS(sys_msg_task_initial());
    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(UserTaskManageProcess,"UserTaskManageProcess",480,NULL,4,NULL));
    userapp_initial();
    CI_ARDUINO_REQUIRE_PASS(xTaskCreate(audio_in_manage_inner_task, "audio_in_manage_inner_task", 300, NULL, 4, NULL));
'@
$arduinoMainContent = Replace-RequiredLiteral `
    -Content $arduinoMainContent `
    -OldValue $audioInputTaskCall `
    -NewValue $earlyMessageConsumer.TrimEnd("`r", "`n") `
    -ExpectedCount 1 `
    -Description "start the SDK system-message consumer before producers in $arduinoMainPath"

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
$ciToolKitSource = Join-Path $sdkRoot 'tools\ci-tool-kit.exe'
if (-not (Test-Path -LiteralPath $ciToolKitSource -PathType Leaf)) {
    # The official V2.7.14 archive omits the standalone executable while the
    # Arduino package still needs it for post-build image composition. Preserve
    # the already validated package copy when regenerating the SDK payload.
    $packagedCiToolKit = Join-Path $sdkOutput 'bin\ci-tool-kit.exe'
    if (-not (Test-Path -LiteralPath $packagedCiToolKit -PathType Leaf)) {
        throw "The SDK and current Arduino payload both lack ci-tool-kit.exe: $ciToolKitSource"
    }
    $expectedCiToolKitHash = 'F10CBF3C262AF9375251DB48A1941D79B14FF9AEB3D2B0BE12041B25B7EE095E'
    $packagedCiToolKitHash =
        (Get-FileHash -LiteralPath $packagedCiToolKit -Algorithm SHA256).Hash
    if ($packagedCiToolKitHash -ne $expectedCiToolKitHash) {
        throw "The preserved ci-tool-kit.exe SHA256 is $packagedCiToolKitHash; expected $expectedCiToolKitHash."
    }
    Write-Warning "V2.7.14 omits ci-tool-kit.exe; preserving the validated Arduino payload copy."
    $ciToolKitSource = $packagedCiToolKit
}
Copy-RequiredFile -Source $ciToolKitSource -DestinationDirectory $binaryOutput
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

foreach ($systemMessageHeaderPath in @(
    (Join-Path $includeOutput 'system_msg_deal.h'),
    (Join-Path $preservedIncludeOutput 'projects\offline_asr_alg_pro_sample\app\app_main\system_msg_deal.h')
)) {
    $systemMessageHeaderContent = [IO.File]::ReadAllText($systemMessageHeaderPath)
    $systemMessageHeaderContent = Replace-RequiredLiteral `
        -Content $systemMessageHeaderContent `
        -OldValue 'void sys_msg_task_initial(void);' `
        -NewValue 'BaseType_t sys_msg_task_initial(void);' `
        -ExpectedCount 1 `
        -Description "update the system-message initializer declaration in $systemMessageHeaderPath"
    $systemMessageHeaderContent = $systemMessageHeaderContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
    [IO.File]::WriteAllText($systemMessageHeaderPath, $systemMessageHeaderContent, [Text.UTF8Encoding]::new($false))
}

foreach ($flashDataHeaderPath in @(
    (Join-Path $includeOutput 'ci_flash_data_info.h'),
    (Join-Path $preservedIncludeOutput 'components\flash_control\flash_control_inc\ci_flash_data_info.h')
)) {
    $flashDataHeaderContent = [IO.File]::ReadAllText($flashDataHeaderPath)
    $flashDataHeaderContent = Replace-RequiredLiteral `
        -Content $flashDataHeaderContent `
        -OldValue 'extern uint32_t get_userfile_addr(uint16_t file_id, uint32_t *p_file_addr, uint32_t *p_file_size);' `
        -NewValue @'
extern uint32_t get_userfile_addr(uint16_t file_id, uint32_t *p_file_addr, uint32_t *p_file_size);

/*
 * Temporarily remap one user-file ID for lookups made by the current FreeRTOS
 * task. Nested aliases are rejected. The task that begins an alias must end it.
 */
extern bool ci_userfile_id_alias_begin(uint16_t logical_id, uint16_t physical_id);
extern void ci_userfile_id_alias_end(void);
'@.TrimEnd("`r", "`n") `
        -ExpectedCount 1 `
        -Description "declare the task-scoped user-file alias API in $flashDataHeaderPath"
    $flashDataHeaderContent = $flashDataHeaderContent.Replace("`r`n", "`n").Replace("`n", "`r`n")
    [IO.File]::WriteAllText($flashDataHeaderPath, $flashDataHeaderContent, [Text.UTF8Encoding]::new($false))
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
Generated from: CI130X_SDK_ALG_V2.7.14
Project: projects/offline_asr_alg_pro_sample
Validated variant: $Variant
Board: $($profile.Board) / $($profile.Chip) / $($profile.Flash)
Algorithm profile: USE_NULL=1, NO_ASR_FLOW=0
SDK message UART: disabled; Arduino HardwareSerial owns UART2
NVDM readiness: published only after cinv_init() completes
Audio mute compatibility: weak gain hook for ChipIntelliAudio
Prompt callback safety: weak hook dispatched after the prompt mutex is released
IR database compatibility: task-scoped user-file ID alias (Arduino default physical ID 50000)
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
