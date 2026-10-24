#include <stdbool.h>

#include "hue/hue_voice_intent.h"
#include "test_support.h"

static const hue_group_t s_groups[] = {
    {"1", "Living Room"},
    {"2", "Kitchen"},
    {"3", "Bedroom Lights"},
};

static bool parse_and_expect(const char *text, bool expected, size_t expected_index, bool expected_on) {
    hue_voice_intent_t intent = {0};
    bool matched = hue_voice_intent_parse(text, s_groups, 3, &intent);
    if (matched != expected) {
        return false;
    }
    if (!expected) {
        return true;
    }
    return intent.group_index == expected_index && intent.on == expected_on;
}

static bool test_hue_voice_intent_turn_on_exact(void) {
    hue_voice_intent_t intent = {0};

    ASSERT_TRUE(hue_voice_intent_parse("Turn on living room", s_groups, 3, &intent));
    ASSERT_EQ_INT(0, (int) intent.group_index);
    ASSERT_TRUE(intent.on);
    return true;
}

static bool test_hue_voice_intent_turn_off_with_filler(void) {
    hue_voice_intent_t intent = {0};

    ASSERT_TRUE(hue_voice_intent_parse("turn off the kitchen", s_groups, 3, &intent));
    ASSERT_EQ_INT(1, (int) intent.group_index);
    ASSERT_TRUE(!intent.on);
    return true;
}

static bool test_hue_voice_intent_matches_partial_group_name(void) {
    hue_voice_intent_t intent = {0};

    ASSERT_TRUE(hue_voice_intent_parse("turn off bedroom", s_groups, 3, &intent));
    ASSERT_EQ_INT(2, (int) intent.group_index);
    ASSERT_TRUE(!intent.on);

    ASSERT_TRUE(hue_voice_intent_parse("turn on the living room lights", s_groups, 3, &intent));
    ASSERT_EQ_INT(0, (int) intent.group_index);
    ASSERT_TRUE(intent.on);
    return true;
}

static bool test_hue_voice_intent_word_order_and_light_prefix(void) {
    hue_voice_intent_t intent = {0};

    ASSERT_TRUE(hue_voice_intent_parse("turn the bedroom lights on", s_groups, 3, &intent));
    ASSERT_EQ_INT(2, (int) intent.group_index);
    ASSERT_TRUE(intent.on);

    ASSERT_TRUE(hue_voice_intent_parse("lights on in the kitchen", s_groups, 3, &intent));
    ASSERT_EQ_INT(1, (int) intent.group_index);
    ASSERT_TRUE(intent.on);
    return true;
}

static bool test_hue_voice_intent_handles_please_and_punctuation(void) {
    hue_voice_intent_t intent = {0};

    ASSERT_TRUE(hue_voice_intent_parse("Please turn on the kitchen now.", s_groups, 3, &intent));
    ASSERT_EQ_INT(1, (int) intent.group_index);
    ASSERT_TRUE(intent.on);

    ASSERT_TRUE(hue_voice_intent_parse("SWITCH OFF THE LIVING ROOM!", s_groups, 3, &intent));
    ASSERT_EQ_INT(0, (int) intent.group_index);
    ASSERT_TRUE(!intent.on);
    return true;
}

static bool test_hue_voice_intent_rejects_unknown_group(void) {
    ASSERT_TRUE(parse_and_expect("turn on the garage", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on the", false, 0, false));
    return true;
}

static bool test_hue_voice_intent_rejects_questions_and_compound_requests(void) {
    ASSERT_TRUE(parse_and_expect("is the kitchen on", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on the kitchen?", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on the kitchen and turn off the bedroom", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on the kitchen then tell me a joke", false, 0, false));
    ASSERT_TRUE(parse_and_expect("turn on the kitchen when I get home", false, 0, false));
    return true;
}

static bool test_hue_voice_intent_rejects_unrelated_and_empty(void) {
    ASSERT_TRUE(parse_and_expect("what time is it", false, 0, false));
    ASSERT_TRUE(parse_and_expect("", false, 0, false));
    ASSERT_TRUE(parse_and_expect(NULL, false, 0, false));
    ASSERT_TRUE(!hue_voice_intent_parse("turn on the kitchen", NULL, 0, NULL));
    return true;
}

const test_case_t g_hue_voice_intent_tests[] = {
    {"hue_voice_intent_turn_on_exact", test_hue_voice_intent_turn_on_exact},
    {"hue_voice_intent_turn_off_with_filler", test_hue_voice_intent_turn_off_with_filler},
    {"hue_voice_intent_matches_partial_group_name", test_hue_voice_intent_matches_partial_group_name},
    {"hue_voice_intent_word_order_and_light_prefix", test_hue_voice_intent_word_order_and_light_prefix},
    {"hue_voice_intent_handles_please_and_punctuation", test_hue_voice_intent_handles_please_and_punctuation},
    {"hue_voice_intent_rejects_unknown_group", test_hue_voice_intent_rejects_unknown_group},
    {"hue_voice_intent_rejects_questions_and_compound_requests",
     test_hue_voice_intent_rejects_questions_and_compound_requests},
    {"hue_voice_intent_rejects_unrelated_and_empty", test_hue_voice_intent_rejects_unrelated_and_empty},
};

const int g_hue_voice_intent_test_count =
    (int) (sizeof(g_hue_voice_intent_tests) / sizeof(g_hue_voice_intent_tests[0]));
