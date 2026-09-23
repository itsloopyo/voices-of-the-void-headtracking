# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Cuts a release of Voices of the Void Head Tracking: stamp version, changelog, package,
    commit, tag, push.

.DESCRIPTION
    Runs end to end with no operator interaction. `pixi run release minor` IS
    the authorization - there is no second gate, and there must never be one:
    `pixi run` allocates no TTY, so any stdin read here dies with
    "IOException: The handle is invalid" and takes the release with it.

    Safety comes from deterministic preconditions instead: on main, clean tree,
    tag absent, semver valid, notices in sync, and a changelog that has
    something to say. Each of those fails fast with a non-zero exit before
    anything is mutated.

    Nothing here is destructive: no force push, no amend, no tag overwrite.

.PARAMETER Version
    major | minor | patch | nightly | X.Y.Z

.PARAMETER Force
    Ship even when every commit since the last tag was filtered as noise
    (writes a maintenance changelog entry instead of aborting).
#>

[CmdletBinding()]
param(
    # NOT Mandatory: PowerShell satisfies a missing mandatory parameter by
    # reading stdin, and with no TTY that read throws instead of printing a
    # usage line. Validate it ourselves and fail fast.
    [Parameter(Position = 0)][string]$Version,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $Version) {
    Write-Host 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z> [-Force]' -ForegroundColor Red
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

Import-Module (Join-Path $root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force


# Windows PowerShell 5.1's `-Encoding utf8` means UTF-8 WITH a BOM, and pixi
# rejects a pixi.toml that starts with one ("Missing table in manifest"). The
# release would then abort inside `pixi run package`, after the version had been
# stamped into several files and before the tag existed - a half-bumped tree
# with no way forward. Write raw bytes through .NET instead, which also leaves
# each file's existing line endings alone (Get-Content/Set-Content round-trips
# everything to CRLF).
function Set-TextFileNoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Update-VersionInFile {
    param([string]$Path, [string]$Pattern, [string]$Replacement)
    $full = Join-Path $root $Path
    $text = [System.IO.File]::ReadAllText($full)
    $updated = $text -replace $Pattern, $Replacement
    if ($updated -eq $text) { throw "Version stamp did not match anything in $Path" }
    Set-TextFileNoBom -Path $full -Text $updated
}

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = [System.IO.File]::ReadAllText((Join-Path $root $Path))
    $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    Set-TextFileNoBom -Path (Join-Path $root $Path) -Text ($changelog.TrimEnd() + "`n")
}

Push-Location $root
try {
    # CMakeLists.txt is canonical: release.yml re-reads it through the same
    # Get-ProjectVersion to check the tag against the built artifact.
    $current = Get-ProjectVersion -Source 'cmake' -Path 'CMakeLists.txt'
    $new = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current

    # New-ReleaseTag pushes to `main`, so releasing from any other branch would
    # push commits that branch does not contain. Gate before anything mutates.
    $branch = (& git rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne 'main') { throw "Releases cut from 'main' only; currently on '$branch'." }
    if (-not (Test-CleanGitStatus)) { throw 'Working tree is dirty - commit or stash first.' }
    if (Test-GitTagExists -Tag "v$new") { throw "Tag v$new already exists." }

    # THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
    # release ZIP, and bumping the submodule does not touch it. Copy-SharedBundle
    # refuses to package that mismatch, so a bump with no notices edit would stop
    # the release inside `pixi run package` - or in CI, after the tag was already
    # pushed. Re-sync it here and let this release carry the correction.
    & (Join-Path $root 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $root
    if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
    & git -C $root diff --quiet -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) {
        & git -C $root commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
        if ($LASTEXITCODE -ne 0) { throw 'Could not commit the re-synced THIRD-PARTY-NOTICES.md.' }
        Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
    }

    # Changelog first. This is the gate that aborts when every commit since the
    # last tag was filtered as noise, so it runs BEFORE any version file is
    # touched - a failure here leaves a clean tree instead of stranding a
    # half-applied bump with no tag.
    try {
        New-ChangelogFromCommits -ChangelogPath 'CHANGELOG.md' -Version $new | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
            exit 1
        }
        Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path 'CHANGELOG.md' -NewVersion $new
    }

    # Stamp the new version everywhere it lives. CMakeLists.txt is canonical;
    # the other two are hand-kept copies that drift silently if skipped
    # (install.cmd prints MOD_VERSION to the user, pixi.toml is what
    # release-nightly.ps1 and the workspace metadata read).
    $versionFiles = @('CMakeLists.txt', 'pixi.toml', 'scripts/install.cmd', 'CHANGELOG.md')
    Update-VersionInFile -Path 'CMakeLists.txt' -Pattern 'project\(VoicesOfTheVoidHeadTracking VERSION [0-9.]+' -Replacement "project(VoicesOfTheVoidHeadTracking VERSION $new"
    Update-VersionInFile -Path 'pixi.toml' -Pattern '(?m)^version = "[0-9.]+"' -Replacement "version = `"$new`""
    # install.cmd is CRLF-sensitive; -replace on the raw text plus the byte
    # write above keep the line endings the file already has.
    Update-VersionInFile -Path 'scripts/install.cmd' -Pattern '(?m)^set "MOD_VERSION=[0-9.]+"' -Replacement "set `"MOD_VERSION=$new`""

    # Through the same pixi chain CI runs (setup -> build -> package), not a
    # bare cmake invocation, which fails outright on a checkout where build/ was
    # never configured. Packaging rather than building alone: it proves the ZIP
    # this version ships can actually be produced, before the tag exists.
    & pixi run package
    if ($LASTEXITCODE -ne 0) { throw 'Build/packaging failed' }

    # Not Invoke-VersionCommit: it hardcodes "chore: bump version to X", and
    # build.yml skips its redundant build job only on a head commit starting
    # with "Release v". A mismatched subject there costs a full duplicate CI
    # build of the tag release.yml is already building.
    foreach ($f in $versionFiles) {
        & git add -- $f
        if ($LASTEXITCODE -ne 0) { throw "git add failed for $f" }
    }
    if (-not (& git diff --cached --name-only)) { throw 'Version stamping produced no staged changes.' }
    & git commit -m "Release v$new"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to commit the release' }

    New-ReleaseTag -Version $new -Message "Release v$new"
    Write-Host "Released v$new" -ForegroundColor Green
} finally {
    Pop-Location
}
