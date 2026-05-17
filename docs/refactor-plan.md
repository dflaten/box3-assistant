# Refactor Plan

## Goal

Refactor the assistant firmware so file boundaries and method ownership better match responsibilities, with the main focus on reducing orchestration sprawl in `main/box3_assistant.c`, shrinking shared mutable state in `assistant_runtime_t`, and making subsystem boundaries more explicit.

## Principles

- Preserve behavior while changing structure.
- Prefer extracting cohesive subsystems over introducing broad abstractions.
- Keep protocol-specific parsing inside feature modules.
- Reduce direct access to unrelated fields in shared runtime state.
- Add or keep host-test coverage for extracted pure logic.

## Structural Problems To Address

### `main/box3_assistant.c` owns too many responsibilities

The file currently mixes:

- boot and initialization flow
- wake/listen/execute session control
- timer follow-up capture and handling
- presence clock behavior
- watchdog and restart policy
- model initialization
- UI recovery and standby transitions

This is the highest-priority structural problem.

### `assistant_runtime_t` is too broad

`assistant_runtime_t` currently holds state for:

- speech session lifecycle
- watchdog heartbeats
- audio feed control
- presence tracking
- timer state
- Hue runtime groups
- speech model handles
- device handles

This creates coupling between modules that are separated on disk but still share internal state too freely.

### Transport and request lifecycle code is duplicated

Hue, weather, STT, and TTS each manage active requests, cancellation, and transport details in similar but separate ways. The feature split is reasonable, but the shared plumbing is inconsistent.

### `main/board/ui_status.c` is growing into a monolith

The UI status module currently combines:

- font data
- drawing primitives
- rendering policy
- framebuffer and panel control
- idle/display task logic

This is lower priority than the assistant orchestration work, but it should eventually be split.

### Assistant core is too feature-aware

The assistant layer should manage generic voice-assistant concerns:

- boot and task wiring
- wake/listen/execute session control
- watchdog and recovery behavior
- shared execution context
- command handler dispatch

It should not own feature-specific execution flows such as timer duration capture, weather fetch orchestration, or Hue-specific command execution. Those flows should live with their respective feature modules behind a generic assistant command-handler interface.

## Intended End State

This section describes the likely repository layout after the refactor is complete. It is a target state, not a requirement that every change happen in a single PR.

### Files expected to remain

- `main/app_main.c` or a thin `main/box3_assistant.c` during migration
- `main/commands/assistant_command_dispatch.c`
- `main/commands/assistant_command_dispatch.h`
- `main/commands/assistant_command_text.c`
- `main/commands/assistant_command_text.h`
- `main/commands/assistant_commands.h`
- `main/hue/hue_client.c`
- `main/hue/hue_client.h`
- `main/hue/hue_command_map.c`
- `main/hue/hue_command_map.h`
- `main/hue/hue_discovery_response.c`
- `main/hue/hue_discovery_response.h`
- `main/hue/hue_group.h`
- `main/hue/hue_group_store.c`
- `main/hue/hue_group_store.h`
- `main/net/line_socket.c`
- `main/net/line_socket.h`
- `main/stt/local_stt_protocol.c`
- `main/stt/local_stt_protocol.h`
- `main/system/time_support.c`
- `main/system/time_support.h`
- `main/system/wifi_support.c`
- `main/system/wifi_support.h`
- `main/timer/timer_parse.c`
- `main/timer/timer_parse.h`
- `main/timer/timer_runtime.c`
- `main/timer/timer_runtime.h`
- `main/weather/weather_client.c`
- `main/weather/weather_client.h`
- `main/weather/weather_format.c`
- `main/weather/weather_format.h`
- `main/weather/weather_open_meteo_provider.c`
- `main/weather/weather_open_meteo_provider.h`
- `main/weather/weather_provider.h`
- `main/weather/weather_types.h`

### Files expected to be added

#### Assistant core split

