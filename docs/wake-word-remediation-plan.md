# Wake Word Remediation Plan

This plan addresses poor `Hi ESP` wake-word recall on the ESP32-S3-BOX-3. It is
deliberately split into independent chunks so each one can be built, flashed,
lived with for a while, and then kept or reverted before the next one starts.

The chunks are ordered by expected benefit against effort. The first few are
small configuration or wiring changes that can be evaluated quickly. The later
chunks are larger changes that should only be attempted if the earlier ones do
not solve the problem.

## Current Baseline

| Item | Current value | Location |
| --- | --- | --- |
| Wake word model | `wn10_hiesp` | `sdkconfig.defaults`, `main/assistant/session.c` |
| AFE WakeNet mode | `DET_MODE_95` (single-channel; WakeNet10 has no 2CH modes) | `main/assistant/session.c` |
| WakeNet trigger threshold | `60` (0.60, trial) | `sdkconfig.defaults` |
| Microphone gain | `24 dB` (trial) | `sdkconfig.defaults` |
| ESP-SR version | `2.6.1` | `main/idf_component.yml` |
| ESP-DL version | `3.3.14` (3.3.15 regresses; 3.3.12 does not link) | `main/idf_component.yml` |
| AFE input | `MM` (two microphone channels) | `main/assistant/session.c` |
| Feed buffer | PSRAM first, internal RAM fallback | `main/assistant/session.c` |
| Feed task | core 0, priority 6 | `main/assistant/session.c` |

Relevant code:

- `main/assistant/session.c` — model selection, AFE configuration, threshold,
  audio feed task, speech-detect task.
- `main/board/board_audio.c` — microphone stream format and gain.
- `main/Kconfig.projbuild` — wake threshold and microphone gain knobs.
- `main/idf_component.yml` — ESP-SR dependency version.

## How To Run Each Chunk

1. Make only the changes for the current chunk on its own branch.
2. Run `make format` if C files changed, then `make test` and `make build`.
3. Flash and use the device normally for a few days. Do not stack the next chunk
   on top until the current one has a verdict.
4. Record the verdict in the status table below: better, same, or worse, plus
   any false-wake change.
5. Keep the chunk if it helped. Revert it if it did not, then move to the next
   chunk from a known-good baseline.
6. When a chunk is accepted, update `docs/configuration.md` so the tracked
   defaults describe what is actually shipping.

Suggested verdict scale:

- **Better** — wakes reliably enough to use normally.
- **Same** — no noticeable change.
- **Worse** — more misses, more false wakes, or new instability.

## Delivery Rounds

Chunks 1-3 are additive and have been shipped together. Chunk 4 cannot be
combined with chunk 1 on the old model because it changes the same WakeNet mode
in the opposite direction; with WakeNet10 the single-channel mode is already in
use, so chunk 4 is effectively moot.

| Round | Chunks | Notes |
| --- | --- | --- |
| 1 | 1 + 2 + 3 | Implemented, flashed, and awaiting a verdict. |
| 2 | 4 | Only relevant if we revert to WakeNet9. Not needed while `wn10_hiesp` is in use. |
| 3 | 5 | Reliability work; additive but changes task priority and recovery behavior. |
| 4 | 6 | Wake phrase change. |
| 5 | 7 | Fallback only. |

Even when combining a round, keep the changes as separate commits and a separate
`idf.py build`/flash. If the combined result is worse, revert commits one at a
time rather than the whole round.

## Chunk Status

| Chunk | Change | Status | Verdict |
| --- | --- | --- | --- |
| 1 | WakeNet sensitivity pass | Implemented — awaiting verdict | |
| 2 | Microphone gain pass | Implemented — awaiting verdict | |
| 3 | ESP-SR 2.6.1 and `wn10_hiesp` | Implemented — awaiting verdict | |
| 4 | Dual-mic versus single-mic path | Moot while WakeNet10 is active | |
| 5 | Audio feed reliability | Not started | |
| 6 | Wake phrase change or custom model | Not started | |
| 7 | Fallback: always-on AFE / hardware review | Not started | |

