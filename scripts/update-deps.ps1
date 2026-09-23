#!/usr/bin/env pwsh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range. Manual; commit the result. CI never refreshes;
# install.cmd extracts the committed vendor tree.
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset, so this script extracts dinput8.dll rather than calling
# Update-VendoredLoader, which vendors the downloaded artifact whole.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init --recursive'."
}
Import-Module $module -Force

$vendorAsiDir     = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll     = Join-Path $vendorAsiDir 'dinput8.dll'
$vendorAsiLicense = Join-Path $vendorAsiDir 'LICENSE'
$vendorAsiReadme  = Join-Path $vendorAsiDir 'README.md'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

# Fixed by the PE format: the offset of the PE header pointer in the DOS stub,
# the "PE\0\0" signature it points at, and IMAGE_FILE_MACHINE_AMD64. A linker's
# DOS stub puts the PE header a few hundred bytes in, so a 4 KB prefix always
# covers it.
$PeHeaderPointerOffset = 0x3C
$PeSignature           = 0x00004550
$ImageFileMachineAmd64 = 0x8664
$PeHeaderSearchBytes   = 0x1000

$tempDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$tempZip     = Join-Path $tempDir 'upstream.zip'
$tempDll     = Join-Path $tempDir 'dinput8.dll'
$tempLicense = Join-Path $tempDir 'LICENSE'
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $tempDll, $true)

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($licenseEntry, $tempLicense, $true)
        }
    } finally { $zip.Dispose() }

    # Vetted in the temp directory, before anything is moved into vendor/. An x86
    # proxy in an x64 game's exe directory crashes it on launch before our code
    # runs, and the crash points at the game rather than at the loader.
    $header = New-Object byte[] $PeHeaderSearchBytes
    $stream = [System.IO.File]::OpenRead($tempDll)
    try {
        # Looped, because FileStream.Read may return fewer bytes than asked for.
        # Treating one short read as end-of-file rejects a sound loader with a
        # "no PE header" message that sends the next session hunting upstream.
        $headerLength = 0
        while ($headerLength -lt $header.Length) {
            $read = $stream.Read($header, $headerLength, $header.Length - $headerLength)
            if ($read -le 0) { break }
            $headerLength += $read
        }
    } finally { $stream.Dispose() }

    $peOffset = if ($headerLength -ge ($PeHeaderPointerOffset + 4)) {
        [BitConverter]::ToInt32($header, $PeHeaderPointerOffset)
    } else {
        -1
    }
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $headerLength) {
        throw "Extracted ASI Loader has no PE header within its first $PeHeaderSearchBytes bytes; refusing to vendor it."
    }
    if ([BitConverter]::ToUInt32($header, $peOffset) -ne $PeSignature) {
        throw "Extracted ASI Loader is not a PE image; refusing to vendor it."
    }
    $machine = [BitConverter]::ToUInt16($header, $peOffset + 4)
    if ($machine -ne $ImageFileMachineAmd64) {
        throw ("Extracted ASI Loader is not x64 (machine=0x{0:X4}); refusing to vendor it." -f $machine)
    }

    $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    # Idempotency: an upstream that has not moved must leave the tree clean. Without
    # this the FetchedAt line rewrites README.md on every run, so `git status` after a
    # no-op refresh shows a timestamp-only diff with no artifact behind it.
    if ((Test-Path -LiteralPath $vendorAsiDll) -and (Test-Path -LiteralPath $vendorAsiLicense) -and (Test-Path -LiteralPath $vendorAsiReadme) -and
        ((Get-FileHash -LiteralPath $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
        Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "vendor/ultimate-asi-loader is already up to date." -ForegroundColor Green
        return
    }

    Move-Item -LiteralPath $tempDll -Destination $vendorAsiDll -Force

    if (Test-Path -LiteralPath $tempLicense) {
        Move-Item -LiteralPath $tempLicense -Destination $vendorAsiLicense -Force
    } else {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorAsiLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it to',
        '`winmm.dll` beside the game exe (`VotV/Binaries/Win64/`) as the ASI hook slot:',
        '`VotV-Win64-Shipping.exe` imports WINMM.dll statically and does not import',
        'dinput8.dll, which Windows would only resolve out of System32.'
    ) -join "`n"
    Set-Content -Path $vendorAsiReadme -Value $readme -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
