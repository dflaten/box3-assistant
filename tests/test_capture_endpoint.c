#include <stdbool.h>
#include <string.h>

#include "assistant/capture_endpoint.h"
#include "test_support.h"

#define TEST_RATE_HZ   1000
#define TEST_CHUNK_LEN 50

static void fill_chunk(int16_t *samples, size_t count, int16_t amplitude) {
    for (size_t i = 0; i < count; ++i) {
        samples[i] = amplitude;
    }
}

static bool test_capture_endpoint_stops_after_trailing_silence(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 100,
        .rms_threshold = 100,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t chunk[TEST_CHUNK_LEN];

    fill_chunk(chunk, TEST_CHUNK_LEN, 1000);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(endpoint.speech_detected);

    fill_chunk(chunk, TEST_CHUNK_LEN, 0);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    return true;
}

static bool test_capture_endpoint_ignores_sub_threshold_noise(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 200,
        .trailing_silence_ms = 50,
        .rms_threshold = 100,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t chunk[TEST_CHUNK_LEN];
    fill_chunk(chunk, TEST_CHUNK_LEN, 50);

    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(!endpoint.speech_detected);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    return true;
}

static bool test_capture_endpoint_honors_min_capture(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 200,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 50,
        .rms_threshold = 100,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t speech[TEST_CHUNK_LEN];
    int16_t silence[TEST_CHUNK_LEN];
    fill_chunk(speech, TEST_CHUNK_LEN, 1000);
    fill_chunk(silence, TEST_CHUNK_LEN, 0);

    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, speech, TEST_CHUNK_LEN));
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, silence, TEST_CHUNK_LEN));
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, silence, TEST_CHUNK_LEN));
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, silence, TEST_CHUNK_LEN));
    return true;
}

static bool test_capture_endpoint_accepts_box3_speech_levels(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 100,
        /* Matches the tracked BOX-3 defaults: speech around 150-700 RMS and noise well below. */
        .rms_threshold = 50,
        .silence_rms_threshold = 150,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t chunk[TEST_CHUNK_LEN];

    fill_chunk(chunk, TEST_CHUNK_LEN, 10);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(!endpoint.speech_detected);
    ASSERT_EQ_INT(10, (int) endpoint.peak_rms);

    fill_chunk(chunk, TEST_CHUNK_LEN, 200);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(endpoint.speech_detected);
    ASSERT_EQ_INT(200, (int) endpoint.peak_rms);
    return true;
}

static bool test_capture_endpoint_tracks_peak_rms(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 100,
        .rms_threshold = 50,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t chunk[TEST_CHUNK_LEN];
    fill_chunk(chunk, TEST_CHUNK_LEN, 500);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(500, (int) endpoint.peak_rms);

    fill_chunk(chunk, TEST_CHUNK_LEN, 0);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(500, (int) endpoint.peak_rms);
    return true;
}

static bool test_capture_endpoint_silence_threshold_controls_reset(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 100,
        .rms_threshold = 50,
        .silence_rms_threshold = 150,
    };
    capture_endpoint_init(&endpoint, &config);

    int16_t chunk[TEST_CHUNK_LEN];

    fill_chunk(chunk, TEST_CHUNK_LEN, 500);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(endpoint.speech_detected);
    ASSERT_EQ_INT(0, (int) endpoint.silent_ms);

    /* Above the speech gate but below the silence threshold: counts toward silence. */
    fill_chunk(chunk, TEST_CHUNK_LEN, 120);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(50, (int) endpoint.silent_ms);

    /* At or above the silence threshold resets the trailing-silence window. */
    fill_chunk(chunk, TEST_CHUNK_LEN, 200);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(0, (int) endpoint.silent_ms);

    fill_chunk(chunk, TEST_CHUNK_LEN, 10);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(50, (int) endpoint.silent_ms);
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_EQ_INT(100, (int) endpoint.silent_ms);
    return true;
}

static bool test_capture_endpoint_normalizes_silence_threshold(void) {
    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {
        .sample_rate_hz = TEST_RATE_HZ,
        .min_capture_ms = 0,
        .max_capture_ms = 100000,
        .trailing_silence_ms = 50,
        .rms_threshold = 100,
        .silence_rms_threshold = 0,
    };
    capture_endpoint_init(&endpoint, &config);
    ASSERT_EQ_INT(100, (int) endpoint.config.silence_rms_threshold);

    int16_t chunk[TEST_CHUNK_LEN];
    fill_chunk(chunk, TEST_CHUNK_LEN, 1000);
    ASSERT_TRUE(!capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    ASSERT_TRUE(endpoint.speech_detected);

    fill_chunk(chunk, TEST_CHUNK_LEN, 50);
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    return true;
}

static bool test_capture_endpoint_rejects_empty_configuration(void) {
    int16_t chunk[TEST_CHUNK_LEN];
    fill_chunk(chunk, TEST_CHUNK_LEN, 1000);

    ASSERT_TRUE(capture_endpoint_should_stop(NULL, chunk, TEST_CHUNK_LEN));

    capture_endpoint_t endpoint;
    capture_endpoint_config_t config = {0};
    capture_endpoint_init(&endpoint, &config);
    ASSERT_TRUE(capture_endpoint_should_stop(&endpoint, chunk, TEST_CHUNK_LEN));
    return true;
}

const test_case_t g_capture_endpoint_tests[] = {
    {"capture_endpoint_stops_after_trailing_silence", test_capture_endpoint_stops_after_trailing_silence},
    {"capture_endpoint_ignores_sub_threshold_noise", test_capture_endpoint_ignores_sub_threshold_noise},
    {"capture_endpoint_honors_min_capture", test_capture_endpoint_honors_min_capture},
    {"capture_endpoint_accepts_box3_speech_levels", test_capture_endpoint_accepts_box3_speech_levels},
    {"capture_endpoint_tracks_peak_rms", test_capture_endpoint_tracks_peak_rms},
    {"capture_endpoint_silence_threshold_controls_reset", test_capture_endpoint_silence_threshold_controls_reset},
    {"capture_endpoint_normalizes_silence_threshold", test_capture_endpoint_normalizes_silence_threshold},
    {"capture_endpoint_rejects_empty_configuration", test_capture_endpoint_rejects_empty_configuration},
};

const int g_capture_endpoint_test_count =
    (int) (sizeof(g_capture_endpoint_tests) / sizeof(g_capture_endpoint_tests[0]));
