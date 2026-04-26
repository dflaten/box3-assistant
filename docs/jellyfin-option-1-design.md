# Jellyfin Integration Design

## Goal

Add Jellyfin integration to `box3-assistant` as a control-only feature.

In this design, the ESP32-S3-BOX-3 does not act as the media player. It remains a networked voice assistant terminal that:

- listens for wake word and fixed spoken commands locally
- sends control requests to a Jellyfin server over the LAN
- targets an existing Jellyfin playback client for actual playback
- shows simple playback state on the BOX-3 screen

This matches the current project direction in `README.md` and fits the existing firmware shape better than trying to turn the BOX-3 into a full media renderer.

## Non-Goals

This design does not include:

- direct audio playback on the BOX-3
- Bluetooth audio output from the BOX-3
- video playback on the BOX-3
- open-ended natural-language media search
- multi-room synchronization

Those can be considered later, but they should not be part of the first Jellyfin integration.

## Required Hardware

Assumptions:

- a working Jellyfin server already exists on the local network
- the BOX-3 is connected to the same network

Required hardware for this option:

- ESP32-S3-BOX-3 running `box3-assistant`
- one existing Jellyfin playback endpoint
- a Raspberry Pi or mini PC running a Jellyfin client

Important constraint:

- the BOX-3 is only the controller in this option
- the external Jellyfin client is what actually plays the media

If the user wants to listen through a Bluetooth speaker, that speaker must pair to the playback endpoint, not to the BOX-3.

## User Experience

### Target Interaction

Example flow:

1. user says `Hi ESP`
2. BOX-3 wakes
3. user says `play favorites on living room tv`
4. BOX-3 sends a Jellyfin request
5. the named Jellyfin client begins playback
6. BOX-3 shows now-playing status and returns to standby

### Initial Supported Commands

The first version should focus on deterministic commands only.

Recommended fixed commands:

- `sync jellyfin devices`
- `what is playing`
- `pause media`
- `resume media`
- `stop media`
- `next item`
- `previous item`

Recommended dynamic commands after sync/cache:

- `play favorites`
- `play favorites on <device>`
- `play playlist <name>`
- `play album <name>`
- `play artist <name>`

The command set should stay small at first because the current firmware uses runtime phrase generation and fixed command recognition rather than open-ended speech understanding.

## Architecture

This feature should follow the same high-level pattern already used for Hue:

- service client module for HTTP calls
- small persistent store for synced device and media metadata
- runtime speech phrase generation from cached names
- simple execution path from recognized command to network action
- optional spoken confirmations through the existing `tts_player_speak()` facade, not direct Piper calls

### Proposed Modules

Suggested additions:

- `main/jellyfin/jellyfin_client.c`
- `main/jellyfin/jellyfin_client.h`
- `main/jellyfin/jellyfin_store.c`
- `main/jellyfin/jellyfin_store.h`
- `main/jellyfin/jellyfin_types.h`

Responsibilities:

- `jellyfin_client`
  - authenticate with Jellyfin using configured credentials or API token
  - fetch users, sessions, and selected media lists
  - send playback control requests to a target session
  - fetch now-playing state for UI feedback

- `jellyfin_store`
  - persist discovered playback devices
  - persist normalized spoken names for devices and selected media items
  - preserve a default playback target across reboots

- `jellyfin_types`
  - shared structs for devices, media items, and session state

- `box3_assistant.c`
  - add Jellyfin commands to the runtime command table
  - route recognized commands to Jellyfin actions
  - update UI status based on control outcome

- existing `main/tts/tts_player.c`
  - optional generic spoken confirmation path
  - keeps Jellyfin code independent of Piper socket protocol details

## Configuration

Add `menuconfig` options for Jellyfin connectivity and defaults.

Recommended config items:

- `CONFIG_JELLYFIN_ENABLED`
- `CONFIG_JELLYFIN_BASE_URL`
- `CONFIG_JELLYFIN_API_KEY`
- `CONFIG_JELLYFIN_USER_ID`
- `CONFIG_JELLYFIN_DEFAULT_DEVICE_ID`
- `CONFIG_JELLYFIN_HTTP_TIMEOUT_MS`
- `CONFIG_JELLYFIN_MAX_SYNCED_DEVICES`
- `CONFIG_JELLYFIN_MAX_SYNCED_MEDIA_ITEMS`

If the API key is sufficient for the chosen server setup, that is the simplest first approach. Avoid building a more complex login flow unless it is required.

## Data Model

The first version only needs a narrow cached model.

### Playback Device

Each synced playback endpoint should store:

- Jellyfin device or session identifier
- human-readable device name
- normalized spoken name
- capability flags if available
- last-seen timestamp or activity marker

### Media Cache

The first version should cache only a small command-friendly subset:

- favorites
- a limited number of playlists
- a limited number of albums
- a limited number of artists

Each cached media item should store:

- item identifier
- item type
- display name
- normalized spoken name

This mirrors the current Hue approach: keep only what is needed to build reliable spoken commands.

## Jellyfin Integration Strategy

### Device Sync

The first sync path should:

1. query Jellyfin for active sessions and available playback targets
2. filter to usable endpoints
3. normalize names into simple spoken forms
4. reject duplicates or unusable names
5. store the results in flash-backed storage
6. rebuild the runtime command table

This should behave similarly to the current Hue group sync flow.

### Playback Targeting

The design should support two targeting modes:

- default target device
- explicitly named target device

If a command does not specify a device, the firmware should use the configured default target.

If no default target exists and no device is specified, the firmware should fail clearly and display a message such as `No Jellyfin player selected`.

### Media Selection

The first release should avoid full library search from the ESP32.