- `main/assistant/boot.c`
- `main/assistant/boot.h`
- `main/assistant/command_context.h`
- `main/assistant/command_handler.h`
- `main/assistant/command_registry.c`
- `main/assistant/command_registry.h`
- `main/assistant/diagnostics.c`
- `main/assistant/diagnostics.h`
- `main/assistant/session.c`
- `main/assistant/session.h`
- `main/assistant/state.c`
- `main/assistant/state.h`
- `main/assistant/watchdog.c`
- `main/assistant/watchdog.h`
- `main/assistant/presence.c`
- `main/assistant/presence.h`

#### Runtime and internal shared state split

- `main/assistant/runtime/session_state.h`
- `main/assistant/runtime/watchdog_state.h`
- `main/assistant/runtime/audio_runtime.h`
- `main/assistant/runtime/presence_state.h`
- `main/assistant/runtime/command_runtime.h`

#### Optional shared transport helpers

- `main/net/request_cancel.h`
- `main/net/http_trace.c`
- `main/net/http_trace.h`

#### Feature-owned command handlers

- `main/hue/hue_command_handler.c`
- `main/hue/hue_command_handler.h`
- `main/timer/timer_command_handler.c`
- `main/timer/timer_command_handler.h`
- `main/weather/weather_command_handler.c`
- `main/weather/weather_command_handler.h`

#### UI split

- `main/board/ui_status_render.c`
- `main/board/ui_status_render.h`
- `main/board/ui_status_display.c`
- `main/board/ui_status_display.h`
- `main/board/ui_status_font.c`
- `main/board/ui_status_font.h`

### Files expected to be removed

- `main/box3_assistant.c`

### Files that may be removed after follow-up cleanup

- `main/assistant_runtime.h`
- `main/board/ui_status.c`

These two files are likely to disappear only if the runtime state and UI code are fully decomposed. If compatibility or rollout risk argues for it, they may survive as thinner facades for a while.

### Expected top-level ownership after refactor

- `main/app_main.c`
  - thin firmware entrypoint and composition root
- `main/assistant/boot.*`
  - assistant startup, subsystem initialization, and task creation
- `main/assistant/session.*`
  - wake/listen/execute state machine and standby transitions
- `main/assistant/command_handler.h`
  - generic command execution contract implemented by feature modules
- `main/assistant/command_registry.*`
  - mapping from resolved commands to registered feature handlers
- `main/assistant/watchdog.*`
  - heartbeat, stall, timeout, and recovery policy
- `main/assistant/presence.*`
  - presence-triggered clock ownership and idle timer display behavior
- `main/assistant/runtime/*`
  - narrower runtime and state ownership headers
- `main/timer/timer_command_handler.*`
  - timer-specific command execution flow owned by the timer feature
- `main/weather/weather_command_handler.*`
  - weather-specific command execution flow owned by the weather feature, including spoken-response decisions through the shared TTS facade
- `main/hue/hue_command_handler.*`
  - Hue-specific command execution flow owned by the Hue feature
- `main/board/ui_status_*`
  - board display implementation split by rendering, display control, and font data

### Alternative acceptable end state

If removing `main/box3_assistant.c` entirely creates too much churn, an acceptable intermediate end state is:

- keep `main/box3_assistant.c` as a thin entrypoint and composition file
- move nearly all helper logic into `main/assistant/*`
- keep `main/assistant_runtime.h` as a compatibility header that mostly aggregates `main/assistant/runtime/*` headers

The key requirement is not the filename itself. The key requirement is that orchestration, watchdog, presence, command dispatch, and runtime state no longer live as one large implementation unit, and that feature-specific command flows stay owned by their feature modules.

## Directory View

The trees below show the current relevant firmware layout and the intended end-state layout after the refactor.

### Current State

