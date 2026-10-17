#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "fireworks/fireworks_request.h"
#include "test_support.h"

static bool tool_schema_contains(const cJSON *tools, const char *name) {
    const cJSON *tool = NULL;
    cJSON_ArrayForEach(tool, tools) {
        const cJSON *function = cJSON_GetObjectItemCaseSensitive(tool, "function");
        const cJSON *name_item = cJSON_GetObjectItemCaseSensitive(function, "name");
        if (cJSON_IsString(name_item) && strcmp(name_item->valuestring, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool test_fireworks_tools_respect_integration_flags(void) {
    char *all = fireworks_request_build_tools_json(true, true, true, true);
    ASSERT_TRUE(all != NULL);

    cJSON *tools = cJSON_Parse(all);
    ASSERT_TRUE(cJSON_IsArray(tools));
    ASSERT_TRUE(tool_schema_contains(tools, "hue_set_group"));
    ASSERT_TRUE(tool_schema_contains(tools, "timer_start"));
    ASSERT_TRUE(tool_schema_contains(tools, "weather_get"));
    ASSERT_TRUE(tool_schema_contains(tools, "volume_adjust"));
    cJSON_Delete(tools);
    free(all);

    char *hue_only = fireworks_request_build_tools_json(true, false, false, false);
    ASSERT_TRUE(hue_only != NULL);
    tools = cJSON_Parse(hue_only);
    ASSERT_TRUE(tool_schema_contains(tools, "hue_set_group"));
    ASSERT_TRUE(!tool_schema_contains(tools, "timer_start"));
    ASSERT_TRUE(!tool_schema_contains(tools, "weather_get"));
    cJSON_Delete(tools);
    free(hue_only);
    return true;
}

static bool test_fireworks_system_prompt_lists_groups(void) {
    const hue_group_t groups[] = {
        {"1", "Living Room"},
        {"2", "Kitchen"},
    };
    char prompt[FIREWORKS_SYSTEM_PROMPT_MAX_LEN];

    fireworks_request_build_system_prompt(groups, 2, prompt, sizeof(prompt));

    ASSERT_TRUE(strstr(prompt, "Living Room") != NULL);
    ASSERT_TRUE(strstr(prompt, "Kitchen") != NULL);

    fireworks_request_build_system_prompt(NULL, 0, prompt, sizeof(prompt));
    ASSERT_TRUE(strstr(prompt, "no configured Hue groups") != NULL);
    return true;
}

static bool test_fireworks_request_body_includes_messages_and_tools(void) {
    char *tools = fireworks_request_build_tools_json(true, false, false, false);
    ASSERT_TRUE(tools != NULL);

    char *body = fireworks_request_build_body("test-model", "system text", "turn off the lights", tools, 128);
    ASSERT_TRUE(body != NULL);

    cJSON *root = cJSON_Parse(body);
    ASSERT_TRUE(cJSON_IsObject(root));

    const cJSON *model = cJSON_GetObjectItemCaseSensitive(root, "model");
    ASSERT_TRUE(cJSON_IsString(model));
    ASSERT_TRUE(strcmp(model->valuestring, "test-model") == 0);

    const cJSON *messages = cJSON_GetObjectItemCaseSensitive(root, "messages");
    ASSERT_TRUE(cJSON_IsArray(messages));
    ASSERT_EQ_INT(2, cJSON_GetArraySize(messages));

    const cJSON *tools_item = cJSON_GetObjectItemCaseSensitive(root, "tools");
    ASSERT_TRUE(cJSON_IsArray(tools_item));

    const cJSON *tool_choice = cJSON_GetObjectItemCaseSensitive(root, "tool_choice");
    ASSERT_TRUE(cJSON_IsString(tool_choice));

    cJSON_Delete(root);
    free(body);
    free(tools);
    return true;
}

static bool test_fireworks_request_body_without_tools(void) {
    char *body = fireworks_request_build_body("test-model", "system text", "hello", NULL, 0);
    ASSERT_TRUE(body != NULL);

    cJSON *root = cJSON_Parse(body);
    ASSERT_TRUE(cJSON_IsObject(root));
    ASSERT_TRUE(cJSON_GetObjectItemCaseSensitive(root, "tools") == NULL);

    const cJSON *max_tokens = cJSON_GetObjectItemCaseSensitive(root, "max_tokens");
    ASSERT_TRUE(cJSON_IsNumber(max_tokens));
    ASSERT_EQ_INT(256, max_tokens->valueint);

    cJSON_Delete(root);
    free(body);
    return true;
}

static bool test_fireworks_build_url_normalizes_base(void) {
    char url[256];

    fireworks_request_build_url("https://api.fireworks.ai/inference/v1", url, sizeof(url));
    ASSERT_TRUE(strcmp(url, "https://api.fireworks.ai/inference/v1/chat/completions") == 0);

    fireworks_request_build_url("https://api.fireworks.ai/inference/v1/", url, sizeof(url));
    ASSERT_TRUE(strcmp(url, "https://api.fireworks.ai/inference/v1/chat/completions") == 0);

    fireworks_request_build_url("https://api.fireworks.ai/inference/v1/completions", url, sizeof(url));
    ASSERT_TRUE(strcmp(url, "https://api.fireworks.ai/inference/v1/chat/completions") == 0);

    fireworks_request_build_url("https://api.fireworks.ai/inference/v1/chat/completions", url, sizeof(url));
    ASSERT_TRUE(strcmp(url, "https://api.fireworks.ai/inference/v1/chat/completions") == 0);

    return true;
}

const test_case_t g_fireworks_request_tests[] = {
    {"fireworks_tools_respect_integration_flags", test_fireworks_tools_respect_integration_flags},
    {"fireworks_system_prompt_lists_groups", test_fireworks_system_prompt_lists_groups},
    {"fireworks_request_body_includes_messages_and_tools", test_fireworks_request_body_includes_messages_and_tools},
    {"fireworks_request_body_without_tools", test_fireworks_request_body_without_tools},
    {"fireworks_build_url_normalizes_base", test_fireworks_build_url_normalizes_base},
};

const int g_fireworks_request_test_count =
    (int) (sizeof(g_fireworks_request_tests) / sizeof(g_fireworks_request_tests[0]));
