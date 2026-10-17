# Fireworks Voice Routing Design

This design supersedes the former Ask GPT design. The explicit `Ask GPT` trigger
command and the OpenAI-specific client are replaced by universal routing: every
post-wake-word utterance goes through the existing local STT service and then to
a Fireworks AI model that understands the utterance and decides what to execute.

## Goal

Route every utterance spoken after the wake word through the local STT service,
then submit the transcript to a model hosted on Fireworks AI for understanding
and execution, so the assistant can:

- control the home with natural phrasing, for example `Turn off living room` or
  `kill the lights in the living room`
- answer general questions, for example `What is the definition of superfluous?`

This is intended to preserve the current strengths of the firmware:

- fast local wake word detection
- local deterministic execution behind the existing command handler registry
- simple on-device state management and UI
- audio that never leaves the local network

while removing the fixed-phrase limits of on-device command recognition.

## Desired User Flow

### Home control example

1. user says `Hi ESP`
2. device wakes and captures the utterance
3. device sends the recorded audio to the local STT service
4. local STT service returns transcript text such as `turn off living room`
5. device sends the transcript to Fireworks with a constrained tool schema
6. model returns a tool call, for example `hue_set_group` with group `living room` and `on` false
7. device executes the tool call through the existing Hue command handler
8. device shows the result and returns to standby

### General question example

1. user says `Hi ESP`
2. user says `What is the definition of superfluous?`
3. device captures, transcribes, and submits the transcript to Fireworks
4. model returns an answer with no tool call
5. device shows the answer on screen and speaks it through the local TTS player
6. device returns to standby

## Why Route Through STT And A Model

The ESP32-S3 is a good fit for:

- wake word detection
- audio capture
- orchestration of network requests
- local deterministic execution of already-understood intents

It is not a good fit for open-ended transcription or open-ended natural language
understanding. The current design already acknowledges this for timers and city
lookup: those features capture audio, transcribe it on a local machine, and parse
the transcript locally.

On-device MultiNet command recognition has hard limits that this design removes:

- the phrase table is rebuilt from synced Hue groups and supports only a small
  fixed number of groups
- phrasing must match the registered phrases exactly enough for phoneme
  matching
- intents are encoded as integer command IDs with per-feature arithmetic
- free-form input exists only in two hardcoded follow-up flows, timer duration
  and remote city, which duplicate nearly identical orchestration code

Routing through STT plus a model replaces all of that with one understanding
path that accepts arbitrary phrasing, arbitrary group counts, and general
questions.

The Fireworks inference API is OpenAI compatible and supports tool calling, so
intent understanding and execution selection fit in a single chat completion
request. Only the transcript and the returned text leave the local network; the
recorded audio itself is transcribed on the LAN.

## Proposed Architecture

### Speech modes

1. standby mode
   WakeNet detection as today. After a wake word, the device does not enter
   MultiNet command listening; it enters utterance capture.

2. utterance capture mode
   Raw microphone audio is recorded for a bounded window, reusing the existing
   follow-up capture path.

3. transcribe mode
   Recorded audio is sent to the local STT service, which returns transcript
   text.

4. think mode
   The transcript is sent to Fireworks with a system prompt and a constrained
   tool schema.

5. execute or answer mode
   A tool call result is dispatched to the existing command handlers.
   A plain answer result is shown on screen and spoken through the local TTS
   player.

6. direct command mode
   The existing timer alarm `stop` path stays entirely local. A ringing alarm
   must not depend on Wi-Fi, the STT server, or the model, so MultiNet direct
   command detection remains for alarm dismissal.

### Routing mode decision

Universal routing changes the reliability posture of basic home control: the
local STT server and internet access become prerequisites for voice control.

The recommended approach is universal routing behind a feature flag:

- `CONFIG_INTEGRATION_FIREWORKS_ENABLED` selects the routed path after a wake
  word
- when disabled, the device keeps today's MultiNet command flow, so the flag
  also provides an offline fallback and a privacy opt-out
- MultiNet stays compiled in for the direct command alarm stop path either way
- tools are advertised to the model based on the other integration feature
  flags, so disabled integrations are not offered to the model