```text
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── assistant_diagnostics.c
├── assistant_diagnostics.h
├── assistant_runtime.h
├── assistant_state.c
├── assistant_state.h
├── box3_assistant.c
├── board/
│   ├── board_audio.c
│   ├── board_audio.h
│   ├── ui_status.c
│   └── ui_status.h
├── commands/
│   ├── assistant_command_dispatch.c
│   ├── assistant_command_dispatch.h
│   ├── assistant_command_text.c
│   ├── assistant_command_text.h
│   └── assistant_commands.h
├── hue/
│   ├── hue_client.c
│   ├── hue_client.h
│   ├── hue_command_map.c
│   ├── hue_command_map.h
│   ├── hue_command_runtime.c
│   ├── hue_command_runtime.h
│   ├── hue_discovery_response.c
│   ├── hue_discovery_response.h
│   ├── hue_group.h
│   ├── hue_group_store.c
│   └── hue_group_store.h
├── idf_component.yml
├── net/
│   ├── line_socket.c
│   └── line_socket.h
├── stt/
│   ├── local_stt_client.c
│   ├── local_stt_client.h
│   ├── local_stt_protocol.c
│   └── local_stt_protocol.h
├── system/
│   ├── time_support.c
│   ├── time_support.h
│   ├── wifi_support.c
│   └── wifi_support.h
├── timer/
│   ├── timer_parse.c
│   ├── timer_parse.h
│   ├── timer_runtime.c
│   └── timer_runtime.h
├── tts/
│   ├── local_tts_client.c
│   ├── local_tts_client.h
│   ├── tts_player.c
│   └── tts_player.h
└── weather/
    ├── weather_client.c
    ├── weather_client.h
    ├── weather_format.c
    ├── weather_format.h
    ├── weather_open_meteo_provider.c
    ├── weather_open_meteo_provider.h
    ├── weather_provider.h
    └── weather_types.h
```

### Intended End State

```text
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── app_main.c
├── assistant/
│   ├── boot.c
│   ├── boot.h
│   ├── command_context.h
│   ├── command_handler.h
│   ├── command_registry.c
│   ├── command_registry.h
│   ├── diagnostics.c
│   ├── diagnostics.h
│   ├── presence.c
│   ├── presence.h
│   ├── session.c
│   ├── session.h
│   ├── state.c
│   ├── state.h
│   ├── watchdog.c
│   ├── watchdog.h
│   └── runtime/
│       ├── audio_runtime.h
│       ├── command_runtime.h
│       ├── presence_state.h
│       ├── session_state.h
│       └── watchdog_state.h
├── board/
│   ├── board_audio.c
│   ├── board_audio.h
│   ├── ui_status.h
│   ├── ui_status_display.c
│   ├── ui_status_display.h
│   ├── ui_status_font.c
│   ├── ui_status_font.h
│   ├── ui_status_render.c
│   └── ui_status_render.h
├── commands/
│   ├── assistant_command_dispatch.c
│   ├── assistant_command_dispatch.h
│   ├── assistant_command_text.c
│   ├── assistant_command_text.h
│   └── assistant_commands.h
├── hue/
│   ├── hue_client.c
│   ├── hue_client.h
│   ├── hue_command_handler.c
│   ├── hue_command_handler.h
│   ├── hue_command_map.c
│   ├── hue_command_map.h
│   ├── hue_command_runtime.c
│   ├── hue_command_runtime.h
│   ├── hue_discovery_response.c
│   ├── hue_discovery_response.h
│   ├── hue_group.h
│   ├── hue_group_store.c
│   └── hue_group_store.h
├── idf_component.yml
├── net/
│   ├── http_trace.c
│   ├── http_trace.h
│   ├── line_socket.c
│   ├── line_socket.h
│   └── request_cancel.h
├── stt/
│   ├── local_stt_client.c
│   ├── local_stt_client.h
│   ├── local_stt_protocol.c
│   └── local_stt_protocol.h
├── system/
│   ├── time_support.c
│   ├── time_support.h
│   ├── wifi_support.c
│   └── wifi_support.h
├── timer/
│   ├── timer_command_handler.c
│   ├── timer_command_handler.h
│   ├── timer_parse.c
│   ├── timer_parse.h
│   ├── timer_runtime.c
│   └── timer_runtime.h
├── tts/
│   ├── local_tts_client.c
│   ├── local_tts_client.h
│   ├── tts_player.c
│   └── tts_player.h
└── weather/
    ├── weather_client.c
    ├── weather_client.h
    ├── weather_command_handler.c
    ├── weather_command_handler.h
    ├── weather_format.c
    ├── weather_format.h
    ├── weather_open_meteo_provider.c
    ├── weather_open_meteo_provider.h
    ├── weather_provider.h
    └── weather_types.h
```

