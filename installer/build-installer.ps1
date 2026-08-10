# SPDX-FileCopyrightText: 2026 Mugen Art Lab
# SPDX-License-Identifier: GPL-2.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = (Get-Content (Join-Path $repoRoot "VERSION") -Raw).Trim()
$payload = (Resolve-Path -LiteralPath $PayloadRoot).Path

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDir).Path

function Find-Iscc {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $null
}

$iscc = Find-Iscc
if (-not $iscc) {
    Write-Host ""
    Write-Host "Inno Setup 6 was not found." -ForegroundColor Yellow
    Write-Host "The ZIP package can still be released without the installer." -ForegroundColor Yellow
    Write-Host "Install Inno Setup 6, then run BUILD_RELEASE.cmd again." -ForegroundColor Yellow
    Write-Host "winget install JRSoftware.InnoSetup" -ForegroundColor DarkGray
    exit 0
}

$iss = Join-Path $PSScriptRoot "Mugen-Heartbeat.iss"

Write-Host ""
Write-Host "Building Mugen Heartbeat installer..." -ForegroundColor Cyan
Write-Host "Inno Setup: $iscc" -ForegroundColor DarkGray

& $iscc `
    "/DMyAppVersion=$version" `
    "/DPayloadRoot=$payload" `
    "/DOutputDir=$output" `
    "/DRepoRoot=$repoRoot" `
    $iss

exit $LASTEXITCODE
