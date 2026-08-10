# SPDX-FileCopyrightText: 2026 Mugen Art Lab
# SPDX-License-Identifier: GPL-2.0-or-later

param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release", "MinSizeRel")]
    [string]$Configuration = "RelWithDebInfo",

    [switch]$BuildInstallerIfAvailable
)

$ErrorActionPreference = "Stop"

$pluginName = "mugen-heartbeat"
$dllName = "mugen-heartbeat.dll"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$version = (Get-Content (Join-Path $sourceRoot "VERSION") -Raw).Trim()
$buildRoot = Join-Path $sourceRoot "build_x64"

if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "build_x64 was not found. Build Mugen Heartbeat first."
}

function Find-PluginDll {
    param(
        [string]$Root,
        [string]$Config
    )

    $preferred = @(
        (Join-Path $Root "rundir\$Config\mugen-heartbeat.dll"),
        (Join-Path $Root "$Config\mugen-heartbeat.dll"),
        (Join-Path $Root "rundir\$Config\obs-plugins\64bit\mugen-heartbeat.dll"),
        (Join-Path $Root "dist\$Config\obs-plugins\64bit\mugen-heartbeat.dll"),
        (Join-Path $Root "dist\$Config\bin\64bit\mugen-heartbeat.dll")
    )

    foreach ($candidate in $preferred) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $found = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $dllName -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $found) {
        throw "$dllName was not found under build_x64."
    }

    return $found.FullName
}

function Find-PluginData {
    param(
        [string]$Root,
        [string]$Config
    )

    $preferred = @(
        (Join-Path $Root "rundir\$Config\mugen-heartbeat"),
        (Join-Path $Root "rundir\$Config\data\obs-plugins\mugen-heartbeat"),
        (Join-Path $Root "dist\$Config\data\obs-plugins\mugen-heartbeat")
    )

    foreach ($candidate in $preferred) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    # Safe fallback: the repository's data/ is the canonical plugin resource tree.
    $fallback = Join-Path $sourceRoot "data"
    if (Test-Path -LiteralPath $fallback -PathType Container) {
        Write-Host "Runtime data directory was not found; using repository data/." -ForegroundColor Yellow
        return (Resolve-Path -LiteralPath $fallback).Path
    }

    throw "Plugin data directory was not found."
}

function Reset-Directory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Copy-ReleaseDocuments {
    param([string]$Destination)

    $files = @(
        "README.md",
        "README-RU.md",
        "INSTALL.md",
        "INSTALL-RU.md",
        "CHANGELOG.md",
        "AI-DISCLOSURE.md",
        "SUPPORT.md",
        "SKIN-FORMAT.md",
        "SKIN-FORMAT.ru.md",
        "THIRD-PARTY-NOTICES.md",
        "LICENSE"
    )

    foreach ($name in $files) {
        $source = Join-Path $sourceRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-Item -LiteralPath $source -Destination $Destination -Force
        }
    }

    $licenses = Join-Path $sourceRoot "LICENSES"
    if (Test-Path -LiteralPath $licenses -PathType Container) {
        Copy-Item -LiteralPath $licenses -Destination (Join-Path $Destination "LICENSES") -Recurse -Force
    }
}

function New-ZipFromDirectory {
    param(
        [string]$SourceDirectory,
        [string]$ZipPath
    )

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    Compress-Archive `
        -Path (Join-Path $SourceDirectory "*") `
        -DestinationPath $ZipPath `
        -CompressionLevel Optimal
}

$dll = Find-PluginDll -Root $buildRoot -Config $Configuration
$pluginData = Find-PluginData -Root $buildRoot -Config $Configuration

$releaseRoot = Join-Path $sourceRoot "release\$version"
$workRoot = Join-Path $sourceRoot "release-work\$version"
$stageRoot = Join-Path $workRoot "standard"
$payloadRoot = Join-Path $workRoot "installer-payload"

Reset-Directory $releaseRoot
Reset-Directory $workRoot

try {
    # Manual ZIP layout:
    # extract into C:\ProgramData\obs-studio\plugins
    $standardPlugin = Join-Path $stageRoot $pluginName
    $standardBin = Join-Path $standardPlugin "bin\64bit"
    $standardData = Join-Path $standardPlugin "data"

    New-Item -ItemType Directory -Path $standardBin -Force | Out-Null
    New-Item -ItemType Directory -Path $standardData -Force | Out-Null

    Copy-Item -LiteralPath $dll -Destination (Join-Path $standardBin $dllName) -Force
    Copy-Item -Path (Join-Path $pluginData "*") -Destination $standardData -Recurse -Force
    Copy-ReleaseDocuments -Destination $standardPlugin

    # Installer payload mirrors the recommended ProgramData plugin folder.
    New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
    Copy-Item -LiteralPath $standardPlugin -Destination $payloadRoot -Recurse -Force

    $standardZip = Join-Path $releaseRoot "Mugen-Heartbeat-$version-Windows-x64.zip"
    New-ZipFromDirectory -SourceDirectory $stageRoot -ZipPath $standardZip

    if ($BuildInstallerIfAvailable) {
        & (Join-Path $sourceRoot "installer\build-installer.ps1") `
            -PayloadRoot $payloadRoot `
            -OutputDir $releaseRoot

        if ($LASTEXITCODE -ne 0) {
            throw "Installer build failed with code $LASTEXITCODE"
        }
    }

    $assets = Get-ChildItem -LiteralPath $releaseRoot -File |
        Where-Object { $_.Extension -in @(".zip", ".exe") } |
        Sort-Object Name

    $hashLines = foreach ($asset in $assets) {
        $hash = (Get-FileHash -LiteralPath $asset.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $($asset.Name)"
    }

    $hashPath = Join-Path $releaseRoot "SHA256SUMS.txt"
    $hashLines | Set-Content -LiteralPath $hashPath -Encoding ASCII

    Write-Host ""
    Write-Host "Release packages ready:" -ForegroundColor Green
    foreach ($asset in $assets) {
        Write-Host "  $($asset.FullName)"
    }

    if (-not ($assets | Where-Object { $_.Extension -eq ".exe" })) {
        Write-Host "  Installer was not built (Inno Setup 6 not available)." -ForegroundColor Yellow
    }

    Write-Host "  $hashPath"
}
finally {
    if (Test-Path -LiteralPath $workRoot) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
