# box3-assistant

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-ESP32--S3--BOX--3-0f766e" />
  <img alt="Framework" src="https://img.shields.io/badge/framework-ESP--IDF-1d4ed8" />
  <img alt="Language" src="https://img.shields.io/badge/language-C-334155" />
  <img alt="Voice" src="https://img.shields.io/badge/voice-Hi%20ESP-f59e0b" />
  <img alt="Integrations" src="https://img.shields.io/badge/integrations-Hue%20%2B%20Weather-7c3aed" />
  <img alt="Tests" src="https://img.shields.io/badge/tests-host%20unit-16a34a" />
</p>

<img src="docs/images/box3.jpg" alt="ESP32-S3-BOX-3 device" width="220" align="right" />

`box3-assistant` is an ESP32-S3-BOX-3 firmware project for a networked voice assistant terminal.

This firmware image boots directly on the BOX-3 and acts as a smart front end for home and media integrations.

## Table Of Contents

- [Overview](#overview)
- [Status](#status)
- [Architecture](#architecture)
- [Design Docs](#design-docs)
- [Wake Word And Commands](#wake-word-and-commands)
- [Secrets](#secrets)
- [Wi-Fi Credentials](#wi-fi-credentials)
- [Weather Configuration](#weather-configuration)
- [Tests](#tests)

## Overview

| Icon | Item | Details |
| --- | --- | --- |
| 🧠 | Device | ESP32-S3-BOX-3 |
| 🛠 | Firmware stack | ESP-IDF |
| 🎙 | Wake word | `Hi ESP` |
| 💡 | Integrations | Philips Hue and Open-Meteo weather |
| 🖥 | Output | On-device status and weather screens |
| 🧪 | Validation | Host-side unit tests and firmware builds |

## Status

The project currently includes:

- ✅ ESP-IDF based firmware for the ESP32-S3-BOX-3
- ✅ Speech model loading and local command detection
- ✅ Wi-Fi configuration hooks
- ✅ Hue bridge control path
- ✅ Open-Meteo weather commands for a configurable location
- ✅ On-device weather display with multiline forecast details
- ✅ Persisted assistant diagnostics for timeout and reboot debugging
- ✅ Host-side unit tests for assistant state, command labeling, and weather formatting

Planned future work includes local spoken weather playback, broader assistant features, richer UI, ChatGPT-backed interactions, and Jellyfin/media support.

## Architecture

The firmware is currently organized around a small set of runtime-oriented modules:

| Module | Responsibility |
| --- | --- |
| `main/box3_assistant.c` | Boot flow, task startup, and top-level assistant orchestration |
| `main/assistant_runtime.h` | Shared in-memory `assistant_runtime_t` state passed through assistant helpers and tasks |
| `main/hue/hue_command_runtime.c` | Load stored Hue groups, sync groups from the bridge, and rebuild the runtime speech command table |
| `main/assistant_command_text.c` | Format user-facing labels for built-in and Hue-backed commands |
| `main/assistant_state.c` | Pure assistant state and timeout decision helpers |
| `main/assistant_diagnostics.c` | Persist lightweight reboot and command breadcrumbs for post-restart debugging |

## Design Docs

Current design notes in `docs/`:

- 📄 Ask GPT Design
- 📄 Jellyfin Option 1 Design
- 📄 Local Weather TTS Design With Piper

## Wake Word And Commands

The expected boot flow after flashing is:

1. boot the firmware
2. connect to Wi-Fi
3. load the speech models
4. automatically refresh Hue groups once from the bridge
5. fall back to the last saved group list if the Hue refresh fails
6. enter standby and wait for the wake word

The current assistant interaction flow is:

1. wait in standby for the wake word
2. wake up and listen for one command
3. execute the command
4. show the result on screen
5. return to standby

If a weather or Hue HTTP request stalls during execution, the firmware now attempts to cancel the active request first. If that recovery does not finish within a short grace window, the device falls back to a restart.

On the next boot, the firmware logs the previous command diagnostics and briefly shows a short `Prev ...` message on screen when the prior run ended in a notable timeout or reboot during command handling.

### Quick Reference

| Icon | Area | Current behavior |
| --- | --- | --- |
| 🎙 | Wake word | `Hi ESP` |
| 🔄 | Built-in commands | `update groups from hue`, `weather today`, `weather tomorrow` |
| 💡 | Dynamic commands | `turn on <group>`, `turn off <group>` |
| ⚠ | Timeout handling | Cancel active HTTP work first, then restart only if recovery stalls |
| 🧾 | Weather failure text | `Weather network error`, `Weather timeout`, `Weather unavailable` |

Current wake word:

- `Hi ESP`

Current always-available voice commands:

- `update groups from hue`
- `weather today`
- `weather tomorrow`

On boot, the firmware now automatically attempts a Hue group refresh after Wi-Fi connects so a newly flashed device can rebuild its group command list without requiring a manual sync. The `update groups from hue` command is still available to force a refresh later.

After a successful sync, the firmware fetches the Hue groups list from the bridge, normalizes the spoken names, saves the accepted groups to the `storage` partition, and rebuilds the active MultiNet command table.

After a successful sync, the firmware supports commands like:

- `turn on living room`
- `turn off living room`
- `turn on kitchen`
- `turn off office`

Saying `weather today` or `weather tomorrow` causes the firmware to:

1. fetch the requested forecast for the configured location from Open-Meteo over HTTPS
2. display a multiline weather summary on the BOX-3 screen
3. hold that weather screen for 15 seconds
4. return to standby

Transient connection failures are retried automatically. Weather network failures show `Weather network error`, and execution-time cancellation recovery shows `Weather timeout`.

The current weather display format is:

- `Now in <location>` for `weather today`, or `Tomorrow in <location>` for `weather tomorrow`
- current condition and current temperature for `weather today`
- daily high and low
- wind speed for `weather today`
- precipitation chance

The weather result screen does not show the generic `COMMAND COMPLETED` banner. It displays only the weather details for the requested day.

Current limits:

- only the first 6 usable Hue groups are added as direct voice commands
- group names are normalized into simple spoken forms before they become commands
- synced groups persist across power cycles, but you may need to resync after reflashing if the `storage` partition is erased or rewritten
- weather location is configurable through local sdkconfig values
- weather playback is screen-only for now; spoken playback is planned separately

## Secrets

To avoid committing real Wi-Fi passwords, Hue API keys, or other credentials these are stored in the config.

Recommended options:

- use `idf.py menuconfig` and keep secrets in `sdkconfig` only
- or create a local `sdkconfig.defaults.local` file based on sdkconfig.defaults.local.example

Both `sdkconfig` and `sdkconfig.defaults.local` are ignored by git.

| Icon | File | Purpose |
| --- | --- | --- |
| 🔒 | `sdkconfig.defaults.local` | Untracked local seed values for secrets and overrides |
| ⚙ | `sdkconfig` | Generated effective build configuration |

Important distinction:

- `sdkconfig.defaults.local` is a local input file that seeds config values
- `sdkconfig` is the generated effective config that ESP-IDF actually builds with

That means your Wi-Fi password can exist in `sdkconfig` locally even if you only typed it into `sdkconfig.defaults.local`. That is expected. The important part is that neither file is committed.

If `sdkconfig` already contains stale values, ESP-IDF will keep using them until you regenerate it. In that case, remove `sdkconfig` and `sdkconfig.old`, then reconfigure with your local defaults file enabled.

If you want to build with a local defaults override file in `fish`, run:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py build
```

In `bash`, use `export` instead:

```bash
cd /home/<user-name>/projects/esp-projects/box3-assistant
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py build
```

For flashing with the same local override in `fish`:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py -p /dev/ttyACM0 flash monitor
```

## Wi-Fi Credentials

You can set Wi-Fi credentials in either of these ways:

1. `idf.py menuconfig`
2. a local `sdkconfig.defaults.local` file

| Option | Best for | Notes |
| --- | --- | --- |
| `idf.py menuconfig` | One-off local configuration | Writes values into generated `sdkconfig` |
| `sdkconfig.defaults.local` | Repeatable local builds | Best for machine-specific secrets and overrides |

For the local file workflow:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Then edit `sdkconfig.defaults.local` and set:

```text
CONFIG_HUE_WIFI_SSID="your-ssid"
CONFIG_HUE_WIFI_PASSWORD="your-password"
```

When building or flashing from a fresh shell, include the local defaults file and regenerate `sdkconfig` if needed:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
grep CONFIG_HUE_WIFI_SSID sdkconfig
grep CONFIG_HUE_WIFI_PASSWORD sdkconfig
idf.py build
```

Or to flash and monitor:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
grep CONFIG_HUE_WIFI_SSID sdkconfig
grep CONFIG_HUE_WIFI_PASSWORD sdkconfig
idf.py -p /dev/ttyACM0 flash monitor
```

The `set -x SDKCONFIG_DEFAULTS ...` command only applies to the current shell session. The credentials stored in `sdkconfig.defaults.local` remain on disk, but you need to set the environment variable again in each new terminal. If `grep` still shows empty strings, the build is not using your local defaults file yet.


## Weather Configuration

The weather command target is configurable through `menuconfig` or your local `sdkconfig.defaults.local` file.

Config values available through `menuconfig`:

- `CONFIG_WEATHER_BASE_URL`
- `CONFIG_WEATHER_LOCATION_NAME`
- `CONFIG_WEATHER_LATITUDE`
- `CONFIG_WEATHER_LONGITUDE`
- `CONFIG_WEATHER_TIMEZONE`
- `CONFIG_WEATHER_TIMEOUT_MS`

### Tracked Project Defaults

| Setting | Default |
| --- | --- |
| Base URL | `https://api.open-meteo.com` |
| Location name | `New York City, NY` |
| Latitude | `40.7128` |
| Longitude | `-74.0060` |
| Timezone | `America/New_York` |
| Timeout | `8000` ms |

For the untracked local-file workflow, add weather settings to `sdkconfig.defaults.local` alongside your Wi-Fi values:

```text
CONFIG_WEATHER_LOCATION_NAME="New York City, NY"
CONFIG_WEATHER_LATITUDE="40.7128"
CONFIG_WEATHER_LONGITUDE="-74.0060"
CONFIG_WEATHER_TIMEZONE="America/New_York"
```

For this repo, the intended setup is:

- tracked project default: `New York City, NY`
- local machine override example: edit your untracked `sdkconfig.defaults.local` to whatever location you actually want, such as `Fargo, ND`

The firmware also enables the ESP certificate bundle in tracked defaults so HTTPS weather requests can validate the remote certificate.

Note: the firmware uses Espressif's built-in `Hi, ESP` WakeNet model (`wn9s_hiesp`) for this wake-word flow. Say `Hi ESP` when testing the current firmware.

## Tests

Host-side unit tests cover assistant state-machine decisions, command-label formatting, and weather formatting regressions, including the recovery logic for:

- listening timeout recovery
- repeated missing AFE fetch recovery
- empty MultiNet result recovery
- assistant session timeout recovery
- built-in and Hue command label formatting

The host test target does not currently exercise the ESP-IDF HTTP stack, persisted diagnostics module, or request-cancellation path.

Run them with:

```bash
./tests/run_unit_tests.sh
```

Or from the build tree:

```bash
cmake --build build --target unit-tests
```
