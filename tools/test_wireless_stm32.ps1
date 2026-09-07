param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ArduinoData = "$env:LOCALAPPDATA/Arduino15",
    [string]$BuildDirectory = '',
    [string]$Fqbn = 'STMicroelectronics:stm32:Nucleo_64:pnum=NUCLEO_F411RE',
    [string[]]$Examples = @('LinkDiagnostics', 'PortableWiFi', 'PortableMQTTBLE', 'STM32Peripheral')
)
$ErrorActionPreference = 'Stop'
$platformRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory) } else { Join-Path $platformRoot '.build/wireless' }
$sketchbook = Join-Path $testRoot 'sketchbook'
New-Item -ItemType Directory -Force $sketchbook | Out-Null

# Use installed STM32 tools with an isolated sketchbook; no CI core or compiler
# installation is required. Keep this configuration separate from CI builds.
$config = Join-Path $testRoot 'arduino-cli-stm32.yaml'
$dataYaml = [IO.Path]::GetFullPath($ArduinoData).Replace('\', '/').Replace("'", "''")
$userYaml = $sketchbook.Replace('\', '/').Replace("'", "''")
$downloadYaml = (Join-Path $testRoot 'downloads').Replace('\', '/').Replace("'", "''")
$yaml = "directories:`n  data: '$dataYaml'`n  user: '$userYaml'`n  downloads: '$downloadYaml'`n"
[IO.File]::WriteAllText($config, $yaml, [Text.UTF8Encoding]::new($false))

$coreJson = & $ArduinoCli core list --config-file $config --format json
if ($LASTEXITCODE) { throw 'Cannot inspect installed Arduino cores.' }
$coreData = $coreJson | ConvertFrom-Json
$platforms = if ($coreData.platforms) { $coreData.platforms } else { $coreData }
$core = $platforms | Where-Object { $_.id -eq 'STMicroelectronics:stm32' } | Select-Object -First 1
$coreVersion = if ($core.installed) { $core.installed } else { $core.installed_version }
if ($coreVersion -ne '3.0.0') {
    throw 'Install STM32duino 3.0.0 first (STMicroelectronics:stm32@3.0.0). This script does not install or replace board cores.'
}

$libraryRoots = @('ESPLink', 'WiFi', 'BLE') | ForEach-Object {
    $path = Join-Path $platformRoot "libraries/$_"
    if (-not (Test-Path -LiteralPath (Join-Path $path 'library.properties') -PathType Leaf)) {
        throw "Missing local library: $path"
    }
    $path
}
if ($Examples -contains 'PortableMQTTBLE') {
    $mqttProperties = Join-Path $sketchbook 'libraries/ArduinoMqttClient/library.properties'
    $mqttInstalled = (Test-Path -LiteralPath $mqttProperties -PathType Leaf) -and
        ([IO.File]::ReadAllText($mqttProperties) -match '(?m)^version=0\.1\.8\s*$')
    if (-not $mqttInstalled) {
        & $ArduinoCli lib install 'ArduinoMqttClient@0.1.8' --config-file $config
        if ($LASTEXITCODE) { throw 'ArduinoMqttClient 0.1.8 installation failed.' }
    }
}

$exampleLibraries = @{
    LinkDiagnostics = 'ESPLink'
    PortableWiFi = 'WiFi'
    PortableMQTTBLE = 'WiFi'
    STM32Peripheral = 'BLE'
}
foreach ($name in $Examples) {
    if (-not $exampleLibraries.ContainsKey($name)) { throw "Unknown STM32 validation example: $name" }
    $library = $exampleLibraries[$name]
    $source = Join-Path $platformRoot "libraries/$library/examples/$name/$name.ino"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing example: $source" }
    $sketch = Join-Path $testRoot "sketches/STM32-$name/$name"
    New-Item -ItemType Directory -Force $sketch | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $sketch "$name.ino") -Force

    # STM32 acceptance configuration: reserve a 4096-byte UART RX ring. This is
    # not a measured throughput guarantee. build_opt.h preserves the STM32 core's
    # own build.extra_flags (including version and board configuration macros).
    [IO.File]::WriteAllText((Join-Path $sketch 'build_opt.h'), "-DSERIAL_RX_BUFFER_SIZE=4096`n", [Text.UTF8Encoding]::new($false))
    $log = Join-Path $testRoot "STM32-$name.log"
    $build = Join-Path $testRoot "STM32-$name-build"
    & $ArduinoCli compile --config-file $config --fqbn $Fqbn `
        --library $libraryRoots[0] --library $libraryRoots[1] --library $libraryRoots[2] `
        --build-path $build $sketch *> $log
    $result = $LASTEXITCODE
    Get-Content -LiteralPath $log -Tail 25
    if ($result) { throw "STM32 build failed: $name; log: $log" }
    Write-Host "PASS: STM32 $name ($Fqbn)"
}
