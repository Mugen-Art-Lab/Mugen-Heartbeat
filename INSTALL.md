
# Installation, update, and removal

[Русская версия](INSTALL-RU.md)

## Recommended installer

1. Close OBS Studio completely.
2. Run `Mugen-Heartbeat-<version>-Windows-x64-Setup.exe`.
3. Accept the Windows administrator prompt.
4. Start OBS Studio.
5. Add a source and choose **Mugen Heartbeat**.

The installer uses:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat`

The installer is expected to be unsigned for the initial release, so Windows
SmartScreen may show an unknown-publisher warning. Download release files only
from the Mugen Art Lab GitHub repository and compare SHA-256 values with the
release checksum file when desired.

## Manual ZIP

Close OBS Studio and extract the `mugen-heartbeat` folder from
`Mugen-Heartbeat-<version>-Windows-x64.zip` into:

`C:\ProgramData\obs-studio\plugins`

The ZIP includes the same DLL in two locations for OBS compatibility:

- OBS Studio 33 and later: `C:\ProgramData\obs-studio\plugins\mugen-heartbeat\mugen-heartbeat.dll`
- OBS Studio 31/32: `C:\ProgramData\obs-studio\plugins\mugen-heartbeat\bin\64bit\mugen-heartbeat.dll`

Do not remove either DLL copy; OBS selects the appropriate layout. The plugin metadata for the OBS 33 plugin manager lives at `data\manifest.json`.

To remove a manual installation, close OBS Studio and delete the installed
`mugen-heartbeat` folder.

## Portable OBS ZIP (32 and 33+)

Close the portable OBS installation completely. Extract
`Mugen-Heartbeat-<version>-Windows-x64-portable.zip` **into the OBS root folder**.
The archive contains both OBS loader layouts:

```text
plugins\mugen-heartbeat\mugen-heartbeat.dll             (OBS 33+)
plugins\mugen-heartbeat\data\...                    (OBS 33+)
obs-plugins\64bit\mugen-heartbeat.dll                 (OBS 32)
data\obs-plugins\mugen-heartbeat\...                (OBS 32, including skins)
```

Both loader layouts contain the **same compiled DLL**. The portable ZIP does not
install anything under `ProgramData`. To remove it, close OBS and delete only
`plugins\mugen-heartbeat`, `obs-plugins\64bit\mugen-heartbeat.dll`,
and `data\obs-plugins\mugen-heartbeat`. Keep the shared parent folders.

## User settings and skins

Per-user plugin data is stored separately under:

`%APPDATA%\obs-studio\plugin_config\mugen-heartbeat`

This folder may contain saved settings and user/community skins.

Normal plugin updates and uninstallers must not remove this directory
automatically.

## Updating

Close OBS Studio before replacing the plugin.

For an installer build, run the newer installer over the existing installation.
For a ZIP build, replace the installed `mugen-heartbeat` plugin folder with the
newer one.

User settings and user skins are stored separately and should remain intact.

## Troubleshooting

- Make sure OBS Studio was fully closed during installation or update.
- Confirm that Windows and OBS Studio are x64.
- Confirm that Bluetooth is enabled.
- Enable heart-rate sharing/broadcasting on the wearable before scanning.
- Open **Help → Log Files → View Current Log** in OBS and search for
  `Mugen Heartbeat` or `mugen-heartbeat.dll`.
- If the device is detected but BPM is unavailable, restart heart-rate sharing
  on the wearable and reconnect.
- For a bug report, include the OBS version, Windows version, wearable model,
  Bluetooth adapter when known, installation method, reproduction steps and the
  relevant OBS log.
