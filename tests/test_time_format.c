#include <string.h>
#include <time.h>

#include "test_support.h"
#include "time/time_format.h"

static bool test_time_format_normalizes_location_queries(void) {
    char query[TIME_QUERY_TEXT_LEN];

    ASSERT_TRUE(time_format_normalize_location_query("in Chicago Illinois", query, sizeof(query)));
    ASSERT_TRUE(strcmp(query, "Chicago Illinois") == 0);

    ASSERT_TRUE(time_format_normalize_location_query("what time is it in London England", query, sizeof(query)));
    ASSERT_TRUE(strcmp(query, "London England") == 0);

    ASSERT_TRUE(time_format_normalize_location_query("  for Paris France. ", query, sizeof(query)));
    ASSERT_TRUE(strcmp(query, "Paris France") == 0);

    ASSERT_TRUE(!time_format_normalize_location_query("in", query, sizeof(query)));
    return true;
}

static bool test_time_format_from_utc_offset_uses_offset_seconds(void) {
    char time_text[TIME_TIME_TEXT_LEN];
    char date_text[TIME_DATE_TEXT_LEN];

    ASSERT_TRUE(time_format_from_utc_offset(
        (time_t) 1704110400, -21600, time_text, sizeof(time_text), date_text, sizeof(date_text)));
    ASSERT_TRUE(strcmp(time_text, "6:00 AM") == 0);
    ASSERT_TRUE(strcmp(date_text, "Mon Jan 01") == 0);
    return true;
}

static bool test_time_format_splits_location_qualifiers(void) {
    char search_query[TIME_QUERY_TEXT_LEN];
    char qualifier[TIME_QUERY_TEXT_LEN];

    ASSERT_TRUE(time_format_split_location_qualifier(
        "Seattle Washington", 1, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    ASSERT_TRUE(strcmp(search_query, "Seattle") == 0);
    ASSERT_TRUE(strcmp(qualifier, "Washington") == 0);

    ASSERT_TRUE(time_format_split_location_qualifier(
        "Fargo ND", 1, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    ASSERT_TRUE(strcmp(search_query, "Fargo") == 0);
    ASSERT_TRUE(strcmp(qualifier, "ND") == 0);

    ASSERT_TRUE(time_format_split_location_qualifier(
        "Kansas City Missouri", 1, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    ASSERT_TRUE(strcmp(search_query, "Kansas City") == 0);
    ASSERT_TRUE(strcmp(qualifier, "Missouri") == 0);

    ASSERT_TRUE(time_format_split_location_qualifier(
        "Tokyo Japan", 1, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    ASSERT_TRUE(strcmp(search_query, "Tokyo") == 0);
    ASSERT_TRUE(strcmp(qualifier, "Japan") == 0);

    ASSERT_TRUE(time_format_split_location_qualifier(
        "London United Kingdom", 2, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    ASSERT_TRUE(strcmp(search_query, "London") == 0);
    ASSERT_TRUE(strcmp(qualifier, "United Kingdom") == 0);

    ASSERT_TRUE(!time_format_split_location_qualifier(
        "Paris", 1, search_query, sizeof(search_query), qualifier, sizeof(qualifier)));
    return true;
}

static bool test_time_format_detail_and_spoken_text(void) {
    time_report_t report = {
        .location = "Chicago, Illinois",
        .time_text = "6:00 AM",
    };
    char detail[TIME_DETAIL_TEXT_LEN];
    char spoken[TIME_SPOKEN_TEXT_LEN];

    time_format_detail(&report, detail, sizeof(detail));
    time_format_spoken(&report, spoken, sizeof(spoken));

    ASSERT_TRUE(strcmp(detail, "6:00 AM in Chicago, Illinois") == 0);
    ASSERT_TRUE(strcmp(spoken, "It is 6:00 AM in Chicago, Illinois.") == 0);
    return true;
}

const test_case_t g_time_format_tests[] = {
    {"time_format_normalizes_location_queries", test_time_format_normalizes_location_queries},
    {"time_format_from_utc_offset_uses_offset_seconds", test_time_format_from_utc_offset_uses_offset_seconds},
    {"time_format_splits_location_qualifiers", test_time_format_splits_location_qualifiers},
    {"time_format_detail_and_spoken_text", test_time_format_detail_and_spoken_text},
};

const int g_time_format_test_count = (int) (sizeof(g_time_format_tests) / sizeof(g_time_format_tests[0]));
