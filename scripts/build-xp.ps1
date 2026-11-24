<#
.SYNOPSIS
    Builds VoIPModem for Windows XP / Server 2003 (the -XP configurations).

.DESCRIPTION
    The XP build needs the v141_xp toolset ("C++ Windows XP Support for
    VS 2017 (v141) tools"), which current Visual Studio releases no longer
    offer; Visual Studio 2019 or 2022 (Build Tools is enough) still do. This
    script finds an installation that has it, builds the separate PJSIP
    checkout (third_party/pjproject-xp) and then VoIPModem.sln.

    v141_xp's MSBuild targets live in that installation's VC\v150 folder.
    A Build Tools install with only the v141 components has no VC\v170
    (VS 2022's own targets), so VCTargetsPath is pointed at v150 explicitly.

    Run scripts\setup-pjproject.ps1 once first.

.PARAMETER Platform
    Win32 (Windows XP, 32-bit) or x64 (XP x64 / Server 2003 x64).

.PARAMETER Configuration
    Release-XP or Debug-XP.

.PARAMETER SkipPjproject
    Don't rebuild PJSIP (it is slow and rarely changes).
#>
param(
    [ValidateSet("Win32", "x64")] [string] $Platform = "Win32",
    [ValidateSet("Release-XP", "Debug-XP")] [string] $Configuration = "Release-XP",
    [switch] $SkipPjproject
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: is Visual Studio installed?" }

# Every installation, Build Tools included (-products *); the first one
# that has v141_xp for this platform is used.
$installs = & $vswhere -all -prerelease -products * -property installationPath
$vcTargets = $null
$msbuild = $null
foreach ($install in $installs) {
    $candidate = Join-Path $install "MSBuild\Microsoft\VC\v150"
    if (Test-Path (Join-Path $candidate "Platforms\$Platform\PlatformToolsets\v141_xp")) {
        $vcTargets = $candidate
        $msbuild = Join-Path $install "MSBuild\Current\Bin\MSBuild.exe"
        break
    }
}
if (-not $vcTargets) {
    throw "No Visual Studio installation with the v141_xp toolset for $Platform. Install 'C++ Windows XP Support for VS 2017 (v141) tools' (Visual Studio 2019 or 2022, Build Tools is enough)."
}
Write-Host "Using $msbuild"
Write-Host "  VCTargetsPath = $vcTargets"

$pjConfiguration = if ($Configuration -eq "Debug-XP") { "Debug-Static" } else { "Release-Static" }
$common = @("/p:Platform=$Platform", "/p:VCTargetsPath=$vcTargets", "/p:XPDeprecationWarning=false", "/m", "/v:minimal", "/nologo")

if (-not $SkipPjproject) {
    Write-Host "Building PJSIP (pjproject-xp) $Platform $pjConfiguration with v141_xp..."
    & $msbuild (Join-Path $repoRoot "third_party\pjproject-xp\pjproject-vs14.sln") /t:libpjproject `
        "/p:Configuration=$pjConfiguration" /p:PlatformToolset=v141_xp @common
    if ($LASTEXITCODE -ne 0) { throw "PJSIP build failed" }
}

Write-Host "Building VoIPModem.sln $Platform $Configuration..."
& $msbuild (Join-Path $repoRoot "VoIPModem.sln") "/p:Configuration=$Configuration" @common
if ($LASTEXITCODE -ne 0) { throw "VoIPModem build failed" }

Write-Host "Done: $(Join-Path $repoRoot "$Platform\$Configuration\VoIPModem.exe")"
