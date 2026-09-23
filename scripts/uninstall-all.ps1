# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Runs uninstall.cmd against every local install of Voices of the Void.

.DESCRIPTION
    uninstall.cmd resolves a single install when it is given no path, because
    find-game.ps1 falls back to Find-GamePath. `pixi run deploy` writes to every
    copy Find-AllGamePaths reports, so uninstalling only the first one leaves a
    stale .asi still loading in the other - which then reads as "the fix did not
    land" on the next test run.

    Unattended: /y is passed, and a copy that fails is reported without stopping
    the rest.
#>

[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

$paths = @(Find-AllGamePaths -GameId 'voices-of-the-void')
if ($paths.Count -eq 0) {
    Write-Host 'No Voices of the Void installation found; nothing to uninstall.' -ForegroundColor Yellow
    exit 0
}

$failed = 0
foreach ($path in $paths) {
    Write-Host "Uninstalling from $path" -ForegroundColor Cyan
    & cmd /c (Join-Path $PSScriptRoot 'uninstall.cmd') "$path" /y
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  uninstall.cmd exited $LASTEXITCODE for $path" -ForegroundColor Red
        $failed++
    }
}

Write-Host "$($paths.Count) install(s) processed, $failed failed." -ForegroundColor Green
if ($failed -gt 0) { exit 1 }
exit 0
