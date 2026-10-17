#pragma once

#include <stdbool.h>
#include <stddef.h>

#define ANSWER_PAGE_MAX_CHARS 180
#define ANSWER_PAGE_MAX_COUNT 4
#define ANSWER_PAGE_LEN       (ANSWER_PAGE_MAX_CHARS + 1)

/**
 * @brief Split an answer into word-wrapped pages that fit the assistant screen.
 * @param text Answer text to split.
 * @param pages Destination page buffer with ANSWER_PAGE_LEN characters per page.
 * @param max_pages Maximum number of pages that may be written.
 * @param out_truncated Optional output set true when text did not fit within max_pages.
 * @return Number of pages written, or zero when text is empty or invalid.
 * @note Pages break on whitespace where possible; a single word longer than a page is hard-split.
 */
size_t answer_pager_split(const char *text, char pages[][ANSWER_PAGE_LEN], size_t max_pages, bool *out_truncated);
