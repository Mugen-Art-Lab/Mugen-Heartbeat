
# Building Mugen Heartbeat on Windows

Mugen Heartbeat currently targets Windows x64.

## Requirements

- Windows 10 or Windows 11 x64;
- Visual Studio 2022 with Desktop development with C++;
- Windows 10/11 SDK;
- CMake 3.28 or newer;
- Git.

The first CMake configure step needs internet access to obtain pinned OBS build
dependencies and `msdfgen` v1.13.

## Configure

From the repository root:

```powershell
cmake --preset windows-x64
```

## Build

```powershell
cmake --build --preset windows-x64
```

The plugin-template build uses the `RelWithDebInfo` configuration.

The assembled runtime tree is expected under:

`build_x64\rundir\RelWithDebInfo`

It contains `mugen-heartbeat.dll` and the corresponding plugin data directory.

## Local test installation

Close OBS Studio before replacing the plugin.

Copy the built DLL to:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat\bin\64bit\mugen-heartbeat.dll`

and copy the assembled data directory to:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat\data`

Do not copy build directories, dependency caches, PDB files or patch backups into
release packages.

## Dependencies

The plugin links against OBS `libobs` and Windows system APIs.

The MTSDF text renderer embeds `msdfgen-core` v1.13. The project enables
`MSDFGEN_CORE_ONLY`, so the msdfgen extension dependencies are not part of this
build.

See `THIRD-PARTY-NOTICES.md` and `LICENSES/`.