A hybrid mode that keeps MultiNet as a fast local path for known phrases is a
possible later refinement, but it is not part of the first implementation. Two
understanders introduce ambiguity and extra routing logic, and the flag already
covers the offline case.

### High-level component flow

- BOX-3 firmware
  - wake word detection
  - utterance capture
  - UI state management
  - local STT request
  - Fireworks request
  - tool dispatch to existing handlers
  - answer display and speech

- local STT service
  - receives recorded audio from BOX-3
  - returns transcript text
  - unchanged from the timer feature

- Fireworks inference API
  - receives transcript text plus tool schema
  - returns a tool call or an answer

- local TTS service
  - receives answer text
  - returns PCM audio
  - unchanged from the weather feature

## Firmware Flow Changes

### 1. Route After Wake Word To Utterance Capture

In `main/assistant/session.c`, a wake word detection enters a capture stage
instead of MultiNet command listening:

- new assistant stages such as `ASSISTANT_STAGE_CAPTURING`,
  `ASSISTANT_STAGE_TRANSCRIBING`, `ASSISTANT_STAGE_THINKING`, and reuse of the
  existing executing stage
- WakeNet is disabled during the interaction as today
- MultiNet is not fed during capture
- the command window logic in `assistant_state.c` is replaced by capture window
  logic for the routed path

### 2. Record The Utterance

Reuse `assistant_followup_audio_capture_mono()` from `main/assistant/followup_audio.c`:

- mono PCM16 at 16 kHz, which is the format the local STT client already sends
- the capture window is longer than the timer follow-up window, configurable
  with a new setting such as `CONFIG_VOICE_ROUTING_CAPTURE_MS`
- a fixed window is preferred for the first implementation
- simple energy-based early stop can be added later as a pure host-testable
  module
- silence-based end-of-speech detection is deferred

### 3. Transcribe With The Local STT Service

Reuse `local_stt_client_transcribe()` unchanged:

- the client already streams PCM16 chunks to the local STT service and returns
  transcript text
- the local STT server only needs to accept longer clips than the timer
  follow-up window
- no firmware changes are expected here beyond the longer capture window

### 4. Send The Transcript To Fireworks

A new client module posts a chat completion request:

- system prompt describing the assistant, its tools, and the synced Hue group
  names
- user message containing the transcript
- the constrained tool schema
- bounded response handling with a single answer or a single tool call
- cancellation hook registered with the watchdog cancel list

### 5. Dispatch Tool Calls To Existing Handlers

A new pure module maps a returned tool call to the existing command handler
registry:

- `hue_set_group` maps to the Hue command handler with the matched group
- `timer_start` maps to the timer command handler with a clamped duration
- `weather_get` maps to the weather command handler
- `volume_adjust` maps to the volume command handler
- the LLM becomes the intent source in place of MultiNet command IDs, and the
  existing handlers remain the execution layer unchanged

### 6. Match Hue Group Names

The model is given the actual synced group names, so it can return a group name
as text. A new pure module resolves a returned group name to a synced group:

- exact match first, then case-insensitive, then simple fuzzy matching
- an unmatched group produces a spoken `I don't have a group called X` response
  rather than a failed command

This removes the fixed phrase table limit on usable Hue groups.

### 7. Build The System Prompt From Device State

The system prompt is built at request time from:

- synced Hue group names from `hue_group_store`
- tools available under the current integration feature flags
- the expected concise answer style for general questions

Disabled integrations are omitted from the tool schema, so the model cannot
attempt them.

### 8. Rework The Watchdog For Phase Timeouts

The current watchdog restarts the device when a session exceeds 30 seconds. A
routed interaction can legitimately chain capture, STT, model, and speech
playback, so the watchdog needs:

- per-phase timeouts instead of a single session cap
- Fireworks client cancellation registered in the watchdog cancel list
- answer speech duration kept inside watchdog limits by capping answer length

### 9. Render And Speak Answers

For general answers:

- prompt the model to answer in at most three short sentences
- split the answer into screen-sized pages with the host-tested `answer_pager` module
- show each page on screen and speak that page through `tts_player_speak()`, so the page stays visible for about its spoken duration
- page through longer answers instead of truncating them
- keep the model token cap tight (`CONFIG_FIREWORKS_MAX_TOKENS`) so pages stay bounded

