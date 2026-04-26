# Local Weather TTS Design With Piper

## Goal

Add an optional local text-to-speech path for weather commands so the ESP32-S3-BOX-3 can:

1. fetch the configured location's weather
2. display the weather on screen
3. request synthesized speech from a nearby machine running Piper
4. play the returned audio through the BOX-3 speaker

This design has been implemented for `weather today` and `weather tomorrow`.

## Why A Local TTS Service

The current firmware is a thin networked assistant:

- wake word detection and fixed command recognition happen locally
- network calls handle heavier work
- the BOX-3 is responsible for UI, microphone, speaker, and orchestration

That model still fits TTS well.

Using a local TTS service keeps the firmware smaller and avoids cloud speech dependencies for weather playback.

It also avoids embedding a speech engine directly into the ESP firmware, which would add:

- more RAM pressure
- more flash usage
- more codec/playback edge cases
- tighter coupling to one synthesis engine

## Local Engine

Target engine:

- `OHF-Voice/piper1-gpl`
  - repository: https://github.com/OHF-Voice/piper1-gpl
  - described upstream as a fast local neural text-to-speech engine
  - provides a web server mode and C/C++ API
  - licensed under GPL-3.0

Important licensing note:

This repo should not directly vendor Piper into the firmware tree.

Instead, the safer first design is:

- run Piper as a separate service on a local machine
- have the firmware talk to that service over a network protocol
- keep the BOX-3 firmware independent of Piper internals

That separation keeps this project at the service-boundary level rather than redistributing Piper code inside the firmware.

## Desired User Flow

The intended interaction for weather becomes:

1. user says `Hi ESP`
2. device wakes and listens for a command
3. user says `weather today`
4. device fetches weather from Open-Meteo
5. device shows the weather details on screen
6. device sends a natural-language weather sentence to the local TTS service
7. local TTS service synthesizes speech with Piper
8. device receives streamed PCM audio
9. device plays the speech through the BOX-3 speaker
10. device keeps the weather on screen while playback completes and targets about 30 seconds total visible time
11. device returns to standby

## Implemented Architecture

There are three moving pieces:

- BOX-3 firmware
  - weather fetch
  - weather sentence construction
  - generic `tts_player_speak()` request
  - audio playback and sample-rate adaptation
  - UI timing and standby transitions

- local TTS service
  - socket/event wrapper around Piper
  - accepts plain text
  - streams raw PCM response chunks

- Piper engine host
  - voice model storage
  - synthesis execution
  - CPU usage isolated from the ESP

## Firmware Responsibilities

The firmware changes should stay modular and small.

Implemented additions:

- `main/tts/`
  - `local_tts_client.c`
  - `local_tts_client.h`
  - `tts_player.c`
  - `tts_player.h`

Implemented board-audio additions:

- `board_audio_init_speaker()`
- `board_audio_begin_pcm()`
- `board_audio_write_pcm()`
- `board_audio_end_pcm()`
- `board_audio_play_pcm()`

Responsibilities:

- `weather_client`
  - fetch and parse forecast data

- `box3_assistant.c`
  - orchestrate weather fetch, screen display, TTS request, and standby timing

- `local_tts_client`
  - connect to the local TTS service
  - send Piper synthesis events
  - receive socket events and PCM chunks
  - expose synthesized audio stream or buffered audio plus format metadata

- `tts_player`
  - provide the generic assistant-facing `tts_player_speak()` API
  - stream PCM chunks to board audio
  - downsample higher-rate Piper PCM to the BOX-3 playback rate while the microphone path remains open

- `board_audio`
  - initialize the speaker codec
  - open playback stream
  - write PCM frames to the BOX-3 speaker

## Implemented Local TTS Service Contract

The firmware does not know Piper command-line details, but the current working service is not HTTP.

It uses a raw TCP socket protocol with newline-delimited JSON events and binary payloads.

### Request

- TCP connect to the host and port in `CONFIG_TTS_PIPER_BASE_URL`
- send a newline-terminated JSON event

Example:

```json
{
  "type": "synthesize",
  "data": {
    "text": "Today in Fargo, it is 39 degrees and cloudy. The high is 64 and the low is 27."
  }
}
```

The serialized request is followed by `\n`.

### Response

The service sends newline-delimited JSON events. Audio events can include a binary payload immediately after the event line.

Events handled by firmware:

- `audio-start`
- `audio-chunk`
- `audio-stop`

Important metadata:

- `rate`
- `width`
- `channels`
- `data_length`
- `payload_length`

`audio-chunk` payloads are 16-bit PCM bytes. The firmware streams these chunks through `tts_player` instead of accumulating the full utterance in RAM.

## Legacy HTTP/WAV Path

The lower-level `local_tts_client` still contains a buffered HTTP/WAV implementation behind config options. That path is useful for simple services, but it is not the active Piper setup.

The active setup uses:

- `CONFIG_TTS_PIPER_EVENT_SOCKET=y`
- `CONFIG_TTS_PIPER_BASE_URL="http://host:port"`

`CONFIG_TTS_PIPER_BASE_URL` is parsed for host and port in socket mode.

## Natural-Language Weather Text

Do not read the compact screen string aloud.

Build a separate spoken sentence such as:

