#include <stdint.h>

#include "board/ui_status_render.h"
#include "test_support.h"

#define TEST_SCREEN_WIDTH       320
#define TEST_SCREEN_HEIGHT      240
#define TEST_FRAMEBUFFER_PIXELS (TEST_SCREEN_WIDTH * TEST_SCREEN_HEIGHT)
#define TEST_CLEAR_START_Y      25
#define TEST_CLEAR_END_Y        39

static uint16_t s_frame_buffer[TEST_FRAMEBUFFER_PIXELS];

/**
 * @brief Provide a deterministic Wi-Fi indicator level for renderer tests.
 * @return Wi-Fi signal level shown by the test renderer.
 */
uint8_t wifi_signal_level(void) {
    return 4;
}

/**
 * @brief Check that framebuffer rows are filled with a single color.
 * @param start_y First row to inspect.
 * @param end_y Last row to inspect.
 * @return True when every pixel in the row range has the same color.
 */
static bool rows_are_solid_color(int start_y, int end_y) {
    const uint16_t color = s_frame_buffer[(size_t) start_y * TEST_SCREEN_WIDTH];

    for (int y = start_y; y <= end_y; ++y) {
        for (int x = 0; x < TEST_SCREEN_WIDTH; ++x) {
            if (s_frame_buffer[((size_t) y * TEST_SCREEN_WIDTH) + x] != color) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Verify status text leaves clear margin below the icon row.
 * @return True when the status screen keeps the reserved rows clear.
 */
static bool status_text_starts_below_icon_margin(void) {
    ui_status_render_status(s_frame_buffer, UI_STATUS_LISTENING, NULL, 50);

    ASSERT_TRUE(rows_are_solid_color(TEST_CLEAR_START_Y, TEST_CLEAR_END_Y));

    return true;
}

/**
 * @brief Verify clock text leaves clear margin below the icon row.
 * @return True when the clock screen keeps the reserved rows clear.
 */
static bool clock_text_starts_below_icon_margin(void) {
    ui_status_render_clock(s_frame_buffer, "12:34", "Sat Jul 11", "Test City", 50);

    ASSERT_TRUE(rows_are_solid_color(TEST_CLEAR_START_Y, TEST_CLEAR_END_Y));

    return true;
}

const test_case_t g_ui_status_render_tests[] = {
    {"status text starts below icon margin", status_text_starts_below_icon_margin},
    {"clock text starts below icon margin", clock_text_starts_below_icon_margin},
};

const int g_ui_status_render_test_count =
    (int) (sizeof(g_ui_status_render_tests) / sizeof(g_ui_status_render_tests[0]));
