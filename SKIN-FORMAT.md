# Mugen Heartbeat Skin Format

Skin format version: **1**

A skin is a folder containing a `skin.json` file. Built-in skins ship with
Mugen Heartbeat. User/community skins live in the folder opened by the
**Open user skins folder** button.

The format is declarative: skins do not contain JavaScript or executable code.

## Minimal example

```json
{
  "schema": 1,
  "id": "my-skin",
  "name": "My Skin",
  "author": "Your Name",
  "version": "1.0",
  "description": "My first Mugen Heartbeat skin.",
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

## Metadata

Required:

- `schema` — currently `1`
- `id` — stable unique skin id
- `name` — display name

Optional:

- `author`
- `version`
- `description`

A user skin with the same `id` as a built-in skin overrides the built-in skin.

### Localized metadata

`name` and `description` may be either a plain string:

```json
"name": "Heartbeat Lite",
"description": "Compact stream HUD."
```

or a localization object:

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

Locale keys may use a language code (`en`, `ru`) or a full OBS-style locale
(`en-US`, `ru-RU`).

Resolution order:

1. exact current OBS locale, for example `ru-RU`;
2. base language, for example `ru`;
3. `en-US`;
4. `en`;
5. first non-empty translation in the object.

An object containing only one translation is valid and that translation becomes
the fallback for every OBS language.

## Canvas

```json
"canvas": {
  "width": 640,
  "height": 260,
  "background": "#101417"
}
```

- `width`, `height` — 64..4096 pixels
- `background` — `#RRGGBB` or `#RRGGBBAA`

## BPM zones

```json
"zones": [
  { "max_bpm": 89,  "color": "#45d36b", "status": "STABLE" },
  { "max_bpm": 119, "color": "#e0b54c", "status": "CAUTION" },
  { "max_bpm": 240, "color": "#e85757", "status": "DANGER" }
]
```

Elements may use `"color": "zone"` to follow the active BPM zone.

Optional no-signal state:

```json
"offline": {
  "color": "#7b858a",
  "status": "NO SIGNAL"
}
```

## Variables

Text elements support:

- `{bpm}` — current BPM, or `--` without a fresh reading
- `{heart}` — heart symbol
- `{status}` — status from the active BPM zone / offline state

## Elements

Schema 1 currently supports `text`, `rect`, and `ecg`.

All supported elements may use:

- `x`, `y`
- `pulse: true` — beat-synced element pulse

### Text

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

When `width` / `height` are provided, Mugen Heartbeat aligns the visible MTSDF
glyph bounds inside that exact box.

### Rectangle

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

### ECG

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

The ECG sweep moves left to right and uses the same beat timing core as the rest
of Mugen Heartbeat.

## Pulse animation

Any supported element may set:

```json
"pulse": true
```

The element then follows Mugen Heartbeat beat timing. RR timing is used when
available; otherwise timing is derived from BPM.

## Native skin customization

A skin may expose native Mugen Heartbeat controls:

```json
"customization": {
  "background_opacity": true,
  "background_opacity_default": 80
}
```

When `background_opacity` is enabled, OBS Properties shows a
**Background opacity** slider for that skin.

The setting is absolute:

- `100%` — fully opaque canvas background
- `0%` — fully transparent canvas background

It affects `canvas.background` only. Text, ECG, rectangles and status elements
keep their own opacity.

`background_opacity_default` is optional and defaults to `100`.

Mugen Heartbeat remembers the chosen background opacity independently for each
skin.

## Safety and compatibility

- Schema remains version `1`.
- Existing skins with plain string metadata continue to work.
- Unknown element types are ignored.
- Skins are declarative and cannot execute downloaded code.

Future schema revisions may add more assets, elements, animations and native
customization options.