## Chunk 1 — WakeNet Sensitivity Pass

**Goal:** Raise the detector's recall before changing hardware paths or models.

**Changes:**

1. Change the AFE WakeNet mode from `DET_MODE_2CH_90` to `DET_MODE_2CH_95` in
   `main/assistant/session.c`:
   ```c
   afe_cfg->wakenet_mode = DET_MODE_2CH_95;
   ```
2. Set an explicit trigger threshold in `sdkconfig.defaults`:
   ```text
   CONFIG_ASSISTANT_WAKE_WORD_THRESHOLD=60
   ```
   This activates the existing `set_wakenet_threshold()` block, which is
   currently compiled out because the value is `0`. The boot log will then show
   `WakeNet trigger threshold set to 0.60`.

**Why this is first:** It is a small, low-risk change that directly targets
recall. `DET_MODE_2CH_95` is the more aggressive WakeNet operating point, and
lowering the cutoff from about 0.63 to 0.60 makes real utterances easier to
accept. It keeps the existing dual-microphone path, so it tests sensitivity and
not the microphone geometry.

**Status:** Implemented and flashed. The boot log reports
`WakeNet trigger threshold set to 0.60`. The mode was originally tested with
`wn9_hiesp` at `DET_MODE_2CH_95`; chunk 3 has since moved the active model to
`wn10_hiesp` with the same `0.60` cutoff.

**Interaction with the model upgrade:** This chunk shipped with chunks 2 and 3.
A cutoff of `0.60` is below the mode-95 defaults for both models: about `0.635`
for `wn9_hiesp` and about `0.623` for `wn10_hiesp`. It therefore remains a
recall-increasing value after the model upgrade. If the effective mode ends up
being `90`, `0.60` is above `wn10`'s mode-90 default (about `0.587`) and would
reduce recall. Check the boot log's WakeNet mode line after flashing.

**Validation:** Use the device normally and note whether it wakes more reliably
and whether it now wakes from ordinary conversation or room noise.

**Keep if:** Recall improves and false wakes remain tolerable.

**Back off if false wakes appear, in this order:**

1. Raise the threshold to `63` or `66` while keeping `DET_MODE_2CH_95`.
2. Restore `DET_MODE_2CH_90` while keeping the threshold at `60`.

**Rollback:** Restore `DET_MODE_2CH_90` and `CONFIG_ASSISTANT_WAKE_WORD_THRESHOLD=0`.

## Chunk 2 — Microphone Gain Pass

**Goal:** Rule out clipping or an over-hot microphone path as the reason WakeNet
scores real utterances poorly.

**Changes:** Lower the microphone gain in `sdkconfig.defaults` from the
tracked `30` to `24`:

```text
CONFIG_HUE_MIC_GAIN_DB=24
```

If `24` helps but does not fully solve it, try `18` as a second step. Do not go
below `12` without a reason; the far-field level will drop too much.

**Status:** Implemented and flashed with chunk 1; the device is currently
running at `24 dB`.

**Why this is second:** `30 dB` is a hot input for close speech. The ES7210
driver already sets `30 dB` when it opens, and the application then sets the same
value again. If normal speech is reaching the codec near full scale, the AFE and
WakeNet see flattened audio, which hurts detection. This is a one-line trial.

**Validation:** Use the device at normal speaking distance and from across the
room. Note whether near speech wakes more reliably and whether far speech gets
worse.

**Keep if:** Near-field waking improves without losing usable far-field range.

**Rollback:** Restore `CONFIG_HUE_MIC_GAIN_DB=30`.

**Note:** This chunk is independent of chunk 1. If both are kept, re-check false
wakes once, because the two changes compound.

## Chunk 3 — ESP-SR 2.6.1 And `wn10_hiesp`

