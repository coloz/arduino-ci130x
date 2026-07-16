[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'

$python = Get-Command py -ErrorAction SilentlyContinue
$arguments = @('-3', '-m', 'http.server', $Port, '--bind', '127.0.0.1', '--directory', $PSScriptRoot)
if (-not $python) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    $arguments = @('-m', 'http.server', $Port, '--bind', '127.0.0.1', '--directory', $PSScriptRoot)
}
if (-not $python) {
    throw 'Python 3 is required to serve the local Boards Manager repository.'
}

Write-Host "ChipIntelli Boards Manager repository: http://127.0.0.1:$Port/package_chipintelli_index.json"
Write-Host 'Keep this window open while installing or updating the platform. Press Ctrl+C to stop.'
& $python.Source @arguments
