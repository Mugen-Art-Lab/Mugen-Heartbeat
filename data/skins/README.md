# Mugen Heartbeat — User Skins

This folder is for **user/community skins**.

Built-in skins such as **Bio Monitor** and **Heartbeat Lite** are bundled with
Mugen Heartbeat and are stored separately with the plugin. They are intentionally
not copied here.

## Installing a user skin

Place each skin in its own folder:

```text
skins/
└─ my-skin/
   └─ skin.json
```

Then open the Mugen Heartbeat source properties in OBS and click
**Refresh skins**.

A user skin with the same `id` as a built-in skin overrides the built-in skin.
This makes it possible to test modified versions locally without changing the
installed plugin files.

## Creating a skin

See:

- `SKIN-FORMAT.md` — English skin format reference
- `SKIN-FORMAT.ru.md` — Russian skin format reference

Russian version of this README: `README.ru.md`.

These documentation files are maintained by Mugen Heartbeat and may be refreshed
when the plugin updates. Your own skin folders are not modified.
