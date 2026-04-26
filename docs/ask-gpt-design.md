# Ask GPT Design

## Goal

Add a new assistant path that begins with the existing wake word, accepts a local command of `Ask GPT`, records a freeform spoken question, transcribes that question on a local machine, and then sends the resulting text to ChatGPT for a response.

This is intended to preserve the current strengths of the firmware:

- fast local wake word detection
- fast local deterministic command handling for Hue
- simple on-device state management and UI

while adding a new cloud-backed question/answer mode without forcing all speech handling through the cloud.

## Desired User Flow

The intended interaction is:

1. user says `Hi ESP`
2. device wakes and listens for a command
3. user says `Ask GPT`
4. device enters a dedicated question-listening state
5. device records the spoken question audio
6. device sends the recorded audio to a local transcription service
7. local transcription service returns plain text
8. device sends the transcribed text to ChatGPT
9. device displays the answer on screen
10. optionally speaks the answer through the existing local TTS player
11. device returns to standby

Example:

1. `Hi ESP`
2. `Ask GPT`
3. `What is the 2nd largest mountain in the world?`
4. local machine transcribes the question
5. device sends text to ChatGPT
6. screen shows the answer

## Why Use A Local Transcription Service

The ESP32-S3 is a good fit for:

- wake word detection
- fixed command recognition
- audio capture
- orchestration of network requests

It is not a good fit for high-quality open-ended transcription of arbitrary spoken questions.

For this reason, the recommended architecture is:

- local wake word and local command spotting on the BOX-3
- question audio sent to a transcription service on a nearby machine
- only the resulting text sent to ChatGPT

This keeps the cloud side smaller and avoids sending every spoken interaction to OpenAI as raw audio.

## Proposed Architecture

There are three distinct speech modes:

1. standby mode
   The current WakeNet plus MultiNet assistant mode.

2. deterministic command mode
   Existing local commands such as Hue controls and `update groups from hue`.

3. GPT question mode
   Triggered only after the local command `Ask GPT`.

High-level component flow:

- BOX-3 firmware
  - wake word detection
  - command detection
  - question recording
  - UI state management
  - local STT request
  - ChatGPT request

- local STT service
  - receives recorded audio from BOX-3
  - returns transcript text

- OpenAI API
  - receives transcript text
  - returns answer text

- local TTS player, optional
  - reuses `tts_player_speak()` for spoken answers
  - keeps GPT-specific code independent of Piper socket details

## Firmware Flow Changes

### 1. Add A New MultiNet Command

Add a static command:

- `ask gpt`

This should remain a fixed local command, similar to `update groups from hue`.

### 2. Add A GPT Question State

After `Ask GPT` is recognized, the assistant should not immediately return to standby.

Instead it should enter a new state such as:

- `ASSISTANT_MODE_GPT_QUESTION`

In this state:

- WakeNet remains disabled temporarily
- MultiNet command recognition is no longer the primary input path
- raw microphone audio is recorded for a fixed question window

### 3. Record Audio For The Question

Record a short bounded question clip, for example:

- 4 to 8 seconds

The first implementation should prefer a fixed recording window over complex silence detection. Silence detection can be added later if needed.

The firmware should:

- collect PCM from the microphone path
- store it in RAM or PSRAM while recording
- package it in a format accepted by the local transcription service

Likely formats:

- WAV with PCM16
- raw PCM with explicit metadata

WAV is the easier first option because it is self-describing and simple for server-side tools to accept.

### 4. Send Audio To A Local STT Service

The firmware sends the recorded question audio to a configurable local HTTP endpoint.

Recommended config items:

- `CONFIG_LOCAL_STT_BASE_URL`
- `CONFIG_LOCAL_STT_TIMEOUT_MS`
- `CONFIG_LOCAL_STT_ENABLED`

Example request shape:

- `POST /transcribe`
- body: multipart form upload or raw WAV bytes

Example response shape:

```json
{
  "text": "what is the weather supposed to be like tomorrow"
}
```

### 5. Send Transcript Text To ChatGPT

After transcription succeeds, the device sends only the text to OpenAI.

Recommended request path:

- OpenAI Responses API

Example logical payload:

- system instruction describing the BOX-3 assistant persona
- user transcript text

Example response usage:

- show a concise answer on screen
- optionally feed the answer into `tts_player_speak()` for local spoken playback

### 6. Return To Standby

After success or failure:

- restore the normal standby state
- re-enable wake word detection
- reset temporary GPT question buffers

## Recommended Module Layout

Current layout already supports feature growth:

- `main/box3_assistant.c`
- `main/board/`
- `main/hue/`
- `main/system/`

Suggested additions:

- `main/openai/`
  - `openai_client.c`
  - `openai_client.h`

- `main/stt/`
  - `local_stt_client.c`
  - `local_stt_client.h`

Optional later:

- `main/assistant/`
  - conversation/session state
  - prompt building helpers

Existing reusable TTS modules:

- `main/tts/local_tts_client.c`
  - low-level Piper connection and event parsing

- `main/tts/tts_player.c`
  - generic text-to-speech playback facade for assistant features

Responsibilities:

- `box3_assistant.c`
  - route between local command mode and GPT mode

