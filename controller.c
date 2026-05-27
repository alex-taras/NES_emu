#include "controller.h"

void controller_reset(Controller *c) {
    c->state  = 0;
    c->shift  = 0xFF; /* open-bus: reads before first strobe return 1 */
    c->strobe = 0;
}

void controller_set_state(Controller *c, uint8_t buttons) {
    c->state = buttons;
    if (c->strobe)
        c->shift = c->state; /* keep reloading while strobe is high */
}

void controller_write(Controller *c, Byte data) {
    c->strobe = data & 0x01;
    if (c->strobe)
        c->shift = c->state; /* latch on strobe high */
}

Byte controller_read(Controller *c) {
    Byte bit = (c->shift & 0x01); /* LSB out */
    c->shift >>= 1;
    c->shift |= 0x80; /* fill with 1s (open bus) so reads past 8 return 1 */
    return bit & 0x01;
}