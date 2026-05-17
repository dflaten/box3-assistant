#include <stdbool.h>

#include "esp_err.h"
#include "net/request_cancel.h"
#include "test_support.h"

static int s_cancel_call_order;

static esp_err_t cancel_invalid_state_first(void) {
    s_cancel_call_order = (s_cancel_call_order * 10) + 1;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t cancel_invalid_state_second(void) {
    s_cancel_call_order = (s_cancel_call_order * 10) + 2;
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t cancel_success_third(void) {
    s_cancel_call_order = (s_cancel_call_order * 10) + 3;
    return ESP_OK;
}

static bool test_request_cancel_first_active_returns_first_non_invalid_state(void) {
    const request_cancel_fn_t cancel_fns[] = {
        cancel_invalid_state_first,
        cancel_success_third,
        cancel_invalid_state_second,
    };

    s_cancel_call_order = 0;
    ASSERT_EQ_INT(ESP_OK, request_cancel_first_active(cancel_fns, 3));
    ASSERT_EQ_INT(13, s_cancel_call_order);
    return true;
}

static bool test_request_cancel_first_active_returns_invalid_state_when_none_active(void) {
    const request_cancel_fn_t cancel_fns[] = {
        cancel_invalid_state_first,
        cancel_invalid_state_second,
    };

    s_cancel_call_order = 0;
    ASSERT_EQ_INT(ESP_ERR_INVALID_STATE, request_cancel_first_active(cancel_fns, 2));
    ASSERT_EQ_INT(12, s_cancel_call_order);
    return true;
}

static bool test_request_cancel_first_active_rejects_null_array(void) {
    ASSERT_EQ_INT(ESP_ERR_INVALID_ARG, request_cancel_first_active(NULL, 1));
    return true;
}

const test_case_t g_request_cancel_tests[] = {
    {"request_cancel_first_active_returns_first_non_invalid_state",
     test_request_cancel_first_active_returns_first_non_invalid_state},
    {"request_cancel_first_active_returns_invalid_state_when_none_active",
     test_request_cancel_first_active_returns_invalid_state_when_none_active},
    {"request_cancel_first_active_rejects_null_array", test_request_cancel_first_active_rejects_null_array},
};

const int g_request_cancel_test_count = (int) (sizeof(g_request_cancel_tests) / sizeof(g_request_cancel_tests[0]));
