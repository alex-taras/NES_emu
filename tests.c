#include <stdio.h>

#include "types.h"
#include "bus.h"
#include "cpu.h"
#include "memory.h"
#include "opcodes.h"

// --- Test config ---
// Program area: 0x0200-0x02FF (page 2)
// Data area:    0x0300-0x03FF (page 3) for ABS tests
// Zero page:    0x0000-0x00FF as usual
#define PRG_START 0x0200
#define DATA_PAGE 0x0300

static CPU cpu;
static int test_pass = 0;
static int test_fail = 0;

// --- Helpers ---

void print_flags(CPU *c) {
    printf("  Flags: ");
    // NV-BDIZC
    char names[] = "CZIDBUVN";  // bit 0 to bit 7
    for (int i = 7; i >= 0; i--) {
        printf("%c=%d ", names[i], (c->flags >> i) & 1);
    }
    printf(" [0b");
    for (int i = 7; i >= 0; i--) {
        printf("%d", (c->flags >> i) & 1);
    }
    printf("]\n");
}

void print_regs(CPU *c) {
    printf("  Regs: A=0x%02X", c->regs.A);
    // show signed interpretation if bit 7 set
    if (c->regs.A & 0x80) printf(" (%d)", (int8_t)c->regs.A);
    printf("  X=0x%02X  Y=0x%02X  SP=0x%02X  PC=0x%04X\n",
           c->regs.X, c->regs.Y, c->SP, c->PC);
}

void print_mem_range(Word start, Word len) {
    printf("  Mem [0x%04X - 0x%04X]: ", start, start + len - 1);
    for (Word i = 0; i < len; i++) {
        printf("%02X ", bus_read(start + i));
    }
    printf("\n");
}

void print_program(Word start, Word len) {
    printf("  Program bytes: ");
    for (Word i = 0; i < len; i++) {
        printf("%02X ", bus_read(start + i));
    }
    printf("\n");
}

void test_reset() {
    cpu_reset(&cpu);
    cpu.PC = PRG_START;
}

void test_header(const char *name) {
    printf("\n--- %s ---\n", name);
}

void check(const char *desc, int condition) {
    if (condition) {
        printf("  [PASS] %s\n", desc);
        test_pass++;
    } else {
        printf("  [FAIL] %s\n", desc);
        test_fail++;
    }
}

// helper to write a word (little-endian) via bus
void bus_write_word(Word addr, Word value) {
    bus_write(addr, value & 0xFF);
    bus_write(addr + 1, value >> 8);
}

// --- LDA Tests ---

void test_lda() {
    printf("\n========== LDA TESTS ==========\n");

    // LDA Immediate - basic
    {
        test_reset();
        test_header("LDA IM - load 0x42");
        bus_write(PRG_START, OPC_LDA_IM);
        bus_write(PRG_START + 1, 0x42);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);
        print_flags(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x42", cpu.regs.A == 0x42);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // LDA Immediate - zero (Z flag)
    {
        test_reset();
        test_header("LDA IM - load 0x00 (zero flag)");
        bus_write(PRG_START, OPC_LDA_IM);
        bus_write(PRG_START + 1, 0x00);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // LDA Immediate - negative (N flag)
    {
        test_reset();
        test_header("LDA IM - load 0x80 (negative flag, signed = -128)");
        bus_write(PRG_START, OPC_LDA_IM);
        bus_write(PRG_START + 1, 0x80);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x80", cpu.regs.A == 0x80);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
    }

    // LDA Zero Page
    {
        test_reset();
        test_header("LDA ZP - load from ZP addr 0x10");
        bus_write(0x10, 0xAB);
        bus_write(PRG_START, OPC_LDA_ZP);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_mem_range(0x10, 1);
        print_regs(&cpu);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0xAB", cpu.regs.A == 0xAB);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
    }

    // LDA Zero Page,X - with wrapping
    {
        test_reset();
        test_header("LDA ZP,X - wrap around ZP (addr 0xFF + X=0x02 -> 0x01)");
        cpu.regs.X = 0x02;
        bus_write(0x01, 0x77);  // wrapped target
        bus_write(PRG_START, OPC_LDA_ZPX);
        bus_write(PRG_START + 1, 0xFF);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  X=0x%02X, ZP operand=0xFF, effective=(0xFF+0x02)&0xFF=0x01\n", cpu.regs.X);
        print_mem_range(0x00, 4);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x77 (wrapped ZP read)", cpu.regs.A == 0x77);
    }

    // LDA Absolute
    {
        test_reset();
        test_header("LDA ABS - load from 0x0300");
        bus_write(DATA_PAGE, 0xDE);
        bus_write(PRG_START, OPC_LDA_ABS);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);
        printf(" Before:\n");
        print_mem_range(DATA_PAGE, 1);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xDE", cpu.regs.A == 0xDE);
    }

    // LDA Absolute,X - no page cross
    {
        test_reset();
        test_header("LDA ABS,X - no page cross (0x0300 + X=0x05)");
        cpu.regs.X = 0x05;
        bus_write(DATA_PAGE + 0x05, 0xBB);
        bus_write(PRG_START, OPC_LDA_ABSX);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xBB", cpu.regs.A == 0xBB);
    }

    // LDA Absolute,X - page cross (+1 cycle)
    {
        test_reset();
        test_header("LDA ABS,X - page cross (0x03FF + X=0x01 -> 0x0400)");
        cpu.regs.X = 0x01;
        bus_write(0x0400, 0xCC);
        bus_write(PRG_START, OPC_LDA_ABSX);
        bus_write_word(PRG_START + 1, 0x03FF);
        print_program(PRG_START, 3);
        printf("  Base=0x03FF, X=0x01 -> effective=0x0400 (page cross!)\n");

        cpu_execute(5, &cpu);  // 4 + 1 extra cycle

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xCC (page cross)", cpu.regs.A == 0xCC);
    }

    // LDA Absolute,Y - page cross
    {
        test_reset();
        test_header("LDA ABS,Y - page cross (0x03FE + Y=0x05 -> 0x0403)");
        cpu.regs.Y = 0x05;
        bus_write(0x0403, 0xDD);
        bus_write(PRG_START, OPC_LDA_ABSY);
        bus_write_word(PRG_START + 1, 0x03FE);
        print_program(PRG_START, 3);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xDD", cpu.regs.A == 0xDD);
    }

    // LDA (Indirect,X)
    {
        test_reset();
        test_header("LDA (IND,X) - ptr at ZP (0x20+X=0x04) -> 0x0300, val=0xEE");
        cpu.regs.X = 0x04;
        // pointer at ZP 0x24 -> points to 0x0300
        bus_write(0x24, 0x00);  // lo
        bus_write(0x25, 0x03);  // hi
        bus_write(DATA_PAGE, 0xEE);
        bus_write(PRG_START, OPC_LDA_INDX);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);
        printf("  ZP operand=0x20, X=0x04 -> wrapped=0x24\n");
        print_mem_range(0x24, 2);
        printf("  Pointer -> 0x0300\n");

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xEE", cpu.regs.A == 0xEE);
    }

    // LDA (Indirect,X) - ZP pointer wrap
    {
        test_reset();
        test_header("LDA (IND,X) - ZP wrap: operand=0xFE, X=0x01 -> ptr at 0xFF/0x00");
        cpu.regs.X = 0x01;
        bus_write(0xFF, 0x10);  // lo byte of pointer
        bus_write(0x00, 0x03);  // hi byte wraps to 0x00
        bus_write(0x0310, 0x55);
        bus_write(PRG_START, OPC_LDA_INDX);
        bus_write(PRG_START + 1, 0xFE);
        print_program(PRG_START, 2);
        printf("  (0xFE + 0x01) & 0xFF = 0xFF -> ptr at ZP 0xFF,0x00 -> 0x0310\n");

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x55 (ZP pointer wrap)", cpu.regs.A == 0x55);
    }

    // LDA (Indirect),Y - no page cross
    {
        test_reset();
        test_header("LDA (IND),Y - no page cross");
        // pointer at ZP 0x30 -> 0x0300
        bus_write(0x30, 0x00);
        bus_write(0x31, 0x03);
        cpu.regs.Y = 0x05;
        bus_write(0x0305, 0x99);
        bus_write(PRG_START, OPC_LDA_INDY);
        bus_write(PRG_START + 1, 0x30);
        print_program(PRG_START, 2);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x99", cpu.regs.A == 0x99);
    }

    // LDA (Indirect),Y - page cross
    {
        test_reset();
        test_header("LDA (IND),Y - page cross (base=0x03FE + Y=0x05 -> 0x0403)");
        bus_write(0x40, 0xFE);  // lo
        bus_write(0x41, 0x03);  // hi -> base = 0x03FE
        cpu.regs.Y = 0x05;
        bus_write(0x0403, 0x11);
        bus_write(PRG_START, OPC_LDA_INDY);
        bus_write(PRG_START + 1, 0x40);
        print_program(PRG_START, 2);
        printf("  Base=0x03FE, Y=0x05 -> 0x0403 (page cross!)\n");

        cpu_execute(6, &cpu);  // 5 + 1

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x11 (page cross)", cpu.regs.A == 0x11);
    }
}

