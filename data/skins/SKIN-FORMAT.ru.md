# Формат скинов Mugen Heartbeat

Версия формата скинов: **1**

Скин — это папка, содержащая файл `skin.json`. Встроенные скины поставляются
вместе с Mugen Heartbeat. Пользовательские/community-скины лежат в папке,
которую открывает кнопка **«Открыть папку пользовательских скинов»**.

Формат декларативный: скины не содержат JavaScript и исполняемый код.

## Минимальный пример

```json
{
  "schema": 1,
  "id": "my-skin",
  "name": "My Skin",
  "author": "Your Name",
  "version": "1.0",
  "description": "Мой первый скин Mugen Heartbeat.",
  "canvas": {
    "width": 640,
    "height": 260,
    "background": "#101417"
  },
  "offline": {
    "color": "#7b858a",
    "status": "NO SIGNAL"
  },
  "zones": [
    { "max_bpm": 89,  "color": "#45d36b", "status": "STABLE" },
    { "max_bpm": 119, "color": "#e0b54c", "status": "CAUTION" },
    { "max_bpm": 240, "color": "#e85757", "status": "DANGER" }
  ],
  "elements": [
    {
      "type": "text",
      "value": "{bpm}",
      "x": 200,
      "y": 60,
      "width": 180,
      "height": 110,
      "align": "center",
      "valign": "center",
      "font": "Arial",
      "size": 96,
      "bold": true,
      "color": "zone",
      "pulse": true
    }
  ]
}
```

## Метаданные

Обязательные поля:

- `schema` — сейчас `1`
- `id` — стабильный уникальный идентификатор скина
- `name` — отображаемое имя

Необязательные:

- `author`
- `version`
- `description`

Пользовательский скин с тем же `id`, что и встроенный, имеет приоритет над
встроенным.

### Локализованные метаданные

`name` и `description` могут быть обычной строкой:

```json
"name": "Heartbeat Lite",
"description": "Компактный HUD для стрима."
```

или объектом с переводами:

```json
"name": {
  "en": "Field Medical Monitor",
  "ru": "Полевой медмонитор"
},
"description": {
  "en": "Compact medical HUD for streaming.",
  "ru": "Компактный медицинский HUD для стрима."
}
```

Ключом языка может быть краткий код (`en`, `ru`) или полный locale OBS
(`en-US`, `ru-RU`).

Порядок выбора:

1. точный текущий locale OBS, например `ru-RU`;
2. базовый язык, например `ru`;
3. `en-US`;
4. `en`;
5. первый непустой перевод в объекте.

Если в объекте указан только один перевод, он допустим и используется как
fallback для любого языка OBS.

## Canvas

```json
"canvas": {
  "width": 640,
  "height": 260,
  "background": "#101417"
}
```

- `width`, `height` — 64..4096 пикселей
- `background` — `#RRGGBB` или `#RRGGBBAA`

## Зоны BPM

```json
"zones": [
  { "max_bpm": 89,  "color": "#45d36b", "status": "STABLE" },
  { "max_bpm": 119, "color": "#e0b54c", "status": "CAUTION" },
  { "max_bpm": 240, "color": "#e85757", "status": "DANGER" }
]
```

Элементы могут использовать `"color": "zone"`, чтобы автоматически принимать
цвет активной зоны BPM.

Необязательное состояние без сигнала:

```json
"offline": {
  "color": "#7b858a",
  "status": "NO SIGNAL"
}
```

## Переменные

Текстовые элементы поддерживают:

- `{bpm}` — текущий BPM или `--`, если свежих данных нет
- `{heart}` — символ сердца
- `{status}` — статус активной зоны BPM / offline-состояния

## Элементы

Schema 1 сейчас поддерживает `text`, `rect` и `ecg`.

Все поддерживаемые элементы могут использовать:

- `x`, `y`
- `pulse: true` — пульсация элемента в такт сердцебиению

### Текст

```json
{
  "type": "text",
  "value": "{heart} {bpm}",
  "x": 40,
  "y": 60,
  "width": 300,
  "height": 100,
  "align": "center",
  "valign": "center",

  "font": "Arial",
  "size": 72,
  "bold": true,
  "italic": false,

  "color": "zone",
  "opacity": 100,
  "pulse": true,

  "outline": true,
  "outline_size": 2,
  "outline_color": "#000000",
  "outline_opacity": 100,

  "shadow": false,
  "shadow_x": 3,
  "shadow_y": 3,
  "shadow_color": "#000000",
  "shadow_opacity": 50
}
```

`align`: `left`, `center`, `right`.

`valign`: `top`, `center` / `middle`, `bottom`.

Если заданы `width` / `height`, Mugen Heartbeat выравнивает видимые границы
MTSDF-глифов внутри этой точной области.

### Прямоугольник

```json
{
  "type": "rect",
  "x": 10,
  "y": 10,
  "width": 620,
  "height": 4,
  "color": "zone",
  "opacity": 100,
  "pulse": false
}
```

### ЭКГ

```json
{
  "type": "ecg",
  "x": 40,
  "y": 80,
  "width": 360,
  "height": 90,
  "color": "zone",
  "opacity": 100,
  "pulse": true,
  "line_thickness": 2,
  "history_ms": 3200,
  "amplitude": 0.9,
  "baseline_opacity": 28
}
```

- `line_thickness` — 1..8
- `history_ms` — 1000..8000
- `amplitude` — 0.1..1.0
- `baseline_opacity` — 0..100

Развёртка ЭКГ идёт слева направо и использует то же ядро синхронизации с
сердцебиением, что и остальной Mugen Heartbeat.

## Пульсация элементов

Любой поддерживаемый элемент может задать:

```json
"pulse": true
```

После этого элемент следует таймингу сердцебиения Mugen Heartbeat. Если доступны
RR-интервалы, используется их тайминг; иначе тайминг рассчитывается из BPM.

## Нативные настройки скина

Скин может объявить нативные элементы управления Mugen Heartbeat:

```json
"customization": {
  "background_opacity": true,
  "background_opacity_default": 80
}
```

При `background_opacity: true` в свойствах OBS появляется ползунок
**«Непрозрачность фона»** для этого скина.

Значение абсолютное:

- `100%` — фон canvas полностью непрозрачен
- `0%` — фон canvas полностью прозрачен

Настройка влияет только на `canvas.background`. Текст, ЭКГ, прямоугольники и
статусы сохраняют собственную непрозрачность.

`background_opacity_default` необязателен; значение по умолчанию — `100`.

Mugen Heartbeat запоминает выбранную непрозрачность отдельно для каждого скина.

## Безопасность и совместимость

- Schema остаётся версии `1`.
- Старые скины с обычными строками metadata продолжают работать.
- Неизвестные типы элементов игнорируются.
- Скины декларативны и не могут исполнять скачанный код.

Будущие версии schema могут добавить новые ресурсы, элементы, анимации и
нативные параметры.
