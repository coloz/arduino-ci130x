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
$packageManifestPath = Join-Path $packageProfileResources 'manifest.json'
$managedManifestName = '.chipintelli-package-resources.json'
$projectManagedManifestPath = Join-Path $projectResources $managedManifestName
$expectedProfile = if ($Algorithm -eq 'cwsl') { 'cwsl' } else { 'standard' }

function Read-ResourceManifest {
    param([string]$Path, [string]$Description)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing $Description resource manifest: $Path"
    }
    try {
        $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid $Description resource manifest '$Path': $($_.Exception.Message)"
    }
    if ($manifest.schemaVersion -ne 1 -or -not $manifest.resources) {
        throw "Unsupported $Description resource manifest: $Path"
    }
    return $manifest
}

function Get-ResourceMetadata {
    param($Manifest, [string]$Description)

    $metadata = @{}
    foreach ($name in $requiredFiles) {
        $property = $Manifest.resources.PSObject.Properties[$name]
        if ($null -eq $property -or
            $null -eq $property.Value.size -or
            $property.Value.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "$Description resource manifest is missing valid metadata for $name"
        }
        $metadata[$name] = [pscustomobject]@{
            Size = [long]$property.Value.size
            Sha256 = ([string]$property.Value.sha256).ToUpperInvariant()
        }
    }
    return $metadata
}

function Test-ResourceSet {
    param([string]$Root, [hashtable]$Metadata)

    foreach ($name in $requiredFiles) {
        $path = Join-Path $Root $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            return $false
        }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -ne $Metadata[$name].Size) {
            return $false
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne
            $Metadata[$name].Sha256) {
            return $false
        }
    }
    return $true
}

$packageManifest = Read-ResourceManifest -Path $packageManifestPath -Description 'package'
if ($packageManifest.profile -ne $expectedProfile) {
    throw "Package resource manifest profile '$($packageManifest.profile)' does not match $expectedProfile"
}
$packageMetadata = Get-ResourceMetadata -Manifest $packageManifest -Description 'Package'
foreach ($name in $requiredFiles) {
    $source = Join-Path $packageProfileResources $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Arduino package is missing the $Algorithm profile resource: $source"
    }
}
if (-not (Test-ResourceSet -Root $packageProfileResources -Metadata $packageMetadata)) {
    throw "Arduino package $Algorithm resources do not match their manifest: $packageManifestPath"
}

# v1.0.3 and earlier copied this exact compact TTS-reference set into Standard
# sketches. It is safe to migrate only when all four hashes still match; any
# changed or partial set is treated as user-owned and is preserved.
$legacyStandardMetadata = @{
    'asr.bin' = [pscustomobject]@{ Size = 17323L; Sha256 = '39D9EFF49C52C5ED15E4B98BE309A358D4F92D98AB33431A3FF2B3D045392951' }
    'dnn.bin' = [pscustomobject]@{ Size = 278236L; Sha256 = 'EF94C3CF7AAC6744641CB6607E42598E027C4C37C392550C73DF606EB73D6AE1' }
    'voice.bin' = [pscustomobject]@{ Size = 515597L; Sha256 = '549ECBA3A4CE30B876F00C270AA68356D9F6071021C6022544EEF82C37DB79B7' }
    'user_file.bin' = [pscustomobject]@{ Size = 558615L; Sha256 = 'C3E48B7415ACA245714F88B73F26153D8ADF95EBE9B987D8EF909B51FB1B9E81' }
}

New-Item -ItemType Directory -Path $projectResources -Force | Out-Null
if (Test-ResourceSet -Root $projectResources -Metadata $packageMetadata) {
    Write-Host "CI13XX $Algorithm profile resources already match the package manifest."
    if (Test-Path -LiteralPath $projectManagedManifestPath -PathType Leaf) {
        $packageManifestHash = (Get-FileHash -LiteralPath $packageManifestPath -Algorithm SHA256).Hash
        $managedManifestHash = (Get-FileHash -LiteralPath $projectManagedManifestPath -Algorithm SHA256).Hash
        if ($packageManifestHash -ne $managedManifestHash) {
            Copy-Item -LiteralPath $packageManifestPath -Destination $projectManagedManifestPath -Force
            Write-Host "CI13XX package-managed resource manifest updated: $projectManagedManifestPath"
        }
    }
    return
}

$replaceManagedSet = $false
$replacementReason = $null
if (Test-Path -LiteralPath $projectManagedManifestPath -PathType Leaf) {
    try {
        $managedManifest = Read-ResourceManifest -Path $projectManagedManifestPath -Description 'sketch-managed'
        $managedMetadata = Get-ResourceMetadata -Manifest $managedManifest -Description 'Sketch-managed'
        if (Test-ResourceSet -Root $projectResources -Metadata $managedMetadata) {
            $replaceManagedSet = $true
            $replacementReason = 'managed package resource set'
        }
        else {
            Write-Warning "Sketch resources no longer match $projectManagedManifestPath; preserving them as user-owned files."
        }
    }
    catch {
        Write-Warning "$($_.Exception.Message) Preserving sketch resources as user-owned files."
    }
}
elseif ($Algorithm -eq 'null' -and
        (Test-ResourceSet -Root $projectResources -Metadata $legacyStandardMetadata)) {
    $replaceManagedSet = $true
    $replacementReason = 'legacy v1.0.3 Standard resource set'
}

if ($replaceManagedSet) {
    foreach ($name in $requiredFiles) {
        $source = Join-Path $packageProfileResources $name
        $destination = Join-Path $projectResources $name
        Copy-Item -LiteralPath $source -Destination $destination -Force
        Write-Host "CI13XX $Algorithm profile resource upgraded ($replacementReason): $destination"
    }
    Copy-Item -LiteralPath $packageManifestPath -Destination $projectManagedManifestPath -Force
    return
}

foreach ($name in $requiredFiles) {
    $source = Join-Path $packageProfileResources $name
    $destination = Join-Path $projectResources $name
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        Write-Host "CI13XX user-owned resource kept: $destination"
        continue
    }
    if (Test-Path -LiteralPath $destination) {
        throw "CI13XX resource path exists but is not a file: $destination"
    }
    Copy-Item -LiteralPath $source -Destination $destination
    Write-Host "CI13XX $Algorithm profile resource copied: $destination"
}

if (Test-ResourceSet -Root $projectResources -Metadata $packageMetadata) {
    Copy-Item -LiteralPath $packageManifestPath -Destination $projectManagedManifestPath -Force
    Write-Host "CI13XX package-managed resource manifest copied: $projectManagedManifestPath"
}
else {
    Write-Warning 'Sketch resources are a custom or mixed set; compatibility is the sketch owner''s responsibility.'
}
