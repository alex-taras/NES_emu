#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include "types.h"

/* Button bitmask — bit positions match serial readout order */
#define BTN_A      0x01
#define BTN_B      0x02
#define BTN_SELECT 0x04
#define BTN_START  0x08
#define BTN_UP     0x10
#define BTN_DOWN   0x20
#define BTN_LEFT   0x40
#define BTN_RIGHT  0x80

typedef struct {
    uint8_t state;    /* live button state, set by SDL poll */
    uint8_t shift;    /* shift register: latched on strobe, clocked out on read */
    int     strobe;   /* 1 = strobe high, continuously reload snapshot */
} Controller;

void controller_reset(Controller *c);
void controller_set_state(Controller *c, uint8_t buttons); /* called from SDL loop */
void controller_write(Controller *c, Byte data);           /* $4016 write */
Byte controller_read(Controller *c);                       /* $4016 read  */

#endif