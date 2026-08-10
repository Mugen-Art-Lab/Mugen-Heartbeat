# Установщик Mugen Heartbeat

[English version](README.md)

Windows-установщик собирается через Inno Setup 6.

Он устанавливает OBS-плагин для всех пользователей в:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat`

Установщик:

- имеет интерфейс на английском и русском;
- запрашивает права администратора, потому что плагин ставится в `ProgramData`;
- просит полностью закрыть OBS Studio перед установкой;
- так же проверяет запущенную OBS перед удалением;
- устанавливает только релизный payload, подготовленный
  `tools/package-release.ps1`;
- не удаляет `%APPDATA%\obs-studio\plugin_config\mugen-heartbeat`, поэтому
  пользовательские настройки и community-скины переживают обычные
  обновления/удаление.

Полный релиз собирается командой:

```powershell
.\BUILD_RELEASE.cmd
```

Либо можно отдельно собрать установщик из уже готового payload:

```powershell
.\installer\build-installer.ps1 -PayloadRoot <путь> -OutputDir <путь>
```

Для получения `.exe` должен быть установлен Inno Setup 6.
