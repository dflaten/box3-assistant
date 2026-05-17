#include <stdbool.h>

#include "assistant/command_registry.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"
#include "test_support.h"

static const assistant_command_handler_t s_hue_handler = {
    .action = ASSISTANT_COMMAND_ACTION_SYNC_GROUPS,
    .execute = NULL,
};
static const assistant_command_handler_t s_timer_handler = {
    .action = ASSISTANT_COMMAND_ACTION_SET_TIMER,
    .execute = NULL,
};
static const assistant_command_handler_t s_weather_handler = {
    .action = ASSISTANT_COMMAND_ACTION_WEATHER_TODAY,
    .execute = NULL,
};

const assistant_command_handler_t *hue_command_handler_get(void) {
    return &s_hue_handler;
}

const assistant_command_handler_t *timer_command_handler_get(void) {
    return &s_timer_handler;
}

const assistant_command_handler_t *weather_command_handler_get(void) {
    return &s_weather_handler;
}

static bool test_command_registry_routes_builtin_actions_to_feature_handlers(void) {
    assistant_command_dispatch_t dispatch;
    const assistant_command_handler_t *handler = NULL;

    ASSERT_TRUE(assistant_command_registry_lookup(ASSISTANT_CMD_SYNC_GROUPS, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SYNC_GROUPS, dispatch.type);
    ASSERT_TRUE(handler == &s_hue_handler);

    ASSERT_TRUE(assistant_command_registry_lookup(ASSISTANT_CMD_WEATHER_TODAY, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TODAY, dispatch.type);
    ASSERT_TRUE(handler == &s_weather_handler);

    ASSERT_TRUE(assistant_command_registry_lookup(ASSISTANT_CMD_SET_TIMER, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SET_TIMER, dispatch.type);
    ASSERT_TRUE(handler == &s_timer_handler);

    ASSERT_TRUE(assistant_command_registry_lookup(ASSISTANT_CMD_STOP, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_STOP, dispatch.type);
    ASSERT_TRUE(handler == &s_timer_handler);
    return true;
}

static bool test_command_registry_routes_hue_group_actions_and_rejects_unknown(void) {
    assistant_command_dispatch_t dispatch;
    const assistant_command_handler_t *handler = NULL;

    int group_command_id = hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 1, true);
    ASSERT_TRUE(assistant_command_registry_lookup(group_command_id, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_HUE_GROUP, dispatch.type);
    ASSERT_EQ_INT(1, (int) dispatch.group_index);
    ASSERT_TRUE(dispatch.on);
    ASSERT_TRUE(handler == &s_hue_handler);

    ASSERT_TRUE(!assistant_command_registry_lookup(9999, 2, &dispatch, &handler));
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_UNKNOWN, dispatch.type);
    ASSERT_TRUE(handler == NULL);
    return true;
}

const test_case_t g_command_registry_tests[] = {
    {"command_registry_routes_builtin_actions_to_feature_handlers",
     test_command_registry_routes_builtin_actions_to_feature_handlers},
    {"command_registry_routes_hue_group_actions_and_rejects_unknown",
     test_command_registry_routes_hue_group_actions_and_rejects_unknown},
};

const int g_command_registry_test_count =
    (int) (sizeof(g_command_registry_tests) / sizeof(g_command_registry_tests[0]));
