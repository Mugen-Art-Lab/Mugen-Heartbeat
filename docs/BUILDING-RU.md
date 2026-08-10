
# Сборка Mugen Heartbeat под Windows

Текущая цель Mugen Heartbeat — Windows x64.

## Требования

- Windows 10 или Windows 11 x64;
- Visual Studio 2022 с компонентом Desktop development with C++;
- Windows 10/11 SDK;
- CMake 3.28 или новее;
- Git.

При первом configure нужен интернет: CMake получает зафиксированные зависимости
OBS и `msdfgen` v1.13.

## Настройка проекта

Из корня репозитория:

```powershell
cmake --preset windows-x64
```

## Сборка

```powershell
cmake --build --preset windows-x64
```

Инфраструктура OBS plugin template использует конфигурацию `RelWithDebInfo`.

Собранное runtime-дерево ожидается в:

`build_x64\rundir\RelWithDebInfo`

Там находятся `mugen-heartbeat.dll` и соответствующая data-папка плагина.

## Локальная тестовая установка

Перед заменой плагина полностью закрой OBS Studio.

Скопируй свежую DLL в:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat\bin\64bit\mugen-heartbeat.dll`

а собранную data-папку — в:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat\data`

Не включай в релизные архивы build-папки, кэши зависимостей, PDB и резервные
копии патчеров.

## Зависимости

Плагин линкуется с OBS `libobs` и системными API Windows.

MTSDF-рендерер встраивает `msdfgen-core` v1.13. В проекте включён
`MSDFGEN_CORE_ONLY`, поэтому расширенные зависимости msdfgen в эту сборку не
входят.

См. `THIRD-PARTY-NOTICES.md` и `LICENSES/`.
