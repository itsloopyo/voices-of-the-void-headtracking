# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Publishes a rolling `dev` pre-release from the current commit.

.DESCRIPTION
    Thin shim: read the version, delegate to the shared publisher. See
    cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.
    Reached through `pixi run release nightly`.

    -NoNexusZip: this mod is installer-only (its .asi deploys next to the game
    exe, where no mod manager can put it), so scripts/package-release.ps1
    produces no Nexus ZIP. Without this switch the missing artifact is fatal.
#>

[CmdletBinding()]
param([switch]$AllowDirty)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Import-Module (Join-Path $root 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$version = Get-ProjectVersion -Source 'cmake' -Path (Join-Path $root 'CMakeLists.txt')

Publish-NightlyBuild `
    -ModId 'voices-of-the-void' `
    -ModName 'VoicesOfTheVoidHeadTracking' `
    -Version $version `
    -ProjectRoot $root `
    -BuildCommand 'pixi run build' `
    -NoNexusZip `
    -AllowDirty:$AllowDirty
