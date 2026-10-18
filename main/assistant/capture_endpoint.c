#include "assistant/capture_endpoint.h"

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
    if (config != NULL) {
        endpoint->config = *config;
    }
}

/**
 * @brief Decide whether a captured chunk should end the capture.
 * @param endpoint Detector state updated by this call.
 * @param samples Mono PCM16 samples for the newest chunk.
 * @param sample_count Number of valid samples in the chunk.
 * @return True when capture should stop, otherwise false.
 * @note Speech followed by a configured silence gap stops early; otherwise the max capture time is the only stop.
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
        int64_t threshold_sq = (int64_t) config->rms_threshold * (int64_t) config->rms_threshold;
        bool speech = capture_endpoint_mean_square(samples, sample_count) >= threshold_sq;
        if (speech) {
            endpoint->speech_detected = true;
            endpoint->silent_ms = 0;
        } else if (endpoint->speech_detected) {
            endpoint->silent_ms += (uint32_t) (((uint64_t) sample_count * 1000ULL) / config->sample_rate_hz);
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