### Files Removed Or Replaced In End State

```text
Removed:
- main/box3_assistant.c
- main/assistant_runtime.h                # if runtime split is completed
- main/board/ui_status.c                  # if UI split is completed

Replaced by:
- main/box3_assistant.c -> main/app_main.c + main/assistant/boot.* + main/assistant/session.* +
  main/assistant/watchdog.* + main/assistant/presence.* + main/assistant/command_registry.* +
  feature-owned command handlers in main/hue/, main/timer/, and main/weather/
- main/assistant_runtime.h -> main/assistant/runtime/* headers
- main/board/ui_status.c -> ui_status_display.* + ui_status_render.* + ui_status_font.*
```

## Refactor Phases

## Phase 1: Break Up `box3_assistant.c`

### Objective

Reduce the size and responsibility count of `main/box3_assistant.c` without changing behavior.

### Target files

- `main/assistant/boot.c`
- `main/assistant/boot.h`
- `main/assistant/command_context.h`
- `main/assistant/command_handler.h`
- `main/assistant/command_registry.c`
- `main/assistant/command_registry.h`
- `main/assistant/session.c`
- `main/assistant/session.h`
- `main/assistant/watchdog.c`
- `main/assistant/watchdog.h`
- `main/assistant/presence.c`
- `main/assistant/presence.h`
- `main/hue/hue_command_handler.c`
- `main/hue/hue_command_handler.h`
- `main/timer/timer_command_handler.c`
- `main/timer/timer_command_handler.h`
- `main/weather/weather_command_handler.c`
- `main/weather/weather_command_handler.h`

### Planned moves

Move the following responsibilities out of `main/box3_assistant.c`:

- boot flow and subsystem initialization into `assistant/boot.*`
- wake/listen/execute flow into `assistant/session.*`
- generic command lookup and handler dispatch into `assistant/command_registry.*`
- watchdog timeout and restart logic into `assistant/watchdog.*`
- presence clock and idle timer UI behavior into `assistant/presence.*`
- timer follow-up audio capture, STT call, parse/start/stop helpers into `timer/timer_command_handler.*`
- weather command execution flow into `weather/weather_command_handler.*`
- Hue command execution flow into `hue/hue_command_handler.*`

### Notes

- Keep `app_main()` as a thin composition root.
- For the first pass, allow internal headers and a mostly unchanged runtime struct if that reduces churn.
- Favor moving existing static helpers intact before redesigning APIs.
- The assistant core should depend on a generic command-handler contract rather than feature-specific execution code.
- Feature handlers may decide whether spoken output should occur, but low-level playback should remain behind shared TTS interfaces such as `tts_player.*`.

## Phase 2: Reshape `assistant_runtime_t`

### Objective

Reduce coupling by replacing the flat shared runtime struct with narrower subsystem-owned state.

### Proposed state split

- `main/assistant/runtime/session_state.h`
- `main/assistant/runtime/watchdog_state.h`
- `main/assistant/runtime/audio_runtime.h`
- `main/assistant/runtime/presence_state.h`
- `main/assistant/runtime/command_runtime.h`

### Expected outcomes

- Feature command execution no longer needs to live inside assistant core.
- Presence logic no longer depends directly on speech model internals.
- Watchdog logic reads a dedicated state surface instead of the whole assistant runtime.
- Session execution code can accept narrower pointers and clearer ownership.

### Notes

- Nested structs are a reasonable intermediate step if fully opaque contexts are too disruptive.
- Preserve the current task model until boundaries are stable.

