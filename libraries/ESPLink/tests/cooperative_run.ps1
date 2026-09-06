param([string]$BuildDirectory = '')
# Run from an MSVC developer shell. Production code is compiled, with no CI macro.
$ErrorActionPreference = 'Stop'
$libraryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repoRoot = [IO.Path]::GetFullPath((Join-Path $libraryRoot '../..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot '.build/wireless-portable/cooperative-native' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$compileArgs = @('/nologo', '/EHsc', '/std:c++17', '/W4', '/WX',
    '/I', (Join-Path $PSScriptRoot 'cooperative_fakes'), '/I', (Join-Path $libraryRoot 'src'),
    (Join-Path $PSScriptRoot 'cooperative_test.cpp'), (Join-Path $libraryRoot 'src/ESPLink.cpp'),
    '/Fe:cooperative_test.exe')
Push-Location $BuildDirectory
try {
    & cl @compileArgs
    if ($LASTEXITCODE) { throw 'Cooperative ESPLink test compilation failed' }
    & (Join-Path $BuildDirectory 'cooperative_test.exe')
    if ($LASTEXITCODE) { throw 'Cooperative ESPLink tests failed' }
} finally { Pop-Location }
