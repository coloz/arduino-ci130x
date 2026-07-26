[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,

    [Parameter(Mandatory = $true)]
    [string]$PackageResources,

    [Parameter(Mandatory = $true)]
    [ValidateSet('null', 'cwsl')]
    [string]$Algorithm
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path -LiteralPath $ProjectPath).Path
$packageRoot = (Resolve-Path -LiteralPath $PackageResources).Path
$profileSubdirectory = if ($Algorithm -eq 'cwsl') { 'cwsl' } else { '' }
$packageProfileResources = if ($profileSubdirectory) {
    Join-Path $packageRoot $profileSubdirectory
}
else {
    $packageRoot
}
$projectResourcesBase = Join-Path $projectRoot 'recursos'
$projectResources = if ($profileSubdirectory) {
    Join-Path $projectResourcesBase $profileSubdirectory
}
else {
    $projectResourcesBase
}
$requiredFiles = @('asr.bin', 'dnn.bin', 'voice.bin', 'user_file.bin')

New-Item -ItemType Directory -Path $projectResources -Force | Out-Null
foreach ($name in $requiredFiles) {
    $source = Join-Path $packageProfileResources $name
    $destination = Join-Path $projectResources $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Arduino package is missing the $Algorithm profile resource: $source"
    }
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        Write-Host "CI13XX resource kept: $destination"
        continue
    }
    if (Test-Path -LiteralPath $destination) {
        throw "CI13XX resource path exists but is not a file: $destination"
    }

    Copy-Item -LiteralPath $source -Destination $destination
    Write-Host "CI13XX $Algorithm profile resource copied: $destination"
}