// --- STA Tests ---

void test_sta() {
    printf("\n========== STA TESTS ==========\n");

    // STA Zero Page
    {
        test_reset();
        test_header("STA ZP - store 0x42 to ZP 0x10");
        cpu.regs.A = 0x42;
        bus_write(PRG_START, OPC_STA_ZP);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);
        print_mem_range(0x10, 1);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_mem_range(0x10, 1);
        check("ZP[0x10] == 0x42", bus_read(0x10) == 0x42);
    }

    // STA Zero Page,X - with wrap
    {
        test_reset();
        test_header("STA ZP,X - wrap (addr=0xFF + X=0x03 -> 0x02)");
        cpu.regs.A = 0xBE;
        cpu.regs.X = 0x03;
        bus_write(PRG_START, OPC_STA_ZPX);
        bus_write(PRG_START + 1, 0xFF);
        print_program(PRG_START, 2);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_mem_range(0x00, 4);
        check("ZP[0x02] == 0xBE (wrapped)", bus_read(0x02) == 0xBE);
    }

    // STA Absolute
    {
        test_reset();
        test_header("STA ABS - store to 0x0300");
        cpu.regs.A = 0xAA;
        bus_write(PRG_START, OPC_STA_ABS);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_mem_range(DATA_PAGE, 1);
        check("mem[0x0300] == 0xAA", bus_read(DATA_PAGE) == 0xAA);
    }

    // STA Absolute,X
    {
        test_reset();
        test_header("STA ABS,X - store to 0x0300 + X=0x10 -> 0x0310");
        cpu.regs.A = 0x55;
        cpu.regs.X = 0x10;
        bus_write(PRG_START, OPC_STA_ABSX);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_mem_range(0x0310, 1);
        check("mem[0x0310] == 0x55", bus_read(0x0310) == 0x55);
    }

    // STA Absolute,Y
    {
        test_reset();
        test_header("STA ABS,Y - store to 0x0300 + Y=0x20 -> 0x0320");
        cpu.regs.A = 0x66;
        cpu.regs.Y = 0x20;
        bus_write(PRG_START, OPC_STA_ABSY);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_mem_range(0x0320, 1);
        check("mem[0x0320] == 0x66", bus_read(0x0320) == 0x66);
    }

    // STA (Indirect,X)
    {
        test_reset();
        test_header("STA (IND,X) - ptr at ZP (0x20+X=0x04)=0x24 -> 0x0300");
        cpu.regs.A = 0x77;
        cpu.regs.X = 0x04;
        bus_write(0x24, 0x00);
        bus_write(0x25, 0x03);
        bus_write(PRG_START, OPC_STA_INDX);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_mem_range(DATA_PAGE, 1);
        check("mem[0x0300] == 0x77", bus_read(DATA_PAGE) == 0x77);
    }

    // STA (Indirect),Y
    {
        test_reset();
        test_header("STA (IND),Y - ptr at ZP 0x30 -> 0x0300 + Y=0x08 -> 0x0308");
        cpu.regs.A = 0x88;
        cpu.regs.Y = 0x08;
        bus_write(0x30, 0x00);
        bus_write(0x31, 0x03);
        bus_write(PRG_START, OPC_STA_INDY);
        bus_write(PRG_START + 1, 0x30);
        print_program(PRG_START, 2);

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_mem_range(0x0308, 1);
        check("mem[0x0308] == 0x88", bus_read(0x0308) == 0x88);
    }

    // STA should not affect flags
    {
        test_reset();
        test_header("STA ZP - flags unchanged after store");
        cpu.regs.A = 0x00;
        // set some known flags first
        cpu_set_flag(Z, 0, &cpu);
        cpu_set_flag(N, 1, &cpu);
        Byte flags_before = cpu.flags;
        bus_write(PRG_START, OPC_STA_ZP);
        bus_write(PRG_START + 1, 0x50);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_flags(&cpu);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_flags(&cpu);
        check("Flags unchanged after STA", cpu.flags == flags_before);
    }
}

