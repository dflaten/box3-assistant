#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "assistant_runtime.h"

esp_err_t assistant_followup_audio_capture_mono(
    assistant_runtime_t *rt, uint32_t capture_ms, uint32_t sample_rate_hz, uint8_t **out_pcm, size_t *out_size);
