#include "ppu.h"
#include <string.h>

/* ── NES Palette ──────────────────────────────────────────────────────────── */
/* 64 ARGB8888 entries. Index = 6-bit NES palette index (0x00–0x3F). */
static const uint32_t NES_PALETTE[64] = {
    0xFF626262, 0xFF001FB2, 0xFF2404C8, 0xFF5200B2,
    0xFF730076, 0xFF800024, 0xFF730B00, 0xFF522800,
    0xFF244400, 0xFF005700, 0xFF005C00, 0xFF005324,
    0xFF003C76, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFABABAB, 0xFF0D57FF, 0xFF4B30FF, 0xFF8A13FF,
    0xFFBC08D6, 0xFFD21269, 0xFFC72E00, 0xFF9D5400,
    0xFF607B00, 0xFF209800, 0xFF00A300, 0xFF009942,
    0xFF007DB4, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFF53AEFF, 0xFF9085FF, 0xFFD365FF,
    0xFFFF57FF, 0xFFFF5DCF, 0xFFFF7757, 0xFFFA9E00,
    0xFFBDC700, 0xFF7AE700, 0xFF43F611, 0xFF26EF7E,
    0xFF2CD5F6, 0xFF4E4E4E, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFFB6DFFF, 0xFFCED1FF, 0xFFE9C3FF,
    0xFFFFBCFF, 0xFFFFBDF4, 0xFFFFC6C3, 0xFFFFD59A,
    0xFFE9E681, 0xFFCEF481, 0xFFB6FB9A, 0xFFA9FAC3,
    0xFFA9F0F6, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

/* ── Nametable mirroring ──────────────────────────────────────────────────── */
/*
 * Maps a PPU nametable address (0x2000–0x2FFF, already masked to 12-bit
 * offset from 0x2000) to a byte index into ppu->nametable[2][0x400].
 * Returns a pointer to the target byte.
 */
static Byte *nt_mirror(PPU *ppu, Word addr) {
    /* addr is already in 0x0000–0x0FFF (offset from 0x2000) */
    int slot = (addr >> 10) & 0x03;   /* nametable slot 0–3 */
    Word offset = addr & 0x03FF;       /* byte within nametable */

    int phys;
    // Get mirroring mode from mapper instead of static ppu->mirror
    Byte mirror_mode = mapper_get_mirroring(ppu->mapper);
    switch (mirror_mode) {
        case MIRROR_HORIZONTAL: phys = (slot >= 2) ? 1 : 0;  break;
        case MIRROR_VERTICAL:   phys = slot & 1;              break;
        case MIRROR_SINGLE_A:   phys = 0;                     break;
        case MIRROR_SINGLE_B:   phys = 1;                     break;
        default:                phys = slot & 1;              break;
    }
    return &ppu->nametable[phys][offset];
}

/* ── VRAM read/write ──────────────────────────────────────────────────────── */

Byte ppu_vram_read(PPU *ppu, Word addr) {
    addr &= 0x3FFF;

    if (addr <= 0x1FFF) {
        mapper_ppu_a12_tick(ppu->mapper, addr);
        return mapper_chr_read(ppu->mapper, addr);
    }
    if (addr <= 0x2FFF) {
        return *nt_mirror(ppu, addr - 0x2000);
    }
    if (addr <= 0x3EFF) {
        /* Mirror of 0x2000–0x2EFF */
        return *nt_mirror(ppu, (addr - 0x3000) & 0x0FFF);
    }
    /* 0x3F00–0x3FFF: palette */
    addr &= 0x1F;
    /* Sprite palette mirrors of background palette */
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C)
        addr &= 0x0F;
    return ppu->palette[addr];
}

void ppu_vram_write(PPU *ppu, Word addr, Byte data) {
    addr &= 0x3FFF;

    if (addr <= 0x1FFF) {
        mapper_chr_write(ppu->mapper, addr, data);
        return;
    }
    if (addr <= 0x2FFF) {
        *nt_mirror(ppu, addr - 0x2000) = data;
        return;
    }
    if (addr <= 0x3EFF) {
        *nt_mirror(ppu, (addr - 0x3000) & 0x0FFF) = data;
        return;
    }
    /* palette */
    addr &= 0x1F;
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C)
        addr &= 0x0F;
    ppu->palette[addr] = data;
}

/* ── Register I/O ─────────────────────────────────────────────────────────── */

