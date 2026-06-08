# User Guide

This firmware runs on the ESP32-S3-BOX-3 as a local voice assistant terminal for Hue lights, weather, optional local TTS, dynamic timers, and speaker volume control.

## Wake Word And Commands

The wake word is:

- `Hi ESP`

Always-available commands:

- `update groups from hue`
- `weather today`
- `weather tomorrow`
- `what time is it`
- `current time in`
- `set a timer`
- `stop`
- `volume up by <1-10>`
- `volume down by <1-10>`

Hue group commands are added dynamically after the firmware syncs groups from the Hue bridge:

- `turn on <group>`
- `turn off <group>`

## Startup Flow

After flashing, the firmware:

1. boots on the BOX-3
2. connects to Wi-Fi
3. loads the speech models
4. refreshes Hue groups from the bridge
5. falls back to the last saved group list if Hue refresh fails
6. enters standby and waits for the wake word

The normal interaction flow is:

1. say `Hi ESP`
2. say one command
3. wait for the command to run
4. read the result on screen
5. wait for the device to return to standby

## Hue

On boot, the firmware automatically attempts a Hue group refresh after Wi-Fi connects. Use `update groups from hue` to force another refresh later.

After a successful sync, the firmware fetches Hue groups from the bridge, normalizes the spoken names, saves the accepted groups to the `storage` partition, and rebuilds the active MultiNet command table.

The firmware discovers and caches the Hue bridge address automatically for later boots.

## Weather

Say `weather today` or `weather tomorrow` to fetch the forecast for the configured location.

The weather flow:

1. fetches the requested forecast from the active weather provider over HTTPS
2. displays a multiline weather summary on the BOX-3 screen
3. optionally speaks the forecast when local Piper TTS is configured
4. keeps the weather screen visible for about 30 seconds total, including speech time
5. returns to standby

The default weather provider is Open-Meteo.

The weather screen shows:

- `Now in <location>` for `weather today`, or `Tomorrow in <location>` for `weather tomorrow`
- current condition and current temperature for `weather today`
- daily high and low
- wind speed for `weather today`
- precipitation chance

Weather failures are shown as `Weather network error`, `Weather timeout`, or `Weather unavailable`.

## Time

Say `what time is it` to hear the current time for the configured home location.

Say `current time in` to look up the current time for another city.

The remote time flow:

1. say `Hi ESP`
2. say `current time in`
3. say a city, state, or country, such as `Chicago Illinois` or `London England`
4. wait for the local STT service to transcribe the location
5. hear the local time for the resolved location

Remote time lookup uses the time feature's configured lookup provider and requires a reachable local Wyoming-compatible STT service for the city follow-up phrase.

## Timers

Say `set a timer` to start a timer with a short follow-up phrase.

The timer flow:

1. say `Hi ESP`
2. say `set a timer`
3. say a duration, such as `20 seconds`, `1 minute`, or `1 minute 30 seconds`
4. wait for the local STT service to transcribe the duration
5. watch the countdown on screen
6. say `stop` to stop the alarm after the timer expires

Dynamic timers require a reachable local Wyoming-compatible STT service.

## Volume

Say `volume up by <1-10>` or `volume down by <1-10>` to adjust the speaker. Each step changes the volume by 10 percentage points, and the result is limited to 0-100%.

The configured `CONFIG_TTS_PIPER_VOLUME_PERCENT` value remains the starting volume after each boot. The current percentage is shown beside a speaker icon in the top-left corner of the screen.

## Limits

- only the first 6 usable Hue groups are added as direct voice commands
- synced Hue groups persist across power cycles
- Hue groups may need to be synced again after reflashing if the `storage` partition is erased or rewritten
- spoken weather requires a reachable local Piper-compatible TTS service
- dynamic timers require a reachable local Wyoming-compatible STT service
- remote time lookup requires a reachable local Wyoming-compatible STT service
- only one timer is supported at a time
- timers do not persist across reboot
- the BOX-3 playback path currently uses 16 kHz I2S output to match the active microphone/AFE path

## Recovery

If a weather, Hue, or TTS request stalls, the firmware attempts to cancel the active request first. If that recovery does not finish within a short grace window, the device restarts.

On the next boot, the firmware logs previous command diagnostics and briefly shows a `Prev ...` message on screen when the prior run ended in a notable timeout or reboot during command handling.
