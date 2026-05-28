#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mapper1.h"

// The Mapper1 state struct that will be allocated as part of a larger structure
typedef struct {
    Mapper base;  // MUST be first for casting to work
    
    // Shift register state
    Byte shift_register;
    Byte shift_count;

    // Internal registers
    Byte control;        // Register 0
    Byte chr_bank_0;     // Register 1
    Byte chr_bank_1;     // Register 2
    Byte prg_bank;       // Register 3

    // Calculated bank offsets
    DWord prg_bank_0_offset;  // For $8000-$BFFF
    DWord prg_bank_1_offset;  // For $C000-$FFFF
    DWord chr_bank_0_offset;  // For $0000-$0FFF
    DWord chr_bank_1_offset;  // For $1000-$1FFF

    // Mirroring
    Byte mirroring;  // 0=one-screen-low, 1=one-screen-high, 2=vert, 3=horiz
    
    // CHR RAM (8KB) - for games without CHR ROM
    Byte chr_ram[8192];
    
    // PRG RAM (8KB) - for save games at $6000-$7FFF
    Byte prg_ram[8192];
} Mapper1;

// Forward declarations
static void m1_write_register(Mapper1 *m, Word addr, Byte data);
static void m1_update_banks(Mapper1 *m);
static void m1_set_control(Mapper1 *m, Byte value);
static int m1_trace_enabled = 0;

static int m1_trace_count = 0;
static void m1_trace_state(Mapper1 *m, const char *event, Word addr, Byte data, Byte target) {
    if (!m1_trace_enabled) return;
    if (m1_trace_count >= 1024) return;
    /* PRG-bank commits are extremely frequent in LoZ; sample them sparsely. */
    if (target == 3 && (m1_trace_count % 64) != 0) {
        m1_trace_count++;
        return;
    }
    fprintf(stderr,
            "MMC1[%03d] %s tgt=%u addr=%04X data=%02X | ctrl=%02X mir=%u chr0=%02X chr1=%02X prg=%02X | prg_off=%05X/%05X chr_off=%05X/%05X\n",
            m1_trace_count, event, target, addr, data,
            m->control, m->mirroring, m->chr_bank_0, m->chr_bank_1, m->prg_bank,
            (unsigned)m->prg_bank_0_offset, (unsigned)m->prg_bank_1_offset,
            (unsigned)m->chr_bank_0_offset, (unsigned)m->chr_bank_1_offset);
    m1_trace_count++;
}

// PRG read: 16KB/32KB banking + PRG RAM
static Byte m1_prg_read(Mapper *m, Word addr) {
    Mapper1 *m1 = (Mapper1*)m;
    Cartridge *cart = m->cart;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        return m1->prg_ram[addr - 0x6000];
    }

    if (addr >= 0x8000) {
        if (m1->control & 0x08) {
            // 16KB mode: split at $C000
            if (addr <= 0xBFFF) {
                DWord offset = m1->prg_bank_0_offset + (addr - 0x8000);
                return cart->prg_rom[offset % cart->prg_size];
            } else {
                DWord offset = m1->prg_bank_1_offset + (addr - 0xC000);
                return cart->prg_rom[offset % cart->prg_size];
            }
        } else {
            // 32KB mode: entire $8000-$FFFF uses bank 0
            DWord offset = m1->prg_bank_0_offset + (addr - 0x8000);
            return cart->prg_rom[offset % cart->prg_size];
        }
    }
    return 0;
}

// PRG write: shift register + PRG RAM
static void m1_prg_write(Mapper *m, Word addr, Byte data) {
    Mapper1 *m1 = (Mapper1*)m;
    Cartridge *cart = m->cart;

    if (addr >= 0x6000 && addr <= 0x7FFF) {
        m1->prg_ram[addr - 0x6000] = data;
        return;
    }

    if (addr >= 0x8000 && addr <= 0xFFFF) {
        m1_write_register(m1, addr, data);
    }
}

// CHR read: 4KB banking
static Byte m1_chr_read(Mapper *m, Word addr) {
    Mapper1 *m1 = (Mapper1*)m;
    Cartridge *cart = m->cart;

    if (cart->chr_size == 0) {
        // CHR RAM case - read from RAM
        return m1->chr_ram[addr & 0x1FFF];
    }

    if (addr <= 0x0FFF) {
        // First 4KB bank
        DWord offset = m1->chr_bank_0_offset + addr;
        return cart->chr_rom[offset % cart->chr_size];
    } else if (addr <= 0x1FFF) {
        // Second 4KB bank
        DWord offset = m1->chr_bank_1_offset + (addr - 0x1000);
        return cart->chr_rom[offset % cart->chr_size];
    }
    return 0;
}

// CHR write: only for CHR RAM
static void m1_chr_write(Mapper *m, Word addr, Byte data) {
    Mapper1 *m1 = (Mapper1*)m;
    Cartridge *cart = m->cart;
    
    if (cart->chr_size == 0) {
        // CHR RAM - allow writes
        m1->chr_ram[addr & 0x1FFF] = data;
    }
    // CHR ROM - writes ignored
}

