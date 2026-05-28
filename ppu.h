#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include "types.h"
#include "mapper.h"

typedef enum {
    MIRROR_HORIZONTAL   = 0,
    MIRROR_VERTICAL     = 1,
    MIRROR_SINGLE_A     = 2,
    MIRROR_SINGLE_B     = 3,
    MIRROR_FOUR_SCREEN  = 4,
} MirrorMode;

typedef struct {
    /* CPU-facing registers */
    Byte ctrl;        /* 0x2000 PPUCTRL  (write-only) */
    Byte mask;        /* 0x2001 PPUMASK  (write-only) */
    Byte status;      /* 0x2002 PPUSTATUS (read-only, bits 7-5 meaningful) */
    Byte oam_addr;    /* 0x2003 OAMADDR */

    /* Loopy scroll/address registers */
    Word v;           /* current VRAM address, 15-bit */
    Word t;           /* temporary VRAM address, 15-bit */
    Byte x;           /* fine X scroll, 3-bit */
    Byte w;           /* write latch: 0=first write, 1=second write */

    /* Read buffer for PPUDATA */
    Byte data_buf;

    /* Internal VRAM */
    Byte nametable[2][0x0400];   /* 2KB physical nametable RAM */
    Byte palette[32];            /* palette RAM */
    Byte oam[256];               /* primary OAM: 64 sprites × 4 bytes */

    /* Scanline/dot position */
    int scanline;    /* 0–261; 261 = pre-render */
    int dot;         /* 0–340 */
    int frame;       /* total frames rendered; bit 0 = odd/even */

    /* Background tile fetch latches */
    Byte nt_latch;
    Byte at_latch;
    Byte bg_lo_latch;
    Byte bg_hi_latch;

    /* Background 16-bit shift registers (MSB = current pixel) */
    Word bg_shift_lo;
    Word bg_shift_hi;

    /* Attribute shift registers and latches */
    Word at_shift_lo;
    Word at_shift_hi;
    Byte at_latch_lo;   /* loaded into at_shift at tile boundary */
    Byte at_latch_hi;

    /* Sprite rendering (evaluated for current scanline) */
    Byte secondary_oam[32];          /* up to 8 sprites × 4 bytes */
    int  sprite_count;               /* sprites found for this scanline */
    Byte sprite_shift_lo[8];
    Byte sprite_shift_hi[8];
    Byte sprite_attr[8];             /* attribute byte per sprite slot */
    Byte sprite_x[8];                /* X counter per sprite slot */
    Byte sprite_zero_on_line;        /* sprite 0 is in secondary OAM this scanline */
    Byte sprite_zero_rendered;       /* sprite 0 pixel is being composited this dot */

    /* NMI output latch (read by CPU as nmi_pending) */
    Byte nmi_output;

    /* Framebuffer: ARGB8888, 256×240, row-major */
    uint32_t framebuffer[256 * 240];

    /* Mapper reference for CHR access */
    Mapper *mapper;

    /* Mirroring mode (derived from mapper/cart at init) */
    MirrorMode mirror;

    /* Internal flag: frame just completed (cleared after ppu_frame_complete) */
    Byte frame_done;

    /* Debug: count scanlines where sprite 0 was found this frame */
    int dbg_sp0_eval_count;
    int dbg_sp0_hit_count;
} PPU;

/* Lifecycle */
void ppu_init(PPU *ppu, Mapper *mapper);
void ppu_reset(PPU *ppu);

/* Advance one PPU dot */
void ppu_tick(PPU *ppu);

/* Returns 1 (and clears flag) if a frame just completed. Call after every tick. */
int ppu_frame_complete(PPU *ppu);

/* CPU-facing register I/O — called by bus.c */
Byte ppu_reg_read (PPU *ppu, Byte reg);   /* reg = addr & 0x07 */
void ppu_reg_write(PPU *ppu, Byte reg, Byte data);

/* PPU address space read/write (internal — used by ppu.c and for testing) */
Byte ppu_vram_read (PPU *ppu, Word addr);
void ppu_vram_write(PPU *ppu, Word addr, Byte data);

#endif