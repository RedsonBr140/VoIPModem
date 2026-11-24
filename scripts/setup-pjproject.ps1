<#
.SYNOPSIS
    One-time setup for the pjproject (PJSIP) submodules: makes sure each is
    checked out, then places VoIPModem's config_site.h where pjproject's
    build expects it.

.DESCRIPTION
    There are two pjproject checkouts:
      third_party/pjproject     the modern build (Windows Vista and later)
      third_party/pjproject-xp  the Windows XP / Server 2003 build (v141_xp)
    They are separate because pjproject names its libraries by CPU,
    platform and configuration only, so one tree cannot hold both builds,
    and so the XP one can be pinned to a different release if needed.

    pjproject's build requires pjlib/include/pj/config_site.h to exist but
    deliberately does not track it in git (upstream's own CI copies one into
    place the same way). VoIPModem's lives at
    third_party/pjsip-config/config_site.h; this script copies it into each
    checkout.

    Run this once after cloning (or after updating a pjproject submodule)
    and before building VoIPModem.sln or pjproject-vs14.sln.
#>

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceConfig = Join-Path $repoRoot "third_party\pjsip-config\config_site.h"

foreach ($name in @("pjproject", "pjproject-xp")) {
    $submodulePath = Join-Path $repoRoot "third_party\$name"

    if (-not (Test-Path (Join-Path $submodulePath ".git"))) {
        Write-Host "Initializing $name submodule..."
        Push-Location $repoRoot
        try {
            git submodule update --init --recursive -- "third_party/$name"
        } finally {
            Pop-Location
        }
    }

    $destConfig = Join-Path $submodulePath "pjlib\include\pj\config_site.h"
    Write-Host "Copying $sourceConfig -> $destConfig"
    Copy-Item -Path $sourceConfig -Destination $destConfig -Force
}

Write-Host "Done. pjproject is ready to build (see README.md 'Build')."
