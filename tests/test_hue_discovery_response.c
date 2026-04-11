#include <stdbool.h>
#include <string.h>

#include "hue/hue_discovery_response.h"
#include "test_support.h"

static bool test_hue_discovery_response_matches_case_insensitive_headers(void) {
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "CACHE-CONTROL: max-age=100\r\n"
                           "hue-bridgeid: 001788FFFE123456\r\n"
                           "LOCATION: http://192.168.68.53/description.xml\r\n\r\n";
    char location[96];

    ASSERT_TRUE(hue_discovery_response_is_hue_bridge(response));
    ASSERT_TRUE(hue_discovery_extract_header_value(response, "location", location, sizeof(location)));
    ASSERT_TRUE(strcmp(location, "http://192.168.68.53/description.xml") == 0);
    return true;
}

static bool test_hue_discovery_response_rejects_non_hue_payloads(void) {
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "SERVER: generic-device\r\n"
                           "LOCATION: http://192.168.68.10/\r\n\r\n";
    char bridge_id[32];

    ASSERT_TRUE(!hue_discovery_response_is_hue_bridge(response));
    ASSERT_TRUE(!hue_discovery_extract_header_value(response, "hue-bridgeid", bridge_id, sizeof(bridge_id)));
    return true;
}

static bool test_hue_discovery_extracts_trimmed_header_values(void) {
    const char *response = "HTTP/1.1 200 OK\r\n"
                           "hue-bridgeid:\t 001788FFFE123456 \t\r\n"
                           "location:\t http://192.168.68.63/description.xml  \r\n\r\n";
    char bridge_id[32];
    char location[96];

    ASSERT_TRUE(hue_discovery_extract_header_value(response, "hue-bridgeid", bridge_id, sizeof(bridge_id)));
    ASSERT_TRUE(strcmp(bridge_id, "001788FFFE123456") == 0);
    ASSERT_TRUE(hue_discovery_extract_header_value(response, "location", location, sizeof(location)));
    ASSERT_TRUE(strcmp(location, "http://192.168.68.63/description.xml") == 0);
    return true;
}

const test_case_t g_hue_discovery_response_tests[] = {
    {"hue_discovery_response_matches_case_insensitive_headers",
     test_hue_discovery_response_matches_case_insensitive_headers},
    {"hue_discovery_response_rejects_non_hue_payloads", test_hue_discovery_response_rejects_non_hue_payloads},
    {"hue_discovery_extracts_trimmed_header_values", test_hue_discovery_extracts_trimmed_header_values},
};

const int g_hue_discovery_response_test_count =
    (int) (sizeof(g_hue_discovery_response_tests) / sizeof(g_hue_discovery_response_tests[0]));