`Today in Fargo, it is 39 degrees and cloudy. The high is 64 and the low is 27. Wind is 10 miles per hour with a 1 percent chance of rain.`

This should be generated locally by firmware code from the parsed `weather_report_t`.

Implemented helper:

- `weather_format_spoken()`

That keeps:

- UI text optimized for the screen
- spoken text optimized for audio

## Audio Playback Path

The BOX-3 already supports speaker output through the BSP audio codec layer.

High-level playback flow:

1. initialize speaker codec once at boot
2. open playback stream when TTS audio is ready
3. feed PCM frames to `esp_codec_dev_write`
4. close or reuse the stream after playback

Current playback behavior:

- mono
- 16-bit PCM
- output to BOX-3 speaker at 16000 Hz

Piper currently returns 22050 Hz PCM for the tested voice. The BOX-3 microphone/AFE path is open at 16000 Hz on the same I2S peer, so opening playback at 22050 Hz causes a sample-rate conflict.

Current workaround:

- keep the speaker stream at 16000 Hz
- downsample incoming Piper PCM in `tts_player`
- stream downsampled frames to `board_audio_write_pcm()`

Preferred future fix:

- make the Piper side return 16000 Hz PCM directly
- use a native 16 kHz Piper voice, or
- resample server-side before streaming to the ESP

## Assistant State Handling

The assistant currently blocks inside command execution.

Weather playback will lengthen that path, so the firmware should treat weather speech as an explicit command activity window.

During TTS fetch and playback:

- keep wake word disabled
- keep the weather UI visible
- pause microphone feed, just as other blocking command execution does
- avoid returning to standby until playback completes or fails

This prevents AFE ringbuffer overruns and avoids accidental re-triggering during playback.

## UI Behavior

While speaking weather:

- keep the detailed weather screen visible
- target about 30 seconds total visible time from the moment the forecast first appears
- include TTS playback time in that 30-second window
- if TTS takes longer than 30 seconds, keep the screen visible until playback completes

Failure cases:

- if TTS fetch fails, keep the weather on screen briefly and return to standby
- if playback fails, log the error and return to standby

The first version does not need playback progress UI.

## Configuration

Config for the local TTS service:

- `CONFIG_TTS_PIPER_ENABLED`
- `CONFIG_TTS_PIPER_BASE_URL`
- `CONFIG_TTS_PIPER_EVENT_SOCKET`
- `CONFIG_TTS_PIPER_TIMEOUT_MS`
- `CONFIG_TTS_PIPER_VOLUME_PERCENT`

Example:

```text
CONFIG_TTS_PIPER_BASE_URL="http://192.168.68.65:10200"
CONFIG_TTS_PIPER_EVENT_SOCKET=y
CONFIG_TTS_PIPER_TIMEOUT_MS=20000
CONFIG_TTS_PIPER_VOLUME_PERCENT=85
```

These follow the current local-config workflow and remain generic in tracked defaults.

## Failure Handling

The feature should fail safely.

### TTS service unavailable

- log the socket or HTTP error
- skip spoken playback
- leave the weather display visible for its hold period
- return to standby

### Invalid audio response

- reject malformed events, malformed WAV, or unsupported format
- log parse details
- skip playback
- return to standby cleanly

### Speaker initialization failure

- allow weather display to continue without audio
- keep the assistant usable for non-audio commands

## Licensing Considerations

Because Piper is GPL-3.0 licensed, the lowest-risk integration path for this repo is:

- no bundled Piper source here
- no static or direct linking into firmware
- use Piper as an external local service

If direct embedding is ever considered later, licensing implications need separate review before implementation.

## Suggested Local Service Shape

A small wrapper on the nearby machine should:

1. accept text over the socket-event protocol
2. call Piper with a configured voice model
3. stream PCM chunks with audio metadata

This wrapper can be:

- Python
- Node
- Go
- another small local service runtime

The firmware should not care which runtime hosts Piper.

## Implemented Scope

Included:

- local TTS socket-event client in firmware
- weather spoken sentence builder
- speaker codec initialization
- streaming PCM playback through BOX-3 speaker
- generic `tts_player_speak()` abstraction for future non-weather speech
- device-side downsampling to avoid the shared I2S 16 kHz microphone conflict
- graceful fallback to screen-only weather if TTS fails

Deferred:

- generic assistant speech beyond weather
- user-selectable voices
- caching common spoken phrases
- synchronized lip-sync or progress UI
- full duplex audio with playback reference into AEC
- server-side 16 kHz normalization

## Build Order Used

1. extend `board_audio` with speaker initialization and PCM playback helpers
2. add `local_tts_client` with Piper socket-event support
3. add spoken weather sentence formatting
4. wire weather command flow to fetch TTS after screen update
5. keep the screen visible during playback
6. verify fallback behavior when TTS is unavailable
7. verify end-to-end on device with a local Piper service
8. extract generic `tts_player` so future commands can speak text without knowing Piper details

## Open Questions

- Which voice and sample rate should be standardized?
- Should the TTS server normalize all output to 16 kHz before sending it to the ESP?
- Should weather playback be interruptible by touch or wake word?
- Should the firmware cache the last successful spoken weather audio for quick replay?
- Should spoken weather be optional per command, or always enabled when local TTS is configured?