// --- ADC Tests ---

void test_adc() {
    printf("\n========== ADC TESTS ==========\n");

    // ADC IM - simple add, no carry in
    {
        test_reset();
        test_header("ADC IM - 0x10 + 0x20 = 0x30, no carry");
        cpu.regs.A = 0x10;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);
        print_flags(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x30", cpu.regs.A == 0x30);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
        check("V == 0", cpu_read_flag(V, &cpu) == 0);
    }

    // ADC IM - carry in
    {
        test_reset();
        test_header("ADC IM - 0x10 + 0x20 + C=1 = 0x31");
        cpu.regs.A = 0x10;
        cpu_set_flag(C, 1, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);
        print_flags(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x31", cpu.regs.A == 0x31);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
    }

    // ADC IM - carry out (unsigned overflow)
    {
        test_reset();
        test_header("ADC IM - 0xFF + 0x01 = 0x00, C=1 (unsigned overflow)");
        cpu.regs.A = 0xFF;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x01);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  A=0xFF (unsigned: 255, signed: %d)\n", (int8_t)0xFF);
        print_regs(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        printf("  Result: 0xFF + 0x01 = 0x100 -> A=0x00, carry out\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("C == 1 (carry out)", cpu_read_flag(C, &cpu) == 1);
        check("Z == 1 (result is zero)", cpu_read_flag(Z, &cpu) == 1);
    }

    // ADC IM - signed positive overflow: 0x7F + 0x01 = 0x80 (127 + 1 = -128 !)
    {
        test_reset();
        test_header("ADC IM - signed overflow: 0x7F + 0x01 (127 + 1 = -128)");
        cpu.regs.A = 0x7F;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x01);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  A=0x7F (signed: +127), operand=0x01 (signed: +1)\n");
        print_regs(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        printf("  Result: 0x80 (signed: %d) - positive + positive = negative!\n", (int8_t)0x80);
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x80", cpu.regs.A == 0x80);
        check("V == 1 (signed overflow)", cpu_read_flag(V, &cpu) == 1);
        check("N == 1 (result is negative)", cpu_read_flag(N, &cpu) == 1);
        check("C == 0 (no unsigned overflow)", cpu_read_flag(C, &cpu) == 0);
    }

    // ADC IM - signed negative overflow: 0x80 + 0xFF (-128 + -1 = +127 !)
    {
        test_reset();
        test_header("ADC IM - signed overflow: 0x80 + 0xFF (-128 + -1 = wraps)");
        cpu.regs.A = 0x80;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0xFF);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  A=0x80 (signed: %d), operand=0xFF (signed: %d)\n", (int8_t)0x80, (int8_t)0xFF);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        printf("  Result: 0x7F (signed: %d) - negative + negative = positive!\n", (int8_t)cpu.regs.A);
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x7F", cpu.regs.A == 0x7F);
        check("V == 1 (signed overflow)", cpu_read_flag(V, &cpu) == 1);
        check("C == 1 (unsigned carry)", cpu_read_flag(C, &cpu) == 1);
        check("N == 0 (result is positive)", cpu_read_flag(N, &cpu) == 0);
    }

    // ADC IM - no signed overflow: positive + negative
    {
        test_reset();
        test_header("ADC IM - no overflow: 0x50 + 0xD0 (+80 + -48 = +32)");
        cpu.regs.A = 0x50;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0xD0);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  A=0x50 (signed: %d), operand=0xD0 (signed: %d)\n", (int8_t)0x50, (int8_t)0xD0);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        printf("  Result: 0x20 (signed: %d)\n", (int8_t)cpu.regs.A);
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x20", cpu.regs.A == 0x20);
        check("V == 0 (no signed overflow)", cpu_read_flag(V, &cpu) == 0);
        check("C == 1 (unsigned carry)", cpu_read_flag(C, &cpu) == 1);
    }

    // ADC IM - zero result
    {
        test_reset();
        test_header("ADC IM - 0x01 + 0xFF = 0x00 (zero result, carry out)");
        cpu.regs.A = 0x01;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0xFF);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
        check("C == 1", cpu_read_flag(C, &cpu) == 1);
        check("V == 0", cpu_read_flag(V, &cpu) == 0);
    }

    // ADC IM - 0x00 + 0x00 + C=0 = 0x00
    {
        test_reset();
        test_header("ADC IM - 0x00 + 0x00 + C=0 = 0x00 (all zeros)");
        cpu.regs.A = 0x00;
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x00);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
        check("V == 0", cpu_read_flag(V, &cpu) == 0);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // ADC IM - carry causes overflow: 0x7F + 0x00 + C=1 = 0x80
    {
        test_reset();
        test_header("ADC IM - carry triggers overflow: 0x7F + 0x00 + C=1 = 0x80");
        cpu.regs.A = 0x7F;
        cpu_set_flag(C, 1, &cpu);
        bus_write(PRG_START, OPC_ADC_IM);
        bus_write(PRG_START + 1, 0x00);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        printf("  A=0x7F (+127) + 0x00 + C=1 -> 0x80 (%d)\n", (int8_t)0x80);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x80", cpu.regs.A == 0x80);
        check("V == 1 (carry pushed into signed overflow)", cpu_read_flag(V, &cpu) == 1);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
    }
}

