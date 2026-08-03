[CmdletBinding()]
param(
    [string]$PlatformRoot,
    [string]$ToolchainRoot,
    [string[]]$ToolchainArchives,
    [string]$CitoolCliArchive,
    [string[]]$CitoolCliArchives,
    [string]$CitoolCliVersion = '1.1.2',
    [string]$BaseUrl = 'http://127.0.0.1:8765',
    [switch]$FlatAssetUrls,
    [string]$Version = '1.0.4',
    [string]$OutputDirectory,
    [string]$IndexOutputPath,
    [switch]$RequireAllHostTools
)

$ErrorActionPreference = 'Stop'

if (-not $PlatformRoot) {
    $PlatformRoot = Split-Path -Parent $PSScriptRoot
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot 'dist'
}
if (-not $IndexOutputPath) {
    $IndexOutputPath = Join-Path $PSScriptRoot 'package_chipintelli_index.json'
}

function Resolve-ToolchainRoot {
    param([string]$RequestedRoot)

    $candidates = @()
    if ($RequestedRoot) {
        $candidates += $RequestedRoot
    }
    $candidates += @(
        (Join-Path $env:TEMP 'riscv-nuclei-elf-gcc-9.2.0\gcc_fix_raissrc'),
        (Join-Path $env:TEMP 'riscv-nuclei-elf-gcc-9.2.0')
    )

    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            continue
        }

        $resolved = (Resolve-Path -LiteralPath $candidate).Path
        if ((Split-Path -Leaf $resolved) -ne 'gcc_fix_raissrc') {
            $resolved = Join-Path $resolved 'gcc_fix_raissrc'
        }

        $compiler = Join-Path $resolved 'bin\riscv-nuclei-elf-gcc.exe'
        if (Test-Path -LiteralPath $compiler -PathType Leaf) {
            return $resolved
        }
    }

    throw 'GCC 9.2.0 was not found. Extract the official riscv-nuclei-elf-gcc-9.2.0 archive and pass -ToolchainRoot.'
}

function Resolve-CitoolCliArchives {
    param(
        [string]$PlatformPath,
        [string]$RequestedArchive,
        [string[]]$RequestedArchives,
        [string]$Version
    )

    if ($RequestedArchive -and $RequestedArchives) {
        throw 'Pass either -CitoolCliArchive or -CitoolCliArchives, not both.'
    }

    $candidates = if ($RequestedArchives) {
        @($RequestedArchives)
    }
    elseif ($RequestedArchive) {
        @($RequestedArchive)
    }
    else {
        $workspacePath = Split-Path -Parent $PlatformPath
        $distPath = Join-Path $workspacePath 'citool-cli\dist'
        @(
            (Join-Path $distPath "citool-cli-$Version-windows-x86_64.zip"),
            (Join-Path $distPath "citool-cli-$Version-macos-universal.tar.gz"),
            (Join-Path $distPath "citool-cli-$Version-linux-x86_64.tar.gz")
        )
    }

    $resolvedArchives = @()
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Prebuilt citool-cli release archive was not found: $candidate. Download the three release archives, or pass them with -CitoolCliArchives."
        }
        $resolvedArchives += (Resolve-Path -LiteralPath $candidate).Path
    }
    return $resolvedArchives
}

function Resolve-ToolchainArchives {
    param(
        [string]$PlatformPath,
        [string[]]$RequestedArchives
    )

    $candidates = if ($RequestedArchives) {
        @($RequestedArchives)
    }
    else {
        $toolchainDist = Join-Path $PlatformPath 'package\toolchains'
        @(
            (Join-Path $toolchainDist 'riscv-nuclei-elf-gcc-9.2.0-linux-x86_64.tar.gz'),
            (Join-Path $toolchainDist 'riscv-nuclei-elf-gcc-9.2.0-macos-arm64.tar.gz')
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
    }

    $resolvedArchives = @()
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Prebuilt Nuclei GCC host archive was not found: $candidate"
        }
        $resolvedArchives += (Resolve-Path -LiteralPath $candidate).Path
    }
    return $resolvedArchives
}

