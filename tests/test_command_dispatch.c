#include <stdbool.h>

#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"
#include "test_support.h"

static bool test_command_dispatch_resolves_builtin_and_hue_actions(void) {
    assistant_command_dispatch_t dispatch;

    assistant_command_resolve(ASSISTANT_CMD_SYNC_GROUPS, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SYNC_GROUPS, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_WEATHER_TODAY, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TODAY, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_WEATHER_TOMORROW, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_SET_TIMER, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SET_TIMER, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_STOP, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_STOP, dispatch.type);

    assistant_command_resolve(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 2, false), 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_HUE_GROUP, dispatch.type);
    ASSERT_EQ_INT(2, (int) dispatch.group_index);
    ASSERT_TRUE(!dispatch.on);
    return true;
}

static bool test_command_dispatch_unknown_for_out_of_range_ids(void) {
    assistant_command_dispatch_t dispatch = {
        .type = ASSISTANT_COMMAND_ACTION_HUE_GROUP,
        .group_index = 99,
        .on = true,
    };

    assistant_command_resolve(9999, 1, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_UNKNOWN, dispatch.type);
    ASSERT_EQ_INT(0, (int) dispatch.group_index);
    ASSERT_TRUE(!dispatch.on);

    assistant_command_resolve(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), 0, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_UNKNOWN, dispatch.type);
    return true;
}

const test_case_t g_command_dispatch_tests[] = {
    {"command_dispatch_resolves_builtin_and_hue_actions", test_command_dispatch_resolves_builtin_and_hue_actions},
    {"command_dispatch_unknown_for_out_of_range_ids", test_command_dispatch_unknown_for_out_of_range_ids},
};

const int g_command_dispatch_test_count =
    (int) (sizeof(g_command_dispatch_tests) / sizeof(g_command_dispatch_tests[0]));
