#include <stdbool.h>
#include <string.h>

#include "fireworks/fireworks_cost_parse.h"
#include "test_support.h"

static bool test_fireworks_cost_parse_subtotal(void) {
    uint32_t cents = 0;

    ASSERT_TRUE(fireworks_cost_parse_subtotal_cents(
        "{\"rows\":[],\"subtotal\":{\"currencyCode\":\"USD\",\"units\":\"1\",\"nanos\":853076912}}", &cents));
    ASSERT_EQ_INT(185, (int) cents);

    ASSERT_TRUE(fireworks_cost_parse_subtotal_cents(
        "{\"subtotal\":{\"currencyCode\":\"USD\",\"units\":\"0\",\"nanos\":1073100}}", &cents));
    ASSERT_EQ_INT(0, (int) cents);

    ASSERT_TRUE(fireworks_cost_parse_subtotal_cents("{\"subtotal\":{\"units\":0,\"nanos\":5000000}}", &cents));
    ASSERT_EQ_INT(1, (int) cents);
    return true;
}

static bool test_fireworks_cost_parse_rejects_missing_subtotal(void) {
    uint32_t cents = 123;

    ASSERT_TRUE(!fireworks_cost_parse_subtotal_cents("{\"rows\":[]}", &cents));
    ASSERT_TRUE(!fireworks_cost_parse_subtotal_cents("not json", &cents));
    ASSERT_TRUE(!fireworks_cost_parse_subtotal_cents(NULL, &cents));
    ASSERT_TRUE(!fireworks_cost_parse_subtotal_cents("{}", NULL));
    return true;
}

static bool test_fireworks_cost_format(void) {
    char text[FIREWORKS_COST_TEXT_LEN];

    fireworks_cost_format(185, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "$1.85") == 0);

    fireworks_cost_format(0, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "$0.00") == 0);

    fireworks_cost_format(5, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "$0.05") == 0);

    fireworks_cost_format(1234, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "$12.34") == 0);
    return true;
}

const test_case_t g_fireworks_cost_parse_tests[] = {
    {"fireworks_cost_parse_subtotal", test_fireworks_cost_parse_subtotal},
    {"fireworks_cost_parse_rejects_missing_subtotal", test_fireworks_cost_parse_rejects_missing_subtotal},
    {"fireworks_cost_format", test_fireworks_cost_format},
};

const int g_fireworks_cost_parse_test_count =
    (int) (sizeof(g_fireworks_cost_parse_tests) / sizeof(g_fireworks_cost_parse_tests[0]));
