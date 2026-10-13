<#
.SYNOPSIS
    One-time setup for the pjproject (PJSIP) submodule: makes sure the
    submodule is checked out, then places VoIPModem's config_site.h where
    pjproject's build expects it.

.DESCRIPTION
    pjproject's build requires pjlib/include/pj/config_site.h to exist,
    but deliberately does not track it in git (it's local build
    configuration — see third_party/pjproject/CLAUDE.md, which notes
    upstream's own CI copies a config_site_test.h into place the same
    way). VoIPModem's actual config lives at
    third_party/pjsip-config/config_site.h, tracked normally in this
    repo; this script copies it into the submodule's expected location.

    Run this once after cloning (or after updating the pjproject
    submodule) and before building VoIPModem.sln or pjproject-vs14.sln.
#>

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$submodulePath = Join-Path $repoRoot "third_party\pjproject"
$sourceConfig = Join-Path $repoRoot "third_party\pjsip-config\config_site.h"
$destConfig = Join-Path $submodulePath "pjlib\include\pj\config_site.h"

if (-not (Test-Path (Join-Path $submodulePath ".git"))) {
    Write-Host "Initializing pjproject submodule..."
    Push-Location $repoRoot
    try {
        git submodule update --init --recursive -- third_party/pjproject
    } finally {
        Pop-Location
    }
}

Write-Host "Copying $sourceConfig -> $destConfig"
Copy-Item -Path $sourceConfig -Destination $destConfig -Force

Write-Host "Done. pjproject is ready to build (see docs/ARCHITECTURE.md 'SIP integration (Phase 2)')."