## Phase 3: Standardize Client And Transport Boundaries

### Objective

Reduce duplicated transport and cancellation plumbing across feature modules while keeping feature-specific protocol logic local.

### Candidate shared responsibilities

- active request cancellation handle pattern
- common socket helper extensions under `main/net/`
- shared HTTP trace or request logging helpers
- a common lifecycle pattern for request start, completion, and cancellation
- command cancellation hooks used by feature-owned command handlers

### Non-goals

- Do not build a generic networking framework.
- Do not move feature-specific parsing out of Hue, weather, STT, or TTS modules.

### Expected outcomes

- Less duplicated request lifecycle code in:
  - `main/hue/hue_client.c`
  - `main/weather/weather_open_meteo_provider.c`
  - `main/stt/local_stt_client.c`
  - `main/tts/local_tts_client.c`
- More consistent cancellation behavior across networked features.

## Phase 4: Introduce A Generic Command-Handler Interface

### Objective

Keep `main/assistant/` generic by moving feature-specific command execution back into the feature modules behind a shared interface.

### Planned assistant-side responsibilities

- resolve command ids into generic action metadata
- find the appropriate registered handler
- provide a shared execution context
- invoke `execute(...)`
- use handler cancellation hooks during watchdog recovery when needed

### Planned feature-side responsibilities

- `main/timer/timer_command_handler.*`
  - owns timer voice follow-up flow and timer start/stop command execution
- `main/weather/weather_command_handler.*`
  - owns weather fetch, formatting, spoken-response decisions, and result handling while delegating playback to shared TTS interfaces
- `main/hue/hue_command_handler.*`
  - owns Hue sync and Hue group command execution

### Expected interface shape

The exact types may change, but the intended direction is a small interface similar to:

- `can_handle(command_id or resolved action)`
- `execute(context, command, detail buffer)`
- optional `cancel()`

### Non-goals

- Do not build a plugin system.
- Do not over-generalize command registration.
- Do not move pure timer, weather, or Hue domain logic into assistant core.
- Do not duplicate low-level TTS transport or playback logic inside feature handlers.

## Phase 5: Split `ui_status.c`

### Objective

Separate rendering policy, display control, and asset data inside the board UI implementation.

### Target files

- `main/board/ui_status_render.c`
- `main/board/ui_status_display.c`
- `main/board/ui_status_font.c`
- `main/board/ui_status_font.h`

### Planned moves

- font tables and glyph lookup into `ui_status_font.*`
- framebuffer flush and display power control into `ui_status_display.*`
- layout and status rendering behavior into `ui_status_render.*`

### Notes

- Do this after assistant core boundaries are cleaner.
- Preserve the current public `ui_status.h` API unless there is a clear reason to change it.

## Implementation Plan

This section is the concrete execution plan for carrying out the refactor while keeping the firmware buildable throughout the migration. The entire refactor is intended to land as one overall pull request, but the work should be performed in discrete implementation steps. After each step, stop for manual review, then create a commit before moving to the next step.

## Implementation Sequence

1. Prepare the assistant subtree and internal headers without moving behavior yet.
2. Extract watchdog and presence code out of `main/box3_assistant.c`.
3. Introduce the generic command-handler contract and registry.
4. Move timer command execution into `main/timer/timer_command_handler.*`.
5. Move weather command execution into `main/weather/weather_command_handler.*`.
6. Move Hue command execution into `main/hue/hue_command_handler.*`.
7. Shrink `main/box3_assistant.c` into a thin entrypoint or replace it with `main/app_main.c`.
8. Split `assistant_runtime_t` into narrower runtime headers and state groupings.
9. Extract shared transport helpers only after the feature handlers are in place.
10. Split `main/board/ui_status.c` last.

## Step Plan

### Step 1: Create assistant scaffolding

#### Files to add

