param([string]$BuildDirectory = '', [ValidateSet('optimized','fallback','configured')][string]$Variant = 'optimized')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $repo ('.build/wireless-portable/runtime-native/' + $Variant) }
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (!$compiler) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'MSVC Build Tools are required.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio detection failed.' }
    $devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
    $environment = & $env:ComSpec /d /c "call `"$devcmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed.' }
    foreach ($entry in $environment) {
        if ($entry -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $compiler = Get-Command cl.exe -ErrorAction Stop
}
$source = Join-Path $PSScriptRoot 'runtime_test.cpp'
$fakes = Join-Path $PSScriptRoot 'runtime_fakes'
$include = Join-Path $PSScriptRoot '../src'
$binary = Join-Path $BuildDirectory 'runtime_test.exe'
$defines = @()
if ($Variant -eq 'fallback') {
    $defines = @('/DESPLINK_CI13XX_UART_BULK_RX=0', '/DESPLINK_CI13XX_TX_DMA=0', '/DESPLINK_CI13XX_TASK_NOTIFY=0')
} elseif ($Variant -eq 'configured') {
    $defines = @('/DESPLINK_CI13XX_RX_BUFFER_SIZE=2048', '/DESPLINK_CI13XX_RX_CHUNK_SIZE=32',
        '/DESPLINK_CI13XX_DMA_THRESHOLD=128', '/DESPLINK_CI13XX_TASK_STACK_WORDS=1536', '/DESPLINK_CI13XX_TASK_PRIORITY=2')
}
Write-Host "ESPLink RTOS variant: $Variant"
& $compiler.Source /nologo /std:c++17 /EHsc /W4 /WX /utf-8 @defines "/I$fakes" "/I$include" $source "/Fe$binary" "/Fo$BuildDirectory/"
if ($LASTEXITCODE -ne 0) { throw "ESPLink RTOS test compilation failed ($LASTEXITCODE)." }
& $binary
if ($LASTEXITCODE -ne 0) { throw "ESPLink RTOS runtime tests failed ($LASTEXITCODE)." }
