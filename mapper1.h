#ifndef MAPPER1_H
#define MAPPER1_H

#include "mapper.h"

/* MMC1: 16KB/32KB PRG bank switching, 4KB/8KB CHR bank switching, dynamic mirroring */
Mapper *mapper1_create(Cartridge *cart);

#endif