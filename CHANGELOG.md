# Changelog

## 0.1.37 — 2026-08-11

Initial public release.

- Native local BLE heart-rate source for OBS Studio on Windows.
- Xiaomi Smart Band 10 verified on real hardware.
- Automatic reconnect and persistent device identity.
- Plain text mode and two built-in skins: Bio Monitor and Heartbeat Lite.
- Up to three configurable BPM reactions per source.
- English and Russian localization.
- User/community skin format with bilingual documentation.
- Windows installer and manual ZIP package.

## 0.1.38

- Package both modern and legacy Windows plugin DLL layouts for OBS Studio 33 and earlier supported OBS versions.
- Add OBS plugin-manager metadata with the product name, version, description, and official links.
- Add an About section to the source properties with the Mugen Heartbeat GitHub page and Mugen Art Lab website.
- Show the official Mugen Art Lab website and feedback link in the Windows installer.
- Move BLE device connection controls to the top of source settings; make each reaction editor collapsible without changing its enabled state.
- Hide the manual BPM slider outside Test Mode.
- Respect the auto-connect checkbox for Windows GATT session maintenance; bound manual connect attempts and GATT service-discovery retries.
- Replace separate Connect / Disconnect buttons with a contextual Connect / Cancel / Disconnect control, and identify another Heartbeat source when it enables shared BLE auto-connect.
- Hide initial BLE search and Xiaomi pairing instructions once the selected heart-rate device is connected; restore them when disconnected.
- Make BLE pairing guidance contextual to device selection and a pending connection, including live updates in the OBS properties dialog.
- Add a third Windows release asset: an OBS 32/33+ Portable ZIP with both loader layouts, full skins/resources, and automated checksum/manifest verification.

## Unreleased