- `main/assistant/boot.c`
- `main/assistant/boot.h`
- `main/assistant/session.c`
- `main/assistant/session.h`
- `main/assistant/watchdog.c`
- `main/assistant/watchdog.h`
- `main/assistant/presence.c`
- `main/assistant/presence.h`
- `main/assistant/command_context.h`
- `main/assistant/command_handler.h`
- `main/assistant/command_registry.c`
- `main/assistant/command_registry.h`

#### Files to edit

- `main/CMakeLists.txt`
- `main/box3_assistant.c`

#### Work

- Add empty or minimal files and wire them into the build.
- Move only declarations or thin wrappers first.
- Keep the existing runtime struct and existing command flow behavior unchanged.
- Keep all command execution still routed through `main/box3_assistant.c` for this step.

#### Stop and review

- Confirm the new assistant files compile cleanly and are wired into the build correctly.
- Confirm no runtime behavior changed yet.
- Create a commit after review before starting Step 2.

#### Validation

- run `make format`
- run `make build`

### Step 2: Extract watchdog and presence modules

#### Files to edit

- `main/box3_assistant.c`
- `main/assistant/watchdog.c`
- `main/assistant/watchdog.h`
- `main/assistant/presence.c`
- `main/assistant/presence.h`

#### Function moves

Move or adapt:

- assistant session timeout task logic
- helper logic used only by watchdog timeout handling
- presence clock task logic
- presence sensor init helper if it remains presence-owned

#### Compatibility strategy

- Keep `assistant_runtime_t` intact.
- Pass the full runtime pointer into these modules initially.
- Do not redesign APIs yet beyond what is required to compile.

#### Validation

- run `make format`
- run `make build`

#### Stop and review

- Confirm watchdog and presence code moved without behavior drift.
- Review task startup and task ownership boundaries before continuing.
- Create a commit after review before starting Step 3.

### Step 3: Add generic command-handler interface

#### Files to add or edit

- `main/assistant/command_context.h`
- `main/assistant/command_handler.h`
- `main/assistant/command_registry.c`
- `main/assistant/command_registry.h`
- `main/commands/assistant_command_dispatch.c`
- `main/commands/assistant_command_dispatch.h`
- `main/box3_assistant.c`

#### Work

- Define the generic command handler contract.
- Define the execution context passed from assistant core into feature handlers.
- Add a registry that maps resolved command actions to feature handlers.
- Keep the registry simple and static.
- Do not introduce dynamic plugin registration.

#### Compatibility strategy

- Initially keep existing command execution logic in place and add registry lookups beside it.
- Use the old path as a fallback until the first real handler is migrated.

#### Validation

- run `make format`
- run `make test` if any pure helper logic is added
- run `make build`

#### Stop and review

- Confirm the handler contract is generic and not feature-shaped.
- Confirm the registry remains static and simple.
- Create a commit after review before starting Step 4.

### Step 4: Move timer execution into the timer feature

#### Files to add or edit

- `main/timer/timer_command_handler.c`
- `main/timer/timer_command_handler.h`
- `main/assistant/command_registry.c`
- `main/box3_assistant.c`
- `main/timer/timer_parse.c` only if helper extraction is needed
- `main/timer/timer_runtime.c` only if helper extraction is needed

#### Function moves

Move or adapt:

- timer audio follow-up capture helper
- set-timer command flow
- stop-timer command flow

#### Design constraints

- Keep low-level STT client access in the timer feature handler if it is purely part of timer voice follow-up.
- Keep timer parsing and timer runtime state in `main/timer/`.
- Let the handler decide the command result detail text.

#### Compatibility strategy

- Register only the timer handler first.
- Leave weather and Hue on the old path until their handlers exist.

#### Validation

- run `make format`
- run `make test`
- run `make build`

#### Stop and review

- Confirm timer voice follow-up flow is fully owned by the timer feature.
- Confirm assistant core only sees the generic handler interface.
- Create a commit after review before starting Step 5.

### Step 5: Move weather execution into the weather feature

#### Files to add or edit

