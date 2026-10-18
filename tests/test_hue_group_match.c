#include <stdbool.h>

#include "hue/hue_group_match.h"
#include "test_support.h"

static const hue_group_t s_groups[] = {
    {"1", "Living Room"},
    {"2", "Kitchen"},
    {"3", "Bedroom Lights"},
};

static bool test_hue_group_match_exact_and_case_insensitive(void) {
    size_t index = 99;

    ASSERT_TRUE(hue_group_match_find(s_groups, 3, "Kitchen", &index));
    ASSERT_EQ_INT(1, (int) index);

    ASSERT_TRUE(hue_group_match_find(s_groups, 3, "kitchen", &index));
    ASSERT_EQ_INT(1, (int) index);

    ASSERT_TRUE(hue_group_match_find(s_groups, 3, "LIVING ROOM", &index));
    ASSERT_EQ_INT(0, (int) index);
    return true;
}

static bool test_hue_group_match_substring(void) {
    size_t index = 99;

    ASSERT_TRUE(hue_group_match_find(s_groups, 3, "the living room", &index));
    ASSERT_EQ_INT(0, (int) index);

    ASSERT_TRUE(hue_group_match_find(s_groups, 3, "bedroom", &index));
    ASSERT_EQ_INT(2, (int) index);
    return true;
}

static bool test_hue_group_match_rejects_unknown_and_empty(void) {
    size_t index = 99;

    ASSERT_TRUE(!hue_group_match_find(s_groups, 3, "garage", &index));
    ASSERT_TRUE(!hue_group_match_find(s_groups, 3, "", &index));
    ASSERT_TRUE(!hue_group_match_find(s_groups, 3, NULL, &index));
    ASSERT_TRUE(!hue_group_match_find(NULL, 3, "Kitchen", &index));
    return true;
}

static bool test_hue_group_match_prefers_exact_and_closest_length(void) {
    const hue_group_t ambiguous[] = {
        {"1", "Home Office"},
        {"2", "Office"},
    };
    size_t index = 99;

    ASSERT_TRUE(hue_group_match_find(ambiguous, 2, "office", &index));
    ASSERT_EQ_INT(1, (int) index);

    ASSERT_TRUE(hue_group_match_find(ambiguous, 2, "home office", &index));
    ASSERT_EQ_INT(0, (int) index);

    ASSERT_TRUE(hue_group_match_find(ambiguous, 2, "the home office", &index));
    ASSERT_EQ_INT(0, (int) index);
    return true;
}

const test_case_t g_hue_group_match_tests[] = {
    {"hue_group_match_exact_and_case_insensitive", test_hue_group_match_exact_and_case_insensitive},
    {"hue_group_match_substring", test_hue_group_match_substring},
    {"hue_group_match_rejects_unknown_and_empty", test_hue_group_match_rejects_unknown_and_empty},
    {"hue_group_match_prefers_exact_and_closest_length", test_hue_group_match_prefers_exact_and_closest_length},
};

const int g_hue_group_match_test_count = (int) (sizeof(g_hue_group_match_tests) / sizeof(g_hue_group_match_tests[0]));
