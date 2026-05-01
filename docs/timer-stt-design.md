# Timer Design With Local STT

## Goal

Add timer support to `box3-assistant` so the ESP32-S3-BOX-3 can handle commands such as:

- `set a timer for 20 minutes`
- `set a timer for 1 minute 30 seconds`
- `set a timer for 20 seconds`

The timer should:

1. start from a spoken duration
2. display the remaining time on the BOX-3 screen
3. count down to zero
4. play a pleasant repeating alarm when it expires
5. stop alarming when the device hears `stop`

This design chooses a local LAN speech-to-text path for dynamic durations instead of trying to pre-register every possible timer phrase in the on-device command table.

## Why A Dynamic STT Path Is Needed

The current firmware uses:

- local wake-word detection
- local MultiNet fixed-phrase command recognition

That works well for commands such as:

- `weather today`
- `update groups from hue`
- `turn on kitchen`

It does not scale cleanly to arbitrary timer durations.

A phrase-table-only approach would require enumerating large numbers of combinations such as:

- every second count
- every minute count
- mixed minute/second combinations
- natural wording variants

That approach would be brittle, memory-hungry, and still incomplete.

For timer durations, a better split is:

1. recognize a fixed local timer intent
2. capture a short follow-up utterance
3. transcribe that short utterance on a nearby server
4. parse the transcribed text into total seconds

## Chosen Direction

The first implementation should use:

- a fixed local command: `set a timer`
- a short follow-up recording window
- a local STT service on the LAN
- a deterministic timer-duration parser

Chosen STT engine for the first implementation:

- `faster-whisper`

This is the current preferred option because it offers a good balance of:

- accuracy
- CPU-only usability
- manageable deployment complexity
- acceptable latency for short command utterances

Other local STT options were considered, but are not the initial choice:

- `whisper.cpp`
- `Vosk`
- `NVIDIA Riva`

`faster-whisper` is the current default unless later testing shows that `whisper.cpp` provides materially better simplicity or latency on the target server.

## Available Server Hardware

Current planned host for the local STT service:

- Dell OptiPlex 7070 Micro
- Intel Core i7-9700T
- 16 GB RAM
- 512 GB SSD
- Intel UHD Graphics 630

This is sufficient for a small local Whisper-family service for this feature.

Important constraints:

- plan for CPU inference, not GPU acceleration
- keep the STT model modest in size
- do not design around continuous transcription

Recommended starting models:

- `base.en`
- `tiny.en` if latency needs to be reduced further

The timer feature does not need a large transcription model because the expected utterances are short and structurally simple.

## Desired User Flow

### Timer Start

The intended interaction is:

1. user says `Hi ESP`
2. device wakes and listens for a command
3. user says `set a timer`
4. device enters a timer-duration capture mode
5. device prompts with a short confirmation such as `For how long?`
6. user says `1 minute 30 seconds`
7. device records a short audio clip
8. device sends that clip to the local `faster-whisper` service
9. service returns transcribed text
10. firmware parses the text into total seconds
11. timer starts
12. screen shows the countdown until expiry

### Timer Expiry

When the timer reaches zero:

1. the device begins a repeating pleasant alarm
2. the device keeps listening for `stop`
3. the user says `stop`
4. the device stops the alarm and returns to standby

## Non-Goals

The first implementation should not include:

- open-ended natural-language task management
- multiple concurrent timers
- named timers
- timer persistence across reboot
- cloud STT dependencies
- continuous always-on STT streaming
- arbitrary alarm tones downloaded from the network

Those can be added later if needed, but they should not complicate the first version.

## High-Level Architecture

There are four main pieces:

- BOX-3 firmware
  - wake word
  - initial command recognition
  - follow-up audio capture
  - duration parsing
  - timer countdown state
  - UI updates
  - local alarm playback

- local STT service
  - accepts a short audio clip
  - transcribes it with `faster-whisper`
  - returns text only

- timer parser
  - converts transcription text into seconds
  - rejects malformed or zero-length durations

