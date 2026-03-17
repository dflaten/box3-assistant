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

`sdkconfig` and `sdkconfig.defaults.local` are ignored by git.

If you want to build with a local defaults override file, run:

```fish
cd /home/david/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
idf.py build
```

For flashing with the same local override:

```fish
cd /home/david/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
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

When building or flashing from a fresh shell, include the local defaults file:

```fish
cd /home/david/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
idf.py build
```

Or to flash and monitor:

```fish
cd /home/david/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
idf.py -p /dev/ttyACM0 flash monitor
```

The `set -x SDKCONFIG_DEFAULTS ...` command only applies to the current shell session. The credentials stored in `sdkconfig.defaults.local` remain on disk, but you need to set the environment variable again in each new terminal.
