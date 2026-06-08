#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Apply a spoken volume adjustment in ten-percent steps.
 * @param current_percent Current speaker volume from 0 through 100.
 * @param steps Number of ten-percent steps to apply.
 * @param increase True to increase volume, false to decrease it.
 * @return Adjusted volume clamped to the range 0 through 100.
 */
uint8_t volume_control_adjust(uint8_t current_percent, uint8_t steps, bool increase);
