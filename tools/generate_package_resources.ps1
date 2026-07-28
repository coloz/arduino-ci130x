[CmdletBinding()]
param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot '..\..\CI130X_SDK_ALG_V2.7.14'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\recursos'),
    [string]$CiToolKit = (Join-Path $PSScriptRoot 'sdk\bin\ci-tool-kit.exe'),
    [string]$LamePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

<#
The V2.7.14 TTS reference image retains the older, compact ASR/DNN partitions
and does not bring up the V2.7.14 offline-ASR host/algorithm pair. Standard ASR
and CWSL instead use the current offline_asr_alg_pro_sample inputs. They are
generated into separate profile directories and carry separate manifests even
though the vendor sample currently produces identical resource payloads; the
selected linker script and second-core image remain profile-specific.
#>
$generator = Join-Path $PSScriptRoot 'generate_cwsl_package_resources.ps1'
$arguments = @{
    SdkRoot = $SdkRoot
    OutputDirectory = $OutputDirectory
    CiToolKit = $CiToolKit
    Profile = 'standard'
}
if ($LamePath) {
    $arguments.LamePath = $LamePath
}

& $generator @arguments
