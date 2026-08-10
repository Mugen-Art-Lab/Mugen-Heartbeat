# Mugen Heartbeat installer

[Русская версия](README-RU.md)

The Windows installer is built with Inno Setup 6.

It installs the all-users OBS plugin into:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat`

The installer:

- provides English and Russian setup UI;
- requires administrator privileges because the plugin is installed in
  `ProgramData`;
- asks the user to close OBS Studio before installation;
- performs the same OBS-running check before uninstalling;
- installs only the release payload prepared by `tools/package-release.ps1`;
- does not remove `%APPDATA%\obs-studio\plugin_config\mugen-heartbeat`, so user
  settings and community skins survive normal updates/uninstalls.

Build the complete release with:

```powershell
.\BUILD_RELEASE.cmd
```

or build only the installer from an existing payload:

```powershell
.\installer\build-installer.ps1 -PayloadRoot <path> -OutputDir <path>
```

Inno Setup 6 must be installed for the `.exe` artifact.
