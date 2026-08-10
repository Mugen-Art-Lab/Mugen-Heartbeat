# SPDX-FileCopyrightText: 2026 Mugen Art Lab
# SPDX-License-Identifier: GPL-2.0-or-later

param(
    [switch]$SkipConfigure
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Push-Location $repoRoot
try {
    Write-Host ""
    Write-Host "Mugen Heartbeat release build" -ForegroundColor Cyan
    Write-Host "Repository: $repoRoot" -ForegroundColor DarkGray

    if (-not $SkipConfigure) {
        Write-Host ""
        Write-Host "[1/3] Configure" -ForegroundColor Cyan
        & cmake --preset windows-x64
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed with code $LASTEXITCODE"
        }
    }

    Write-Host ""
    Write-Host "[2/3] Build" -ForegroundColor Cyan
    & cmake --build --preset windows-x64
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with code $LASTEXITCODE"
    }

    Write-Host ""
    Write-Host "[3/3] Package" -ForegroundColor Cyan
    & (Join-Path $repoRoot "tools\package-release.ps1") -BuildInstallerIfAvailable
    if ($LASTEXITCODE -ne 0) {
        throw "Release packaging failed with code $LASTEXITCODE"
    }

    $version = (Get-Content (Join-Path $repoRoot "VERSION") -Raw).Trim()
    $releaseDir = Join-Path $repoRoot "release\$version"

    Write-Host ""
    Write-Host "DONE" -ForegroundColor Green
    Write-Host "Release directory:" -ForegroundColor Green
    Write-Host "  $releaseDir"
}
finally {
    Pop-Location
}