- `main/weather/weather_command_handler.c`
- `main/weather/weather_command_handler.h`
- `main/assistant/command_registry.c`
- `main/box3_assistant.c`
- `main/weather/weather_format.c` only if helper extraction is needed
- `main/tts/tts_player.c` only if shared speech helper changes are required

#### Function moves

Move or adapt:

- weather loading status handling
- weather fetch execution flow
- weather result formatting flow
- spoken weather decision logic

#### Design constraints

- Weather decides whether speech should occur.
- Weather owns spoken text generation through existing weather formatting helpers.
- Playback still goes through shared TTS interfaces such as `tts_player.*`.
- TTS playback failure should remain a feature policy choice, not an assistant-core policy.

#### Validation

- run `make format`
- run `make test`
- run `make build`

#### Stop and review

- Confirm weather owns spoken-response decisions but still uses shared TTS playback interfaces.
- Confirm weather success and failure behavior remain correct.
- Create a commit after review before starting Step 6.

### Step 6: Move Hue execution into the Hue feature

#### Files to add or edit

- `main/hue/hue_command_handler.c`
- `main/hue/hue_command_handler.h`
- `main/assistant/command_registry.c`
- `main/box3_assistant.c`
- `main/hue/hue_command_runtime.c` only if helper extraction is needed
- `main/hue/hue_client.c` only if shared helpers are extracted

#### Function moves

Move or adapt:

- Hue sync command execution
- Hue group on or off execution
- Hue-specific error detail selection if it belongs with Hue behavior

#### Design constraints

- Keep bridge probe and request mechanics in Hue-owned modules.
- Avoid pushing Hue-specific error formatting back into assistant core.

#### Validation

- run `make format`
- run `make build`

#### Stop and review

- Confirm Hue-specific behavior and error handling are no longer in assistant core.
- Confirm command execution now spans timer, weather, and Hue through handlers.
- Create a commit after review before starting Step 7.

### Step 7: Reduce `main/box3_assistant.c` to composition only

#### Files to add or edit

- `main/app_main.c` or keep a thin `main/box3_assistant.c`
- `main/assistant/boot.c`
- `main/assistant/session.c`
- `main/CMakeLists.txt`

#### Work

- Move the remaining orchestration entrypoint code into assistant boot and session modules.
- Keep only startup composition and task launch logic in the final top-level entrypoint.
- Remove direct feature execution logic from the top-level file.

#### End condition

At the end of this step:

- top-level assistant control should be split across assistant boot, session, watchdog, and presence modules
- feature execution should happen through the handler registry
- `main/box3_assistant.c` should either be removed or reduced to a tiny wrapper

#### Validation

- run `make format`
- run `make build`

#### Stop and review

- Confirm the top-level entrypoint is now thin and feature-agnostic.
- Decide whether to keep a thin `main/box3_assistant.c` temporarily or replace it with `main/app_main.c`.
- Create a commit after review before starting Step 8.

### Step 8: Split `assistant_runtime_t`

#### Files to add or edit

- `main/assistant/runtime/session_state.h`
- `main/assistant/runtime/watchdog_state.h`
- `main/assistant/runtime/audio_runtime.h`
- `main/assistant/runtime/presence_state.h`
- `main/assistant/runtime/command_runtime.h`
- `main/assistant_runtime.h` during compatibility period
- all assistant subsystem files

#### Work

- Introduce narrower grouped state headers.
- Convert assistant modules to depend on narrower subsets where practical.
- Keep a compatibility layer if needed while call sites are being converted.

#### Compatibility strategy

- `main/assistant_runtime.h` may temporarily aggregate the new headers.
- Remove it only after all assistant modules stop depending on the old flat layout.

#### Validation

- run `make format`
- run `make test` if helper logic changes
- run `make build`

#### Stop and review

- Confirm runtime dependencies are narrower and clearer.
- Confirm compatibility headers still serve any remaining legacy call sites safely.
- Create a commit after review before starting Step 9.

### Step 9: Extract shared transport helpers

#### Files to add or edit

