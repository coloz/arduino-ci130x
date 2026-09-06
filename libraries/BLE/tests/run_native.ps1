param([string]$BuildDirectory = '')
# Run in an MSVC developer shell. Output stays outside the library directory.
$ErrorActionPreference = 'Stop'
$bleRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repoRoot = [IO.Path]::GetFullPath((Join-Path $bleRoot '../..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '.build/wireless-portable/ble-native' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$sharedRoot = Join-Path $bleRoot '../ESPLink/src'
$common = @('/nologo', '/EHsc', '/std:c++17', '/W4', '/WX',
    '/I', (Join-Path $PSScriptRoot 'fakes'), '/I', (Join-Path $bleRoot 'src'), '/I', $sharedRoot,
    (Join-Path $PSScriptRoot 'transport_test.cpp'), (Join-Path $bleRoot 'src/utility/HCIESPLinkTransport.cpp'))
$upstreamBackends = @('HCICordioTransport', 'HCINinaSpiTransport', 'HCISilabsTransport',
    'HCIUartTransport', 'HCIVirtualTransport', 'HCIVirtualTransportAT', 'HCIVirtualTransportRPC',
    'HCIVirtualTransportZephyr') | ForEach-Object { Join-Path $bleRoot "src/utility/$_.cpp" }
# Deliberately combine conflicting board selectors as a compile-time stress test:
# all legacy backends must remain inert. This does not emulate those boards.
$boardSelectors = @('/DARDUINO_ARCH_STM32', '/DARDUINO_PORTENTA_C33', '/DARDUINO_ARCH_MBED',
    '/DESP32', '/DARDUINO_UNOR4_WIFI', '/DARDUINO_UNO_Q', '/DARDUINO_SILABS',
    '/D__ZEPHYR__', '/DARDUINO_AVR_UNO_WIFI_REV2')
Push-Location $BuildDirectory
try {
    & cl @common /Fe:ble_transport_test.exe
    if ($LASTEXITCODE) { throw 'Portable BLE transport test compilation failed' }
    & (Join-Path $BuildDirectory 'ble_transport_test.exe')
    if ($LASTEXITCODE) { throw 'Portable BLE transport tests failed' }
    & cl @boardSelectors @common @upstreamBackends /Fe:ble_backend_selection_test.exe
    if ($LASTEXITCODE) { throw 'BLE backend selection test compilation failed' }
    & (Join-Path $BuildDirectory 'ble_backend_selection_test.exe')
    if ($LASTEXITCODE) { throw 'BLE backend selection tests failed' }
} finally { Pop-Location }