Byte ppu_reg_read(PPU *ppu, Byte reg) {
    switch (reg) {
        case 0x02: { /* PPUSTATUS */
            Byte val = (ppu->status & 0xE0) | (ppu->data_buf & 0x1F);
            ppu->status &= ~0x80;   /* clear vblank flag */
            ppu->w = 0;             /* reset write latch */
            return val;
        }
        case 0x04:  /* OAMDATA */
            return ppu->oam[ppu->oam_addr];
        case 0x07: { /* PPUDATA */
            Byte val = ppu->data_buf;
            ppu->data_buf = ppu_vram_read(ppu, ppu->v);
            if ((ppu->v & 0x3FFF) >= 0x3F00) val = ppu->data_buf; /* palette: no delay */
            ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
            return val;
        }
        default:
            return 0x00;
    }
}

void ppu_reg_write(PPU *ppu, Byte reg, Byte data) {
    switch (reg) {
        case 0x00: /* PPUCTRL */
            ppu->ctrl = data;
            /* t: nametable bits */
            ppu->t = (ppu->t & 0xF3FF) | ((Word)(data & 0x03) << 10);
            /* NMI enable edge case: if new ctrl has bit 7 set and status has bit 7 set, set nmi_output */
            if ((data & 0x80) && (ppu->status & 0x80)) {
                ppu->nmi_output = 1;
            }
            break;
        case 0x01: /* PPUMASK */
            ppu->mask = data;
            break;
        case 0x03: /* OAMADDR */
            ppu->oam_addr = data;
            break;
        case 0x04: /* OAMDATA */
            ppu->oam[ppu->oam_addr++] = data;
            break;
        case 0x05: /* PPUSCROLL */
            if (ppu->w == 0) {
                ppu->x = data & 0x07;                           /* fine X */
                ppu->t = (ppu->t & 0xFFE0) | (data >> 3);     /* coarse X */
                ppu->w = 1;
            } else {
                /* fine Y into t[14:12], coarse Y into t[9:5] */
                ppu->t = (ppu->t & 0x8FFF) | ((Word)(data & 0x07) << 12);
                ppu->t = (ppu->t & 0xFC1F) | ((Word)(data >> 3) << 5);
                ppu->w = 0;
            }
            break;
        case 0x06: /* PPUADDR */
            if (ppu->w == 0) {
                /* High byte: bits 13–8 of t; bit 14 cleared */
                ppu->t = (ppu->t & 0x00FF) | ((Word)(data & 0x3F) << 8);
                ppu->w = 1;
            } else {
                ppu->t = (ppu->t & 0xFF00) | data;
                ppu->v = ppu->t;
                ppu->w = 0;
            }
            break;
        case 0x07: /* PPUDATA */
            ppu_vram_write(ppu, ppu->v, data);
            ppu->v += (ppu->ctrl & 0x04) ? 32 : 1;
            break;
    }
}

/* ── Loopy v updates (called during rendering) ────────────────────────────── */

static void increment_coarse_x(PPU *ppu) {
    if ((ppu->v & 0x001F) == 31) {    /* coarse X overflow */
        ppu->v &= ~0x001F;
        ppu->v ^= 0x0400;             /* flip horizontal nametable */
    } else {
        ppu->v++;
    }
}