- `main/net/request_cancel.h`
- `main/net/http_trace.c`
- `main/net/http_trace.h`
- `main/hue/hue_client.c`
- `main/weather/weather_open_meteo_provider.c`
- `main/stt/local_stt_client.c`
- `main/tts/local_tts_client.c`

#### Work

- extract repeated active-request cancellation patterns
- extract common HTTP trace or logging helpers only where it reduces real duplication
- keep protocol parsing inside feature modules

#### Validation

- run `make format`
- run `make test` if helper logic becomes host-testable
- run `make build`

#### Stop and review

- Confirm the extracted transport helpers remove real duplication.
- Confirm feature protocol parsing still lives in the feature modules.
- Create a commit after review before starting Step 10.

### Step 10: Split `ui_status.c`

#### Files to add or edit

- `main/board/ui_status_render.c`
- `main/board/ui_status_render.h`
- `main/board/ui_status_display.c`
- `main/board/ui_status_display.h`
- `main/board/ui_status_font.c`
- `main/board/ui_status_font.h`
- `main/board/ui_status.h`
- `main/board/ui_status.c` until removal

#### Work

- move glyph tables and lookup into the font module
- move framebuffer and panel control into display module
- move status and clock rendering policy into render module
- preserve the public `ui_status.h` surface as much as possible

#### Validation

- run `make format`
- run `make build`

#### Stop and review

- Confirm the public `ui_status.h` interface is still coherent.
- Confirm rendering, display control, and font data are separated cleanly.
- Create a commit after review before final cleanup.

## Function Move Map

This map is intentionally high-level. It should be refined when the implementation starts.

### From `main/box3_assistant.c`

- boot and startup wiring
  - move to `main/assistant/boot.c`
- wake/listen/execute loop
  - move to `main/assistant/session.c`
- assistant session timeout task
  - move to `main/assistant/watchdog.c`
- presence clock task and related presence-display helpers
  - move to `main/assistant/presence.c`
- timer follow-up capture and timer command execution
  - move to `main/timer/timer_command_handler.c`
- weather command execution flow
  - move to `main/weather/weather_command_handler.c`
- Hue command execution flow
  - move to `main/hue/hue_command_handler.c`

## Compatibility Rules During Migration

- Keep the firmware building after every step.
- Prefer wrappers and forwarding functions over large all-at-once rewrites.
- Do not remove `main/assistant_runtime.h` until the narrower runtime headers are proven out.
- Do not remove `main/box3_assistant.c` until command execution, watchdog, and presence are already moved.
- Do not split `ui_status.c` until assistant-core churn is done.

## Validation Gates Per Step

- If only file moves and build wiring changed:
  - run `make format`
  - run `make build`
- If host-testable logic changed:
  - run `make format`
  - run `make test`
  - run `make build`
- If TTS, STT, Hue, weather, or session timeout behavior changed:
  - run `make format`
  - run `make build`
  - manually review cancellation and timeout paths in logs when possible

## Execution Strategy

Implement the refactor as a series of behavior-preserving changes:

1. Add tests around any newly extracted pure logic.
2. Move cohesive code into new files with minimal functional change.
3. Introduce internal headers per subsystem.
4. Narrow state access incrementally rather than all at once.
5. Extract shared transport helpers only after the subsystem split makes duplication clearer.
6. Run validation after each phase.

## Validation Expectations

After C/C++ changes:

- run `make format`

After host-testable logic changes:

- run `make test`

After firmware-facing changes:

- run `make build`

## Recommended First Milestone

Start with a low-risk extraction pass:

- extract `main/assistant/watchdog.c/.h`
- extract `main/assistant/presence.c/.h`
- add `main/assistant/command_handler.h` and `main/assistant/command_registry.*`
- move timer execution flow into `main/timer/timer_command_handler.c/.h`
- keep the speech detect state machine in `main/box3_assistant.c` temporarily
- keep `assistant_runtime_t` mostly unchanged for the first pass

This should significantly reduce the size of `main/box3_assistant.c` before deeper API and state redesign work begins.
