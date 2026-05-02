#include <stdio.h>

#include "test_support.h"

extern const test_case_t g_assistant_state_tests[];
extern const int g_assistant_state_test_count;
extern const test_case_t g_command_dispatch_tests[];
extern const int g_command_dispatch_test_count;
extern const test_case_t g_command_text_tests[];
extern const int g_command_text_test_count;
extern const test_case_t g_hue_discovery_response_tests[];
extern const int g_hue_discovery_response_test_count;
extern const test_case_t g_local_stt_protocol_tests[];
extern const int g_local_stt_protocol_test_count;
extern const test_case_t g_weather_format_tests[];
extern const int g_weather_format_test_count;
extern const test_case_t g_timer_parse_tests[];
extern const int g_timer_parse_test_count;
extern const test_case_t g_timer_runtime_tests[];
extern const int g_timer_runtime_test_count;

static int s_failures;
static int s_tests_run;

static void run_test(const char *name, bool (*fn)(void)) {
    s_tests_run++;
    if (!fn()) {
        s_failures++;
        fprintf(stderr, "FAILED: %s\n", name);
        return;
    }
    printf("PASS: %s\n", name);
}

static void run_test_cases(const test_case_t *tests, int count) {
    for (int i = 0; i < count; ++i) {
        run_test(tests[i].name, tests[i].fn);
    }
}

int main(void) {
    run_test_cases(g_assistant_state_tests, g_assistant_state_test_count);
    run_test_cases(g_command_dispatch_tests, g_command_dispatch_test_count);
    run_test_cases(g_command_text_tests, g_command_text_test_count);
    run_test_cases(g_hue_discovery_response_tests, g_hue_discovery_response_test_count);
    run_test_cases(g_local_stt_protocol_tests, g_local_stt_protocol_test_count);
    run_test_cases(g_timer_parse_tests, g_timer_parse_test_count);
    run_test_cases(g_timer_runtime_tests, g_timer_runtime_test_count);
    run_test_cases(g_weather_format_tests, g_weather_format_test_count);

    if (s_failures != 0) {
        fprintf(stderr, "%d/%d tests failed\n", s_failures, s_tests_run);
        return 1;
    }

    printf("%d tests passed\n", s_tests_run);
    return 0;
}
