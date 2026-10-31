#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Tunables for trailing-silence end-of-capture detection.
 * @note Thresholds are expressed in raw PCM16 amplitude units, so they depend on microphone gain.
 */
typedef struct {
    /** Mono sample rate of the analyzed audio in Hz. */
    uint32_t sample_rate_hz;
    /** Minimum capture time before silence may end the capture. */
    uint32_t min_capture_ms;
    /** Hard cap on capture time, always honored even when speech never goes quiet. */
    uint32_t max_capture_ms;
    /** Trailing silence required after detected speech before the capture stops. */
    uint32_t trailing_silence_ms;
    /** Root-mean-square amplitude at or above which a chunk counts as speech. */
    int32_t rms_threshold;
    /** Root-mean-square amplitude below which a chunk counts toward trailing silence after speech.
     *  Values below rms_threshold are raised to rms_threshold during initialization. */
    int32_t silence_rms_threshold;
} capture_endpoint_config_t;

/**
 * @brief Mutable detector state for one capture.
 */
typedef struct {
    /** Configuration copied at init time. */
    capture_endpoint_config_t config;
    /** Milliseconds of audio analyzed so far. */
    uint32_t elapsed_ms;
    /** Milliseconds of trailing sub-threshold audio after the last speech chunk. */
    uint32_t silent_ms;
    /** Highest chunk root-mean-square amplitude observed during the capture. */
    uint32_t peak_rms;
    /** Lowest chunk root-mean-square amplitude observed during the capture. */
    uint32_t min_rms;
    /** True once any chunk has crossed the speech threshold. */
    bool speech_detected;
} capture_endpoint_t;

/**
 * @brief Initialize an end-of-capture detector.
 * @param endpoint Detector state to initialize.
 * @param config Tunables to copy into the detector, or NULL to zero the state.
 * @return This function does not return a value.
 */
void capture_endpoint_init(capture_endpoint_t *endpoint, const capture_endpoint_config_t *config);

/**
 * @brief Decide whether a captured chunk should end the capture.
 * @param endpoint Detector state updated by this call.
 * @param samples Mono PCM16 samples for the newest chunk.
 * @param sample_count Number of valid samples in the chunk.
 * @return True when capture should stop, otherwise false.
 * @note Speech followed by a configured silence gap stops early; otherwise the max capture time is the only stop.
 */
bool capture_endpoint_should_stop(capture_endpoint_t *endpoint, const int16_t *samples, size_t sample_count);