**Goal:** Move to the newer WakeNet10 model, which is the largest available
accuracy improvement for this hardware.

**Status: implemented.** The working pairing is `esp-sr 2.6.1` + `esp-dl
3.3.14` + `wn10_hiesp`. The device boots cleanly with:

```text
Using WakeNet model: wn10_hiesp
WAKENET10: MC Quantized wakenetx: wakenet10_tts4h12q8p1_hiesp_3_0.5872_0.6228, tigger:v4, mode:4
AFE Pipeline: [input] -> |SE(BSS)| -> |VAD(WebRTC)| -> |WakeNet(wn10_hiesp,)| -> [output]
WakeNet trigger threshold set to 0.60
```

The version pin matters:

- `esp-dl 3.3.15` boot-loops with `assert failed: block_trim_free ... block must
  be free` inside `wakenet::WakeNetX::build_model()` / `wakenetx_create`. This
  reproduced after a full clean build, so it is a real 3.3.15 regression with
  WakeNet10.
- `esp-dl 3.3.12` does not link with esp-sr 2.6.1; it is missing the
  `dl::audio::SpeechFeatureBase` vtable.
- `esp-dl 3.3.13` and `3.3.14` are the remaining candidates; `3.3.14` is the
  newest that both links and boots. Pin it explicitly.

The model pack itself was correct: the generated `wn10_data` was byte-identical
to the `wn10_data_p1` source.

**Changes:**

1. Update `main/idf_component.yml`:
   ```yaml
   espressif/esp-sr:
     version: '~2.6.1'
   espressif/esp-dl:
     # 3.3.15 hits a heap assertion in wakenetx_create with wn10_hiesp; 3.3.14 is the newest working release.
     version: '3.3.14'
   ```
2. Regenerate dependencies and `sdkconfig`:
   ```fish
   source "$HOME/.espressif/v6.0.2/esp-idf/export.fish"
   idf.py update-dependencies
   make config-local
   ```
3. In `sdkconfig.defaults`, disable the old model and enable the new one:
   ```text
   # CONFIG_SR_WN_WN9_HIESP is not set
   CONFIG_SR_WN_WN10_HIESP=y
   ```
4. Keep the existing model lookup in `main/assistant/session.c`:
   ```c
   char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hiesp");
   ```
   Only one `hiesp` model should be packed into the model partition after step 3.
5. Use a single-channel detection mode with WakeNet10. `wn10_hiesp` does not
   accept the 2CH modes; `DET_MODE_95` is the safe choice while the AFE still
   uses both microphones for its front-end processing.

**Why this is third:** ESP-SR 2.6.1 adds `wn10_hiesp` and includes an explicit
fix for a missing `wn10_hiesp` kernel in 2.6.0. WakeNet10 is newer and is
expected to generalize better than WakeNet9. The upgrade was originally held
back after the `esp-dl 3.3.15` crash; it is now implemented with `esp-dl
3.3.14`.

**Interaction with the threshold:** `wn10_hiesp` ships with different default
cutoffs than `wn9_hiesp`: about `0.587` for mode 90 and `0.623` for mode 95.
The explicit `0.60` from chunk 1 is still a recall-increasing value in mode 95,
but it is *above* the mode-90 default. If the upgrade makes recall worse, first
confirm the effective mode in the boot log, then try resetting
`CONFIG_ASSISTANT_WAKE_WORD_THRESHOLD=0` to use the model default before
concluding the model is the problem.

**Validation:**

- Boot log shows `Using WakeNet model: wn10_hiesp`.
- The model partition contains only the expected `hiesp` model.
- Normal use shows improved recall with acceptable false wakes.
- Check boot and idle heap/PSRAM headroom for regressions; `wn10` is larger
  than `wn9`.

**Risks:**

- `esp-dl 3.3.15` regresses with WakeNet10, so the manifest must keep the
  explicit `3.3.14` pin. Re-check this pin when a newer esp-dl is released.