function Copy-PlatformTree {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination | Out-Null

    # Boards Manager receives only files needed to build examples and firmware.
    # Repository metadata, release tooling and maintainer-only scripts stay out of
    # the platform archive even when they exist in the source checkout.
    foreach ($name in @(
        'boards.txt',
        'platform.txt',
        'programmers.txt',
        'README.md',
        'cores',
        'examples',
        'libraries',
        'variants'
    )) {
        $item = Join-Path $Source $name
        if (-not (Test-Path -LiteralPath $item)) {
            throw "Required platform release entry is missing: $item"
        }
        Copy-Item -LiteralPath $item -Destination $Destination -Recurse -Force
    }

    $resourcesTarget = Join-Path $Destination 'recursos'
    New-Item -ItemType Directory -Path $resourcesTarget | Out-Null
    $resourceNames = @('manifest.json', 'asr.bin', 'dnn.bin', 'voice.bin', 'user_file.bin')
    foreach ($name in $resourceNames) {
        $item = Join-Path (Join-Path $Source 'recursos') $name
        if (-not (Test-Path -LiteralPath $item -PathType Leaf)) {
            throw "Required Standard release resource is missing: $item"
        }
        Copy-Item -LiteralPath $item -Destination $resourcesTarget -Force
    }

    $cwslTarget = Join-Path $resourcesTarget 'cwsl'
    New-Item -ItemType Directory -Path $cwslTarget | Out-Null
    foreach ($name in $resourceNames) {
        $item = Join-Path (Join-Path (Join-Path $Source 'recursos') 'cwsl') $name
        if (-not (Test-Path -LiteralPath $item -PathType Leaf)) {
            throw "Required CWSL release resource is missing: $item"
        }
        Copy-Item -LiteralPath $item -Destination $cwslTarget -Force
    }

    $toolsTarget = Join-Path $Destination 'tools'
    New-Item -ItemType Directory -Path $toolsTarget | Out-Null
    foreach ($name in @(
        'sdk',
        'compile_sdk_sources.py',
        'compile_sdk_sources.ps1',
        'merge_user_file_entries.py',
        'merge_user_file_entries.ps1',
        'postbuild.py',
        'postbuild.ps1',
        'prepare_resources.py',
        'prepare_resources.ps1'
    )) {
        $item = Join-Path (Join-Path $Source 'tools') $name
        if (-not (Test-Path -LiteralPath $item)) {
            throw "Required platform runtime tool is missing: $item"
        }
        Copy-Item -LiteralPath $item -Destination $toolsTarget -Recurse -Force
    }
}

