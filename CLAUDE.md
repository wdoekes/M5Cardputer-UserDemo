# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This file is kept in a separate worktree (`../M5Cardputer-UserDemo-agent`) in the `x-agent` branch.
Actual coding work should be done in the `../M5Cardputer-UserDemo` directory.

## Project Overview

This is firmware for the M5Stack Cardputer (ESP32-S3 based handheld device with keyboard).
The project is a fork/variant called `cardputer-adv` that extends the official M5Cardputer-UserDemo firmware with additional features including a WiFi keyboard app (`app_wifikbd`) that broadcasts keypresses over UDP.

## Build System

This is an **ESP-IDF** project (not Arduino).
The `Makefile` wraps the build in a Docker container (`espressif/idf:v5.4.2`) so no local toolchain install is needed.

```bash
# Build -- produces ../M5Cardputer-UserDemo/build/cardputer-adv.bin
make agent-build

# After changing sdkconfig.defaults (or sdkconfig), do a full rebuild:
make clean && make agent-build
```

Don't analyze the Makefile. If the build system fails, ask the operator to solve it because claude gets no root/sudo powers.

### Flashing (not done by claude)

Flash with `esptool.py` directly -- do not use `idf.py flash` as it may
trigger an unwanted rebuild:

```bash
# Flash everything (bootloader, partition table, app):
esptool.py --port /dev/ttyACM0 write_flash @build/flash_args

# Flash app only (faster, safe for iterative development):
esptool.py --port /dev/ttyACM0 write_flash 0x10000 build/cardputer-adv.bin
```

App-only flash does not touch the NVS partition (0x9000-0xCFFF), so BLE bonds survive.
Use the full `@flash_args` form when the partition table or bootloader has changed.

### sdkconfig vs sdkconfig.defaults

`sdkconfig` (gitignored) is the canonical config and takes precedence over `sdkconfig.defaults`.
Adding a key to `sdkconfig.defaults` has no effect if `sdkconfig` already contains that key.
To apply a new `sdkconfig.defaults` entry to an existing working tree, either delete `sdkconfig` and rebuild, or edit the relevant line in `sdkconfig` directly.

Component dependencies are split between:
- **`components/`** -- cloned via `fetch_repos.py` using `repos.json` (M5GFX, M5Unified, mooncake, mooncake_log, smooth_ui_toolkit).
  Run `python fetch_repos.py` if this directory is missing.
- **`managed_components/`** -- downloaded by IDF Component Manager from `main/idf_component.yml` (esp-now, esp_tinyusb, radiolib)

## Architecture

### Core Loop (`main/main.cpp`)
The firmware follows a simple HAL + app-framework pattern:
1. `GetHAL().init()` -- initializes all hardware
2. Apps are installed into the Mooncake framework
3. Main loop calls `GetHAL().update()` then `GetMooncake().update()` every tick

### HAL Layer (`main/hal/`)
`Hal` is a singleton (via `GetHAL()`) that owns all hardware abstractions:
- **Display**: Three `LGFX_Sprite` canvases -- `canvas` (main content), `canvasSystemBar` (top-right status bar), `canvasKeyboardBar` (left sidebar with modifier key indicators)
- **Keyboard**: `Keyboard` class wraps a TCA8418 keyboard controller over I2C with interrupt on `GPIO 11`.
  Raw matrix events (7x8) are remapped to match original Cardputer layout, then converted to HID scan codes (`KeScanCode_t`).
  Modifier state (Shift, Ctrl, Alt, Meta) is tracked in `_modifier_mask` (maps directly to HID modifier byte).
  Fn key state is tracked separately in `_fn_state` because Fn is a firmware-only concept and never goes into the HID report.
- **WiFi, BLE, USB HID, IR, ESP-NOW, LoRa, IMU, SD Card** -- all managed as lazy-initialized subsystems in `hal.cpp`
- **Settings**: Persisted to NVS flash via `main/hal/utils/settings/`

Pin assignments are in `main/hal/hal_config.h`.

### Display Geometry

The physical display is 240x135.
The HAL splits this across three sprites that are composed each frame by `pushCanvas`/`pushCanvasSystemBar`/`pushCanvasKeyboardBar`:

