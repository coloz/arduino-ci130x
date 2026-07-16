[CmdletBinding()]
param(
    [string]$PlatformRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ToolchainRoot,
    [string]$BaseUrl = 'http://127.0.0.1:8765',
    [string]$Version = '0.1.0',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'

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

function Copy-PlatformTree {
    param(
        [string]$Source,
        [string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Force) {
        if ($item.Name -eq 'package') {
            $packageTarget = Join-Path $Destination 'package'
            New-Item -ItemType Directory -Path $packageTarget | Out-Null
            foreach ($packageItem in Get-ChildItem -LiteralPath $item.FullName -Force) {
                if ($packageItem.Name -in @('dist', 'package_chipintelli_index.json')) {
                    continue
                }
                Copy-Item -LiteralPath $packageItem.FullName -Destination $packageTarget -Recurse -Force
            }
            continue
        }

        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
    }
}

$PlatformRoot = (Resolve-Path -LiteralPath $PlatformRoot).Path
$ToolchainRoot = Resolve-ToolchainRoot -RequestedRoot $ToolchainRoot
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$BaseUrl = $BaseUrl.TrimEnd('/')

$platformVersion = (Select-String -LiteralPath (Join-Path $PlatformRoot 'platform.txt') -Pattern '^version=(.+)$').Matches.Groups[1].Value
if ($platformVersion -ne $Version) {
    throw "platform.txt version '$platformVersion' does not match package version '$Version'."
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
    $platformTopDirectory = Join-Path $platformStageParent "arduino-chipintelli-$Version"
    New-Item -ItemType Directory -Path $platformStageParent -Force | Out-Null
    Copy-PlatformTree -Source $PlatformRoot -Destination $platformTopDirectory

    $toolStageParent = Join-Path $stageRoot 'toolchain'
    New-Item -ItemType Directory -Path $toolStageParent -Force | Out-Null
    Copy-Item -LiteralPath $ToolchainRoot -Destination $toolStageParent -Recurse -Force

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $platformArchiveName = "arduino-chipintelli-$Version.zip"
    $toolchainArchiveName = 'riscv-nuclei-elf-gcc-9.2.0-windows.zip'
    $platformArchive = Join-Path $OutputDirectory $platformArchiveName
    $toolchainArchive = Join-Path $OutputDirectory $toolchainArchiveName

    foreach ($archive in @($platformArchive, $toolchainArchive)) {
        if (Test-Path -LiteralPath $archive) {
            Remove-Item -LiteralPath $archive -Force
        }
    }

    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $platformStageParent,
        $platformArchive,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $toolStageParent,
        $toolchainArchive,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false
    )

    $platformFile = Get-Item -LiteralPath $platformArchive
    $toolchainFile = Get-Item -LiteralPath $toolchainArchive
    $platformHash = (Get-FileHash -LiteralPath $platformArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    $toolchainHash = (Get-FileHash -LiteralPath $toolchainArchive -Algorithm SHA256).Hash.ToLowerInvariant()

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
                        name = 'ChipIntelli CI13XX Arduino'
                        architecture = 'ci13xx'
                        version = $Version
                        category = 'Contributed'
                        help = [ordered]@{
                            online = 'https://document.chipintelli.com/'
                        }
                        url = "$BaseUrl/dist/$platformArchiveName"
                        archiveFileName = $platformArchiveName
                        checksum = "SHA-256:$platformHash"
                        size = $platformFile.Length.ToString()
                        boards = @(
                            [ordered]@{
                                name = 'ChipIntelli CI-D06GT01D (CI1306, 4 MB)'
                            }
                        )
                        toolsDependencies = @(
                            [ordered]@{
                                packager = 'chipintelli'
                                name = 'riscv-gcc'
                                version = '9.2.0'
                            }
                        )
                    }
                )
                tools = @(
                    [ordered]@{
                        name = 'riscv-gcc'
                        version = '9.2.0'
                        systems = @(
                            [ordered]@{
                                host = 'x86_64-mingw32'
                                url = "$BaseUrl/dist/$toolchainArchiveName"
                                archiveFileName = $toolchainArchiveName
                                checksum = "SHA-256:$toolchainHash"
                                size = $toolchainFile.Length.ToString()
                            }
                        )
                    }
                )
            }
        )
    }

    $indexPath = Join-Path $PSScriptRoot 'package_chipintelli_index.json'
    $indexJson = $index | ConvertTo-Json -Depth 12
    $utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($indexPath, $indexJson + [Environment]::NewLine, $utf8WithoutBom)

    [pscustomobject]@{
        Index = $indexPath
        PlatformArchive = $platformArchive
        PlatformSize = $platformFile.Length
        PlatformSha256 = $platformHash
        ToolchainArchive = $toolchainArchive
        ToolchainSize = $toolchainFile.Length
        ToolchainSha256 = $toolchainHash
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
