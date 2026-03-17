# box3-assistant

`box3-assistant` is an ESP32-S3-BOX-3 firmware project for a networked voice assistant terminal.

The device is planned to be a single firmware image that boots directly on the BOX-3 and acts as a smart front end for home and media integrations. The current direction is:

- local microphone capture and speech command recognition on the BOX-3
- Philips Hue control over the local network
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

Planned future work includes broader assistant features, richer UI, ChatGPT-backed interactions, and Jellyfin/media support.

## Secrets

Do not commit real Wi-Fi passwords, Hue API keys, or other credentials into tracked files.

Recommended options:

- use `idf.py menuconfig` and keep secrets in `sdkconfig` only
- or create a local `sdkconfig.defaults.local` file based on [sdkconfig.defaults.local.example](/home/david/projects/esp-projects/box3-assistant/sdkconfig.defaults.local.example)

Both `sdkconfig` and `sdkconfig.defaults.local` are ignored by git.

Important distinction:

- `sdkconfig.defaults.local` is a local input file that seeds config values
- `sdkconfig` is the generated effective config that ESP-IDF actually builds with

That means your Wi-Fi password can exist in `sdkconfig` locally even if you only typed it into `sdkconfig.defaults.local`. That is expected. The important part is that neither file is committed.

If `sdkconfig` already contains stale values, ESP-IDF will keep using them until you regenerate it. In that case, remove `sdkconfig` and `sdkconfig.old`, then reconfigure with your local defaults file enabled.

If you want to build with a local defaults override file in `fish`, run:

```fish
cd /home/david/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py build
```

In `bash`, use `export` instead:

```bash
cd /home/david/projects/esp-projects/box3-assistant
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py build
```

For flashing with the same local override in `fish`:

```fish
cd /home/david/projects/esp-projects/box3-assistant
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
cd /home/david/projects/esp-projects/box3-assistant
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Then edit `sdkconfig.defaults.local` and set:

```text
CONFIG_HUE_WIFI_SSID="your-ssid"
CONFIG_HUE_WIFI_PASSWORD="your-password"
```

When building or flashing from a fresh shell, include the local defaults file and regenerate `sdkconfig` if needed:

```fish
cd /home/david/projects/esp-projects/box3-assistant
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
cd /home/david/projects/esp-projects/box3-assistant
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

The current assistant flow is:

1. wait in standby for the wake word
2. wake up and listen for one command
3. execute the command
4. return to standby

Current wake word:

- `Hi ESP`

Current supported voice commands:

- `turn on living room`
- `turn off living room`

Note: the firmware uses Espressif's built-in `Hi, ESP` WakeNet model (`wn9s_hiesp`) for this wake-word flow. Say `Hi ESP` when testing the current firmware. The command set now uses Espressif MultiNet5 English phoneme definitions generated for the living-room phrases.