- `local_stt_client`
  - upload recorded audio to a local machine
  - parse transcript result

- `openai_client`
  - send transcript text to OpenAI
  - parse reply text

- `tts_player`
  - speak the final response without exposing Piper protocol details to GPT code

## Local Transcription Options

The preferred design leaves the firmware independent of the exact transcription engine.

The device should speak to a simple HTTP API. Behind that API, a local machine can run any STT engine.

Strong candidates:

- `whisper.cpp`
  - lightweight
  - easy to host locally
  - good first choice for CPU-based local STT

- `faster-whisper`
  - better throughput on stronger hardware
  - good if the local machine has more compute

- `Vosk`
  - lighter, but generally weaker for open-ended assistant questions

Recommended first target:

- a small HTTP wrapper around `whisper.cpp` or `faster-whisper`

The ESP should not care which engine is behind the endpoint as long as the response returns transcript text.

## Suggested Local STT API Contract

### Request

- `POST /transcribe`
- content type:
  - `audio/wav`
  - or multipart form upload with one file named `audio`

### Response

```json
{
  "text": "turn this transcript into plain text here",
  "duration_ms": 4210,
  "engine": "whisper.cpp"
}
```

Minimum required field:

- `text`

Optional fields:

- `duration_ms`
- `engine`
- confidence metadata

## UI States

The UI should make the GPT path obvious.

Suggested states:

- `Listening...`
  Existing post-wake command state.

- `Ask your question`
  Immediately after `Ask GPT` is recognized.

- `Transcribing...`
  Audio has been captured and is being sent to the local STT service.

- `Thinking...`
  Transcript has been sent to ChatGPT.

- `Answer ready`
  Show the answer text.

- `STT failed`
  Local transcription request failed.

- `GPT failed`
  ChatGPT request failed.

## Configuration Needs

This feature will introduce new secrets and local endpoints.

It should also introduce explicit integration feature flags, or an equivalent configuration mechanism, so each major assistant integration can be turned on or off without code changes.

Expected local config:

- local STT server base URL
- OpenAI API key
- optional model name for ChatGPT responses

These should follow the existing local-secret workflow:

- keep tracked defaults generic
- keep real local values in ignored config

Potential new config keys:

- `CONFIG_INTEGRATION_HUE_ENABLED`
- `CONFIG_INTEGRATION_WEATHER_ENABLED`
- `CONFIG_INTEGRATION_ASK_GPT_ENABLED`
- `CONFIG_LOCAL_STT_BASE_URL`
- `CONFIG_LOCAL_STT_TIMEOUT_MS`
- `CONFIG_OPENAI_API_KEY`
- `CONFIG_OPENAI_MODEL`

Feature-flag requirement:

- each major integration should have a clear config switch that can disable its command path and network behavior
- disabled integrations should not register voice commands or appear as active features in the runtime command table
- tracked defaults should remain safe and generic, while local overrides can selectively enable or disable integrations for a specific device
- the mechanism does not need to be limited to simple booleans, but it should be straightforward to understand and operate through normal config workflows

## Failure Handling

The first version should fail safely and clearly.

### STT failure

If the local transcription service is unavailable:

- show `STT failed`
- log the HTTP error
- return to standby

### GPT failure

If the OpenAI request fails:

- show `GPT failed`
- log the HTTP or parse error
- return to standby

### Empty transcript

If the local STT service returns empty text:

- show `I didn't catch that`
- return to standby

## First Implementation Scope

The first implementation should stay intentionally small.

Included:

- `Ask GPT` as a new static local command
- fixed-duration question recording
- local HTTP transcription request
- OpenAI text response request
- screen-only answer display
- optional spoken answer through `tts_player_speak()` after the screen path is stable

Deferred:

- streaming transcription
- streaming GPT responses
- multi-turn conversation memory
- silence-based end-of-speech detection
- tool calling from GPT

## Tradeoffs

### Benefits

- preserves fast local wake word and home-control path
- reduces cloud usage compared to cloud transcription for everything
- keeps the architecture modular
- allows STT backend changes without firmware redesign

### Costs

- requires a separate always-available local machine for STT
- adds another network dependency
- introduces larger audio upload code paths on the ESP
- requires more RAM than the current fixed-command-only assistant

## Open Questions

- What question recording duration should be used first: 4 seconds, 6 seconds, or 8 seconds?
- Should question recording stop on silence in the first version or only use a fixed window?
- Should the first local STT endpoint accept WAV directly or multipart upload?
- Should the first GPT response be capped to a short on-screen answer?
- Should `Ask GPT` be the only GPT trigger, or should there later be a second trigger for tool-like GPT behavior?

## Recommended First Build Order

1. add static `Ask GPT` command
2. add a new GPT question recording state in the assistant
3. record a fixed WAV clip after `Ask GPT`
4. add `local_stt_client` with a simple `/transcribe` HTTP contract
5. add `openai_client` for transcript-to-answer requests
6. add UI states for question, transcribing, thinking, and reply
7. verify end-to-end with screen-only output
8. optionally add spoken answer playback through `tts_player_speak()`

This ordering keeps the risk low and isolates each moving part.
