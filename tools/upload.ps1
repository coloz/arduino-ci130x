[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Image,

    [Parameter(Mandatory = $true)]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [string]$PlatformPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'The vendor CI13XX serial programmer is currently available for Windows only.'
}
if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'This CI13XX Arduino package is supported on 64-bit Windows only.'
}

$imagePath = (Resolve-Path -LiteralPath $Image).Path
$platformRoot = (Resolve-Path -LiteralPath $PlatformPath).Path
$programmer = Join-Path $platformRoot 'tools\sdk\bin\code_program.exe'

if (-not (Test-Path -LiteralPath $programmer -PathType Leaf)) {
    throw "Missing vendor programmer: $programmer. Run tools\rebuild_sdk.ps1 first."
}

if ($Port -notmatch '^COM[0-9]+$') {
    throw "Unsupported serial port '$Port'. Expected a Windows COM port such as COM7."
}

Write-Host "Uploading CI13XX user-code image to $Port ..."

& $programmer $imagePath $Port
if ($LASTEXITCODE -ne 0) {
    throw "code_program.exe failed with exit code $LASTEXITCODE"
}

Write-Host 'Upload command completed. The board must already contain matching ASR/DNN/user-file firmware partitions.'
