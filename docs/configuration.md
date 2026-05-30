# Configuration

Use `menuconfig` for one-off local settings, or use an untracked `sdkconfig.defaults.local` file for repeatable machine-specific settings and secrets.

Both `sdkconfig` and `sdkconfig.defaults.local` are ignored by git.

## Local Defaults

Create a local defaults file:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Edit `sdkconfig.defaults.local` with local values.

To regenerate `sdkconfig` from both defaults files:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"
rm -f sdkconfig sdkconfig.old
idf.py reconfigure
idf.py build
```

The `set -x SDKCONFIG_DEFAULTS ...` command only applies to the current shell session. Set it again in each new terminal when regenerating `sdkconfig`.

## Wi-Fi And Hue

Required local values:

```text
CONFIG_HUE_WIFI_SSID="your-ssid"
CONFIG_HUE_WIFI_PASSWORD="your-password"
CONFIG_HUE_BRIDGE_API_KEY="your-hue-api-key"
```

The firmware discovers and caches the Hue bridge address automatically, so a Hue bridge IP is not required for normal setup.

## Weather

The weather command target is configurable through `menuconfig` or `sdkconfig.defaults.local`. The tracked defaults use Open-Meteo for New York City.

Example local override:

```text
CONFIG_ASSISTANT_LOCATION_NAME="Your City, ST"
CONFIG_WEATHER_LATITUDE="00.0000"
CONFIG_WEATHER_LONGITUDE="00.0000"
CONFIG_WEATHER_TIMEZONE="America/Chicago"
```

Other weather settings, including `CONFIG_WEATHER_BASE_URL` and `CONFIG_WEATHER_TIMEOUT_MS`, are available through `menuconfig`.

## Local TTS

Local speech output is optional. The current implementation targets a Piper service running on the LAN and is used for spoken weather responses.

The working Piper integration uses a raw TCP socket event protocol. The firmware sends one newline-terminated JSON request and reads newline-delimited events such as `audio-start`, `audio-chunk`, and `audio-stop`.

Recommended local defaults:

```text
CONFIG_TTS_PIPER_BASE_URL="http://tts-server.local:10200"
CONFIG_TTS_PIPER_EVENT_SOCKET=y
CONFIG_TTS_PIPER_TIMEOUT_MS=20000
CONFIG_TTS_PIPER_VOLUME_PERCENT=85
```

`CONFIG_TTS_PIPER_BASE_URL` is used for host and port parsing in socket mode. The scheme is ignored by the socket client, but keeping `http://host:port` also works with the legacy HTTP path.

Volume is controlled by `CONFIG_TTS_PIPER_VOLUME_PERCENT`.

## Local STT

Dynamic timers use a local Wyoming speech-to-text service on the LAN. The current implementation is designed around `wyoming-faster-whisper`.

Recommended local defaults:

```text
CONFIG_LOCAL_STT_ENABLED=y
CONFIG_LOCAL_STT_BASE_URL="stt-server.local:10300"
CONFIG_LOCAL_STT_TIMEOUT_MS=30000
CONFIG_LOCAL_STT_CAPTURE_MS=3000
CONFIG_TIMER_MAX_DURATION_SECONDS=86400
```

Notes:

- `CONFIG_LOCAL_STT_BASE_URL` should not include `http://`
- the current STT path expects 16 kHz, 16-bit, mono PCM follow-up audio
- the first request may be slower if the server is still loading the model
- timer parsing currently focuses on minute and second phrases
