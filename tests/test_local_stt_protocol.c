#include <string.h>

#include "stt/local_stt_protocol.h"
#include "test_support.h"

static bool test_local_stt_protocol_prefers_trailing_strings(void) {
    char text[64];

    ASSERT_TRUE(local_stt_protocol_select_string("trailing text", "inline text", text, sizeof(text)));
    ASSERT_TRUE(strcmp(text, "trailing text") == 0);

    ASSERT_TRUE(local_stt_protocol_select_string("", "inline text", text, sizeof(text)));
    ASSERT_TRUE(strcmp(text, "inline text") == 0);

    ASSERT_TRUE(!local_stt_protocol_select_string("", "", text, sizeof(text)));
    ASSERT_TRUE(text[0] == '\0');
    return true;
}

static bool test_local_stt_protocol_classifies_events(void) {
    ASSERT_EQ_INT(LOCAL_STT_PROTOCOL_EVENT_TRANSCRIPT, local_stt_protocol_classify_event("transcript", "1 minute", ""));
    ASSERT_EQ_INT(LOCAL_STT_PROTOCOL_EVENT_ERROR, local_stt_protocol_classify_event("error", "", "model unavailable"));
    ASSERT_EQ_INT(LOCAL_STT_PROTOCOL_EVENT_CONTINUE, local_stt_protocol_classify_event("ready", "", ""));
    ASSERT_EQ_INT(LOCAL_STT_PROTOCOL_EVENT_CONTINUE, local_stt_protocol_classify_event("transcript", "", ""));
    return true;
}

const test_case_t g_local_stt_protocol_tests[] = {
    {"local_stt_protocol_prefers_trailing_strings", test_local_stt_protocol_prefers_trailing_strings},
    {"local_stt_protocol_classifies_events", test_local_stt_protocol_classifies_events},
};

const int g_local_stt_protocol_test_count =
    (int) (sizeof(g_local_stt_protocol_tests) / sizeof(g_local_stt_protocol_tests[0]));
