param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ArduinoData = "$env:LOCALAPPDATA/Arduino15",
    [string[]]$Examples = @('LinkDiagnostics', 'WiFiBasics', 'WiFiUDP', 'WiFiTLSTest', 'MQTTClient', 'MQTTBLE', 'CI1306Peripheral', 'CI1306Central', 'CI1306ReadCallback', 'CI1306EncryptedValue')
)
$ErrorActionPreference = 'Stop'
$platformRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot = Join-Path $platformRoot '.build/wireless'
$sketchbook = Join-Path $testRoot 'sketchbook'
$hardware = Join-Path $sketchbook 'hardware/ciwireless'
New-Item -ItemType Directory -Force $hardware | Out-Null
$junction = Join-Path $hardware 'ci13xx'
if (-not (Test-Path -LiteralPath $junction)) {
    New-Item -ItemType Junction -Path $junction -Target $platformRoot | Out-Null
}
$compilerRoot = Join-Path $ArduinoData 'packages/chipintelli/tools/riscv-gcc/9.2.0'
if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot 'bin/riscv-nuclei-elf-g++.exe'))) {
    throw "Install the chipintelli Nuclei GCC 9.2.0 toolchain first: $compilerRoot"
}
$config = Join-Path $testRoot 'arduino-cli.yaml'
$yaml = "directories:`n  data: '$($ArduinoData.Replace('\','/'))'`n  user: '$($sketchbook.Replace('\','/'))'`n  downloads: '$($testRoot.Replace('\','/'))/downloads'`n"
[IO.File]::WriteAllText($config, $yaml, [Text.UTF8Encoding]::new($false))
if ($Examples -contains 'MQTTClient' -or $Examples -contains 'MQTTBLE') {
    & $ArduinoCli lib install 'ArduinoMqttClient@0.1.8' --config-file $config
    if ($LASTEXITCODE) { throw 'ArduinoMqttClient installation failed' }
}
foreach ($name in $Examples) {
    $library = if ($name.StartsWith('CI1306') -or $name -in @('ESPLinkPeripheral', 'ESPLinkCentral')) { 'BLE' } elseif ($name -eq 'LinkDiagnostics') { 'ESPLink' } else { 'WiFi' }
    $source = Join-Path $platformRoot "libraries/$library/examples/$name/$name.ino"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing example: $source" }
    $sketch = Join-Path $testRoot "sketches/$name"
    New-Item -ItemType Directory -Force $sketch | Out-Null
    Copy-Item -LiteralPath $source -Destination (Join-Path $sketch "$name.ino") -Force
    $log = Join-Path $testRoot "$name.log"
    & $ArduinoCli compile --config-file $config --fqbn 'ciwireless:ci13xx:ci1306:Algorithm=null' --build-property "runtime.tools.riscv-gcc.path=$($compilerRoot.Replace('\','/'))" --build-path (Join-Path $testRoot "$name-build") $sketch *> $log
    $result = $LASTEXITCODE
    Get-Content -LiteralPath $log -Tail 25
    if ($result) { throw "Build failed: $name; log: $log" }
    Write-Host "PASS: $name"
}
