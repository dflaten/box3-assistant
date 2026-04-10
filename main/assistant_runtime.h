#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

#include "esp_afe_sr_iface.h"
#include "esp_codec_dev.h"
#include "esp_mn_iface.h"

#include "hue/hue_group.h"

#define ASSISTANT_MAX_SYNCED_GROUPS HUE_GROUP_MAX_COUNT

typedef enum {
    ASSISTANT_STAGE_STANDBY = 0,
    ASSISTANT_STAGE_LISTENING,
    ASSISTANT_STAGE_EXECUTING,
} assistant_stage_t;

/**
 * @brief Shared in-memory runtime state for the assistant firmware.
 * @note A single instance is created in app_main() and passed to task entry points and helpers.
 */
typedef struct {
    /** True while the assistant is inside an active wake/listen/execute session. */
    bool assistant_awake;
    /** True after the dynamic MultiNet command table has been allocated at least once. */
    bool commands_allocated;
    /** Set to pause microphone feeding while command execution or recovery work is in progress. */
    volatile bool pause_audio_feed;
    /** True while the audio feed task is between its pause check and the end of the current read/feed cycle. */
    volatile bool audio_feed_busy;
    /** True once the audio feed task has observed pause_audio_feed and entered its paused loop. */
    volatile bool audio_feed_paused;
    /** Tick count captured when the current assistant session began. */
    TickType_t assistant_awake_tick;
    /** Tick count updated by the audio feed task to prove it is still running. */
    volatile TickType_t audio_feed_heartbeat_tick;
    /** Tick count updated by the speech detect task to prove it is still running. */
    volatile TickType_t speech_detect_heartbeat_tick;
    /** Tick count updated by the presence clock task to prove it is still running. */
    volatile TickType_t presence_clock_heartbeat_tick;
    /** Tick count updated whenever the speech pipeline makes forward progress. */
    volatile TickType_t speech_progress_tick;
    /** High-level stage used for watchdog diagnostics. */
    volatile assistant_stage_t assistant_stage;
    /** Most recent command id being executed, or 0 when idle. */
    volatile int current_command_id;
    /** True after the watchdog has requested timeout recovery for the active execution. */
    volatile bool execution_timeout_pending;
    /** Tick count captured when timeout recovery was first requested. */
    volatile TickType_t execution_timeout_tick;
    /** MultiNet interface selected from the ESP-SR model bundle. */
    esp_mn_iface_t *multinet;
    /** Opaque model instance owned by the selected MultiNet interface. */
    model_iface_data_t *model_data;
    /** Audio front-end interface used for fetch/feed/reset operations. */
    const esp_afe_sr_iface_t *afe_handle;
    /** Audio front-end instance created from the selected AFE configuration. */
    esp_afe_sr_data_t *afe_data;
    /** Open microphone codec handle for BOX-3 audio capture. */
    esp_codec_dev_handle_t mic_codec;
    /** Runtime Hue groups currently available for spoken on/off commands. */
    hue_group_t groups[ASSISTANT_MAX_SYNCED_GROUPS];
    /** Number of valid entries currently stored in groups[]. */
    size_t group_count;
} assistant_runtime_t;
