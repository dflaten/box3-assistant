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
make config-local
make build
```

`make config-local` regenerates `sdkconfig` from `sdkconfig.defaults` and `sdkconfig.defaults.local`.

## Wi-Fi And Hue

Required local values:

```text
CONFIG_HUE_WIFI_SSID="your-ssid"
CONFIG_HUE_WIFI_PASSWORD="your-password"
CONFIG_HUE_BRIDGE_API_KEY="your-hue-api-key"
```

The firmware discovers and caches the Hue bridge address automatically, so a Hue bridge IP is not required for normal setup.

## Home Location

The assistant home location is configurable through `menuconfig` or `sdkconfig.defaults.local`. Weather and the default time command both use this shared home value, so changing home only requires updating one location block. The tracked defaults use New York City.

Example local override:

```text
CONFIG_ASSISTANT_HOME_LOCATION_NAME="Your City, ST"
CONFIG_ASSISTANT_HOME_LATITUDE="00.0000"
CONFIG_ASSISTANT_HOME_LONGITUDE="00.0000"
CONFIG_ASSISTANT_HOME_TIMEZONE="America/Chicago"
```

Other weather settings, including `CONFIG_WEATHER_BASE_URL` and `CONFIG_WEATHER_TIMEOUT_MS`, are available through `menuconfig`. Remote time lookup has separate provider settings, including `CONFIG_TIME_LOOKUP_BASE_URL`, `CONFIG_TIME_GEOCODING_BASE_URL`, and `CONFIG_TIME_LOOKUP_TIMEOUT_MS`, so the time feature can keep using its own provider if weather moves away from Open-Meteo.

## Local TTS

Local speech output is optional. The current implementation targets a Piper service running on the LAN and is used for spoken weather and time responses.

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

Dynamic timers and remote time lookups use a local Wyoming speech-to-text service on the LAN. The current implementation is designed around `wyoming-faster-whisper`.

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

## Fireworks Voice Routing

Fireworks voice routing is optional and disabled by default. When enabled, every utterance after the wake word is captured, transcribed by the local STT service, and sent to a model on Fireworks AI. The model either selects a local action (Hue, timer, weather, volume, or the local clock) or answers a general question.

Recommended local defaults:

```text
CONFIG_INTEGRATION_FIREWORKS_ENABLED=y
CONFIG_FIREWORKS_API_KEY="your-fireworks-api-key"
CONFIG_FIREWORKS_BASE_URL="https://api.fireworks.ai/inference/v1"
CONFIG_FIREWORKS_MODEL="accounts/fireworks/models/your-model"
CONFIG_FIREWORKS_TIMEOUT_MS=15000
CONFIG_VOICE_ROUTING_CAPTURE_MS=6000
CONFIG_VOICE_ROUTING_SESSION_TIMEOUT_MS=60000
```

Notes:

- routing requires both the local STT service and the Fireworks API to be reachable
- the Fireworks API key is a secret and should stay in the untracked local defaults file
- `CONFIG_FIREWORKS_BASE_URL` should be the inference API root, `https://api.fireworks.ai/inference/v1`; the firmware appends `/chat/completions` and normalizes a pasted `/completions` endpoint
- `CONFIG_FIREWORKS_MODEL` must name a Fireworks model that supports tool calling
- recorded audio stays on the LAN; only the transcript and the returned text are sent to Fireworks
- when routing is disabled, the firmware keeps the local MultiNet command path
- the integration flags `CONFIG_INTEGRATION_HUE_ENABLED`, `CONFIG_INTEGRATION_WEATHER_ENABLED`, and `CONFIG_INTEGRATION_TIMER_ENABLED` control which actions are offered to the model
- the model has no clock of its own, so clock questions resolve through a `time_now` tool that runs the same local time handler as the MultiNet path

### Routed Capture Latency

The routed path pays a fixed capture window plus STT and a model round trip before a command runs. Two options shorten the common case.

Early capture stop ends the capture once speech is followed by a quiet gap instead of always waiting for the full window:

```text
CONFIG_VOICE_ROUTING_EARLY_STOP_ENABLED=y
CONFIG_VOICE_ROUTING_MIN_CAPTURE_MS=1000
CONFIG_VOICE_ROUTING_SILENCE_MS=1200
CONFIG_VOICE_ROUTING_SILENCE_RMS_THRESHOLD=600
```

- `CONFIG_VOICE_ROUTING_SILENCE_RMS_THRESHOLD` is a raw PCM16 root-mean-square amplitude and depends on microphone gain; raise it if background noise counts as speech, lower it if speech is missed
- `CONFIG_VOICE_ROUTING_CAPTURE_MS` remains the hard upper bound even when early stop is enabled

The local fast path runs MultiNet on the same captured audio, so a recognized phrase such as `turn off living room` or `what time is it` executes locally in about a second while free-form questions still fall through to STT and the model:

```text
CONFIG_VOICE_ROUTING_FASTPATH_ENABLED=y
CONFIG_VOICE_ROUTING_FASTPATH_WINDOW_MS=3000
CONFIG_VOICE_ROUTING_FASTPATH_MIN_CONFIDENCE=90
```

- `CONFIG_VOICE_ROUTING_FASTPATH_MIN_CONFIDENCE` is a percent and gates the accepted local match to reduce false positives
- the fast path feeds the recognizer raw microphone audio rather than the processed audio front end, so validate its accuracy on device and lower `CONFIG_VOICE_ROUTING_FASTPATH_WINDOW_MS` or disable the fast path if it misfires
- capture always continues to the STT path when no confident local match is found, so no audio is lost

### Monthly Spend Indicator

The firmware can show the current month's rated spend for one Fireworks API key in the bottom-right corner of the screen. It refreshes about once an hour.

```text
CONFIG_FIREWORKS_COST_ENABLED=y
CONFIG_FIREWORKS_ACCOUNT_ID="your-account-id"
CONFIG_FIREWORKS_COST_KEY_ID="key_xxxxxxxxxxx"
CONFIG_FIREWORKS_COST_REFRESH_SECONDS=3600
```

Notes:

- the cost endpoint (`POST /v1/accounts/{account_id}/usageCosts:query`) authenticates with the same API key used for inference
- the endpoint filters by the stable API key ID, not the human-readable key name; use the Fireworks billing API to find both the account ID and the key ID
- the value shown is the rated subtotal for the configured key over the current UTC month
- Fireworks aggregates costs daily and can lag live traffic, so an hourly refresh may not change every hour