## Recommended Module Layout

Current layout already supports this feature:

- `main/assistant/session.c`
- `main/assistant/followup_audio.c`
- `main/assistant/command_registry.c`
- `main/stt/local_stt_client.c`
- `main/tts/tts_player.c`
- `main/hue/`
- `main/weather/`
- `main/timer/`
- `main/time/`
- `main/volume/`

Suggested additions:

- `main/fireworks/fireworks_client.c` and `.h`
  - HTTPS chat completion request, response capture, cancellation

- `main/fireworks/fireworks_response_parse.c` and `.h`
  - pure parsing of answer text versus tool call, host tested

- `main/assistant/tool_dispatch.c` and `.h`
  - pure mapping of model tool calls to existing handlers, host tested

- `main/hue/hue_group_match.c` and `.h`
  - pure group name matching, host tested

- `main/assistant/answer_pager.c` and `.h`
  - pure answer pagination for the screen, host tested

Responsibilities:

- `session.c`
  - route wake word to capture, then STT, then Fireworks, then execute or
    answer

- `fireworks_client`
  - send transcript and tool schema to Fireworks
  - return answer text or a tool call

- `tool_dispatch`
  - validate tool calls
  - map tool calls to command handlers

- `tts_player`
  - speak final answers without exposing Piper protocol details

Existing modules reused unchanged:

- `local_stt_client`
- `assistant_followup_audio_capture_mono()`
- command handler registry and all feature handlers

## Fireworks API Contract

### Request

- `POST {base}/chat/completions`
- default base `https://api.fireworks.ai/inference/v1`, overridable with
  `CONFIG_FIREWORKS_BASE_URL`
- OpenAI compatible body: `messages`, `tools`, `tool_choice`
- authorization header with `CONFIG_FIREWORKS_API_KEY`

### Response

- `choices[0].message.content` holds the answer text when the model answers
  directly
- `choices[0].message.tool_calls[0].function` holds `name` and a JSON
  `arguments` string when the model selects a tool
- `finish_reason` distinguishes tool calls from plain answers
- the first implementation handles one tool call per response

### Model choice

- a fast model with function calling support should be used for routing and
  control commands
- the current Fireworks model lineup should be checked when implementing, since
  function calling support varies by model
- a larger model can optionally be used later for the general answer path

## Tool Schema

The first implementation advertises a small, fixed set of tools:

- `hue_set_group`
  - `group`: string, one of the synced group names
  - `on`: boolean

- `timer_start`
  - `duration_seconds`: integer, clamped by `CONFIG_TIMER_MAX_DURATION_SECONDS`

- `weather_get`
  - `period`: string, `today` or `tomorrow`

- `volume_adjust`
  - `steps`: integer, positive or negative

Validation rules:

- every tool call is validated before execution
- unknown group names produce a spoken unmatched-group response
- out-of-range durations are clamped or rejected with spoken feedback
- unknown tool names are ignored with an error status
- no tool call in the response is treated as a general answer

## UI States

Suggested states:

- `Listening...`
  While the utterance is being captured, replacing the current MultiNet
  listening state on the routed path.

- `Transcribing...`
  Audio has been captured and is being sent to the local STT service.

- `Thinking...`
  The transcript has been sent to Fireworks.

- Executing
  A tool call is being dispatched, reusing the existing working and result
  screens.

- `Answer ready`
  Show the first page of the answer, then page through the rest as each page is spoken.

- `STT failed`
  Local transcription request failed.

- `Model failed`
  Fireworks request or response parsing failed.

- `I didn't catch that`
  The transcript came back empty.

## Configuration Needs

Feature flags:

- `CONFIG_INTEGRATION_FIREWORKS_ENABLED`
- `CONFIG_INTEGRATION_HUE_ENABLED`
- `CONFIG_INTEGRATION_WEATHER_ENABLED`
- `CONFIG_INTEGRATION_TIMER_ENABLED`

Disabled integrations should not advertise tools to the model and should not
register local commands.

Fireworks settings:

