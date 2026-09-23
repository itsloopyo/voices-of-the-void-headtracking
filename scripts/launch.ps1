# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Launches Voices of the Void for a head-tracking test run.

.DESCRIPTION
    Unattended - Start-Process without -Wait, no prompts. The game is a direct
    download with no store client behind it, so this starts the shipping
    executable itself. The VotV.exe beside WindowsNoEditor is only a launcher
    shim and is not the process the mod loads into.

.PARAMETER GamePath
    Install root (the folder holding WindowsNoEditor). Omit to use the detected
    install.

.PARAMETER Windowed
    Launch windowed at -ResX by -ResY instead of the saved display mode.
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$GamePath,
    [switch]$Windowed,
    [int]$ResX = 1280,
    [int]$ResY = 720
)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $found = @(Find-AllGamePaths -GameId 'voices-of-the-void')
    if ($found.Count -eq 0) {
        Write-Host "ERROR: no Voices of the Void install found. Set VOICES_OF_THE_VOID_PATH or pass the path." -ForegroundColor Red
        exit 1
    }
    $GamePath = $found[0]
}

$exe = Join-Path $GamePath 'VotV\Binaries\Win64\VotV-Win64-Shipping.exe'
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: $exe not found." -ForegroundColor Red
    exit 1
}

$gameArgs = @()
if ($Windowed) { $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY") }

Write-Host "Launching $exe $($gameArgs -join ' ')" -ForegroundColor Cyan
if ($gameArgs.Count -gt 0) { Start-Process -FilePath $exe -ArgumentList $gameArgs }
else { Start-Process -FilePath $exe }
