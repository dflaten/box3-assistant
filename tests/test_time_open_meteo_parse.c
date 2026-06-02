#include <string.h>

#include "esp_err.h"
#include "test_support.h"
#include "time/time_open_meteo_parse.h"

static bool test_time_open_meteo_url_encodes_queries(void) {
    char encoded[64];

    ASSERT_TRUE(time_open_meteo_url_encode_query_value("Chicago Illinois", encoded, sizeof(encoded)));
    ASSERT_TRUE(strcmp(encoded, "Chicago+Illinois") == 0);

    ASSERT_TRUE(time_open_meteo_url_encode_query_value("St. John's", encoded, sizeof(encoded)));
    ASSERT_TRUE(strcmp(encoded, "St.+John%27s") == 0);

    ASSERT_TRUE(!time_open_meteo_url_encode_query_value("Chicago", encoded, 4));
    return true;
}

static bool test_time_open_meteo_parses_location_response(void) {
    const char *json = "{\"results\":[{\"name\":\"Chicago\",\"latitude\":41.85003,\"longitude\":-87.65005,"
                       "\"admin1\":\"Illinois\",\"country\":\"United States\",\"timezone\":\"America/Chicago\"}]}";
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_location_response(json, NULL, false, &location));
    ASSERT_TRUE(strcmp(location.location, "Chicago, Illinois") == 0);
    ASSERT_TRUE(strcmp(location.timezone, "America/Chicago") == 0);
    ASSERT_TRUE(location.latitude > 41.8 && location.latitude < 41.9);
    ASSERT_TRUE(location.longitude < -87.6 && location.longitude > -87.7);
    return true;
}

static bool test_time_open_meteo_ranks_location_by_admin_qualifier(void) {
    const char *json = "{\"results\":["
                       "{\"name\":\"Portland\",\"latitude\":43.6615,\"longitude\":-70.2553,"
                       "\"admin1\":\"Maine\",\"country\":\"United States\",\"country_code\":\"US\","
                       "\"timezone\":\"America/New_York\"},"
                       "{\"name\":\"Portland\",\"latitude\":45.5234,\"longitude\":-122.6762,"
                       "\"admin1\":\"Oregon\",\"country\":\"United States\",\"country_code\":\"US\","
                       "\"timezone\":\"America/Los_Angeles\"}]}";
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_location_response(json, "Oregon", true, &location));
    ASSERT_TRUE(strcmp(location.location, "Portland, Oregon") == 0);
    ASSERT_TRUE(strcmp(location.timezone, "America/Los_Angeles") == 0);

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_location_response(json, "OR", true, &location));
    ASSERT_TRUE(strcmp(location.location, "Portland, Oregon") == 0);
    return true;
}

static bool test_time_open_meteo_matches_district_qualifier(void) {
    const char *json = "{\"results\":[{\"name\":\"Washington\",\"latitude\":38.8951,\"longitude\":-77.0364,"
                       "\"admin1\":\"District of Columbia\",\"country\":\"United States\",\"country_code\":\"US\","
                       "\"timezone\":\"America/New_York\"}]}";
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_location_response(json, "DC", true, &location));
    ASSERT_TRUE(strcmp(location.location, "Washington, District of Columbia") == 0);
    return true;
}

static bool test_time_open_meteo_ranks_location_by_country_qualifier(void) {
    const char *json = "{\"results\":["
                       "{\"name\":\"Tokyo\",\"latitude\":34.2254,\"longitude\":139.0394,"
                       "\"admin1\":\"Tokyo\",\"country\":\"United States\",\"country_code\":\"US\","
                       "\"timezone\":\"America/Adak\"},"
                       "{\"name\":\"Tokyo\",\"latitude\":35.6895,\"longitude\":139.6917,"
                       "\"admin1\":\"Tokyo\",\"country\":\"Japan\",\"country_code\":\"JP\","
                       "\"timezone\":\"Asia/Tokyo\"}]}";
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_location_response(json, "Japan", true, &location));
    ASSERT_TRUE(strcmp(location.location, "Tokyo, Japan") == 0);
    ASSERT_TRUE(strcmp(location.timezone, "Asia/Tokyo") == 0);

    ASSERT_EQ_INT(ESP_ERR_NOT_FOUND, time_open_meteo_parse_location_response(json, "France", true, &location));
    return true;
}

static bool test_time_open_meteo_strict_qualifier_returns_not_found_for_nonmatch(void) {
    const char *json = "{\"results\":["
                       "{\"name\":\"Valid\",\"latitude\":1,\"longitude\":2,"
                       "\"admin1\":\"One\",\"country\":\"Country\",\"country_code\":\"CO\"},"
                       "{\"name\":\"Broken\",\"admin1\":\"Wanted\"}]}";
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_ERR_NOT_FOUND, time_open_meteo_parse_location_response(json, "Wanted", true, &location));
    return true;
}

static bool test_time_open_meteo_rejects_missing_location_fields(void) {
    time_location_t location = {0};

    ASSERT_EQ_INT(ESP_ERR_NOT_FOUND,
                  time_open_meteo_parse_location_response("{\"results\":[]}", NULL, false, &location));
    ASSERT_EQ_INT(
        ESP_ERR_INVALID_RESPONSE,
        time_open_meteo_parse_location_response("{\"results\":[{\"name\":\"Nowhere\"}]}", NULL, false, &location));
    return true;
}

static bool test_time_open_meteo_parses_utc_offset_response(void) {
    int32_t offset = 0;

    ASSERT_EQ_INT(ESP_OK, time_open_meteo_parse_utc_offset_response("{\"utc_offset_seconds\":-21600}", &offset));
    ASSERT_EQ_INT(-21600, offset);
    ASSERT_EQ_INT(ESP_ERR_INVALID_RESPONSE,
                  time_open_meteo_parse_utc_offset_response("{\"timezone\":\"UTC\"}", &offset));
    return true;
}

const test_case_t g_time_open_meteo_parse_tests[] = {
    {"time_open_meteo_url_encodes_queries", test_time_open_meteo_url_encodes_queries},
    {"time_open_meteo_parses_location_response", test_time_open_meteo_parses_location_response},
    {"time_open_meteo_ranks_location_by_admin_qualifier", test_time_open_meteo_ranks_location_by_admin_qualifier},
    {"time_open_meteo_matches_district_qualifier", test_time_open_meteo_matches_district_qualifier},
    {"time_open_meteo_ranks_location_by_country_qualifier", test_time_open_meteo_ranks_location_by_country_qualifier},
    {"time_open_meteo_strict_qualifier_returns_not_found_for_nonmatch",
     test_time_open_meteo_strict_qualifier_returns_not_found_for_nonmatch},
    {"time_open_meteo_rejects_missing_location_fields", test_time_open_meteo_rejects_missing_location_fields},
    {"time_open_meteo_parses_utc_offset_response", test_time_open_meteo_parses_utc_offset_response},
};

const int g_time_open_meteo_parse_test_count =
    (int) (sizeof(g_time_open_meteo_parse_tests) / sizeof(g_time_open_meteo_parse_tests[0]));