- timer runtime
  - owns countdown state
  - owns expired/alarming state
  - coordinates display and stop handling

## Why The STT Boundary Should Be Narrow

The BOX-3 should not send every utterance to the server.

The intended division of labor is:

- local firmware handles wake word and fixed command routing
- server STT is only used after a timer intent has already been recognized

This keeps:

- network usage bounded
- latency acceptable
- privacy surface smaller
- firmware behavior deterministic

It also preserves the current character of the project as a local-first assistant with selective delegation for heavier speech work.

## Firmware Flow Changes

### 1. Add A Timer Intent Command

Add a fixed built-in command:

- `set a timer`

This command should behave similarly to other built-in commands, but instead of executing immediately it transitions the assistant into a short duration-capture state.

### 2. Add A Timer Duration Capture State

After `set a timer` is recognized:

- WakeNet remains disabled temporarily
- MultiNet fixed-command recognition is no longer the primary input path
- the firmware records a bounded follow-up utterance

The first implementation should prefer a fixed capture window over more complex silence detection.

Suggested starting window:

- 2 to 4 seconds

### 3. Send Audio To Local STT

The firmware should package the captured audio and send it to the local STT service as:

- mono
- PCM16
- WAV
- 16 kHz

WAV is the simplest first choice because it is self-describing and easy to handle server-side.

### 4. Parse Duration Text

The STT result should then be parsed locally into total seconds.

Examples:

- `20 seconds` -> `20`
- `1 minute` -> `60`
- `1 minute 30 seconds` -> `90`
- `2 minutes and 5 seconds` -> `125`
- `90 seconds` -> `90`

The parser should:

- accept singular and plural units
- accept optional filler words such as `and`
- reject zero or negative durations
- reject unrecognized combinations
- clamp to a reasonable upper bound

Suggested first upper bound:

- 24 hours

### 5. Run Timer Countdown

Once parsed, the firmware starts a timer runtime that:

- stores deadline tick
- stores original duration
- redraws remaining time on screen at a regular interval
- transitions into alarming state at zero

### 6. Alarm Until `stop`

While alarming:

- the speaker should play a short repeating local chime
- the microphone should still get quiet gaps in which `stop` can be recognized

The alarm should be bursty rather than continuous so the device can still hear the stop command reliably.

## Proposed Modules

Suggested additions:

- `main/timer/timer_parse.c`
- `main/timer/timer_parse.h`
- `main/timer/timer_runtime.c`
- `main/timer/timer_runtime.h`
- `main/stt/local_stt_client.c`
- `main/stt/local_stt_client.h`

Likely updates:

- `main/box3_assistant.c`
- `main/assistant_runtime.h`
- `main/commands/assistant_commands.h`
- `main/commands/assistant_command_dispatch.h`
- `main/commands/assistant_command_dispatch.c`
- `main/commands/assistant_command_text.c`
- `main/board/ui_status.h`
- `main/board/ui_status.c`
- `main/board/board_audio.c`
- `main/board/board_audio.h`

### Responsibilities

- `local_stt_client`
  - send short WAV audio to the local `faster-whisper` service
  - receive transcribed text
  - expose a simple firmware-facing API

- `timer_parse`
  - parse transcription text into total seconds
  - keep logic host-testable

- `timer_runtime`
  - start, stop, and track timer state
  - provide remaining-time formatting helpers
  - coordinate alarming state

- `box3_assistant.c`
  - orchestrate the state transition from local command to follow-up recording to timer start

- `ui_status`
  - render countdown and expired timer display states

- `board_audio`
  - play local timer alarm PCM

## Local STT Service Contract

The firmware should not know internal `faster-whisper` details.

The service boundary should stay simple.

### Request

Recommended request:

- `POST /v1/audio/transcriptions`
- `multipart/form-data`
- audio file field containing a short WAV clip
- optional model field such as `base.en`

This should be compatible with an OpenAI-style audio transcription API shape when practical, even though the backend is local and uses `faster-whisper`.

### Response

Recommended response:

```json
{
  "text": "1 minute 30 seconds"
}
```