- `CONFIG_FIREWORKS_API_KEY`
- `CONFIG_FIREWORKS_BASE_URL`
- `CONFIG_FIREWORKS_MODEL`
- `CONFIG_FIREWORKS_TIMEOUT_MS`

Capture settings:

- `CONFIG_VOICE_ROUTING_CAPTURE_MS`

These follow the existing local-secret workflow:

- tracked defaults stay generic and safe
- real keys and endpoints live in the ignored `sdkconfig.defaults.local`
- `make config-local` regenerates the local file from the example

## Failure Handling

### STT failure

If the local transcription service is unavailable:

- show `STT failed`
- log the socket or protocol error
- return to standby

### Model failure

If the Fireworks request fails:

- show `Model failed`
- log the HTTP or parse error
- return to standby

### Empty transcript

- show `I didn't catch that`
- return to standby

### Invalid tool call

- validate the tool call before execution
- respond with spoken feedback for unmatched groups or invalid values
- return to standby

### Watchdog recovery

- per-phase timeouts with cancellation before any restart
- Fireworks cancellation is part of the watchdog cancel list

### Feature flag disabled

- fall back to today's MultiNet command flow
- no Fireworks traffic occurs

## First Implementation Scope

Included:

- universal routing behind `CONFIG_INTEGRATION_FIREWORKS_ENABLED`
- fixed-duration utterance capture
- reuse of the local STT client and TTS player
- non-streaming Fireworks chat completion with tools
- tool dispatch to existing handlers
- group name matching
- paged screen answer display and spoken answer playback
- per-phase watchdog timeouts
- host unit tests for all new pure modules

Deferred:

- streaming transcription
- streaming model responses
- multi-turn conversation memory
- silence-based end-of-speech detection
- energy-based early capture stop
- barge-in during answer speech
- hybrid MultiNet fast-path routing
- larger answer model selection
- Jellyfin or media tools
- migration of the timer duration and city follow-up flows onto this path

## Tradeoffs

### Benefits

- natural phrasing for all commands
- removes the fixed phrase table limit on Hue groups
- one understanding path replaces MultiNet IDs and the duplicated follow-up
  flows
- general questions work with no additional trigger
- existing command handlers remain the execution layer
- recorded audio stays on the LAN
- offline and privacy opt-out through the feature flag

### Costs

- voice control depends on the local STT server and internet access
- typical latency grows from about one second to roughly three to eight
  seconds for control commands
- every routed interaction spends model tokens
- transcripts and answers leave the local network when enabled
- watchdog and session management become more complex
- answer speech requires length caps to stay within watchdog bounds

## Latency Budget

Approximate expected phases:

- capture: the configured capture window
- STT: roughly one to three seconds for a short clip
- model: roughly one to five seconds depending on model choice
- speech: under one second to first audio

A fast function-calling model keeps control commands near the low end. General
answers can afford the higher end. If control latency is unacceptable later, the
deferred hybrid fast path is the mitigation.

## Open Questions

- What capture window duration should ship first: 6 seconds or 8 seconds?
- Should the general answer path use the same model as routing, or a separate
  larger model from the start?
- How long should the spoken answer cap be: two sentences or three?
- Should the timer duration and city follow-up flows be migrated onto this path
  in a later phase to remove the duplicated orchestration code?
- Should conversation memory cover more than a single exchange eventually?
- Should unknown tool calls be retried with a corrective message to the model,
  or always fail to standby?

## Recommended First Build Order

1. add `fireworks_client` with host-tested response parsing, no behavior change
2. add the tool schema and host-tested `tool_dispatch` mapping
3. add host-tested `hue_group_match`
4. add the Fireworks config keys and feature flag, default off
5. route wake word to utterance capture in `session.c` behind the flag
6. verify transcription of longer clips against the local STT service
7. send transcripts to Fireworks and dispatch tool calls to existing handlers
8. add UI states for transcribing, thinking, and answer
9. rework watchdog timeouts for the routed phases
10. verify end-to-end control and question flows on device
11. add spoken answer playback through `tts_player_speak()`
12. measure latency and watchdog behavior under failure conditions

This ordering keeps each moving part isolated, and the feature flag keeps the
device fully functional while the routed path is built out.
