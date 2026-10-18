#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "assistant/answer_pager.h"
#include "test_support.h"

static bool test_answer_pager_keeps_short_answer_on_one_page(void) {
    char pages[ANSWER_PAGE_MAX_COUNT][ANSWER_PAGE_LEN];
    bool truncated = true;

    size_t count = answer_pager_split("Superfluous means unnecessary.", pages, ANSWER_PAGE_MAX_COUNT, &truncated);

    ASSERT_EQ_INT(1, (int) count);
    ASSERT_TRUE(strcmp(pages[0], "Superfluous means unnecessary.") == 0);
    ASSERT_TRUE(!truncated);
    return true;
}

static bool test_answer_pager_wraps_on_word_boundaries(void) {
    char pages[ANSWER_PAGE_MAX_COUNT][ANSWER_PAGE_LEN];
    bool truncated = true;

    /* Build a sentence longer than one page out of distinct words. */
    char text[512];
    text[0] = '\0';
    for (int i = 0; i < 40; ++i) {
        char word[8];
        snprintf(word, sizeof(word), "word%02d ", i);
        strncat(text, word, sizeof(text) - strlen(text) - 1);
    }

    size_t count = answer_pager_split(text, pages, ANSWER_PAGE_MAX_COUNT, &truncated);

    ASSERT_TRUE(count >= 2);
    ASSERT_TRUE(!truncated);
    ASSERT_TRUE(strlen(pages[0]) <= ANSWER_PAGE_MAX_CHARS);
    ASSERT_TRUE(pages[0][strlen(pages[0]) - 1] != ' ');
    ASSERT_TRUE(strstr(pages[0], "word00") != NULL);
    ASSERT_TRUE(strstr(pages[count - 1], "word39") != NULL);
    return true;
}

static bool test_answer_pager_hard_splits_long_word(void) {
    char pages[ANSWER_PAGE_MAX_COUNT][ANSWER_PAGE_LEN];
    bool truncated = false;

    char text[ANSWER_PAGE_MAX_CHARS + 30];
    memset(text, 'z', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';

    size_t count = answer_pager_split(text, pages, ANSWER_PAGE_MAX_COUNT, &truncated);

    ASSERT_EQ_INT(2, (int) count);
    ASSERT_EQ_INT(ANSWER_PAGE_MAX_CHARS, (int) strlen(pages[0]));
    ASSERT_TRUE(!truncated);
    return true;
}

static bool test_answer_pager_reports_truncation_and_handles_empty(void) {
    char pages[ANSWER_PAGE_MAX_COUNT][ANSWER_PAGE_LEN];
    bool truncated = false;

    char text[ANSWER_PAGE_MAX_CHARS * (ANSWER_PAGE_MAX_COUNT + 2)];
    memset(text, 'a', sizeof(text) - 1);
    for (size_t i = 1; i < sizeof(text) - 1; i += 2) {
        text[i] = ' ';
    }
    text[sizeof(text) - 1] = '\0';

    size_t count = answer_pager_split(text, pages, ANSWER_PAGE_MAX_COUNT, &truncated);

    ASSERT_EQ_INT(ANSWER_PAGE_MAX_COUNT, (int) count);
    ASSERT_TRUE(truncated);

    truncated = true;
    count = answer_pager_split("", pages, ANSWER_PAGE_MAX_COUNT, &truncated);
    ASSERT_EQ_INT(0, (int) count);
    ASSERT_TRUE(!truncated);

    count = answer_pager_split(NULL, pages, ANSWER_PAGE_MAX_COUNT, NULL);
    ASSERT_EQ_INT(0, (int) count);
    return true;
}

const test_case_t g_answer_pager_tests[] = {
    {"answer_pager_keeps_short_answer_on_one_page", test_answer_pager_keeps_short_answer_on_one_page},
    {"answer_pager_wraps_on_word_boundaries", test_answer_pager_wraps_on_word_boundaries},
    {"answer_pager_hard_splits_long_word", test_answer_pager_hard_splits_long_word},
    {"answer_pager_reports_truncation_and_handles_empty", test_answer_pager_reports_truncation_and_handles_empty},
};

const int g_answer_pager_test_count = (int) (sizeof(g_answer_pager_tests) / sizeof(g_answer_pager_tests[0]));
