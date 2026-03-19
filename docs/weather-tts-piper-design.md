# Local Weather TTS Design With Piper

## Goal

Add an optional local text-to-speech path for the existing `weather today` command so the ESP32-S3-BOX-3 can:

1. fetch Fargo weather
2. display the weather on screen
3. request synthesized speech from a nearby machine running Piper
4. play the returned audio through the BOX-3 speaker

This design does not implement voice playback yet. It defines the intended architecture and build order.

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

## Proposed Local Engine

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
- have the firmware talk to that service over HTTP
- keep the BOX-3 firmware independent of Piper internals

That separation keeps this project at the service-boundary level rather than redistributing Piper code inside the firmware.

## Desired User Flow

The intended interaction for weather becomes:

1. user says `Hi ESP`
2. device wakes and listens for a command
3. user says `weather today`
4. device fetches Fargo weather from Open-Meteo
5. device shows the weather details on screen
6. device sends a natural-language weather sentence to the local TTS service
7. local TTS service synthesizes speech with Piper
8. device receives PCM or WAV audio
9. device plays the speech through the BOX-3 speaker
10. device keeps the weather on screen while playback completes
11. device returns to standby

## Proposed Architecture

There are three moving pieces:

- BOX-3 firmware
  - weather fetch
  - weather sentence construction
  - local TTS request
  - audio playback
  - UI timing and standby transitions

- local TTS service
  - HTTP wrapper around Piper
  - accepts plain text
  - synthesizes a WAV or raw PCM response

- Piper engine host
  - voice model storage
  - synthesis execution
  - CPU usage isolated from the ESP

## Firmware Responsibilities

The firmware changes should stay modular and small.

Suggested additions:

- `main/tts/`
  - `local_tts_client.c`
  - `local_tts_client.h`

Potential board-audio additions:

- extend `main/board/board_audio.c`
- extend `main/board/board_audio.h`

Responsibilities:

- `weather_client`
  - fetch and parse forecast data

- `box3_assistant.c`
  - orchestrate weather fetch, screen display, TTS request, and playback

- `local_tts_client`
  - POST text to the local TTS service
  - receive WAV or PCM response
  - expose synthesized audio buffer and format metadata

- `board_audio`
  - initialize the speaker codec
  - open playback stream
  - write PCM frames to the BOX-3 speaker

## Local TTS Service Contract

The firmware should not know Piper-specific command-line details.

Use a small HTTP service contract instead.

### Request

- `POST /synthesize`
- content type: `application/json`

Example:

```json
{
  "text": "Today in Fargo, it is 39 degrees and cloudy. The high is 64 and the low is 27. Wind is 10 miles per hour with a 1 percent chance of rain.",
  "voice": "en_US",
  "format": "wav"
}
```

Required field:

- `text`

Optional fields:

- `voice`
- `format`
- `sample_rate`

### Response

Recommended first response type:

- `audio/wav`

Alternative:

- raw PCM plus JSON metadata headers

WAV is the better first version because it is self-describing and easier to validate.

## Why Use WAV First

For the firmware, WAV simplifies:

- sample rate detection
- channel count detection
- bit depth detection
- debugging captured responses

The BOX-3 speaker path ultimately needs raw PCM frames, but parsing a simple WAV header on-device is straightforward.

## Natural-Language Weather Text

Do not read the compact screen string aloud.

Build a separate spoken sentence such as:

`Today in Fargo, it is 39 degrees and cloudy. The high is 64 and the low is 27. Wind is 10 miles per hour with a 1 percent chance of rain.`

This should be generated locally by firmware code from the parsed `weather_report_t`.

Suggested helper:

- `weather_client_format_spoken()`

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

Expected initial audio format target:

- mono
- 16-bit PCM
- 22050 Hz or 16000 Hz

The local TTS service should preferably normalize to one known format to reduce firmware branching.

Recommended first choice:

- mono 16-bit PCM WAV at 22050 Hz

## Assistant State Handling

The assistant currently blocks inside command execution.

Weather playback will lengthen that path, so the firmware should treat weather speech as an explicit command activity window.

During TTS fetch and playback:

- keep wake word disabled
- keep the weather UI visible
- pause microphone feed, just as the current weather HTTP fetch now does
- avoid returning to standby until playback completes or fails

This prevents AFE ringbuffer overruns and avoids accidental re-triggering during playback.

## UI Behavior

While speaking weather:

- keep the detailed weather screen visible
- optionally change subtitle/detail line to indicate playback, for example `READING WEATHER`

Failure cases:

- if TTS fetch fails, keep the weather on screen briefly and return to standby
- if playback fails, log the error and return to standby

The first version does not need playback progress UI.

## Configuration Needs

Add config for the local TTS service:

- `CONFIG_LOCAL_TTS_BASE_URL`
- `CONFIG_LOCAL_TTS_TIMEOUT_MS`
- `CONFIG_LOCAL_TTS_ENABLED`

Optional later:

- `CONFIG_LOCAL_TTS_VOICE`
- `CONFIG_LOCAL_TTS_FORMAT`

These should follow the current local-config workflow and remain generic in tracked defaults.

## Failure Handling

The feature should fail safely.

### TTS service unavailable

- log the HTTP error
- skip spoken playback
- leave the weather display visible for its hold period
- return to standby

### Invalid audio response

- reject malformed WAV or unsupported format
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

1. accept text over HTTP
2. call Piper with a configured voice model
3. return synthesized WAV bytes

This wrapper can be:

- Python
- Node
- Go
- another small local service runtime

The firmware should not care which runtime hosts Piper.

## First Implementation Scope

Included in the future first build:

- local TTS HTTP client in firmware
- weather spoken sentence builder
- speaker codec initialization
- WAV response parsing
- PCM playback through BOX-3 speaker
- graceful fallback to screen-only weather if TTS fails

Deferred:

- streaming audio from the TTS server
- generic assistant speech beyond weather
- user-selectable voices
- caching common spoken phrases
- synchronized lip-sync or progress UI
- full duplex audio with playback reference into AEC

## Recommended Build Order

1. extend `board_audio` with speaker initialization and PCM playback helpers
2. add `local_tts_client` with a simple `/synthesize` WAV contract
3. add spoken weather sentence formatting
4. wire weather command flow to fetch TTS after screen update
5. keep the screen visible during playback
6. verify fallback behavior when TTS is unavailable
7. verify end-to-end on device with a local Piper service

## Open Questions

- Should the TTS server always return fully buffered WAV, or should it later support chunked streaming?
- Which voice and sample rate should be standardized for the first version?
- Should weather playback be interruptible by touch or wake word?
- Should the firmware cache the last successful spoken weather audio for quick replay?
- Should spoken weather be optional per command, or always enabled when local TTS is configured?
