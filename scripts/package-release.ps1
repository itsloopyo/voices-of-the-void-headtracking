# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Packages the release ZIP for Voices of the Void Head Tracking.

.DESCRIPTION
    Unattended: `pixi run package` allocates no TTY, so this script reads no
    input and asks nothing. It fails fast with a non-zero exit instead.

    One ZIP, not two. The payload is an .asi that Ultimate ASI Loader
    picks up from the game exe's own directory, and Vortex deploys only into a
    single fixed subtree per game, so no mod manager can put the files where
    they have to land. That makes this mod installer-only: there is no Nexus
    page and no `-nexus.zip` stage here. Do not add one back - it would deploy
    to the wrong place and the mod would silently never load. `release-nightly.ps1`
    passes `-NoNexusZip` for the same reason.

    The vendored loader under vendor/ is consumed exactly as committed. Bumping
    it is `pixi run update-deps`, a deliberate act with a commit attached, never
    a packaging side effect.
#>

[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

$modName = 'VoicesOfTheVoidHeadTracking'
$asi = Join-Path $root "build/Release/$modName.asi"
if (-not (Test-Path $asi)) {
    throw "Built .asi not found at $asi. Run 'pixi run build' first."
}

# CMakeLists.txt is canonical, and Get-ProjectVersion is the same reader
# release-mod.yml uses for its tag-vs-file check, so the ZIP name and the tag
# gate can never read the file differently.
$version = Get-ProjectVersion -Source 'cmake' -Path (Join-Path $root 'CMakeLists.txt')

$releaseDir = Join-Path $root 'release'
if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

$stage = Join-Path $env:TEMP "voices-of-the-void-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null

try {
    # Payload. install-body-asi.cmd reads the .asi out of plugins/ beside
    # install.cmd.
    $plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
    Copy-Item -Force $asi (Join-Path $plugins.FullName "$modName.asi")

    # The loader, as committed. install-body-asi.cmd copies
    # vendor/ultimate-asi-loader/dinput8.dll to the game's winmm.dll, so the
    # artifact has to be in the ZIP under that exact name.
    $vendorSrc = Join-Path $root 'vendor/ultimate-asi-loader'
    if (-not (Test-Path (Join-Path $vendorSrc 'dinput8.dll'))) {
        throw "vendor/ultimate-asi-loader/dinput8.dll is missing. Run 'pixi run update-deps' and commit the result - the installer hard-errors without it."
    }
    $vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader') -Force
    Copy-Item -Force -Recurse (Join-Path $vendorSrc '*') $vendorDst.FullName

    # Installer wrappers plus the shared bundle they call into. Copy-SharedBundle
    # stages the whole set (install/uninstall bodies, find-game.ps1,
    # GamePathDetection.psm1, games.json, the arch and marker checks); staging a
    # subset by hand ships an installer that aborts with "Installer ZIP is
    # corrupt" on the first missing piece.
    # The launcher manifest ships at the ZIP root, stamped with this version.
    # Written through a no-BOM encoder: Windows PowerShell 5.1's
    # Set-Content -Encoding UTF8 prepends a BOM that serde_json rejects.
    $manifest = Get-Content -Raw (Join-Path $root 'launcher-manifest.json') | ConvertFrom-Json
    $manifest.mod_info.version = $version
    [System.IO.File]::WriteAllText(
        (Join-Path $stage 'launcher-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 10),
        (New-Object System.Text.UTF8Encoding $false))

    Copy-Item -Force (Join-Path $root 'scripts/install.cmd') $stage
    Copy-Item -Force (Join-Path $root 'scripts/uninstall.cmd') $stage
    Copy-SharedBundle -StagingDir $stage

    # MIT and the BSD licences of everything compiled into the .asi require the
    # notices to travel with the binary, so they ship at the ZIP root.
    foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
        $src = Join-Path $root $doc
        if (-not (Test-Path $src)) {
            throw "Required document not found: $doc. Every published ZIP is a binary distribution and must carry it."
        }
        Copy-Item -Force $src $stage
    }

    $installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
    Write-Host "Built $installerZip" -ForegroundColor Green
} finally {
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
}
