#include <stdbool.h>
#include <string.h>

#include "fireworks/fireworks_response_parse.h"
#include "test_support.h"

static bool test_fireworks_response_parse_tool_call(void) {
    const char *json =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"call_1\","
        "\"type\":\"function\",\"function\":{\"name\":\"hue_set_group\",\"arguments\":\"{\\\"group\\\":\\\"living "
        "room\\\",\\\"on\\\":false}\"}}]},\"finish_reason\":\"tool_calls\"}]}";
    fireworks_response_t response;

    ASSERT_TRUE(fireworks_response_parse(json, &response));
    ASSERT_EQ_INT(FIREWORKS_RESPONSE_TOOL_CALL, response.kind);
    ASSERT_TRUE(strcmp(response.tool_name, "hue_set_group") == 0);
    ASSERT_TRUE(strstr(response.tool_arguments, "\"group\":\"living room\"") != NULL);
    return true;
}

static bool test_fireworks_response_parse_answer(void) {
    const char *json =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Superfluous means unnecessary.\"},"
        "\"finish_reason\":\"stop\"}]}";
    fireworks_response_t response;

    ASSERT_TRUE(fireworks_response_parse(json, &response));
    ASSERT_EQ_INT(FIREWORKS_RESPONSE_ANSWER, response.kind);
    ASSERT_TRUE(strcmp(response.answer, "Superfluous means unnecessary.") == 0);
    ASSERT_TRUE(response.tool_name[0] == '\0');
    return true;
}

static bool test_fireworks_response_parse_none_and_invalid(void) {
    const char *empty =
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"\"},\"finish_reason\":\"stop\"}]}";
    fireworks_response_t response;

    ASSERT_TRUE(fireworks_response_parse(empty, &response));
    ASSERT_EQ_INT(FIREWORKS_RESPONSE_NONE, response.kind);

    ASSERT_TRUE(!fireworks_response_parse("not json", &response));
    ASSERT_TRUE(!fireworks_response_parse("{\"choices\":[]}", &response));
    ASSERT_TRUE(!fireworks_response_parse(NULL, &response));
    return true;
}

const test_case_t g_fireworks_response_parse_tests[] = {
    {"fireworks_response_parse_tool_call", test_fireworks_response_parse_tool_call},
    {"fireworks_response_parse_answer", test_fireworks_response_parse_answer},
    {"fireworks_response_parse_none_and_invalid", test_fireworks_response_parse_none_and_invalid},
};

const int g_fireworks_response_parse_test_count =
    (int) (sizeof(g_fireworks_response_parse_tests) / sizeof(g_fireworks_response_parse_tests[0]));
