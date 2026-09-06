param(
    [string]$Cxx = '',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$platformRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $platformRoot '.build/eeprom'
}
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path

if (-not $Cxx) {
    foreach ($candidate in @('g++', 'clang++', 'cl.exe')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            $Cxx = $command.Source
            break
        }
    }
}
if (-not $Cxx) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio detection failed' }
        if ($installation) {
            $devcmd = Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
            # Import the installed compiler environment; no files are installed.
            $environment = & $env:ComSpec /d /c "call `"$devcmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
            if ($LASTEXITCODE -ne 0) { throw 'Visual Studio environment initialization failed' }
            foreach ($entry in $environment) {
                if ($entry -match '^([^=]+)=(.*)$') {
                    [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
                }
            }
            $command = Get-Command cl.exe -ErrorAction SilentlyContinue
            if ($command) { $Cxx = $command.Source }
        }
    }
}
if (-not $Cxx) {
    throw 'A host C++ compiler is required. Use -Cxx with g++, clang++, or run from a Visual Studio developer shell.'
}

$tests = Join-Path $platformRoot 'libraries/EEPROM/tests'
$fakes = Join-Path $tests 'fakes'
$sdkInclude = Join-Path $platformRoot 'tools/sdk/include'
$source = Join-Path $tests 'runtime_test.cpp'
$executable = Join-Path $BuildDirectory 'eeprom_runtime_test.exe'
$isMsvc = [IO.Path]::GetFileName($Cxx) -ieq 'cl.exe'
if ($isMsvc) {
    $compilerArguments = @('/nologo', '/std:c++14', '/EHsc', '/W4', '/WX', '/utf-8',
        "/I$fakes", "/I$sdkInclude", $source, "/Fe$executable", "/Fo$BuildDirectory/")
} else {
    $compilerArguments = @('-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
        "-I$fakes", "-I$sdkInclude", $source, '-o', $executable)
}
Write-Host "Compiler: $Cxx"
& $Cxx @compilerArguments
if ($LASTEXITCODE -ne 0) { throw "EEPROM host compilation failed ($LASTEXITCODE)" }
& $executable
if ($LASTEXITCODE -ne 0) { throw "EEPROM runtime tests failed ($LASTEXITCODE)" }
