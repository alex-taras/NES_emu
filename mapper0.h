#ifndef MAPPER0_H
#define MAPPER0_H

#include "mapper.h"

/* NROM: no bank switching. PRG 16KB (mirrored) or 32KB. CHR 8KB fixed. */
Mapper *mapper0_create(Cartridge *cart);

#endif