- ESP-SR 2.6.x updates `dl_fft` and `esp-dl` dependencies, which can create
  version conflicts with other components.
- The model is larger, so the 4 MB `model` partition and PSRAM headroom must be
  checked.
- AFE v1 is still supported in 2.6.x, but the component update should be built
  and flashed as a unit.

**Rollback:** Revert `main/idf_component.yml`, `sdkconfig.defaults`, and
`dependencies.lock`, then run `idf.py update-dependencies` and
`IDF_TARGET=esp32s3 make config-local` again. The known-good fallback before
this chunk was `esp-sr 2.3.1` + `wn9_hiesp` + `DET_MODE_2CH_95`.

## Chunk 4 — Dual-Mic Versus Single-Mic Path

**Goal:** Determine whether the two-microphone WakeNet path is helping or
hurting. Only relevant if we fall back to WakeNet9; with `wn10_hiesp` the
single-channel detector mode is already in use, so this chunk is currently
moot.

**Status:** Not applicable while `wn10_hiesp` is active. WakeNet10 has no 2CH
modes, and `DET_MODE_95` is already set. The AFE still uses both microphones for
its BSS stage, so the hardware array has not been abandoned.

**Change A — test a single-channel detector path:**

In `main/assistant/session.c`, keep the `MM` input format but disable the
separation stage and use the single-channel aggressive mode:

```c
afe_cfg->wakenet_mode = DET_MODE_95;
afe_cfg->se_init = false;
```

If AFE still selects between the two microphones through MISO, that is fine for
this test. The point is to stop asking WakeNet to work in two-channel mode.

**Change B — force the first microphone only, if needed:**

If Change A is ambiguous, feed only microphone 0 by switching the AFE input
format to `M` and de-interleaving the microphone read. This is a larger change
and should be a separate follow-up, not part of the first test.

**Why this is fourth:** Two-channel WakeNet depends on a properly matched
microphone array. The BOX-3 has two microphones, but if their spacing, level, or
phase response is marginal, two-channel detection can be worse than a clean
single-channel signal. This chunk isolates that variable.

**Do not combine with chunk 1:** Chunk 1 sets `DET_MODE_2CH_95`; this chunk sets
`DET_MODE_95` with `se_init = false`. Those are alternatives, not additive
changes. Combining them would either overwrite one setting or leave the effective
mode ambiguous, so this chunk only runs as its own round after round 1 has a
verdict.

**Validation:** Same normal-use test as chunk 1. If recall improves, the array
path was the weak link.

**Keep if:** Single-channel is clearly better.

**Rollback:** Restore `DET_MODE_2CH_95` and `se_init = true` (or the AFE default).

## Chunk 5 — Audio Feed Reliability

**Goal:** Remove intermittent sample loss and silent AFE stalls that can make the
device miss a wake word for long periods.

**Changes:**

1. Allocate the feed buffer from internal DMA-capable RAM instead of PSRAM in
   `main/assistant/session.c`:
   ```c
   int16_t *feed_buffer = heap_caps_malloc(feed_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
   ```
   Keep the existing fallback only if the internal allocation fails.
2. Treat a non-`NULL` fetch result with `ret_value == ESP_FAIL` as a failed
   fetch, in addition to `NULL` or `data == NULL`.
3. Count fetch failures even when the assistant is idle. After a small number of
   consecutive failures, log a warning and recover the AFE with:
   ```c
   afe_handle->reset_buffer(afe_data);
   afe_handle->enable_wakenet(afe_data);
   ```
   This closes the current gap where an idle AFE can stall forever without
   triggering the watchdog, because the heartbeat is refreshed before `fetch()`.
4. Raise the audio feed task priority from `6` to `10` in
   `assistant_session_start_tasks()`. Keep it on core 0; core 1 already runs the
   AFE fetch, network, STT, and TTS work.
