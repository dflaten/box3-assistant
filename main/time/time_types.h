#pragma once

#include <stdint.h>

#define TIME_LOCATION_LEN    64
#define TIME_TIME_TEXT_LEN   24
#define TIME_DATE_TEXT_LEN   32
#define TIME_DETAIL_TEXT_LEN 160
#define TIME_SPOKEN_TEXT_LEN 192
#define TIME_QUERY_TEXT_LEN  96

typedef struct {
    char location[TIME_LOCATION_LEN];
    char timezone[TIME_LOCATION_LEN];
    double latitude;
    double longitude;
} time_location_t;

typedef struct {
    char location[TIME_LOCATION_LEN];
    char timezone[TIME_LOCATION_LEN];
    char time_text[TIME_TIME_TEXT_LEN];
    char date_text[TIME_DATE_TEXT_LEN];
} time_report_t;
