# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Fails when a file the config differential test compiles has changed.

.DESCRIPTION
    The differential test is only as good as its claim about what it compiled:
    the published build's reader in tests/config_differential/oracle, the frozen
    import in src/legacy_config, and the core sources that import calls.
    tests/config_differential/provenance.txt records each one's SHA-256, and
    `pixi run test` runs this before the tests. SHA256 goes through .NET, because
    Get-FileHash is not found when a runner's pwsh runs this under Windows
    PowerShell.
#>

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$provenance = Join-Path $root 'tests/config_differential/provenance.txt'
$sha256 = [System.Security.Cryptography.SHA256]::Create()
foreach ($line in Get-Content $provenance) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $root $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
}
Write-Host 'config differential provenance: every listed file has its recorded hash'
