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
$buildspec = Get-Content -LiteralPath (Join-Path $sourceRoot "buildspec.json") -Raw | ConvertFrom-Json
if ($buildspec.version -ne $version) {
    throw "VERSION ($version) and buildspec.json ($($buildspec.version)) do not match."
}
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
$portableStage = Join-Path $workRoot "portable"
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

    # Windows OBS 33+ reads the plugin root DLL.
    # OBS 31/32 keep loading the legacy bin\64bit copy.
    # Both files come from the same build so their versions stay identical.
    Copy-Item -LiteralPath $dll -Destination (Join-Path $standardPlugin $dllName) -Force
    Copy-Item -LiteralPath $dll -Destination (Join-Path $standardBin $dllName) -Force
    Copy-Item -Path (Join-Path $pluginData "*") -Destination $standardData -Recurse -Force
    # OBS 33 plugin manager reads data/manifest.json (not buildspec.json).
    # Generate metadata from the single authoritative VERSION file.
    $manifest = [ordered]@{
        id = $pluginName
        name = $pluginName
        display_name = "Mugen Heartbeat"
        version = $version
        os_arch = "windows-x64"
        description = "Local Bluetooth LE heart-rate source for OBS Studio."
        long_description = "Displays real-time BPM from compatible BLE heart-rate wearables in OBS, with skins and configurable reactions. No cloud or subscription required."
        urls = [ordered]@{
            repository = "https://github.com/Mugen-Art-Lab/Mugen-Heartbeat"
            website = "https://mugenartlab.com/"
            support = "https://mugenartlab.com/"
        }
        has_icon = $false
        has_banner = $false
    }
    $manifestJson = $manifest | ConvertTo-Json -Depth 5
    [System.IO.File]::WriteAllText(
        (Join-Path $standardData "manifest.json"),
        $manifestJson + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
    Copy-ReleaseDocuments -Destination $standardPlugin

    # A single portable ZIP supports BOTH loader layouts:
    #   OBS 33+: plugins/mugen-heartbeat/...
    #   OBS 32:  obs-plugins/64bit/mugen-heartbeat.dll plus
    #            data/obs-plugins/mugen-heartbeat/...
    # Reuse the same compiled DLL and generated data/manifest.json as the
    # standard package. Copy the COMPLETE data tree, including bundled skins,
    # MTSDF effects and localizations, for the legacy loader.
    $portableModern = Join-Path $portableStage "plugins\$pluginName"
    $portableLegacyBin = Join-Path $portableStage "obs-plugins\64bit"
    $portableLegacyData = Join-Path $portableStage "data\obs-plugins\$pluginName"
    foreach ($folder in @($portableModern, $portableLegacyBin, $portableLegacyData)) {
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
    }
    Copy-Item -Path (Join-Path $standardPlugin "*") -Destination $portableModern -Recurse -Force
    Copy-Item -LiteralPath $dll -Destination (Join-Path $portableLegacyBin $dllName) -Force
    Copy-Item -Path (Join-Path $standardData "*") -Destination $portableLegacyData -Recurse -Force

    # Installer payload mirrors the recommended ProgramData plugin folder.
    New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
    Copy-Item -LiteralPath $standardPlugin -Destination $payloadRoot -Recurse -Force

    $standardZip = Join-Path $releaseRoot "Mugen-Heartbeat-$version-Windows-x64.zip"
    $portableZip = Join-Path $releaseRoot "Mugen-Heartbeat-$version-Windows-x64-portable.zip"
    New-ZipFromDirectory -SourceDirectory $stageRoot -ZipPath $standardZip
    New-ZipFromDirectory -SourceDirectory $portableStage -ZipPath $portableZip

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