Instead:

- sync a small list of commandable items from the server
- build spoken commands only from that list
- send direct playback requests using cached item identifiers

This keeps memory usage bounded and avoids trying to do fuzzy search on-device.

## Firmware Flow Changes

### 1. Add Jellyfin Command IDs

Extend the current command ID layout in `main/box3_assistant.c` with a reserved Jellyfin range.

For example:

- fixed control commands
- device-targeted commands
- media item play commands

The exact numeric layout is not important as long as decoding stays deterministic.

### 2. Add Jellyfin Runtime Phrase Builder

Follow the existing `rebuild_command_table()` approach:

- add static phrases like `pause media`
- add dynamic phrases derived from cached devices and media items

Examples:

- `play favorites`
- `play favorites on living room tv`
- `play playlist chill mix`
- `play album random access memories`

As with Hue, all names should be normalized before becoming spoken commands.

### 3. Add Jellyfin Command Execution Path

When a Jellyfin command is recognized:

1. resolve target device
2. resolve target media item if needed
3. call the appropriate `jellyfin_client` function
4. update the screen with success or failure
5. return to standby

### 4. Add Now-Playing Status

The first version should support a simple status request:

- command: `what is playing`

This should query the active target session and show:

- title
- artist or series name if available
- playback state such as playing or paused

This can be text-only on the BOX-3 display.

## UI Changes

The UI should remain minimal.

Recommended states:

- `Standby`
- `Listening`
- `Syncing Jellyfin`
- `Controlling Jellyfin`
- `Playing`
- `Paused`
- `Jellyfin Error`

The goal is confirmation and troubleshooting, not a full media browser UI.

## Storage

Use the existing SPIFFS-backed storage pattern already used by `hue_group_store`.

Suggested files:

- `/jellyfin_devices.json`
- `/jellyfin_media.json`
- `/jellyfin_settings.json`

Stored data should remain small and bounded. Do not persist large library snapshots.

## Error Handling

The Jellyfin path should fail cleanly in the following cases:

- server unreachable
- authentication failure
- no active or valid playback target found
- cached target no longer exists
- media item no longer exists
- duplicate or unusable spoken names

Expected behavior:

- log the detailed failure
- show a short user-facing message on screen
- return to standby without destabilizing wake-word flow

## Security

Do not commit Jellyfin credentials or API keys to tracked files.

Follow the same local configuration pattern already documented in `README.md`:

- `sdkconfig`
- `sdkconfig.defaults.local`

Prefer API-token-based access for the first version if the server setup allows it.

## Implementation Plan

### Phase 1: Foundation

1. Add Kconfig entries for Jellyfin connectivity and limits.
2. Add `jellyfin_types` definitions.
3. Add `jellyfin_store` for devices, media cache, and default target persistence.
4. Add `jellyfin_client` skeleton with shared HTTP helper code similar to `hue_client`.

### Phase 2: Device Control MVP

1. Implement Jellyfin auth header handling.
2. Implement device/session discovery.
3. Implement `sync jellyfin devices`.
4. Implement fixed control commands:
   - pause
   - resume
   - stop
   - next
   - previous
   - what is playing
5. Add default target device behavior.
6. Add UI feedback and storage persistence.

This phase is the first useful milestone and should be completed before adding media launch commands.

### Phase 3: Media Launch Commands

1. Add sync path for a bounded media cache:
   - favorites
   - selected playlists
   - selected albums
   - selected artists
2. Normalize names and reject duplicates.
3. Add dynamic spoken commands for cached media.
4. Add device-targeted play variants such as `play favorites on living room tv`.

### Phase 4: Refinement

1. Add better status strings and error reporting.
2. Improve device selection behavior when the default player is offline.
3. Consider periodic refresh or manual refresh for device/session state.
4. Consider websocket or event-based session updates only if polling proves insufficient.

## Testing Plan

### Unit/Module-Level

Test:

- spoken name normalization
- duplicate name rejection
- cache serialization and deserialization
- command ID encode/decode helpers

### Firmware Integration

Test on real hardware:

1. boot and connect to Wi-Fi
2. sync Jellyfin devices
3. verify stored devices survive reboot
4. issue pause/resume/stop/next/previous commands
5. verify `what is playing`
6. verify default-device fallback
7. verify clear failure when the target device is offline

### Command Recognition Validation

Test with realistic spoken names to confirm:

- device names are recognized reliably
- playlist and album names are not too long or ambiguous
- runtime phrase count stays within practical limits

## Risks And Constraints

### 1. Jellyfin API and Session Semantics

Different clients may expose slightly different control behavior. The design should expect some playback actions to work better on some client types than others.

### 2. Speech Phrase Explosion

If too many devices or media items are added to the command table, recognition quality and memory use may degrade.

Mitigation:

- cap synced items
- keep the vocabulary small
- prefer curated media subsets

### 3. Ambiguous Spoken Names

Two similar device names or album names may normalize to the same spoken form.

Mitigation:

- reject duplicates during sync
- require the user to rename devices or adjust the cache source if needed

### 4. Offline Playback Targets

A cached device may no longer be active or reachable.

Mitigation:

- validate targets before issuing playback actions
- present a short UI error
- allow manual re-sync

## Recommendation

Implement the Jellyfin feature in this order:

1. device sync plus fixed playback controls
2. now-playing query
3. bounded media cache and dynamic play commands

This keeps the first milestone small, useful, and consistent with the current firmware architecture.

It also avoids the biggest trap for this project: trying to make the ESP32-S3-BOX-3 become a full media playback device when the current codebase and hardware direction are much better suited to being a low-latency voice control terminal.
