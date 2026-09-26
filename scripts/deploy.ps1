# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Deploys the built .asi and the vendored ASI loader into every local install
    of Voices of the Void (the dev loop).

.DESCRIPTION
    Unattended: no prompts, exits non-zero with a diagnostic on any failure.
    Every copy Find-AllGamePaths reports is written to; a supplied path wins.
    The mod creates CameraUnlock.ini itself on first launch, importing
    HeadTracking.ini where an earlier build left one, so no config file is
    copied.

.PARAMETER GamePath
    Voices of the Void install root. Omit to deploy to every detected install.
#>

[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/DevDeploy.psm1') -Force
Import-Module (Join-Path $root 'cameraunlock-core/powershell/ModDeployment.psm1') -Force

# The shipping exe (VotV\Binaries\Win64\VotV-Win64-Shipping.exe) imports
# WINMM.dll statically and does not import dinput8.dll, so the loader goes in as
# winmm.dll beside the exe, exactly as install.cmd does it.
$null = Invoke-DevDeployASILoader `
    -GameId 'voices-of-the-void' `
    -GameDisplayName 'Voices of the Void' `
    -BuildOutputPath (Join-Path $root 'build/Release') `
    -ModDllName 'VoicesOfTheVoidHeadTracking.asi' `
    -VendorLoaderDll (Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll') `
    -AsiLoaderName 'winmm.dll' `
    -GivenPath $GamePath
