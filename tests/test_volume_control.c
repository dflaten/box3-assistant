#include <stdbool.h>

#include "test_support.h"
#include "commands/assistant_commands.h"
#include "volume/volume_command_map.h"
#include "volume/volume_control.h"

static bool test_volume_control_adjusts_in_ten_percent_steps(void) {
    ASSERT_EQ_INT(60, volume_control_adjust(50, 1, true));
    ASSERT_EQ_INT(20, volume_control_adjust(50, 3, false));
    ASSERT_EQ_INT(100, volume_control_adjust(0, 10, true));
    return true;
}

static bool test_volume_control_clamps_at_valid_limits(void) {
    ASSERT_EQ_INT(100, volume_control_adjust(90, 4, true));
    ASSERT_EQ_INT(0, volume_control_adjust(20, 5, false));
    ASSERT_EQ_INT(100, volume_control_adjust(100, 1, true));
    ASSERT_EQ_INT(0, volume_control_adjust(0, 1, false));
    return true;
}

static bool test_volume_command_map_decodes_direction_and_steps(void) {
    uint8_t steps = 0;
    bool increase = false;

    ASSERT_EQ_INT(ASSISTANT_CMD_VOLUME_UP_BASE + 6, volume_command_id(ASSISTANT_CMD_VOLUME_UP_BASE, 7));
    ASSERT_TRUE(volume_decode_command_id(ASSISTANT_CMD_VOLUME_UP_BASE + 9,
                                         ASSISTANT_CMD_VOLUME_UP_BASE,
                                         ASSISTANT_CMD_VOLUME_DOWN_BASE,
                                         ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                         &steps,
                                         &increase));
    ASSERT_EQ_INT(10, steps);
    ASSERT_TRUE(increase);

    ASSERT_TRUE(volume_decode_command_id(ASSISTANT_CMD_VOLUME_DOWN_BASE + 2,
                                         ASSISTANT_CMD_VOLUME_UP_BASE,
                                         ASSISTANT_CMD_VOLUME_DOWN_BASE,
                                         ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                         &steps,
                                         &increase));
    ASSERT_EQ_INT(3, steps);
    ASSERT_TRUE(!increase);

    ASSERT_TRUE(!volume_decode_command_id(ASSISTANT_CMD_VOLUME_DOWN_BASE + ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                          ASSISTANT_CMD_VOLUME_UP_BASE,
                                          ASSISTANT_CMD_VOLUME_DOWN_BASE,
                                          ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                          NULL,
                                          NULL));
    return true;
}

const test_case_t g_volume_control_tests[] = {
    {"volume_control_adjusts_in_ten_percent_steps", test_volume_control_adjusts_in_ten_percent_steps},
    {"volume_control_clamps_at_valid_limits", test_volume_control_clamps_at_valid_limits},
    {"volume_command_map_decodes_direction_and_steps", test_volume_command_map_decodes_direction_and_steps},
};

const int g_volume_control_test_count = (int) (sizeof(g_volume_control_tests) / sizeof(g_volume_control_tests[0]));
