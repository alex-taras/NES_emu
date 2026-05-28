#ifndef MAPPER2_H
#define MAPPER2_H

#include "mapper.h"

/* Mapper 2 (UxROM/UNROM): 16KB switchable PRG bank, fixed last bank, CHR RAM or ROM */
Mapper *mapper2_create(Cartridge *cart);

#endif