static void increment_y(PPU *ppu) {
    if ((ppu->v & 0x7000) != 0x7000) {   /* fine Y < 7 */
        ppu->v += 0x1000;
    } else {
        ppu->v &= ~0x7000;                /* fine Y = 0 */
        int y = (ppu->v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            ppu->v ^= 0x0800;            /* flip vertical nametable */
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        ppu->v = (ppu->v & ~0x03E0) | (y << 5);
    }
}

static void copy_horizontal(PPU *ppu) {
    /* Copy coarse X and horizontal NT bit from t to v */
    ppu->v = (ppu->v & ~0x041F) | (ppu->t & 0x041F);
}

static void copy_vertical(PPU *ppu) {
    /* Copy fine Y, coarse Y, vertical NT bit from t to v */
    ppu->v = (ppu->v & ~0x7BE0) | (ppu->t & 0x7BE0);
}

/* ── Background helpers ───────────────────────────────────────────────────── */

static void load_bg_shifters(PPU *ppu) {
    ppu->bg_shift_lo = (ppu->bg_shift_lo & 0xFF00) | ppu->bg_lo_latch;
    ppu->bg_shift_hi = (ppu->bg_shift_hi & 0xFF00) | ppu->bg_hi_latch;
    ppu->at_latch_lo = (ppu->at_latch & 1) ? 0xFF : 0x00;
    ppu->at_latch_hi = (ppu->at_latch & 2) ? 0xFF : 0x00;
    ppu->at_shift_lo = (ppu->at_shift_lo & 0xFF00) | ppu->at_latch_lo;
    ppu->at_shift_hi = (ppu->at_shift_hi & 0xFF00) | ppu->at_latch_hi;
}

static void shift_bg(PPU *ppu) {
    if (ppu->mask & 0x08) {   /* background enable */
        ppu->bg_shift_lo <<= 1;
        ppu->bg_shift_hi <<= 1;
        ppu->at_shift_lo <<= 1;
        ppu->at_shift_hi <<= 1;
    }
}

static void fetch_nt(PPU *ppu) {
    ppu->nt_latch = ppu_vram_read(ppu, 0x2000 | (ppu->v & 0x0FFF));
}

static void fetch_at(PPU *ppu) {
    Word at_addr = 0x23C0
        | (ppu->v & 0x0C00)
        | ((ppu->v >> 4) & 0x38)
        | ((ppu->v >> 2) & 0x07);
    Byte at = ppu_vram_read(ppu, at_addr);
    /* Select the 2-bit quadrant */
    if (ppu->v & 0x0040) at >>= 4;
    if (ppu->v & 0x0002) at >>= 2;
    ppu->at_latch = at & 0x03;
}

static void fetch_bg_lo(PPU *ppu) {
    Word base  = (ppu->ctrl & 0x10) ? 0x1000 : 0x0000;
    Word fine_y = (ppu->v >> 12) & 0x07;
    ppu->bg_lo_latch = ppu_vram_read(ppu, base + ((Word)ppu->nt_latch << 4) + fine_y);
}

static void fetch_bg_hi(PPU *ppu) {
    Word base  = (ppu->ctrl & 0x10) ? 0x1000 : 0x0000;
    Word fine_y = (ppu->v >> 12) & 0x07;
    ppu->bg_hi_latch = ppu_vram_read(ppu, base + ((Word)ppu->nt_latch << 4) + fine_y + 8);
}

/* ── Pixel composition ────────────────────────────────────────────────────── */

static void compose_pixel(PPU *ppu) {
    Byte bg_pixel  = 0;
    Byte bg_pal    = 0;
    Byte sp_pixel  = 0;
    Byte sp_pal    = 0;
    Byte sp_priority = 0;

    if (ppu->mask & 0x08) {   /* background enabled */
        Word mux = 0x8000 >> ppu->x;
        Byte p0 = (ppu->bg_shift_lo & mux) ? 1 : 0;
        Byte p1 = (ppu->bg_shift_hi & mux) ? 1 : 0;
        bg_pixel = (p1 << 1) | p0;

        Byte a0 = (ppu->at_shift_lo & mux) ? 1 : 0;
        Byte a1 = (ppu->at_shift_hi & mux) ? 1 : 0;
        bg_pal = (a1 << 1) | a0;

        /* Left-edge clipping: suppress bg in first 8 pixels when bit 1 clear */
        if (!(ppu->mask & 0x02) && ppu->dot <= 8) {
            bg_pixel = 0;
            bg_pal   = 0;
        }
    }

    if (ppu->mask & 0x10) {   /* sprites enabled */
        ppu->sprite_zero_rendered = 0;
        for (int i = 0; i < ppu->sprite_count; i++) {
            if (ppu->sprite_x[i] != 0) continue;   /* not yet active */
            Byte sp_lo = (ppu->sprite_shift_lo[i] & 0x80) ? 1 : 0;
            Byte sp_hi = (ppu->sprite_shift_hi[i] & 0x80) ? 1 : 0;
            sp_pixel = (sp_hi << 1) | sp_lo;
            sp_pal   = (ppu->sprite_attr[i] & 0x03) + 4;   /* sprite palettes 4–7 */
            sp_priority = (ppu->sprite_attr[i] & 0x20) ? 0 : 1;  /* 1=in front */

            if (sp_pixel != 0) {
                if (i == 0) ppu->sprite_zero_rendered = 1;
                break;
            }
        }

        /* Left-edge clipping: suppress sprites in first 8 pixels when bit 2 clear */
        if (!(ppu->mask & 0x04) && ppu->dot <= 8) {
            sp_pixel = 0;
        }
    }

    /* Sprite-0 hit */
    if (ppu->sprite_zero_on_line && ppu->sprite_zero_rendered &&
        bg_pixel != 0 && sp_pixel != 0 &&
        (ppu->mask & 0x18) == 0x18 &&   /* both bg and sprites enabled */
        ppu->dot < 258)                  /* hit cannot occur at dot 258+ */
    {
        /* When both left-edge bits are clear, hit is suppressed for first 8 dots */
        if (!(ppu->mask & 0x06)) {
            if (ppu->dot >= 9) {
                ppu->status |= 0x40;
                ppu->dbg_sp0_hit_count++;
            }
        } else {
            ppu->status |= 0x40;
            ppu->dbg_sp0_hit_count++;
        }
    }

    /* Pixel priority */
    Byte pixel, pal;
    if (bg_pixel == 0 && sp_pixel == 0) { pixel = 0; pal = 0; }
    else if (bg_pixel == 0)             { pixel = sp_pixel; pal = sp_pal; }
    else if (sp_pixel == 0)             { pixel = bg_pixel; pal = bg_pal; }
    else if (sp_priority)               { pixel = sp_pixel; pal = sp_pal; }
    else                                { pixel = bg_pixel; pal = bg_pal; }

    Byte color_idx = ppu_vram_read(ppu, 0x3F00 + (pal << 2) + pixel) & 0x3F;
    if (ppu->mask & 0x01) color_idx &= 0x30;   /* greyscale */

    int fb_x = ppu->dot - 1;
    int fb_y = ppu->scanline;
    if (fb_x >= 0 && fb_x < 256 && fb_y >= 0 && fb_y < 240)
        ppu->framebuffer[fb_y * 256 + fb_x] = NES_PALETTE[color_idx];
}

/* ── Sprite evaluation ────────────────────────────────────────────────────── */

static void evaluate_sprites(PPU *ppu, int target_scanline) {
    memset(ppu->secondary_oam, 0xFF, 32);
    ppu->sprite_count = 0;
    ppu->sprite_zero_on_line = 0;

    int height = (ppu->ctrl & 0x20) ? 16 : 8;

    for (int i = 0; i < 64; i++) {
        int y = (int)ppu->oam[i * 4];   /* Y position (sprite drawn on scanlines y+1..y+height) */
        int diff = target_scanline - (y + 1);
        if (diff >= 0 && diff < height) {
            if (ppu->sprite_count < 8) {
                memcpy(&ppu->secondary_oam[ppu->sprite_count * 4], &ppu->oam[i * 4], 4);
                if (i == 0) { ppu->sprite_zero_on_line = 1; ppu->dbg_sp0_eval_count++; }
                ppu->sprite_count++;
            } else {
                ppu->status |= 0x20;   /* sprite overflow */
                break;
            }
        }
    }
}

static void fetch_sprites(PPU *ppu, int target_scanline) {
    int height = (ppu->ctrl & 0x20) ? 16 : 8;

    for (int i = 0; i < ppu->sprite_count; i++) {
        Byte y_pos  = ppu->secondary_oam[i * 4 + 0];
        Byte tile   = ppu->secondary_oam[i * 4 + 1];
        Byte attr   = ppu->secondary_oam[i * 4 + 2];
        Byte x_pos  = ppu->secondary_oam[i * 4 + 3];

        int row = target_scanline - ((int)y_pos + 1);
        int flip_v = attr & 0x80;
        if (flip_v) row = (height - 1) - row;

        Word pat_addr;
        if (height == 8) {
            Word base = (ppu->ctrl & 0x08) ? 0x1000 : 0x0000;
            pat_addr = base + ((Word)tile << 4) + row;
        } else {
            /* 8×16 sprites: tile bank from bit 0 of tile index */
            Word base = (tile & 0x01) ? 0x1000 : 0x0000;
            tile &= 0xFE;
            if (row >= 8) { tile++; row -= 8; }
            pat_addr = base + ((Word)tile << 4) + row;
        }

        Byte lo = ppu_vram_read(ppu, pat_addr);
        Byte hi = ppu_vram_read(ppu, pat_addr + 8);

        if (attr & 0x40) {   /* horizontal flip */
            /* Reverse bits */
            lo = (Byte)(((lo * 0x0802LU & 0x22110LU) | (lo * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);
            hi = (Byte)(((hi * 0x0802LU & 0x22110LU) | (hi * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16);
        }

        ppu->sprite_shift_lo[i] = lo;
        ppu->sprite_shift_hi[i] = hi;
        ppu->sprite_attr[i]     = attr;
        ppu->sprite_x[i]        = x_pos;
    }
}

/* ── Main tick ────────────────────────────────────────────────────────────── */

void ppu_tick(PPU *ppu) {
    int sl  = ppu->scanline;
    int dot = ppu->dot;
    int rendering = (ppu->mask & 0x18) != 0;  /* BG or sprite enabled */

    /* ── Pre-render scanline (261) ── */
    if (sl == 261) {
        if (dot == 1) {
            ppu->status &= ~0xE0;   /* clear vblank, sprite-0, overflow */
            memset(ppu->sprite_shift_lo, 0, 8);
            memset(ppu->sprite_shift_hi, 0, 8);
            ppu->dbg_sp0_eval_count = 0;
            ppu->dbg_sp0_hit_count = 0;
        }
        if (rendering && dot >= 280 && dot <= 304) {
            copy_vertical(ppu);
        }
    }

    /* ── Visible scanlines (0–239) ── */
    if (sl <= 239 || sl == 261) {
        /* Shift registers (dots 2–257, 322–337) */
        if ((dot >= 2 && dot <= 257) || (dot >= 321 && dot <= 337)) {
            shift_bg(ppu);

            /* Sprite X counters/shifters only during visible pixel output */
            if (sl < 240 && dot <= 257) {
                for (int i = 0; i < ppu->sprite_count; i++) {
                    if (ppu->sprite_x[i] > 0) {
                        ppu->sprite_x[i]--;
                    } else {
                        ppu->sprite_shift_lo[i] <<= 1;
                        ppu->sprite_shift_hi[i] <<= 1;
                    }
                }
            }

            switch (dot & 0x07) {
                case 1: load_bg_shifters(ppu);  fetch_nt(ppu);    break;
                case 3: fetch_at(ppu);                             break;
                case 5: fetch_bg_lo(ppu);                         break;
                case 7: fetch_bg_hi(ppu);                         break;
                case 0: if (rendering) increment_coarse_x(ppu);   break;
            }
        }

        if (dot == 256 && rendering) increment_y(ppu);
        if (dot == 257 && rendering) {
            copy_horizontal(ppu);
            load_bg_shifters(ppu);
        }

        /* Sprite evaluation / fetch at end of visible scanline */
        if (sl < 240) {
            if (dot == 257) evaluate_sprites(ppu, sl + 1);  /* prepare next scanline */
            if (dot == 320) fetch_sprites(ppu, sl + 1);
        }
    }

    /* ── Pixel output (visible scanlines, dots 1–256) ── */
    if (sl < 240 && dot >= 1 && dot <= 256) {
        compose_pixel(ppu);
    }

    /* ── VBlank ── */
    if (sl == 241 && dot == 1) {
        ppu->status |= 0x80;   /* set vblank flag */
        if (ppu->ctrl & 0x80) {
            ppu->nmi_output = 1;
        }
    }

    /* ── Advance dot/scanline ── */
    dot++;
    if (dot > 340) {
        dot = 0;
        sl++;
        if (sl > 261) {
            sl = 0;
            ppu->frame++;
            ppu->frame_done = 1;
        }
    }
    /* Odd-frame skip: NTSC PPU shortens odd frames by one dot when rendering is on.
       Scanline 0, dot 0 is skipped → jump directly to dot 1. */
    if (sl == 0 && dot == 0 && (ppu->frame & 1) && rendering) {
        dot = 1;
    }
    ppu->dot = dot;
    ppu->scanline = sl;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

void ppu_init(PPU *ppu, Mapper *mapper) {
    memset(ppu, 0, sizeof(PPU));
    ppu->mapper = mapper;
    ppu->mirror = (MirrorMode)mapper->cart->mirroring;
    ppu->scanline = 0;
    ppu->dot = 0;
    ppu->frame = 0;
    ppu->frame_done = 0;
}

void ppu_reset(PPU *ppu) {
    ppu->ctrl   = 0;
    ppu->mask   = 0;
    ppu->status = 0;
    ppu->v = ppu->t = ppu->x = ppu->w = 0;
    ppu->data_buf = 0;
    ppu->scanline = 0;
    ppu->dot = 0;
    ppu->frame_done = 0;
    memset(ppu->sprite_shift_lo, 0, 8);
    memset(ppu->sprite_shift_hi, 0, 8);
}

int ppu_frame_complete(PPU *ppu) {
    if (ppu->frame_done) {
        ppu->frame_done = 0;
        return 1;
    }
    return 0;
}