[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$PackageResources
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path -LiteralPath $ProjectPath).Path
$packageRoot = (Resolve-Path -LiteralPath $PackageResources).Path
$projectResources = Join-Path $projectRoot 'recursos'
$requiredFiles = @('asr.bin', 'dnn.bin', 'voice.bin', 'user_file.bin')

New-Item -ItemType Directory -Path $projectResources -Force | Out-Null
foreach ($name in $requiredFiles) {
    $source = Join-Path $packageRoot $name
    $destination = Join-Path $projectResources $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Arduino package is missing the default resource: $source"
    }
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        Write-Host "CI13XX resource kept: $destination"
        continue
    }
    if (Test-Path -LiteralPath $destination) {
        throw "CI13XX resource path exists but is not a file: $destination"
    }

    Copy-Item -LiteralPath $source -Destination $destination
    Write-Host "CI13XX default resource copied: $destination"
}