The firmware only needs the final normalized transcript text.

No streaming transcription is required for the first timer implementation.

## UI Design

The timer should not reuse the generic success/error status screen.

It should have a dedicated timer presentation with:

- large remaining time
- optional small label such as `TIMER`
- a clear expired state such as `TIME IS UP`

While a timer is active, the presence-clock flow should not take over the display.

While a timer is alarming, the timer UI should continue to own the display.

## Audio Design

The alarm should be generated locally rather than requested from TTS.

Reasons:

- avoids server round trips at expiry time
- avoids failure if TTS is unavailable
- keeps the alarm immediate and deterministic
- simplifies stop-listening behavior

The first alarm sound should be:

- a short pleasant synthesized chime
- repeated with a gap between bursts

The gap is important because the device needs quiet windows to hear `stop`.

## Stop Recognition Strategy

The first implementation should treat alarm-stop handling as a special mode.

Normal standby behavior is:

- wake word first
- command second

Alarm behavior should instead allow:

- direct `stop` recognition without requiring `Hi ESP`

This avoids a poor user experience at expiry time.

That means the firmware needs a temporary recognition mode that bypasses the normal wake-word gate while the timer is alarming.

## Configuration

Recommended config items:

- `CONFIG_LOCAL_STT_ENABLED`
- `CONFIG_LOCAL_STT_BASE_URL`
- `CONFIG_LOCAL_STT_TIMEOUT_MS`
- `CONFIG_LOCAL_STT_MODEL`
- `CONFIG_TIMER_MAX_DURATION_SECONDS`
- `CONFIG_TIMER_CAPTURE_MS`

Example defaults:

- local STT enabled
- timeout around 5 to 10 seconds
- model `base.en`
- capture window around 3000 ms

## Error Handling

The first implementation should fail clearly when:

- STT service is unreachable
- transcription text cannot be parsed into a valid duration
- parsed duration exceeds the configured maximum

Suggested user-facing messages:

- `Timer voice service unavailable`
- `Could not understand timer`
- `Timer duration too long`

The firmware should then return to standby cleanly.

## Testing Strategy

The most important host-side tests are the pure logic pieces.

### Unit Tests

Add unit tests for:

- duration parsing
- normalization of accepted timer phrases
- upper-bound validation
- timer expiry logic
- stop-state transitions where host-testable

### Manual Tests

Manual end-to-end checks should include:

1. `set a timer` + `20 seconds`
2. `set a timer` + `1 minute`
3. `set a timer` + `1 minute 30 seconds`
4. malformed input such as `for a while`
5. expired timer followed by `stop`
6. STT server offline behavior

## Risks And Tradeoffs

### STT Misrecognition

Short command-like utterances are simpler than open conversation, but mistakes are still possible.

Mitigations:

- keep the utterance short
- use an English-only model first
- optionally speak back the parsed duration later if needed

### CPU Contention On The Server

The current home server already runs other services.

Mitigations:

- keep the STT model small
- only use STT after timer intent recognition
- avoid continuous transcription

### Audio Feedback Loop During Alarm

The speaker may interfere with stop recognition.

Mitigations:

- use short alarm bursts
- leave silent gaps
- keep stop recognition narrow

## Future Extensions

Possible follow-up improvements:

- support `cancel timer`
- support `how much time is left`
- support multiple timers
- support hours
- support direct one-shot command flow such as `Hi ESP set a timer for 3 minutes`
- support speaking back confirmation such as `Timer set for 3 minutes`
- reuse the same STT path for other bounded follow-up commands

## Summary

The timer feature should not be implemented as a giant fixed phrase table.

The better design is:

1. local fixed timer intent recognition
2. short follow-up audio capture
3. local LAN transcription with `faster-whisper`
4. deterministic local duration parsing
5. local timer UI and alarm behavior on the BOX-3

That keeps the firmware aligned with the current architecture:

- lightweight local orchestration on the ESP32
- heavier speech work delegated to a nearby server
- deterministic user-facing behavior for a bounded voice feature
