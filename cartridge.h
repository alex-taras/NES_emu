#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <stddef.h>
#include "types.h"

/* iNES header: 16 bytes at start of .nes file */
typedef struct {
    Byte magic[4];      /* 0x4E 0x45 0x53 0x1A = "NES\x1A" */
    Byte prg_banks;     /* number of 16KB PRG-ROM banks */
    Byte chr_banks;     /* number of 8KB CHR-ROM banks (0 = CHR-RAM) */
    Byte flags6;        /* D0=mirroring, D1=battery, D2=trainer, D3=4-screen, D7-D4=mapper_lo */
    Byte flags7;        /* D0=VS, D1=PlayChoice, D3-D2=format, D7-D4=mapper_hi */
    Byte flags8;        /* PRG-RAM size (rarely used) */
    Byte flags9;        /* TV system (rarely used) */
    Byte flags10;       /* TV system, PRG-RAM presence (unofficial) */
    Byte padding[5];    /* zero-filled padding to 16 bytes */
} INESHeader;

typedef struct {
    Byte *prg_rom;      /* PRG-ROM data */
    size_t prg_size;    /* PRG-ROM size in bytes */
    Byte *chr_rom;      /* CHR-ROM data (NULL if CHR-RAM) */
    size_t chr_size;    /* CHR-ROM size in bytes (0 if CHR-RAM) */
    int mapper_id;      /* mapper number (0–255) */
    Byte mirroring;     /* 0 = horizontal, 1 = vertical */
    Byte has_battery;   /* battery-backed SRAM present */
} Cartridge;

/* Load from .nes file. Returns NULL on error (bad magic, unsupported, OOM). */
Cartridge *cartridge_load(const char *path);

/* Construct from raw buffers (for tests). chr can be NULL, chr_size can be 0. */
Cartridge *cartridge_create_from_buffer(const Byte *prg, size_t prg_size,
                                        const Byte *chr, size_t chr_size,
                                        int mapper_id, Byte mirroring);

void cartridge_free(Cartridge *cart);

#endif