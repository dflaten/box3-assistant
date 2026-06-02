#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "time/time_types.h"

bool time_format_normalize_location_query(const char *text, char *query, size_t query_size);
bool time_format_split_location_qualifier(const char *query,
                                          size_t qualifier_words,
                                          char *search_query,
                                          size_t search_query_size,
                                          char *qualifier,
                                          size_t qualifier_size);
bool time_format_from_utc_offset(time_t utc_now,
                                 int32_t utc_offset_seconds,
                                 char *time_buffer,
                                 size_t time_buffer_size,
                                 char *date_buffer,
                                 size_t date_buffer_size);
void time_format_detail(const time_report_t *report, char *detail, size_t detail_size);
void time_format_spoken(const time_report_t *report, char *spoken, size_t spoken_size);