5. Optional, only if feed overruns are still suspected: initialize the BSP audio
   with a deeper I2S receive DMA ring before the microphone codec is opened.

**Why this is fifth:** These are real robustness gaps, but they only explain
intermittent misses. The changes are more invasive than chunks 1–3, so they
should not be mixed into the first sensitivity trials.

**Validation:** Normal use over several days, especially after long idle periods
and after TTS playback. Watch for `Microphone read failed`, `AFE feed failed`,
and the new recovery warning.

**Keep if:** Misses become less frequent or the new recovery log never appears
but behavior is unchanged.

**Rollback:** Revert the feed buffer and priority changes; the failure handling
can stay if it is harmless.

## Chunk 6 — Wake Phrase Change Or Custom Model

**Goal:** Use a wake phrase with better acoustic distinctiveness if `Hi ESP`
still performs poorly.

**Changes:**

1. Pick a bundled model from the ESP-SR list. Examples:
   - `wn10_heyhermes`
   - `wn10_heynova`
   - `wn10_alexa`
   - `wn9_computer_tts`
   - `wn9_himfive`
2. Enable the matching `CONFIG_SR_WN_*` symbol in `sdkconfig.defaults` and disable
   `CONFIG_SR_WN_WN10_HIESP`.
3. Update the model filter suffix in `main/assistant/session.c`, the `WAKE_WORD`
   string, `docs/configuration.md`, and `docs/user-guide.md`.
4. Alternatively, train a custom wake word through the ESP-SR TTS pipeline and
   add it as an external model. This is a larger project and should only be
   considered if no bundled phrase is acceptable.

**Why this is sixth:** Changing the phrase changes the product experience and
requires documentation and user retraining. `Hi ESP` is a short two-syllable
phrase, so it is inherently easier to miss than a longer or more distinctive
phrase.

**Validation:** Normal use for several days with the new phrase. Compare recall
and false wakes against the best result from chunks 1–5.

**Keep if:** The new phrase is clearly more reliable and acceptable to say.

**Rollback:** Restore the previous model and wake word text.

## Chunk 7 — Fallback: Always-On AFE / Hardware Review

Only pursue this if chunks 1–6 all fail.

1. Keep the AFE running during playback with a proper playback reference channel
   (`R`) and AEC enabled, instead of pausing the microphone feed. This lets the
   device hear the wake word during and immediately after speech playback.
2. Review the physical setup:
   - microphone openings are not covered
   - the device is not coupled to the speaker or a vibrating surface
   - the device is not directly against a wall or under a shelf
   - normal speaking distance and room noise are reasonable
3. If the unit is older or has been handled roughly, compare against a second
   BOX-3 to rule out a hardware or microphone-seal problem.

## Things That Are Not Bugs

Do not spend time on these unless the behavior differs from the description:

- **Timer alarm:** when a timer alarm is active, the firmware intentionally
  disables WakeNet and listens only for `stop`. The wake word will not work until
  the alarm is stopped. See `main/assistant/presence.c` and
  `main/timer/timer_command_handler.c`.
- **Active session:** the wake word is disabled while the assistant is capturing,
  thinking, or speaking, and is re-enabled when it returns to standby.
- **Pronunciation:** the model is trained on `Hi ESP` as the letters
  `E-S-P`. `Hey ESP`, a mumbled `hi esp`, or saying `ESP` as one word will score
  lower. Ask the user to say `Hi E-S-P` clearly when evaluating any chunk.
- **False wakes versus misses:** every change trades one for the other. Record
  both when giving a verdict.

## Follow-Up Documentation

When a chunk is accepted:

- update `docs/configuration.md` with the new tracked values and any new caveats
- update `docs/user-guide.md` if the wake phrase or expected behavior changes
- keep `main/Kconfig.projbuild` help text in sync with the actual defaults
- leave a short note in the chunk status table with the verdict and the date