// Shift register write logic
static void m1_write_register(Mapper1 *m, Word addr, Byte data) {
    // Bit 7 set = reset
    if (data & 0x80) {
        m->shift_register = 0;
        m->shift_count = 0;
        // Reset forces PRG mode to 3 (fix last bank at $C000).
        m->control |= 0x0C;
        m1_update_banks(m);
        m1_trace_state(m, "reset", addr, data, 0xFF);
        return;
    }

    // Shift in bit 0, LSB first, so shift right and place at bit 4
    m->shift_register >>= 1;
    m->shift_register |= (data & 1) << 4;
    m->shift_count++;

    // After 5 writes, update target register
    if (m->shift_count == 5) {
        Byte reg_value = m->shift_register;
        Byte target = (addr >> 13) & 0x03;

        if (target == 0) {  // $8000-$9FFF
            m1_set_control(m, reg_value);
        } else if (target == 1) {  // $A000-$BFFF
            if (m->control & 0x10) {
                m->chr_bank_0 = reg_value & 0x1F;  // 4KB mode
            } else {
                m->chr_bank_0 = reg_value & 0x1E;  // 8KB mode: ignore low bit
            }
        } else if (target == 2) {  // $C000-$DFFF
            if (m->control & 0x10) {
                m->chr_bank_1 = reg_value & 0x1F;  // 4KB mode only
            }
        } else {  // target == 3, $E000-$FFFF
            m->prg_bank = reg_value & 0x0F;
        }

        m->shift_register = 0;
        m->shift_count = 0;
        m1_update_banks(m);
        m1_trace_state(m, "commit", addr, reg_value, target);
    }
}

// Set control register and update mirroring
static void m1_set_control(Mapper1 *m, Byte value) {
    m->control = value & 0x1F;

    // Update mirroring
    m->mirroring = value & 0x03;
}

// Recalculate all bank offsets based on current register values
static void m1_update_banks(Mapper1 *m) {
    Cartridge *cart = m->base.cart;
    
    // PRG banking - check bit 3 (0x08) for 16KB vs 32KB mode
    Byte prg_mode = (m->control >> 2) & 0x03;
    Byte prg_bank_num = m->prg_bank & 0x0F;

    if (m->control & 0x08) {
        // 16KB PRG mode (bit 3 set)
        if (prg_mode == 2) {
            // Fix first bank at $8000, switch $C000
            m->prg_bank_0_offset = 0;
            m->prg_bank_1_offset = prg_bank_num * 0x4000;
        } else {
            // prg_mode == 3: Switch $8000, fix last bank at $C000
            m->prg_bank_0_offset = prg_bank_num * 0x4000;
            DWord last_bank = (cart->prg_size / 0x4000) - 1;
            m->prg_bank_1_offset = last_bank * 0x4000;
        }
    } else {
        // 32KB PRG mode (bit 3 clear)
        // Ignore low bit of bank number (divide by 2)
        Byte bank32 = (prg_bank_num & 0x0E) >> 1;
        m->prg_bank_0_offset = bank32 * 0x8000;
        m->prg_bank_1_offset = bank32 * 0x8000 + 0x4000;
    }

    // CHR banking - bit 4 controls CHR ROM mode
    Byte chr_mode = (m->control >> 4) & 0x01;
    
    if (chr_mode) {
        // 4KB mode: each register selects a 4KB bank
        m->chr_bank_0_offset = m->chr_bank_0 * 0x1000;
        m->chr_bank_1_offset = m->chr_bank_1 * 0x1000;
    } else {
        // 8KB mode: register value uses 4KB units with bit 0 forced to 0.
        // So byte offset uses 0x1000 scaling, then second half is +0x1000.
        m->chr_bank_0_offset = (DWord)m->chr_bank_0 * 0x1000;
        m->chr_bank_1_offset = m->chr_bank_0_offset + 0x1000;
    }
}

// Translate MMC1 control bits 1:0 to PPU MirrorMode constants
// MMC1: 0=one-screen-low, 1=one-screen-high, 2=vertical, 3=horizontal
// PPU:  MIRROR_HORIZONTAL=0, MIRROR_VERTICAL=1, MIRROR_SINGLE_A=2, MIRROR_SINGLE_B=3
static const Byte MMC1_TO_PPU_MIRROR[4] = { 2, 3, 1, 0 };

static Byte m1_get_mirroring(Mapper *m) {
    Mapper1 *m1 = (Mapper1*)m;
    return MMC1_TO_PPU_MIRROR[m1->mirroring & 0x03];
}

static void m1_destroy(Mapper *m) {
    free(m);
}

// Mapper operations
static const MapperOps MAPPER1_OPS = {
    .prg_read  = m1_prg_read,
    .prg_write = m1_prg_write,
    .chr_read  = m1_chr_read,
    .chr_write = m1_chr_write,
    .get_mirroring = m1_get_mirroring,
    .destroy   = m1_destroy,
};

Mapper *mapper1_create(Cartridge *cart) {
    Mapper1 *m1 = malloc(sizeof(Mapper1));
    if (!m1) return NULL;
    
    memset(m1, 0, sizeof(Mapper1));  // Zero all including CHR RAM

    // Set up base mapper
    m1->base.ops = &MAPPER1_OPS;
    m1->base.cart = cart;

    // Initial state: control = 0x1C (PRG mode 3, CHR 4KB, one-screen-low)
    m1->control = 0x1C;  // Reference uses 0x1C on reset
    m1->shift_register = 0;
    m1->shift_count = 0;
    m1->chr_bank_0 = 0;
    m1->chr_bank_1 = 0;
    m1->prg_bank = 0;
    // Keep mirrored mode in sync with control register low bits.
    m1->mirroring = m1->control & 0x03;

    // Calculate initial banks
    m1_update_banks(m1);
    m1_trace_count = 0;
    m1_trace_enabled = 0;

    return (Mapper*)m1;
}