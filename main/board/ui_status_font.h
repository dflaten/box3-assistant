#pragma once

#include <stdint.h>

typedef struct {
    char code;
    uint8_t rows[7];
} ui_status_glyph_t;

/**
 * @brief Look up a bitmap glyph definition for a character.
 * @param c Character to render.
 * @return Pointer to the matching glyph, or the space glyph when none exists.
 */
const ui_status_glyph_t *ui_status_font_find_glyph(char c);
