#include <stdio.h>

#include "types.h"
#include "bus.h"
#include "cpu.h"
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

// --- Menu ---

void print_menu() {
    printf("\n=== 6502 Emulator Test Suite ===\n");
    printf("Select instruction set to test:\n");
    printf("  1. LDA (all addressing modes)\n");
    printf("  2. STA (all addressing modes)\n");
    printf("  3. ADC (immediate - flag corner cases)\n");
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
            case 'a':
                test_lda();
                test_sta();
                test_adc();
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
