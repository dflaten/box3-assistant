#include "assistant/capture_endpoint.h"

#include <stdint.h>
#include <string.h>

/**
 * @brief Compute the mean-square amplitude of a PCM16 chunk.
 * @param samples Mono PCM16 samples to measure.
 * @param sample_count Number of valid samples in the chunk.
 * @return Mean-square amplitude, or zero when the chunk is empty.
 */
static int64_t capture_endpoint_mean_square(const int16_t *samples, size_t sample_count) {
    if (samples == NULL || sample_count == 0) {
        return 0;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t sample = samples[i];
        sum += (int64_t) sample * (int64_t) sample;
    }

    return sum / (int64_t) sample_count;
}

/**
 * @brief Compute the integer square root of a non-negative value.
 * @param value Value to take the square root of.
 * @return Floor of the square root of value.
 * @note Uses an integer-only restoring algorithm so the capture path does not depend on floating point math.
 */
static uint32_t capture_endpoint_integer_sqrt(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t) result;
}

/**
 * @brief Initialize an end-of-capture detector.
 * @param endpoint Detector state to initialize.
 * @param config Tunables to copy into the detector, or NULL to zero the state.
 * @return This function does not return a value.
 */
void capture_endpoint_init(capture_endpoint_t *endpoint, const capture_endpoint_config_t *config) {
    if (endpoint == NULL) {
        return;
    }

    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->min_rms = UINT32_MAX;
    if (config != NULL) {
        endpoint->config = *config;
        /* The silence threshold must not be lower than the speech threshold, or speech would
         * immediately count as silence and end the capture early. */
        if (endpoint->config.silence_rms_threshold < endpoint->config.rms_threshold) {
            endpoint->config.silence_rms_threshold = endpoint->config.rms_threshold;
        }
    }
}

/**
 * @brief Decide whether a captured chunk should end the capture.
 * @param endpoint Detector state updated by this call.
 * @param samples Mono PCM16 samples for the newest chunk.
 * @param sample_count Number of valid samples in the chunk.
 * @return True when capture should stop, otherwise false.
 * @note Speech starts the endpoint, and audio below the silence threshold then accumulates toward the trailing silence
 * window. Audio at or above the silence threshold resets that window. Keeping the silence threshold above the speech
 * threshold makes the endpoint ignore room-noise transients that are loud enough to trip the speech gate but not loud
 * enough to be a real command.
 */
bool capture_endpoint_should_stop(capture_endpoint_t *endpoint, const int16_t *samples, size_t sample_count) {
    if (endpoint == NULL) {
        return true;
    }

    const capture_endpoint_config_t *config = &endpoint->config;
    if (config->max_capture_ms == 0) {
        return true;
    }

    if (samples != NULL && sample_count > 0 && config->sample_rate_hz > 0) {
        int64_t mean_square = capture_endpoint_mean_square(samples, sample_count);
        int64_t threshold_sq = (int64_t) config->rms_threshold * (int64_t) config->rms_threshold;
        int64_t silence_threshold_sq =
            (int64_t) config->silence_rms_threshold * (int64_t) config->silence_rms_threshold;
        uint32_t rms = capture_endpoint_integer_sqrt((uint64_t) mean_square);
        if (rms > endpoint->peak_rms) {
            endpoint->peak_rms = rms;
        }
        if (rms < endpoint->min_rms) {
            endpoint->min_rms = rms;
        }

        bool speech = mean_square >= threshold_sq;
        bool loud = mean_square >= silence_threshold_sq;
        if (speech) {
            endpoint->speech_detected = true;
        }
        if (endpoint->speech_detected) {
            if (loud) {
                endpoint->silent_ms = 0;
            } else {
                endpoint->silent_ms += (uint32_t) (((uint64_t) sample_count * 1000ULL) / config->sample_rate_hz);
            }
        }

        endpoint->elapsed_ms += (uint32_t) (((uint64_t) sample_count * 1000ULL) / config->sample_rate_hz);
    }

    if (endpoint->elapsed_ms >= config->max_capture_ms) {
        return true;
    }

    if (endpoint->speech_detected && endpoint->elapsed_ms >= config->min_capture_ms &&
        endpoint->silent_ms >= config->trailing_silence_ms) {
        return true;
    }

    return false;
}
