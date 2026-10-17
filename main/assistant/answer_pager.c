#include "assistant/answer_pager.h"

#include <stdbool.h>
#include <string.h>

/**
 * @brief Check whether a character counts as page-break whitespace.
 * @param character Character to inspect.
 * @return True when the character is a space, tab, or newline, otherwise false.
 */
static bool answer_pager_is_space(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

/**
 * @brief Split an answer into word-wrapped pages that fit the assistant screen.
 * @param text Answer text to split.
 * @param pages Destination page buffer with ANSWER_PAGE_LEN characters per page.
 * @param max_pages Maximum number of pages that may be written.
 * @param out_truncated Optional output set true when text did not fit within max_pages.
 * @return Number of pages written, or zero when text is empty or invalid.
 */
size_t answer_pager_split(const char *text, char pages[][ANSWER_PAGE_LEN], size_t max_pages, bool *out_truncated) {
    if (out_truncated != NULL) {
        *out_truncated = false;
    }
    if (text == NULL || pages == NULL || max_pages == 0) {
        return 0;
    }

    size_t page_count = 0;
    const char *cursor = text;

    while (*cursor != '\0' && page_count < max_pages) {
        while (answer_pager_is_space(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        size_t remaining = strlen(cursor);
        size_t take = remaining < ANSWER_PAGE_MAX_CHARS ? remaining : ANSWER_PAGE_MAX_CHARS;

        if (take < remaining) {
            size_t last_space = 0;
            bool found_space = false;
            for (size_t i = 0; i < take; ++i) {
                if (answer_pager_is_space(cursor[i])) {
                    last_space = i;
                    found_space = true;
                }
            }
            if (found_space && last_space > 0) {
                take = last_space;
            }
        }

        size_t length = take;
        while (length > 0 && answer_pager_is_space(cursor[length - 1])) {
            length--;
        }

        memcpy(pages[page_count], cursor, length);
        pages[page_count][length] = '\0';
        page_count++;

        cursor += take;
        while (answer_pager_is_space(*cursor)) {
            cursor++;
        }
    }

    if (out_truncated != NULL && *cursor != '\0') {
        *out_truncated = true;
    }

    return page_count;
}
