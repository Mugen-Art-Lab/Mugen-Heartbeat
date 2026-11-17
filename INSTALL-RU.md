
# Установка, обновление и удаление

[English version](INSTALL.md)

## Установщик — рекомендуется

1. Полностью закрой OBS Studio.
2. Запусти `Mugen-Heartbeat-<version>-Windows-x64-Setup.exe`.
3. Подтверди запрос администратора Windows.
4. Запусти OBS Studio.
5. Добавь источник **Mugen Heartbeat**.

Установщик использует папку:

`C:\ProgramData\obs-studio\plugins\mugen-heartbeat`

Для первого релиза установщик, вероятно, будет без цифровой подписи, поэтому
Windows SmartScreen может показать предупреждение о неизвестном издателе.
Скачивай релизные файлы только из репозитория Mugen Art Lab и при необходимости
сверяй SHA-256 с файлом контрольных сумм релиза.

## Ручная установка ZIP

Полностью закрой OBS Studio и распакуй папку `mugen-heartbeat` из
`Mugen-Heartbeat-<version>-Windows-x64.zip` в:

`C:\ProgramData\obs-studio\plugins`

ZIP содержит две одинаковые DLL для совместимости с разными версиями OBS:

- OBS Studio 33 и новее: `C:\ProgramData\obs-studio\plugins\mugen-heartbeat\mugen-heartbeat.dll`
- OBS Studio 31/32: `C:\ProgramData\obs-studio\plugins\mugen-heartbeat\bin\64bit\mugen-heartbeat.dll`

Не удаляй ни одну из копий: OBS выберет подходящий вариант. Метаданные для менеджера плагинов OBS 33 находятся в `data\manifest.json`.

Для ручного удаления полностью закрой OBS и удали установленную папку
`mugen-heartbeat`.

## ZIP для портативной OBS (32 и 33+)

Полностью закрой портативную OBS. Распакуй содержимое
`Mugen-Heartbeat-<version>-Windows-x64-portable.zip` **в корень папки OBS**,
например `C:\OBS-Portable`. В архиве сразу две структуры:

```text
plugins\mugen-heartbeat\mugen-heartbeat.dll             (OBS 33+)
plugins\mugen-heartbeat\data\...                    (OBS 33+)
obs-plugins\64bit\mugen-heartbeat.dll                 (OBS 32)
data\obs-plugins\mugen-heartbeat\...                (OBS 32, включая скины)
```

Оба пути используют **одну и ту же DLL**, повторно собирать плагин не нужно.
Портативный ZIP не устанавливает ничего в `ProgramData`.
Для ручного удаления сначала закрой OBS, затем удали только
`plugins\mugen-heartbeat`, `obs-plugins\64bit\mugen-heartbeat.dll`
и `data\obs-plugins\mugen-heartbeat`. Не удаляй общие папки других плагинов.

## Пользовательские настройки и скины

Пользовательские данные плагина хранятся отдельно:

`%APPDATA%\obs-studio\plugin_config\mugen-heartbeat`

Там могут находиться сохранённые настройки и пользовательские/community-скины.

Обычное обновление и деинсталлятор не должны автоматически удалять эту папку.

## Обновление

Перед заменой файлов полностью закрой OBS Studio.

Для установщика просто запусти более новую версию поверх существующей. Для ZIP
замени установленную папку `mugen-heartbeat` на новую.

Пользовательские настройки и скины лежат отдельно и должны сохраниться.

## Диагностика

- Проверь, что OBS была полностью закрыта во время установки или обновления.
- Проверь, что Windows и OBS Studio — x64.
- Проверь, что Bluetooth включён.
- Перед поиском устройств включи передачу/шаринг пульса на браслете или часах.
- В OBS открой **Справка → Файлы журналов → Просмотреть текущий журнал** и найди
  `Mugen Heartbeat` или `mugen-heartbeat.dll`.
- Если устройство найдено, но BPM недоступен, перезапусти передачу пульса на
  устройстве и подключись заново.
- Для баг-репорта приложи версию OBS, версию Windows, модель устройства,
  Bluetooth-адаптер (если известен), способ установки, шаги воспроизведения и
  соответствующий журнал OBS.
