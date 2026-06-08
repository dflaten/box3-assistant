#include "volume/volume_control.h"

uint8_t volume_control_adjust(uint8_t current_percent, uint8_t steps, bool increase) {
    int adjusted = current_percent;
    int delta = (int) steps * 10;

    adjusted += increase ? delta : -delta;
    if (adjusted < 0) {
        return 0;
    }
    if (adjusted > 100) {
        return 100;
    }
    return (uint8_t) adjusted;
}
