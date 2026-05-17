#pragma once

#include <stdbool.h>

#include "esp_afe_sr_iface.h"
#include "esp_codec_dev.h"

/**
 * @brief Audio capture and AFE state shared by assistant session tasks.
 */
typedef struct {
    /** Set to pause microphone feeding while command execution or recovery work is in progress. */
    volatile bool pause_audio_feed;
    /** True while the audio feed task is between its pause check and the end of the current read/feed cycle. */
    volatile bool audio_feed_busy;
    /** True once the audio feed task has observed pause_audio_feed and entered its paused loop. */
    volatile bool audio_feed_paused;
    /** Audio front-end interface used for fetch/feed/reset operations. */
    const esp_afe_sr_iface_t *afe_handle;
    /** Audio front-end instance created from the selected AFE configuration. */
    esp_afe_sr_data_t *afe_data;
    /** Open microphone codec handle for BOX-3 audio capture. */
    esp_codec_dev_handle_t mic_codec;
} assistant_audio_runtime_t;