function New-PortableZipArchive {
    param(
        [string]$SourceDirectory,
        [string]$DestinationArchive
    )

    $sourceRoot = [System.IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $archiveStream = [System.IO.File]::Open(
        $DestinationArchive,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None
    )
    try {
        $archive = New-Object System.IO.Compression.ZipArchive(
            $archiveStream,
            [System.IO.Compression.ZipArchiveMode]::Create,
            $true
        )
        try {
            $fixedTimestamp = [System.DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)
            $files = Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | Sort-Object FullName
            foreach ($file in $files) {
                $entryName = $file.FullName.Substring($sourceRoot.Length).Replace('\', '/')
                $entry = $archive.CreateEntry(
                    $entryName,
                    [System.IO.Compression.CompressionLevel]::Optimal
                )
                $entry.LastWriteTime = $fixedTimestamp

                $inputStream = $file.OpenRead()
                $outputStream = $entry.Open()
                try {
                    $inputStream.CopyTo($outputStream)
                }
                finally {
                    $outputStream.Dispose()
                    $inputStream.Dispose()
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $archiveStream.Dispose()
    }
}

$PlatformRoot = (Resolve-Path -LiteralPath $PlatformRoot).Path
$ToolchainRoot = Resolve-ToolchainRoot -RequestedRoot $ToolchainRoot
$ToolchainArchives = @(
    Resolve-ToolchainArchives `
        -PlatformPath $PlatformRoot `
        -RequestedArchives $ToolchainArchives
)
$CitoolCliArchives = @(
    Resolve-CitoolCliArchives `
        -PlatformPath $PlatformRoot `
        -RequestedArchive $CitoolCliArchive `
        -RequestedArchives $CitoolCliArchives `
        -Version $CitoolCliVersion
)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$IndexOutputPath = [System.IO.Path]::GetFullPath($IndexOutputPath)
$BaseUrl = $BaseUrl.TrimEnd('/')
$AssetBaseUrl = if ($FlatAssetUrls) { $BaseUrl } else { "$BaseUrl/dist" }

$resourceRoot = Join-Path $PlatformRoot 'recursos'
$requiredResources = @('asr.bin', 'dnn.bin', 'voice.bin', 'user_file.bin')
foreach ($profile in @(
        [pscustomobject]@{
            Name = 'Standard'
            ManifestProfile = 'standard'
            Root = $resourceRoot
        },
        [pscustomobject]@{
            Name = 'CWSL'
            ManifestProfile = 'cwsl'
            Root = (Join-Path $resourceRoot 'cwsl')
        }
    )) {
    $manifestPath = Join-Path $profile.Root 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Missing Arduino package $($profile.Name) resource manifest: $manifestPath"
    }
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid Arduino package $($profile.Name) resource manifest '$manifestPath': $($_.Exception.Message)"
    }
    if ($manifest.schemaVersion -ne 1 -or
        $manifest.profile -ne $profile.ManifestProfile -or
        $manifest.mode -ne 'vendor-sample' -or
        $manifest.source -ne 'CI130X_SDK_ALG_V2.7.14/projects/offline_asr_alg_pro_sample/firmware' -or
        $manifest.vendorSpokenControlIdsIncluded -ne $true) {
        throw "Unexpected $($profile.Name) resource manifest metadata: $manifestPath"
    }
    foreach ($resourceName in $requiredResources) {
        $manifestProperty = $manifest.resources.PSObject.Properties[$resourceName]
        if ($null -eq $manifestProperty -or
            $manifestProperty.Value.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "$($profile.Name) resource manifest is missing valid metadata for $resourceName`: $manifestPath"
        }
        $resourcePath = Join-Path $profile.Root $resourceName
        if (-not (Test-Path -LiteralPath $resourcePath -PathType Leaf)) {
            throw "Missing Arduino package $($profile.Name) firmware resource: $resourcePath"
        }
        $resourceFile = Get-Item -LiteralPath $resourcePath
        if ($resourceFile.Length -ne [long]$manifestProperty.Value.size) {
            throw "$($profile.Name) resource size does not match manifest: $resourcePath"
        }
        $resourceHash = (Get-FileHash -LiteralPath $resourcePath -Algorithm SHA256).Hash
        if ($resourceHash -ne $manifestProperty.Value.sha256) {
            throw "$($profile.Name) resource SHA-256 does not match manifest: $resourcePath"
        }
    }
}

$platformVersion = (Select-String -LiteralPath (Join-Path $PlatformRoot 'platform.txt') -Pattern '^version=(.+)$').Matches.Groups[1].Value
if ($platformVersion -ne $Version) {
    throw "platform.txt version '$platformVersion' does not match package version '$Version'."
}

$crossHostLibraryRoot = Join-Path $PlatformRoot 'tools\sdk\lib-cross-host'
$crossHostManifestPath = Join-Path $crossHostLibraryRoot 'BUILD-MANIFEST.txt'
if (-not (Test-Path -LiteralPath $crossHostManifestPath -PathType Leaf)) {
    throw "Missing cross-host vendor-library manifest: $crossHostManifestPath"
}
$crossHostManifest = @(Get-Content -LiteralPath $crossHostManifestPath)
if ($crossHostManifest -notcontains 'format=ci13xx-cross-host-native-archives-v2' -or
    -not ($crossHostManifest -match 'materialization\.flags=.*-ffunction-sections.*-fdata-sections')) {
    throw "Cross-host vendor-library manifest does not preserve function/data sections: $crossHostManifestPath"
}
$crossHostArchiveLines = @($crossHostManifest | Where-Object { $_ -like 'archive=*' })
if ($crossHostArchiveLines.Count -eq 0) {
    throw "Cross-host vendor-library manifest has no archive records: $crossHostManifestPath"
}
foreach ($line in $crossHostArchiveLines) {
    if ($line -notmatch '^archive=(?<archive>\S+) members=(?<members>[0-9]+) materialized=(?<materialized>[0-9]+) passthrough=(?<passthrough>[0-9]+) source\.sha256=(?<source>[0-9a-f]{64}) output\.sha256=(?<output>[0-9a-f]{64})$') {
        throw "Invalid cross-host vendor-library manifest entry: $line"
    }
    if ([int]$Matches.materialized + [int]$Matches.passthrough -ne [int]$Matches.members) {
        throw "Cross-host vendor-library member counts do not add up: $line"
    }
    $relativeArchive = $Matches.archive.Replace('/', '\')
    $sourceArchive = Join-Path (Join-Path $PlatformRoot 'tools\sdk\lib') $relativeArchive
    $outputArchive = Join-Path $crossHostLibraryRoot $relativeArchive
    foreach ($archivePath in @($sourceArchive, $outputArchive)) {
        if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
            throw "Cross-host vendor-library manifest references a missing archive: $archivePath"
        }
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $outputHash = (Get-FileHash -LiteralPath $outputArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne $Matches.source -or $outputHash -ne $Matches.output) {
        throw "Cross-host vendor-library manifest hash mismatch: $relativeArchive"
    }
}

$citoolVersion = $CitoolCliVersion
$citoolReleaseTargets = @(
    [pscustomobject]@{
        ArchiveName = "citool-cli-$citoolVersion-windows-x86_64.zip"
        Executable = 'citool-cli/citool-cli.exe'
        Hosts = @('x86_64-mingw32')
    },
    [pscustomobject]@{
        ArchiveName = "citool-cli-$citoolVersion-macos-universal.tar.gz"
        Executable = 'citool-cli/citool-cli'
        Hosts = @('x86_64-apple-darwin', 'arm64-apple-darwin')
    },
    [pscustomobject]@{
        ArchiveName = "citool-cli-$citoolVersion-linux-x86_64.tar.gz"
        Executable = 'citool-cli/citool-cli'
        Hosts = @('x86_64-pc-linux-gnu')
    }
)

$toolchainVersion = '9.2.0'
$windowsToolchainArchiveName = "riscv-nuclei-elf-gcc-$toolchainVersion-windows.zip"
$toolchainReleaseTargets = @(
    [pscustomobject]@{
        ArchiveName = $windowsToolchainArchiveName
        Executable = 'gcc_fix_raissrc/bin/riscv-nuclei-elf-gcc.exe'
        Hosts = @('x86_64-mingw32')
        Source = 'generated'
    },
    [pscustomobject]@{
        ArchiveName = "riscv-nuclei-elf-gcc-$toolchainVersion-linux-x86_64.tar.gz"
        Executable = 'riscv-gcc/bin/riscv-nuclei-elf-gcc'
        Hosts = @('x86_64-pc-linux-gnu')
        Source = 'external'
    },
    [pscustomobject]@{
        ArchiveName = "riscv-nuclei-elf-gcc-$toolchainVersion-macos-arm64.tar.gz"
        Executable = 'riscv-gcc/bin/riscv-nuclei-elf-gcc'
        Hosts = @('arm64-apple-darwin')
        Source = 'external'
    }
)

$toolchainArchiveByName = @{}
foreach ($archivePath in $ToolchainArchives) {
    $archiveName = Split-Path -Leaf $archivePath
    if ($toolchainArchiveByName.ContainsKey($archiveName)) {
        throw "Duplicate Nuclei GCC host archive: $archiveName"
    }
    $toolchainArchiveByName[$archiveName] = $archivePath
}
foreach ($target in @($toolchainReleaseTargets | Where-Object Source -eq 'external')) {
    if (-not $toolchainArchiveByName.ContainsKey($target.ArchiveName)) {
        if ($RequireAllHostTools) {
            throw "Missing Nuclei GCC host archive '$($target.ArchiveName)'."
        }
        continue
    }
    $archivePath = $toolchainArchiveByName[$target.ArchiveName]
    $entries = @(& tar -tzf $archivePath)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect Nuclei GCC host archive: $archivePath"
    }
    $entries = @($entries | ForEach-Object { $_.Replace('\', '/').TrimStart([char[]]'./') })
    if ($entries -notcontains $target.Executable) {
        throw "Nuclei GCC host archive must contain $($target.Executable): $archivePath"
    }
}
$linuxToolchainName = "riscv-nuclei-elf-gcc-$toolchainVersion-linux-x86_64.tar.gz"
if ($toolchainArchiveByName.ContainsKey($linuxToolchainName)) {
    $expectedLinuxToolchainHash = '0EE91C983F2CF3EAA26B444EB553847A7DDA34F3FB5D97C34B977CA43E593CA5'
    $linuxToolchainHash = (Get-FileHash -LiteralPath $toolchainArchiveByName[$linuxToolchainName] -Algorithm SHA256).Hash
    if ($linuxToolchainHash -ne $expectedLinuxToolchainHash) {
        throw "Unexpected ChipIntelli Linux GCC archive SHA-256: $linuxToolchainHash"
    }
}

$citoolArchiveByName = @{}
foreach ($archivePath in $CitoolCliArchives) {
    $archiveName = Split-Path -Leaf $archivePath
    if ($citoolArchiveByName.ContainsKey($archiveName)) {
        throw "Duplicate citool-cli archive: $archiveName"
    }
    $citoolArchiveByName[$archiveName] = $archivePath
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($target in $citoolReleaseTargets) {
    if (-not $citoolArchiveByName.ContainsKey($target.ArchiveName)) {
        throw "Missing citool-cli release archive '$($target.ArchiveName)'."
    }

    $archivePath = $citoolArchiveByName[$target.ArchiveName]
    if ($target.ArchiveName.EndsWith('.zip')) {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/').TrimStart([char[]]'./') })
        }
        finally {
            $zip.Dispose()
        }
    }
    else {
        $entries = @(& tar -tzf $archivePath)
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to inspect citool-cli archive: $archivePath"
        }
        $entries = @($entries | ForEach-Object { $_.Replace('\', '/').TrimStart([char[]]'./') })
    }

    if ($entries -notcontains $target.Executable) {
        throw "citool-cli archive must contain $($target.Executable): $archivePath"
    }
}

$compiler = Join-Path $ToolchainRoot 'bin\riscv-nuclei-elf-gcc.exe'
$expectedCompilerHash = '84B0FFB1FB194CC41FCFA96FB01D65B3A6289147041CF8BC76DB60BD05FBCB6D'
$compilerHash = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash
if ($compilerHash -ne $expectedCompilerHash) {
    throw "Unexpected GCC executable SHA-256: $compilerHash"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$stageRoot = Join-Path $env:TEMP ("chipintelli-arduino-package-{0}-{1}" -f $PID, [guid]::NewGuid().ToString('N'))
$tempRoot = [System.IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
$fullStageRoot = [System.IO.Path]::GetFullPath($stageRoot)
if (-not $fullStageRoot.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use a staging directory outside TEMP: $fullStageRoot"
}

try {
    $platformStageParent = Join-Path $stageRoot 'platform'
    $platformTopDirectory = Join-Path $platformStageParent "arduino-ci130x-$Version"
    New-Item -ItemType Directory -Path $platformStageParent -Force | Out-Null
    Copy-PlatformTree -Source $PlatformRoot -Destination $platformTopDirectory

    $toolStageParent = Join-Path $stageRoot 'toolchain'
    New-Item -ItemType Directory -Path $toolStageParent -Force | Out-Null
    Copy-Item -LiteralPath $ToolchainRoot -Destination $toolStageParent -Recurse -Force

    $platformArchiveName = "arduino-ci130x-$Version.zip"
    $platformArchive = Join-Path $OutputDirectory $platformArchiveName
    $windowsToolchainArchive = Join-Path $OutputDirectory $windowsToolchainArchiveName
    $toolchainPackageFiles = @()
    $citoolPackageFiles = @()

    foreach ($archive in @($platformArchive, $windowsToolchainArchive)) {
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
    }
    foreach ($target in $citoolReleaseTargets) {
        $sourceArchive = $citoolArchiveByName[$target.ArchiveName]
        $destinationArchive = Join-Path $OutputDirectory $target.ArchiveName
        if ([System.IO.Path]::GetFullPath($sourceArchive) -ne [System.IO.Path]::GetFullPath($destinationArchive)) {
            Copy-Item -LiteralPath $sourceArchive -Destination $destinationArchive -Force
        }
        $archiveFile = Get-Item -LiteralPath $destinationArchive
        $citoolPackageFiles += [pscustomobject]@{
            Target = $target
            File = $archiveFile
            Sha256 = (Get-FileHash -LiteralPath $destinationArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    New-PortableZipArchive -SourceDirectory $platformStageParent -DestinationArchive $platformArchive
    New-PortableZipArchive -SourceDirectory $toolStageParent -DestinationArchive $windowsToolchainArchive
    $platformFile = Get-Item -LiteralPath $platformArchive
    $platformHash = (Get-FileHash -LiteralPath $platformArchive -Algorithm SHA256).Hash.ToLowerInvariant()

    $windowsToolchainTarget = $toolchainReleaseTargets | Where-Object ArchiveName -eq $windowsToolchainArchiveName
    $windowsToolchainFile = Get-Item -LiteralPath $windowsToolchainArchive
    $toolchainPackageFiles += [pscustomobject]@{
        Target = $windowsToolchainTarget
        File = $windowsToolchainFile
        Sha256 = (Get-FileHash -LiteralPath $windowsToolchainArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    foreach ($target in @($toolchainReleaseTargets | Where-Object Source -eq 'external')) {
        if (-not $toolchainArchiveByName.ContainsKey($target.ArchiveName)) {
            continue
        }
        $sourceArchive = $toolchainArchiveByName[$target.ArchiveName]
        $destinationArchive = Join-Path $OutputDirectory $target.ArchiveName
        if ([IO.Path]::GetFullPath($sourceArchive) -ne [IO.Path]::GetFullPath($destinationArchive)) {
            Copy-Item -LiteralPath $sourceArchive -Destination $destinationArchive -Force
        }
        $archiveFile = Get-Item -LiteralPath $destinationArchive
        $toolchainPackageFiles += [pscustomobject]@{
            Target = $target
            File = $archiveFile
            Sha256 = (Get-FileHash -LiteralPath $destinationArchive -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    $toolchainSystems = @()
    foreach ($packageFile in $toolchainPackageFiles) {
        foreach ($hostName in $packageFile.Target.Hosts) {
            $toolchainSystems += [ordered]@{
                host = $hostName
                url = "$AssetBaseUrl/$($packageFile.File.Name)"
                archiveFileName = $packageFile.File.Name
                checksum = "SHA-256:$($packageFile.Sha256)"
                size = $packageFile.File.Length.ToString()
            }
        }
    }
    $citoolSystems = @()
    foreach ($packageFile in $citoolPackageFiles) {
        foreach ($hostName in $packageFile.Target.Hosts) {
            $citoolSystems += [ordered]@{
                host = $hostName
                url = "$AssetBaseUrl/$($packageFile.File.Name)"
                archiveFileName = $packageFile.File.Name
                checksum = "SHA-256:$($packageFile.Sha256)"
                size = $packageFile.File.Length.ToString()
            }
        }
    }

    $index = [ordered]@{
        packages = @(
            [ordered]@{
                name = 'chipintelli'
                maintainer = 'ChipIntelli'
                websiteURL = 'https://www.chipintelli.com/zh-cn/'
                email = 'support@chipintelli.com'
                help = [ordered]@{
                    online = 'https://document.chipintelli.com/'
                }
                platforms = @(
                    [ordered]@{
                        name = 'ChipIntelli CI130X Arduino'
                        architecture = 'ci13xx'
                        version = $Version
                        category = 'Contributed'
                        help = [ordered]@{
                            online = 'https://document.chipintelli.com/'
                        }
                        url = "$AssetBaseUrl/$platformArchiveName"
                        archiveFileName = $platformArchiveName
                        checksum = "SHA-256:$platformHash"
                        size = $platformFile.Length.ToString()
                        boards = @(
                            [ordered]@{
                                name = 'ChipIntelli CI1302 (SSOP24, 2 MB)'
                            }
                            [ordered]@{
                                name = 'ChipIntelli CI1303 (SSOP24, 4 MB)'
                            }
                            [ordered]@{
                                name = 'ChipIntelli CI1306 (QFN40, 4 MB)'
                            }
                        )
                        toolsDependencies = @(
                            [ordered]@{
                                packager = 'chipintelli'
                                name = 'riscv-gcc'
                                version = '9.2.0'
                            },
                            [ordered]@{
                                packager = 'chipintelli'
                                name = 'citool-cli'
                                version = $citoolVersion
                            }
                        )
                    }
                )
                tools = @(
                    [ordered]@{
                        name = 'riscv-gcc'
                        version = $toolchainVersion
                        systems = $toolchainSystems
                    },
                    [ordered]@{
                        name = 'citool-cli'
                        version = $citoolVersion
                        systems = $citoolSystems
                    }
                )
            }
        )
    }

    $indexJson = $index | ConvertTo-Json -Depth 12
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    $indexParent = Split-Path -Parent $IndexOutputPath
    New-Item -ItemType Directory -Path $indexParent -Force | Out-Null
    [System.IO.File]::WriteAllText($IndexOutputPath, $indexJson + [Environment]::NewLine, $utf8WithoutBom)

    [pscustomobject]@{
        Index = $IndexOutputPath
        PlatformArchive = $platformArchive
        PlatformSize = $platformFile.Length
        PlatformSha256 = $platformHash
        ToolchainArchives = @($toolchainPackageFiles | ForEach-Object { $_.File.FullName })
        CitoolCliArchives = @($citoolPackageFiles | ForEach-Object { $_.File.FullName })
    } | Format-List
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        $verifiedStageRoot = [System.IO.Path]::GetFullPath($stageRoot)
        if ($verifiedStageRoot.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $verifiedStageRoot -Recurse -Force
        }
    }
}
