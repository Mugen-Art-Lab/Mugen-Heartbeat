
# Contributing

Contributions, bug reports and compatibility results are welcome.

## Code changes

Before opening a pull request:

1. keep changes focused;
2. build the Windows x64 target;
3. test the affected feature in OBS Studio;
4. include reproduction/test notes in the pull request;
5. update English/Russian user-facing text together when the change affects both
   languages.

Mugen Heartbeat is licensed under GPL-2.0-or-later. Contributions are expected
to be compatible with that license.

## Skin format

Community skins do not require rebuilding the plugin.

Use the user-skin folder opened from Mugen Heartbeat properties and follow:

- `SKIN-FORMAT.md`;
- `SKIN-FORMAT.ru.md`.

A user skin with the same `id` as a built-in skin overrides the built-in copy,
which is useful for local testing.

When extending the skin schema itself, update both English and Russian
`SKIN-FORMAT` documentation.

## Third-party code and assets

Do not add third-party code, fonts, artwork or other assets without preserving
their license and attribution requirements.

When a new bundled dependency is introduced, update `LICENSES/` and
`THIRD-PARTY-NOTICES.md`.
