# box3-assistant

`box3-assistant` is an ESP32-S3-BOX-3 firmware project for a networked voice assistant terminal.

The device is planned to be a single firmware image that boots directly on the BOX-3 and acts as a smart front end for home and media integrations. The current direction is:

- local microphone capture and speech command recognition on the BOX-3
- Philips Hue control over the local network
- configurable location weather lookup over the network via Open-Meteo
- future ChatGPT integration for cloud-backed conversational features
- future Jellyfin integration for media control and possible playback features

The intended architecture is a thin but capable assistant device:

- low-latency audio capture, command spotting, and board control happen locally
- cloud or LAN services handle heavier tasks such as LLM responses, home APIs, and media services
- the BOX-3 provides the microphone, speaker, touchscreen, and embedded control surface

This keeps the project realistic for ESP32-S3 hardware while still allowing one device to coordinate voice commands, smart-home actions, AI requests, and media integrations.

## Status

The project currently includes:

- ESP-IDF based firmware for the ESP32-S3-BOX-3
- BOX-3 board support integration
- speech model loading and local command detection
- Wi-Fi configuration hooks
- Hue bridge control path
- Open-Meteo weather commands for a configurable location
- on-device weather display with multiline forecast details
- host-side unit tests for assistant state and weather formatting

Planned future work includes local spoken weather playback, broader assistant features, richer UI, ChatGPT-backed interactions, and Jellyfin/media support.

## Design Docs

Current design notes in `docs/`:

- Ask GPT Design
- Jellyfin Option 1 Design
- Local Weather TTS Design With Piper

## Secrets

Do not commit real Wi-Fi passwords, Hue API keys, or other credentials into tracked files.

Recommended options:

- use `idf.py menuconfig` and keep secrets in `sdkconfig` only
- or create a local `sdkconfig.defaults.local` file based on sdkconfig.defaults.local.example

Both `sdkconfig` and `sdkconfig.defaults.local` are ignored by git.

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

## Weather Configuration

The weather command target is configurable through `menuconfig` or your local `sdkconfig.defaults.local` file.

Config values available through `menuconfig`:

- `CONFIG_WEATHER_BASE_URL`
- `CONFIG_WEATHER_LOCATION_NAME`
- `CONFIG_WEATHER_LATITUDE`
- `CONFIG_WEATHER_LONGITUDE`
- `CONFIG_WEATHER_TIMEZONE`
- `CONFIG_WEATHER_TIMEOUT_MS`

Defaults:

- base URL: `https://api.open-meteo.com`
- location name: `New York City, NY`
- latitude: `40.7128`
- longitude: `-74.0060`
- timezone: `America/New_York`
- timeout: `8000` ms

For the untracked local-file workflow, add weather settings to `sdkconfig.defaults.local` alongside your Wi-Fi values:

```text
CONFIG_WEATHER_LOCATION_NAME="New York City, NY"
CONFIG_WEATHER_LATITUDE="40.7128"
CONFIG_WEATHER_LONGITUDE="-74.0060"
CONFIG_WEATHER_TIMEZONE="America/New_York"
```

The firmware also enables the ESP certificate bundle in tracked defaults so HTTPS weather requests can validate the remote certificate.

Note: the firmware uses Espressif's built-in `Hi, ESP` WakeNet model (`wn9s_hiesp`) for this wake-word flow. Say `Hi ESP` when testing the current firmware.

## Tests

Host-side unit tests cover assistant state-machine decisions and weather formatting regressions, including the hang fixes for:

- listening timeout recovery
- repeated missing AFE fetch recovery
- empty MultiNet result recovery
- awake-session watchdog recovery

Run them with:

```bash
./tests/run_unit_tests.sh
```

Or from the ESP-IDF build tree:

```bash
cmake --build build --target unit-tests
```