- `canvasKeyboardBar` -- 36x135, drawn at display (0, 0). Modifier-key indicators on the left.
- `canvasSystemBar` -- 204x26, drawn at display (36, 0). Status bar across the top-right.
- `canvas` -- 204x109, drawn at display (36, 26). The main app content area.

Apps render into `canvas` in **canvas-local coordinates**: (0, 0) is the top-left of the visible app area, (203, 108) is the bottom-right.
The HAL handles the offset.
Do not subtract gutters from your coordinates -- writing at canvas (0, 0) is already past the surrounding bars.
Conversely, do not use coordinates beyond (203, 108) -- they are silently clipped by the sprite, not relocated.

Common pitfall: assuming the canvas is 240x135.
A 32-pixel-tall element placed at y=100 overflows the 109-tall canvas and only the top 9 rows render.
When laying out vertically, treat the canvas as 109 px tall (not 135) and the keyboard bar / system bar as already accounted for.

### App Framework (Mooncake)
Each app inherits from `mooncake::AppAbility` and overrides lifecycle methods:
- `onCreate()` / `onOpen()` / `onRunning()` / `onClose()`

Apps are registered in `main/main.cpp`. Some apps are commented out (GPS, LoRa Chat, SD Card, StringIR) -- uncomment in both `main/main.cpp` and `main/apps/apps.h` to enable.

### Keyboard Pipeline
`TCA8418` -> raw event (row/col/state) -> `Keyboard::remap()` (hardware layout correction) -> `Keyboard::update_modifier_mask()` (updates `_modifier_mask` and `_fn_state`) -> `Keyboard::convertToKeyEvent()` (applies Fn/shift layers, maps to `KeScanCode_t`) -> emitted via `onKeyEvent` signal -> consumed by apps or forwarded via BLE/USB HID

### Key Layout (`_key_value_map`)
`KeyValue_t` has three layers per cell: `first` (normal), `second` (shift), `fn` (Fn-held). The Fn layer is `nullptr`/`KEY_NONE` for unoverridden keys, which fall through to the normal lookup. Letter keys carry `fnExtraModifiers = KEY_MOD_LSHIFT` so Fn doubles as Shift for A-Z. The six special Fn keys are: `` ` ``->ESC, `del`->DEL (forward), `;`->UP, `,`->LEFT, `.`->DOWN, `/`->RIGHT.

Caps lock was removed; Aa button is now a plain Shift key. The `KeyboardBarState_t` struct in `app_launcher.h` tracks `shift` (not `caps_lock`) to update the sidebar indicator.

### WiFi Keyboard App (`main/apps/app_wifikbd/`)
Broadcasts keypress events as JSON over UDP to the LAN broadcast address on port 5005:
```json
{"code": 5, "btn": "b", "row": 3, "col": 7, "state": 1}
```
A companion Python daemon in `contrib/wifikbd_homeassistant_proxy.py` can receive these and relay them to Home Assistant.

## BLE Pairing

NimBLE stores bond/key data in NVS flash (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`).
Bonds survive power cycles and normal app-only reflashes.
The NVS partition (0x9000) is not touched by `write_flash @flash_args` or `write_flash 0x10000`.

If the Cardputer loses its bond (e.g. after `erase-flash` or a first flash of new firmware), the host will enter an authentication failure loop:
reconnecting every ~600 ms and immediately dropping (disconnect reason 517 = HCI Authentication Failure).
Fix on the host side:
```bash
bluetoothctl remove D8:85:AC:A5:9D:4A   # use the device's actual MAC
```

Then re-pair normally.
The device MAC is printed in the serial log at BLE init time: `I (x) BLE_INIT: Bluetooth MAC: xx:xx:xx:xx:xx:xx`

## Code Style

Formatted with clang-format (Google base style, 4-space indent, 120 column limit).
Run:
```bash
clang-format -i <file>
```

Use `// clang-format off` / `// clang-format on` around intentionally columnar structures (e.g. `_key_value_map`) to prevent the formatter from collapsing them.

**Do not use non-ASCII characters (codepoint > 0x7F) anywhere in source files** -- not in strings, comments, or identifiers.
Not even in CLAUDE.md.
The toolchain or terminal output may mishandle them silently.

If possible, keep line lengths short (about max 80 columns), but this is not a strict rule.
Look at the surrounding code and behave the same.
For markdown/docs, prefer one sentence per line.
This help when reviewing documentation changes.
