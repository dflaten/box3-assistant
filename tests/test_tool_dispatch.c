#include <stdbool.h>
#include <string.h>

#include "assistant/tool_dispatch.h"
#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "test_support.h"

#define TEST_MAX_TIMER_SECONDS 3600

static const hue_group_t s_groups[] = {
    {"1", "Living Room"},
    {"2", "Kitchen"},
};

static tool_dispatch_result_t resolve(const char *tool, const char *args) {
    return tool_dispatch_resolve(tool, args, s_groups, 2, TEST_MAX_TIMER_SECONDS, ASSISTANT_CMD_VOLUME_STEP_COUNT);
}

static bool test_tool_dispatch_hue_group(void) {
    tool_dispatch_result_t result = resolve("hue_set_group", "{\"group\":\"living room\",\"on\":false}");

    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_HUE_GROUP, result.dispatch.type);
    ASSERT_EQ_INT(0, (int) result.dispatch.group_index);
    ASSERT_TRUE(!result.dispatch.on);

    result = resolve("hue_set_group", "{\"group\":\"Kitchen\",\"on\":true}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(1, (int) result.dispatch.group_index);
    ASSERT_TRUE(result.dispatch.on);
    return true;
}

static bool test_tool_dispatch_hue_group_unknown_and_invalid(void) {
    tool_dispatch_result_t result = resolve("hue_set_group", "{\"group\":\"Garage\",\"on\":true}");
    ASSERT_EQ_INT(TOOL_DISPATCH_UNKNOWN_GROUP, result.status);
    ASSERT_TRUE(strcmp(result.detail, "Garage") == 0);

    result = resolve("hue_set_group", "{\"group\":\"Kitchen\"}");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);
    return true;
}

static bool test_tool_dispatch_timer_clamps_duration(void) {
    tool_dispatch_result_t result = resolve("timer_start", "{\"duration_seconds\":300}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SET_TIMER_DURATION, result.dispatch.type);
    ASSERT_EQ_INT(300, (int) result.dispatch.duration_seconds);

    result = resolve("timer_start", "{\"duration_seconds\":999999}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(TEST_MAX_TIMER_SECONDS, (int) result.dispatch.duration_seconds);

    result = resolve("timer_start", "{\"duration_seconds\":0}");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);

    result = resolve("timer_start", "{\"duration_seconds\":1e10}");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);
    return true;
}

static bool test_tool_dispatch_weather_period(void) {
    tool_dispatch_result_t result = resolve("weather_get", "{\"period\":\"today\"}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TODAY, result.dispatch.type);

    result = resolve("weather_get", "{\"period\":\"tomorrow\"}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW, result.dispatch.type);

    result = resolve("weather_get", "{\"period\":\"next week\"}");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);
    return true;
}

static bool test_tool_dispatch_volume_direction_and_clamp(void) {
    tool_dispatch_result_t result = resolve("volume_adjust", "{\"steps\":3}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_VOLUME_UP, result.dispatch.type);
    ASSERT_EQ_INT(3, result.dispatch.volume_steps);

    result = resolve("volume_adjust", "{\"steps\":-2}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_VOLUME_DOWN, result.dispatch.type);
    ASSERT_EQ_INT(2, result.dispatch.volume_steps);

    result = resolve("volume_adjust", "{\"steps\":-99}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_CMD_VOLUME_STEP_COUNT, result.dispatch.volume_steps);

    result = resolve("volume_adjust", "{\"steps\":0}");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);
    return true;
}

static bool test_tool_dispatch_time_now(void) {
    tool_dispatch_result_t result = resolve("time_now", "{}");
    ASSERT_EQ_INT(TOOL_DISPATCH_OK, result.status);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_TIME_NOW, result.dispatch.type);
    return true;
}

static bool test_tool_dispatch_unknown_and_malformed(void) {
    tool_dispatch_result_t result = resolve("do_the_thing", "{}");
    ASSERT_EQ_INT(TOOL_DISPATCH_UNKNOWN_TOOL, result.status);

    result = resolve("timer_start", "not json");
    ASSERT_EQ_INT(TOOL_DISPATCH_INVALID_ARGS, result.status);

    result = resolve(NULL, "{}");
    ASSERT_EQ_INT(TOOL_DISPATCH_UNKNOWN_TOOL, result.status);
    return true;
}

const test_case_t g_tool_dispatch_tests[] = {
    {"tool_dispatch_hue_group", test_tool_dispatch_hue_group},
    {"tool_dispatch_hue_group_unknown_and_invalid", test_tool_dispatch_hue_group_unknown_and_invalid},
    {"tool_dispatch_timer_clamps_duration", test_tool_dispatch_timer_clamps_duration},
    {"tool_dispatch_weather_period", test_tool_dispatch_weather_period},
    {"tool_dispatch_volume_direction_and_clamp", test_tool_dispatch_volume_direction_and_clamp},
    {"tool_dispatch_time_now", test_tool_dispatch_time_now},
    {"tool_dispatch_unknown_and_malformed", test_tool_dispatch_unknown_and_malformed},
};

const int g_tool_dispatch_test_count = (int) (sizeof(g_tool_dispatch_tests) / sizeof(g_tool_dispatch_tests[0]));