// --- AND Tests ---

void test_and() {
    printf("\n========== AND TESTS ==========\n");

    // AND Immediate - basic
    {
        test_reset();
        test_header("AND IM - 0xFF & 0x0F = 0x0F");
        cpu.regs.A = 0xFF;
        bus_write(PRG_START, OPC_AND_IM);
        bus_write(PRG_START + 1, 0x0F);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x0F", cpu.regs.A == 0x0F);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // AND Immediate - zero flag
    {
        test_reset();
        test_header("AND IM - 0xAA & 0x55 = 0x00 (zero flag)");
        cpu.regs.A = 0xAA;
        bus_write(PRG_START, OPC_AND_IM);
        bus_write(PRG_START + 1, 0x55);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // AND Immediate - negative flag
    {
        test_reset();
        test_header("AND IM - 0xFF & 0x80 = 0x80 (negative flag)");
        cpu.regs.A = 0xFF;
        bus_write(PRG_START, OPC_AND_IM);
        bus_write(PRG_START + 1, 0x80);
        print_program(PRG_START, 2);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x80", cpu.regs.A == 0x80);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
    }

    // AND Zero Page
    {
        test_reset();
        test_header("AND ZP - A=0xF0 & ZP[0x10]=0x33 = 0x30");
        cpu.regs.A = 0xF0;
        bus_write(0x10, 0x33);
        bus_write(PRG_START, OPC_AND_ZP);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x30", cpu.regs.A == 0x30);
    }

    // AND Zero Page,X - with wrap
    {
        test_reset();
        test_header("AND ZP,X - wrap (addr=0xFF + X=0x02 -> 0x01)");
        cpu.regs.A = 0xFF;
        cpu.regs.X = 0x02;
        bus_write(0x01, 0x5A);
        bus_write(PRG_START, OPC_AND_ZPX);
        bus_write(PRG_START + 1, 0xFF);
        print_program(PRG_START, 2);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x5A (wrapped ZP)", cpu.regs.A == 0x5A);
    }

    // AND Absolute
    {
        test_reset();
        test_header("AND ABS - A=0xCC & mem[0x0300]=0x0F = 0x0C");
        cpu.regs.A = 0xCC;
        bus_write(DATA_PAGE, 0x0F);
        bus_write(PRG_START, OPC_AND_ABS);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x0C", cpu.regs.A == 0x0C);
    }

    // AND Absolute,X - no page cross
    {
        test_reset();
        test_header("AND ABS,X - no page cross (0x0300 + X=0x05)");
        cpu.regs.A = 0xFF;
        cpu.regs.X = 0x05;
        bus_write(DATA_PAGE + 0x05, 0x3C);
        bus_write(PRG_START, OPC_AND_ABSX);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x3C", cpu.regs.A == 0x3C);
    }

    // AND Absolute,Y - page cross
    {
        test_reset();
        test_header("AND ABS,Y - page cross (0x03FE + Y=0x05 -> 0x0403)");
        cpu.regs.A = 0xFF;
        cpu.regs.Y = 0x05;
        bus_write(0x0403, 0x71);
        bus_write(PRG_START, OPC_AND_ABSY);
        bus_write_word(PRG_START + 1, 0x03FE);
        print_program(PRG_START, 3);
        printf("  Base=0x03FE, Y=0x05 -> 0x0403 (page cross!)\n");

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0x71 (page cross)", cpu.regs.A == 0x71);
    }

    // AND (Indirect,X)
    {
        test_reset();
        test_header("AND (IND,X) - ptr at ZP (0x20+X=0x04)=0x24 -> 0x0300");
        cpu.regs.A = 0xFF;
        cpu.regs.X = 0x04;
        bus_write(0x24, 0x00);
        bus_write(0x25, 0x03);
        bus_write(DATA_PAGE, 0xAB);
        bus_write(PRG_START, OPC_AND_INDX);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xAB", cpu.regs.A == 0xAB);
    }

    // AND (Indirect),Y - no page cross
    {
        test_reset();
        test_header("AND (IND),Y - no page cross, ptr at ZP 0x30 -> 0x0300 + Y=0x05");
        cpu.regs.A = 0xFF;
        cpu.regs.Y = 0x05;
        bus_write(0x30, 0x00);
        bus_write(0x31, 0x03);
        bus_write(0x0305, 0xC3);
        bus_write(PRG_START, OPC_AND_INDY);
        bus_write(PRG_START + 1, 0x30);
        print_program(PRG_START, 2);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("A == 0xC3", cpu.regs.A == 0xC3);
    }
}

// --- ASL Tests ---

void test_asl() {
    printf("\n========== ASL TESTS ==========\n");

    // ASL Accumulator - basic shift
    {
        test_reset();
        test_header("ASL ACC - 0x01 << 1 = 0x02, C=0");
        cpu.regs.A = 0x01;
        bus_write(PRG_START, OPC_ASL_ACC);
        print_program(PRG_START, 1);
        printf(" Before:\n");
        print_regs(&cpu);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x02", cpu.regs.A == 0x02);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0", cpu_read_flag(N, &cpu) == 0);
    }

    // ASL Accumulator - carry out + zero
    {
        test_reset();
        test_header("ASL ACC - 0x80 << 1 = 0x00 (carry out, zero flag)");
        cpu.regs.A = 0x80;
        bus_write(PRG_START, OPC_ASL_ACC);
        print_program(PRG_START, 1);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x00", cpu.regs.A == 0x00);
        check("C == 1 (carry out)", cpu_read_flag(C, &cpu) == 1);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
    }

    // ASL Accumulator - negative flag
    {
        test_reset();
        test_header("ASL ACC - 0x40 << 1 = 0x80 (negative flag)");
        cpu.regs.A = 0x40;
        bus_write(PRG_START, OPC_ASL_ACC);
        print_program(PRG_START, 1);

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A == 0x80", cpu.regs.A == 0x80);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
    }

    // ASL Zero Page
    {
        test_reset();
        test_header("ASL ZP - ZP[0x10]=0x21 << 1 = 0x42");
        bus_write(0x10, 0x21);
        bus_write(PRG_START, OPC_ASL_ZP);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_mem_range(0x10, 1);

        cpu_execute(5, &cpu);

        printf(" After:\n");
        print_mem_range(0x10, 1);
        check("ZP[0x10] == 0x42", bus_read(0x10) == 0x42);
        check("C == 0", cpu_read_flag(C, &cpu) == 0);
    }

    // ASL Zero Page,X
    {
        test_reset();
        test_header("ASL ZP,X - ZP[0x10+X=0x04]=0x14 (addr 0x14), val=0x08 << 1 = 0x10");
        cpu.regs.X = 0x04;
        bus_write(0x14, 0x08);
        bus_write(PRG_START, OPC_ASL_ZPX);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        printf("  operand=0x10, X=0x04 -> effective addr=0x14\n");
        printf(" Before:\n");
        print_mem_range(0x14, 1);

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_mem_range(0x14, 1);
        check("ZP[0x14] == 0x10", bus_read(0x14) == 0x10);
    }

    // ASL Absolute
    {
        test_reset();
        test_header("ASL ABS - mem[0x0300]=0x40 << 1 = 0x80 (N flag)");
        bus_write(DATA_PAGE, 0x40);
        bus_write(PRG_START, OPC_ASL_ABS);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);
        printf(" Before:\n");
        print_mem_range(DATA_PAGE, 1);

        cpu_execute(6, &cpu);

        printf(" After:\n");
        print_mem_range(DATA_PAGE, 1);
        print_flags(&cpu);
        check("mem[0x0300] == 0x80", bus_read(DATA_PAGE) == 0x80);
        check("N == 1", cpu_read_flag(N, &cpu) == 1);
    }

    // ASL Absolute,X - carry out
    {
        test_reset();
        test_header("ASL ABS,X - mem[0x0300+X=0x02]=0x02 (addr 0x0302), val=0x80 -> carry");
        cpu.regs.X = 0x02;
        bus_write(DATA_PAGE + 0x02, 0x80);
        bus_write(PRG_START, OPC_ASL_ABSX);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);
        printf("  base=0x0300, X=0x02 -> addr=0x0302, val=0x80 -> 0x00 carry out\n");
        printf(" Before:\n");
        print_mem_range(DATA_PAGE + 0x02, 1);

        cpu_execute(7, &cpu);

        printf(" After:\n");
        print_mem_range(DATA_PAGE + 0x02, 1);
        print_flags(&cpu);
        check("mem[0x0302] == 0x00", bus_read(DATA_PAGE + 0x02) == 0x00);
        check("C == 1 (carry out)", cpu_read_flag(C, &cpu) == 1);
        check("Z == 1", cpu_read_flag(Z, &cpu) == 1);
    }
}

