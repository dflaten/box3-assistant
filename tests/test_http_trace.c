#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "net/http_trace.h"
#include "test_support.h"

static bool test_http_trace_append_accumulates_and_terminates(void) {
    char body[16] = {0};
    http_trace_buffer_t trace = {
        .body = body,
        .capacity = (int) sizeof(body),
        .len = 0,
    };

    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, "abc", 3));
    ASSERT_EQ_INT(3, trace.len);
    ASSERT_TRUE(strcmp(body, "abc") == 0);

    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, "def", 3));
    ASSERT_EQ_INT(6, trace.len);
    ASSERT_TRUE(strcmp(body, "abcdef") == 0);
    return true;
}

static bool test_http_trace_append_truncates_to_capacity(void) {
    char body[6] = {0};
    http_trace_buffer_t trace = {
        .body = body,
        .capacity = (int) sizeof(body),
        .len = 0,
    };

    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, "abcdefghi", 9));
    ASSERT_EQ_INT(5, trace.len);
    ASSERT_TRUE(strcmp(body, "abcde") == 0);

    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, "zzz", 3));
    ASSERT_EQ_INT(5, trace.len);
    ASSERT_TRUE(strcmp(body, "abcde") == 0);
    return true;
}

static bool test_http_trace_append_ignores_invalid_inputs(void) {
    char body[4] = "xy";
    http_trace_buffer_t trace = {
        .body = body,
        .capacity = (int) sizeof(body),
        .len = 2,
    };

    ASSERT_EQ_INT(ESP_OK, http_trace_append(NULL, "a", 1));
    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, NULL, 1));
    ASSERT_EQ_INT(ESP_OK, http_trace_append(&trace, "a", 0));
    ASSERT_EQ_INT(2, trace.len);
    ASSERT_TRUE(strcmp(body, "xy") == 0);
    return true;
}

const test_case_t g_http_trace_tests[] = {
    {"http_trace_append_accumulates_and_terminates", test_http_trace_append_accumulates_and_terminates},
    {"http_trace_append_truncates_to_capacity", test_http_trace_append_truncates_to_capacity},
    {"http_trace_append_ignores_invalid_inputs", test_http_trace_append_ignores_invalid_inputs},
};

const int g_http_trace_test_count = (int) (sizeof(g_http_trace_tests) / sizeof(g_http_trace_tests[0]));
