#include <stdbool.h>
#include <string.h>

#include "commands/assistant_command_text.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"
#include "hue/hue_group.h"
#include "test_support.h"

static bool test_command_text_labels_builtin_and_hue_commands(void) {
    hue_group_t groups[] = {
        {.id = "1", .name = "kitchen"},
        {.id = "2", .name = "desk"},
    };
    char text[96];

    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_SYNC_GROUPS, groups, 2, text, sizeof(text)),
                       "Update groups from Hue") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_WEATHER_TODAY, groups, 2, text, sizeof(text)),
                       "Weather today") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_WEATHER_TOMORROW, groups, 2, text, sizeof(text)),
                       "Weather tomorrow") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_SET_TIMER, groups, 2, text, sizeof(text)), "Set a timer") ==
                0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_STOP, groups, 2, text, sizeof(text)), "Stop") == 0);

    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 2, text, sizeof(text)),
                       "Turn on kitchen") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 1, false), groups, 2, text, sizeof(text)),
                       "Turn off desk") == 0);
    return true;
}

static bool test_command_text_unknown_for_invalid_ids(void) {
    hue_group_t groups[] = {
        {.id = "1", .name = "kitchen"},
    };
    char text[96];

    ASSERT_TRUE(strcmp(assistant_command_text(9999, groups, 1, text, sizeof(text)), "Unknown command") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 0, text, sizeof(text)),
                       "Unknown command") == 0);
    ASSERT_TRUE(
        strcmp(assistant_command_text(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 1, NULL, 0),
               "Unknown command") == 0);
    return true;
}

const test_case_t g_command_text_tests[] = {
    {"command_text_labels_builtin_and_hue_commands", test_command_text_labels_builtin_and_hue_commands},
    {"command_text_unknown_for_invalid_ids", test_command_text_unknown_for_invalid_ids},
};

const int g_command_text_test_count = (int) (sizeof(g_command_text_tests) / sizeof(g_command_text_tests[0]));