// --- Branch Tests ---

void test_branches() {
    printf("\n========== BRANCH TESTS ==========\n");

    // BCC - not taken (C=1)
    {
        test_reset();
        test_header("BCC - not taken (C=1), PC unchanged");
        cpu_set_flag(C, 1, &cpu);
        bus_write(PRG_START, OPC_BCC_REL);
        bus_write(PRG_START + 1, 0x10);  // offset +16
        print_program(PRG_START, 2);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BCC - taken, no page cross
    {
        test_reset();
        test_header("BCC - taken (C=0), forward offset +0x10");
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_BCC_REL);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        // After fetch opcode+operand, PC=0x0202, then +0x10 = 0x0212
        Word expected_pc = PRG_START + 2 + 0x10;

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        printf("  Expected PC=0x%04X\n", expected_pc);
        check("PC == 0x0212 (taken)", cpu.PC == expected_pc);
    }

    // BCC - taken, page cross (branch near end of page)
    {
        test_reset();
        // Place branch at 0x02FD: after fetching opcode+operand, PC=0x02FF
        // offset=+0x01 -> new_PC=0x0300, which crosses from page 2 to page 3
        Word branch_addr = 0x02FD;
        cpu.PC = branch_addr;
        cpu_set_flag(C, 0, &cpu);
        bus_write(branch_addr, OPC_BCC_REL);
        bus_write(branch_addr + 1, 0x01);
        print_program(branch_addr, 2);
        test_header("BCC - taken, page cross (0x02FD+2+0x01=0x0300)");
        printf("  Branch at 0x02FD, offset=+1 -> 0x02FF+1=0x0300 (page cross!)\n");

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("PC == 0x0300 (page cross taken)", cpu.PC == 0x0300);
    }

    // BCS - not taken (C=0)
    {
        test_reset();
        test_header("BCS - not taken (C=0)");
        cpu_set_flag(C, 0, &cpu);
        bus_write(PRG_START, OPC_BCS_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BCS - taken (C=1)
    {
        test_reset();
        test_header("BCS - taken (C=1), offset +0x08");
        cpu_set_flag(C, 1, &cpu);
        bus_write(PRG_START, OPC_BCS_REL);
        bus_write(PRG_START + 1, 0x08);
        Word expected_pc = PRG_START + 2 + 0x08;

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        check("PC == 0x020A (taken)", cpu.PC == expected_pc);
    }

    // BNE - not taken (Z=1)
    {
        test_reset();
        test_header("BNE - not taken (Z=1)");
        cpu_set_flag(Z, 1, &cpu);
        bus_write(PRG_START, OPC_BNE_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BNE - taken (Z=0)
    {
        test_reset();
        test_header("BNE - taken (Z=0), offset +0x05");
        cpu_set_flag(Z, 0, &cpu);
        bus_write(PRG_START, OPC_BNE_REL);
        bus_write(PRG_START + 1, 0x05);
        Word expected_pc = PRG_START + 2 + 0x05;

        cpu_execute(3, &cpu);

        check("PC == 0x0207 (taken)", cpu.PC == expected_pc);
    }

    // BEQ - not taken (Z=0)
    {
        test_reset();
        test_header("BEQ - not taken (Z=0)");
        cpu_set_flag(Z, 0, &cpu);
        bus_write(PRG_START, OPC_BEQ_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BEQ - taken (Z=1)
    {
        test_reset();
        test_header("BEQ - taken (Z=1), offset +0x05");
        cpu_set_flag(Z, 1, &cpu);
        bus_write(PRG_START, OPC_BEQ_REL);
        bus_write(PRG_START + 1, 0x05);
        Word expected_pc = PRG_START + 2 + 0x05;

        cpu_execute(3, &cpu);

        check("PC == 0x0207 (taken)", cpu.PC == expected_pc);
    }

    // BPL - not taken (N=1)
    {
        test_reset();
        test_header("BPL - not taken (N=1)");
        cpu_set_flag(N, 1, &cpu);
        bus_write(PRG_START, OPC_BPL_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BPL - taken (N=0), backward branch
    {
        test_reset();
        test_header("BPL - taken (N=0), backward offset -0x10 (0xF0)");
        cpu_set_flag(N, 0, &cpu);
        bus_write(PRG_START, OPC_BPL_REL);
        bus_write(PRG_START + 1, 0xF0);  // signed: -16
        // After fetch: PC=0x0202, +(-16) = 0x01F2
        Word expected_pc = (Word)(PRG_START + 2 + (int8_t)0xF0);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        printf("  Expected PC=0x%04X\n", expected_pc);
        check("PC == 0x01F2 (backward branch taken)", cpu.PC == expected_pc);
    }

    // BMI - not taken (N=0)
    {
        test_reset();
        test_header("BMI - not taken (N=0)");
        cpu_set_flag(N, 0, &cpu);
        bus_write(PRG_START, OPC_BMI_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BMI - taken (N=1)
    {
        test_reset();
        test_header("BMI - taken (N=1), offset +0x06");
        cpu_set_flag(N, 1, &cpu);
        bus_write(PRG_START, OPC_BMI_REL);
        bus_write(PRG_START + 1, 0x06);
        Word expected_pc = PRG_START + 2 + 0x06;

        cpu_execute(3, &cpu);

        check("PC == 0x0208 (taken)", cpu.PC == expected_pc);
    }

    // BVC - not taken (V=1)
    {
        test_reset();
        test_header("BVC - not taken (V=1)");
        cpu_set_flag(V, 1, &cpu);
        bus_write(PRG_START, OPC_BVC_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BVC - taken (V=0)
    {
        test_reset();
        test_header("BVC - taken (V=0), offset +0x04");
        cpu_set_flag(V, 0, &cpu);
        bus_write(PRG_START, OPC_BVC_REL);
        bus_write(PRG_START + 1, 0x04);
        Word expected_pc = PRG_START + 2 + 0x04;

        cpu_execute(3, &cpu);

        check("PC == 0x0206 (taken)", cpu.PC == expected_pc);
    }

    // BVS - not taken (V=0)
    {
        test_reset();
        test_header("BVS - not taken (V=0)");
        cpu_set_flag(V, 0, &cpu);
        bus_write(PRG_START, OPC_BVS_REL);
        bus_write(PRG_START + 1, 0x10);
        Word expected_pc = PRG_START + 2;

        cpu_execute(2, &cpu);

        check("PC == PRG_START+2 (not taken)", cpu.PC == expected_pc);
    }

    // BVS - taken (V=1)
    {
        test_reset();
        test_header("BVS - taken (V=1), offset +0x04");
        cpu_set_flag(V, 1, &cpu);
        bus_write(PRG_START, OPC_BVS_REL);
        bus_write(PRG_START + 1, 0x04);
        Word expected_pc = PRG_START + 2 + 0x04;

        cpu_execute(3, &cpu);

        check("PC == 0x0206 (taken)", cpu.PC == expected_pc);
    }
}

// --- BIT Tests ---

void test_bit() {
    printf("\n========== BIT TESTS ==========\n");

    // BIT ZP - Z=1 when AND result is zero
    {
        test_reset();
        test_header("BIT ZP - A=0x0F & ZP[0x10]=0xF0 -> Z=1 (no bits in common)");
        cpu.regs.A = 0x0F;
        bus_write(0x10, 0xF0);
        bus_write(PRG_START, OPC_BIT_ZP);
        bus_write(PRG_START + 1, 0x10);
        print_program(PRG_START, 2);
        printf(" Before:\n");
        print_regs(&cpu);
        print_mem_range(0x10, 1);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A unchanged == 0x0F", cpu.regs.A == 0x0F);
        check("Z == 1 (AND result is zero)", cpu_read_flag(Z, &cpu) == 1);
        check("N == 1 (bit7 of mem=0xF0)", cpu_read_flag(N, &cpu) == 1);
        check("V == 1 (bit6 of mem=0xF0)", cpu_read_flag(V, &cpu) == 1);
    }

    // BIT ZP - Z=0, N and V from memory bits
    {
        test_reset();
        test_header("BIT ZP - A=0xFF & ZP[0x20]=0x7F -> Z=0, N=0, V=1");
        cpu.regs.A = 0xFF;
        bus_write(0x20, 0x7F);  // 0111 1111: bit7=0, bit6=1
        bus_write(PRG_START, OPC_BIT_ZP);
        bus_write(PRG_START + 1, 0x20);
        print_program(PRG_START, 2);

        cpu_execute(3, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("Z == 0 (AND result non-zero)", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0 (bit7 of 0x7F is 0)", cpu_read_flag(N, &cpu) == 0);
        check("V == 1 (bit6 of 0x7F is 1)", cpu_read_flag(V, &cpu) == 1);
    }

    // BIT ZP - all zero flags clear
    {
        test_reset();
        test_header("BIT ZP - A=0xFF & ZP[0x30]=0x01 -> Z=0, N=0, V=0");
        cpu.regs.A = 0xFF;
        bus_write(0x30, 0x01);  // bit7=0, bit6=0
        bus_write(PRG_START, OPC_BIT_ZP);
        bus_write(PRG_START + 1, 0x30);

        cpu_execute(3, &cpu);

        print_flags(&cpu);
        check("Z == 0", cpu_read_flag(Z, &cpu) == 0);
        check("N == 0 (bit7=0)", cpu_read_flag(N, &cpu) == 0);
        check("V == 0 (bit6=0)", cpu_read_flag(V, &cpu) == 0);
    }

    // BIT Absolute
    {
        test_reset();
        test_header("BIT ABS - A=0x00 & mem[0x0300]=0xC0 -> Z=1, N=1, V=1");
        cpu.regs.A = 0x00;
        bus_write(DATA_PAGE, 0xC0);  // bit7=1, bit6=1
        bus_write(PRG_START, OPC_BIT_ABS);
        bus_write_word(PRG_START + 1, DATA_PAGE);
        print_program(PRG_START, 3);

        cpu_execute(4, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        check("A unchanged == 0x00", cpu.regs.A == 0x00);
        check("Z == 1 (0x00 & 0xC0 = 0)", cpu_read_flag(Z, &cpu) == 1);
        check("N == 1 (bit7 of 0xC0)", cpu_read_flag(N, &cpu) == 1);
        check("V == 1 (bit6 of 0xC0)", cpu_read_flag(V, &cpu) == 1);
    }
}

// --- BRK Tests ---

void test_brk() {
    printf("\n========== BRK TESTS ==========\n");

    // BRK - basic: push PC+2, push flags (B set), jump to IRQ vector
    {
        test_reset();
        test_header("BRK - push PC, push flags w/ B set, jump to IRQ vector");

        // Set IRQ vector at 0xFFFE/0xFFFF -> 0x1234
        bus_write(0xFFFE, 0x34);
        bus_write(0xFFFF, 0x12);

        // BRK at PRG_START (0x0200)
        bus_write(PRG_START, OPC_BRK_IMP);
        print_program(PRG_START, 1);
        printf(" Before:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        printf("  SP=0x%02X\n", cpu.SP);
        printf("  IRQ vector -> 0x1234\n");

        // After fetching opcode PC=0x0201, stack_PC = 0x0201+2 = 0x0203
        Byte flags_before = cpu.flags;
        cpu_execute(7, &cpu);

        printf(" After:\n");
        print_regs(&cpu);
        print_flags(&cpu);
        printf("  SP=0x%02X\n", cpu.SP);
        printf("  Stack[0x1FF]=0x%02X (PC hi), Stack[0x1FE]=0x%02X (PC lo)\n",
               bus_read(0x1FF), bus_read(0x1FE));
        printf("  Stack[0x1FD]=0x%02X (pushed flags)\n", bus_read(0x1FD));

        // PC after fetch opcode = 0x0201; stack_PC = 0x0201 + 2 = 0x0203
        Word pushed_pc = ((Word)bus_read(0x1FF) << 8) | bus_read(0x1FE);
        Byte pushed_flags = bus_read(0x1FD);

        check("PC == 0x1234 (IRQ vector)", cpu.PC == 0x1234);
        check("SP == 0xFC (3 bytes pushed)", cpu.SP == 0xFC);
        check("Pushed PC == 0x0203", pushed_pc == 0x0203);
        check("Pushed flags has B set (bit4)", (pushed_flags >> 4) & 1);
        check("I flag set after BRK", cpu_read_flag(I, &cpu) == 1);
        check("B flag cleared after BRK (not in cpu.flags)", cpu_read_flag(B, &cpu) == 0);
    }
}

// --- Memory Read/Write Tests ---

void test_mem_rw() {
    printf("\n========== MEM READ/WRITE TESTS ==========\n");

    // Basic write then read
    {
        test_reset();
        test_header("MEM RW - write 0xAB to 0x0400, read it back");
        mem_write(0x0400, 0xAB);
        printf("  mem[0x0400] = 0x%02X\n", mem_read(0x0400));
        check("mem_read == 0xAB after mem_write", mem_read(0x0400) == 0xAB);
    }

    // Zero page write/read
    {
        test_reset();
        test_header("MEM RW - zero page 0x0042");
        mem_write(0x0042, 0x55);
        check("ZP mem_read == 0x55", mem_read(0x0042) == 0x55);
    }

    // Overwrite same address
    {
        test_reset();
        test_header("MEM RW - overwrite 0x0010: 0xAA -> 0xBB");
        mem_write(0x0010, 0xAA);
        check("First write == 0xAA", mem_read(0x0010) == 0xAA);
        mem_write(0x0010, 0xBB);
        check("Overwrite -> 0xBB", mem_read(0x0010) == 0xBB);
    }

    // Write zero byte
    {
        test_reset();
        test_header("MEM RW - write 0xFF then overwrite with 0x00");
        mem_write(0x0500, 0xFF);
        mem_write(0x0500, 0x00);
        check("mem_read == 0x00", mem_read(0x0500) == 0x00);
    }

    // Reset clears memory
    {
        test_reset();
        test_header("MEM RESET - cleared on bus_reset");
        mem_write(0x0300, 0xDE);
        mem_write(0x00FF, 0xAD);
        bus_reset();
        check("mem[0x0300] == 0x00 after reset", mem_read(0x0300) == 0x00);
        check("mem[0x00FF] == 0x00 after reset", mem_read(0x00FF) == 0x00);
    }

    // Adjacent addresses independent
    {
        test_reset();
        test_header("MEM RW - adjacent addresses 0x0200/0x0201 are independent");
        mem_write(0x0200, 0x11);
        mem_write(0x0201, 0x22);
        check("mem[0x0200] == 0x11", mem_read(0x0200) == 0x11);
        check("mem[0x0201] == 0x22", mem_read(0x0201) == 0x22);
    }

    // High address
    {
        test_reset();
        test_header("MEM RW - high address 0xFFF0");
        mem_write(0xFFF0, 0x7E);
        check("mem[0xFFF0] == 0x7E", mem_read(0xFFF0) == 0x7E);
    }
}

// --- Stack Push/Pop Tests ---

void test_stack() {
    printf("\n========== STACK PUSH/POP TESTS ==========\n");

    // Single push: SP decrements, value lands on stack
    {
        test_reset();
        test_header("STACK PUSH - push 0x42, SP decrements");
        Byte sp_before = cpu.SP;
        stack_push(0x42, &cpu);
        printf("  SP: 0x%02X -> 0x%02X, Stack[0x1%02X]=0x%02X\n",
               sp_before, cpu.SP, sp_before, bus_read(0x100 + sp_before));
        check("SP decremented", cpu.SP == (Byte)(sp_before - 1));
        check("Value on stack == 0x42", bus_read(0x100 + sp_before) == 0x42);
    }

    // Round-trip push/pop
    {
        test_reset();
        test_header("STACK PUSH/POP - round trip 0xBE");
        Byte sp_orig = cpu.SP;
        stack_push(0xBE, &cpu);
        Byte popped = stack_pop(&cpu);
        check("SP restored", cpu.SP == sp_orig);
        check("Popped == 0xBE", popped == 0xBE);
    }

    // LIFO order: 3 values
    {
        test_reset();
        test_header("STACK LIFO - push 0x11, 0x22, 0x33 -> pop 0x33, 0x22, 0x11");
        stack_push(0x11, &cpu);
        stack_push(0x22, &cpu);
        stack_push(0x33, &cpu);
        printf("  SP after 3 pushes = 0x%02X\n", cpu.SP);
        Byte p3 = stack_pop(&cpu);
        Byte p2 = stack_pop(&cpu);
        Byte p1 = stack_pop(&cpu);
        printf("  Popped: 0x%02X, 0x%02X, 0x%02X\n", p3, p2, p1);
        check("Pop 1 == 0x33 (LIFO)", p3 == 0x33);
        check("Pop 2 == 0x22", p2 == 0x22);
        check("Pop 3 == 0x11", p1 == 0x11);
        check("SP back to 0xFF", cpu.SP == 0xFF);
    }

    // Stack lives in page 1 (0x100-0x1FF)
    {
        test_reset();
        test_header("STACK PAGE - stack page 0x01 (0x1FF down to 0x100)");
        cpu.SP = 0xFF;
        stack_push(0xCA, &cpu);
        stack_push(0xFE, &cpu);
        printf("  Stack[0x1FF]=0x%02X, Stack[0x1FE]=0x%02X\n",
               bus_read(0x1FF), bus_read(0x1FE));
        check("Stack[0x1FF] == 0xCA", bus_read(0x1FF) == 0xCA);
        check("Stack[0x1FE] == 0xFE", bus_read(0x1FE) == 0xFE);
    }

    // Push word big-endian (hi first, lo second) - like BRK does for PC
    {
        test_reset();
        test_header("STACK WORD - push 0xABCD hi-then-lo, recover word");
        Word word = 0xABCD;
        stack_push((word >> 8) & 0xFF, &cpu);  // push hi
        stack_push(word & 0xFF, &cpu);          // push lo
        Byte got_lo = stack_pop(&cpu);
        Byte got_hi = stack_pop(&cpu);
        Word recovered = got_lo | ((Word)got_hi << 8);
        printf("  Pushed 0x%04X, recovered 0x%04X\n", word, recovered);
        check("Recovered word == 0xABCD", recovered == word);
        check("SP restored to 0xFF", cpu.SP == 0xFF);
    }

    // Push/pop 0x00 (edge: zero value)
    {
        test_reset();
        test_header("STACK PUSH/POP - zero value 0x00");
        stack_push(0x00, &cpu);
        Byte val = stack_pop(&cpu);
        check("Popped == 0x00", val == 0x00);
    }

    // Push/pop 0xFF (edge: max value)
    {
        test_reset();
        test_header("STACK PUSH/POP - max value 0xFF");
        stack_push(0xFF, &cpu);
        Byte val = stack_pop(&cpu);
        check("Popped == 0xFF", val == 0xFF);
    }
}

// --- Menu ---

void print_menu() {
    printf("\n=== 6502 Emulator Test Suite ===\n");
    printf("Select instruction set to test:\n");
    printf("  0. Mem read/write + Stack push/pop\n");
    printf("  1. LDA (all addressing modes)\n");
    printf("  2. STA (all addressing modes)\n");
    printf("  3. ADC (immediate - flag corner cases)\n");
    printf("  4. AND (all addressing modes)\n");
    printf("  5. ASL (all addressing modes)\n");
    printf("  6. Branches (BCC/BCS/BNE/BEQ/BPL/BMI/BVC/BVS)\n");
    printf("  7. BIT (zero page and absolute)\n");
    printf("  8. BRK (implied)\n");
    printf("  a. Run all tests\n");
    printf("  q. Quit\n");
    printf("Choice: ");
}

void print_summary() {
    printf("\n=== SUMMARY ===\n");
    printf("Passed: %d\n", test_pass);
    printf("Failed: %d\n", test_fail);
    if (test_fail == 0) {
        printf("All tests passed!\n");
    }
}

int main() {
    char choice;

    while (1) {
        test_pass = 0;
        test_fail = 0;
        print_menu();

        choice = getchar();
        // consume newline
        while (getchar() != '\n');

        switch (choice) {
            case '0':
                test_mem_rw();
                test_stack();
                print_summary();
                break;
            case '1':
                test_lda();
                print_summary();
                break;
            case '2':
                test_sta();
                print_summary();
                break;
            case '3':
                test_adc();
                print_summary();
                break;
            case '4':
                test_and();
                print_summary();
                break;
            case '5':
                test_asl();
                print_summary();
                break;
            case '6':
                test_branches();
                print_summary();
                break;
            case '7':
                test_bit();
                print_summary();
                break;
            case '8':
                test_brk();
                print_summary();
                break;
            case 'a':
                test_mem_rw();
                test_stack();
                test_lda();
                test_sta();
                test_adc();
                test_and();
                test_asl();
                test_branches();
                test_bit();
                test_brk();
                print_summary();
                break;
            case 'q':
                printf("Bye.\n");
                return 0;
            default:
                printf("Invalid choice.\n");
                break;
        }
    }

    return 0;
}
