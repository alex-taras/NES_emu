#include <string.h>
#include <stdio.h>

#include "cpu.h"

#include "bus.h"
#include "opcodes.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/*  Type definitions                                                   */
/* ------------------------------------------------------------------ */

typedef Byte (*AddrModeFn)(CPU *cpu);
typedef Byte (*OpcodeFn)(CPU *cpu);

typedef struct {
    const char *name;
    OpcodeFn    op;
    AddrModeFn  mode;
    Byte        cycles;
} Instruction;

typedef enum {
    AM_IMP, AM_ACC, AM_IMM, AM_ZP0, AM_ZPX, AM_ZPY,
    AM_REL, AM_ABS, AM_ABX, AM_ABY, AM_IZX, AM_IZY,
    AM_IND
} AddrModeId;

/* ------------------------------------------------------------------ */
/*  CPU lifecycle                                                      */
/* ------------------------------------------------------------------ */

void cpu_reset(CPU *cpu) {
    /* NES reset leaves SP at FD */
    cpu->SP = 0xFD;

    cpu->regs.A = 0x00;
    cpu->regs.X = 0x00;
    cpu->regs.Y = 0x00;
               /*NVUBDIZC*/
    cpu->flags = 0b00100100;

    cpu->opcode       = 0;
    cpu->addr_abs     = 0;
    cpu->fetched      = 0;
    cpu->page_crossed = 0;
    cpu->addr_mode_id = AM_IMP;
    cpu->cycles       = 0;
    cpu->nmi_pending  = 0;
    cpu->cycles_remaining = 0;

    bus_reset();

    /* Read reset vector from 0xFFFC/0xFFFD (little-endian).
       Returns 0x0000 until a mapper/ROM is connected. */
    Byte lo = bus_read(0xFFFC);
    Byte hi = bus_read(0xFFFD);
    cpu->PC = (Word)lo | ((Word)hi << 8);
}

/* ------------------------------------------------------------------ */
/*  Stack                                                              */
/* ------------------------------------------------------------------ */

void stack_push(Byte value, CPU *cpu) {
    bus_write(0x100 + cpu->SP, value);
    cpu->SP -= 1;
}

Byte stack_pop(CPU *cpu) {
    cpu->SP += 1;
    return bus_read(0x100 + cpu->SP);
}

/* ------------------------------------------------------------------ */
/*  Flags                                                              */
/* ------------------------------------------------------------------ */

Byte cpu_read_flag(Flags flag, CPU *cpu) {
    return (cpu->flags >> flag) & 0x01;
}

void cpu_set_flag(Flags flag, Byte value, CPU *cpu) {
    Byte mask = 0x01 << flag;

    if (value == 0) {
        cpu->flags &= ~mask;
    } else {
        cpu->flags |= mask;
    }

    /* force U on 1 */
    mask = 0x01 << U;
    cpu->flags |= mask;
}

void cpu_toggle_flag(Flags flag, CPU *cpu) {
    Byte mask = 0x01 << flag;
    cpu->flags ^= mask;
}

/* ------------------------------------------------------------------ */
/*  Memory helpers                                                     */
/* ------------------------------------------------------------------ */

Byte fetch_program_byte(CPU *cpu) {
    Byte data = bus_read(cpu->PC);
    cpu->PC++;
    return data;
}

Word fetch_program_word(CPU *cpu) {
    Byte data_lo = fetch_program_byte(cpu);
    Byte data_hi = fetch_program_byte(cpu);
    return data_lo | (data_hi << 8);
}

/* ------------------------------------------------------------------ */
/*  Addressing modes                                                   */
/* ------------------------------------------------------------------ */

static Byte am_IMP(CPU *cpu) {
    cpu->addr_mode_id = AM_IMP;
    return 0;
}

static Byte am_ACC(CPU *cpu) {
    cpu->addr_mode_id = AM_ACC;
    return 0;
}

static Byte am_IMM(CPU *cpu) {
    cpu->addr_abs     = cpu->PC++;
    cpu->addr_mode_id = AM_IMM;
    return 0;
}

static Byte am_ZP0(CPU *cpu) {
    cpu->addr_abs     = fetch_program_byte(cpu) & 0xFF;
    cpu->addr_mode_id = AM_ZP0;
    return 0;
}

static Byte am_ZPX(CPU *cpu) {
    cpu->addr_abs     = (fetch_program_byte(cpu) + cpu->regs.X) & 0xFF;
    cpu->addr_mode_id = AM_ZPX;
    return 0;
}

static Byte am_ZPY(CPU *cpu) {
    cpu->addr_abs     = (fetch_program_byte(cpu) + cpu->regs.Y) & 0xFF;
    cpu->addr_mode_id = AM_ZPY;
    return 0;
}

static Byte am_REL(CPU *cpu) {
    Byte raw          = fetch_program_byte(cpu);
    cpu->addr_abs     = (Word)(int8_t)raw;
    cpu->addr_mode_id = AM_REL;
    return 0;
}

static Byte am_ABS(CPU *cpu) {
    cpu->addr_abs     = fetch_program_word(cpu);
    cpu->addr_mode_id = AM_ABS;
    return 0;
}

static Byte am_ABX(CPU *cpu) {
    Word base         = fetch_program_word(cpu);
    cpu->addr_abs     = base + cpu->regs.X;
    cpu->addr_mode_id = AM_ABX;
    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

static Byte am_ABY(CPU *cpu) {
    Word base         = fetch_program_word(cpu);
    cpu->addr_abs     = base + cpu->regs.Y;
    cpu->addr_mode_id = AM_ABY;
    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

static Byte am_IZX(CPU *cpu) {
    Byte zp           = (fetch_program_byte(cpu) + cpu->regs.X) & 0xFF;
    cpu->addr_abs     = bus_read(zp) | (bus_read((zp + 1) & 0xFF) << 8);
    cpu->addr_mode_id = AM_IZX;
    return 0;
}

static Byte am_IZY(CPU *cpu) {
    Byte zp           = fetch_program_byte(cpu);
    Word base         = bus_read(zp) | (bus_read((zp + 1) & 0xFF) << 8);
    cpu->addr_abs     = base + cpu->regs.Y;
    cpu->addr_mode_id = AM_IZY;
    return (cpu->addr_abs & 0xFF00) != (base & 0xFF00) ? 1 : 0;
}

static Byte am_IND(CPU *cpu) {
    Word ptr = fetch_program_word(cpu);
    /* 6502 page-wrap bug: if low byte of ptr is 0xFF, high byte wraps within
       the same page instead of crossing to the next page. */
    Byte lo = bus_read(ptr);
    Byte hi = bus_read((ptr & 0xFF00) | ((ptr + 1) & 0xFF));
    cpu->addr_abs     = lo | (hi << 8);
    cpu->addr_mode_id = AM_IND;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  fetch()                                                            */
/* ------------------------------------------------------------------ */

static Byte fetch(CPU *cpu) {
    if (cpu->addr_mode_id == AM_IMP) return 0;
    if (cpu->addr_mode_id == AM_ACC) cpu->fetched = cpu->regs.A;
    else                             cpu->fetched = bus_read(cpu->addr_abs);
    return cpu->fetched;
}

/* ------------------------------------------------------------------ */
/*  Flag helpers                                                       */
/* ------------------------------------------------------------------ */

static void set_NZ_from(Byte v, CPU *cpu) {
    cpu_set_flag(Z, v == 0, cpu);
    cpu_set_flag(N, (v >> 7) & 1, cpu);
}

static void set_NZ_flags(CPU *cpu) {
    set_NZ_from(cpu->regs.A, cpu);
}

void set_CMP_flags(Word result, CPU *cpu) {
    cpu_set_flag(N, read_bit(result & 0xFF, 7), cpu);
    cpu_set_flag(Z, (result & 0xFF) == 0x00, cpu);
    cpu_set_flag(C, result <= 0xFF, cpu);
}

/* ------------------------------------------------------------------ */
/*  Branch helper                                                      */
/* ------------------------------------------------------------------ */

static void branch_if(CPU *cpu, Byte cond) {
    if (!cond) return;
    Word new_pc = cpu->PC + (int8_t)(cpu->addr_abs & 0xFF);
    /* +1 for taken, +1 more for page cross */
    cpu->cycles += 1 + ((cpu->PC & 0xFF00) != (new_pc & 0xFF00));
    cpu->PC = new_pc;
}

/* ------------------------------------------------------------------ */
/*  Opcode implementations                                             */
/* ------------------------------------------------------------------ */

/* --- LDA --- */
static Byte op_LDA(CPU *cpu) {
    fetch(cpu);
    cpu->regs.A = cpu->fetched;
    set_NZ_flags(cpu);
    return 1; /* page-cross-sensitive */
}

/* --- STA --- */
static Byte op_STA(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->regs.A);
    return 0;
}

/* --- ADC --- */
static Byte op_ADC(CPU *cpu) {
    fetch(cpu);
    Byte operand = cpu->fetched;
    Word result  = cpu->regs.A + operand + cpu_read_flag(C, cpu);
    cpu_set_flag(C, result > 0xFF, cpu);
    cpu_set_flag(V, ((cpu->regs.A ^ result) & (operand ^ result) & 0x80) != 0, cpu);
    cpu->regs.A = result & 0xFF;
    set_NZ_flags(cpu);
    return 1;
}

/* --- AND --- */
static Byte op_AND(CPU *cpu) {
    fetch(cpu);
    cpu->regs.A &= cpu->fetched;
    set_NZ_flags(cpu);
    return 1;
}

/* --- ASL --- */
static Byte op_ASL(CPU *cpu) {
    fetch(cpu);
    Word shifted = cpu->fetched << 1;
    cpu_set_flag(C, shifted > 0xFF, cpu);
    cpu_set_flag(Z, (shifted & 0xFF) == 0, cpu);
    cpu_set_flag(N, ((shifted & 0xFF) >> 7) & 1, cpu);
    if (cpu->addr_mode_id == AM_ACC)
        cpu->regs.A = shifted & 0xFF;
    else
        bus_write(cpu->addr_abs, shifted & 0xFF);
    return 0;
}

/* --- Branches --- */
static Byte op_BCC(CPU *cpu) { branch_if(cpu, cpu_read_flag(C, cpu) == 0); return 0; }
static Byte op_BCS(CPU *cpu) { branch_if(cpu, cpu_read_flag(C, cpu) == 1); return 0; }
static Byte op_BNE(CPU *cpu) { branch_if(cpu, cpu_read_flag(Z, cpu) == 0); return 0; }
static Byte op_BEQ(CPU *cpu) { branch_if(cpu, cpu_read_flag(Z, cpu) == 1); return 0; }
static Byte op_BPL(CPU *cpu) { branch_if(cpu, cpu_read_flag(N, cpu) == 0); return 0; }
static Byte op_BMI(CPU *cpu) { branch_if(cpu, cpu_read_flag(N, cpu) == 1); return 0; }
static Byte op_BVC(CPU *cpu) { branch_if(cpu, cpu_read_flag(V, cpu) == 0); return 0; }
static Byte op_BVS(CPU *cpu) { branch_if(cpu, cpu_read_flag(V, cpu) == 1); return 0; }

/* --- BIT --- */
static Byte op_BIT(CPU *cpu) {
    fetch(cpu);
    cpu_set_flag(Z, (cpu->regs.A & cpu->fetched) == 0, cpu);
    cpu_set_flag(N, read_bit(cpu->fetched, 7), cpu);
    cpu_set_flag(V, read_bit(cpu->fetched, 6), cpu);
    return 0;
}

/* --- BRK --- */
static Byte op_BRK(CPU *cpu) {
    /* After dispatch fetched opcode, PC = BRK_addr+1.
       Push PC+1 to skip the padding byte; RTI returns to BRK_addr+2. */
    Word stack_PC    = cpu->PC + 1;
    Byte stack_PC_lo = stack_PC & 0xFF;
    Byte stack_PC_hi = (stack_PC >> 8) & 0xFF;
    stack_push(stack_PC_hi, cpu);
    stack_push(stack_PC_lo, cpu);

    cpu_set_flag(I, 1, cpu);
    cpu_set_flag(B, 1, cpu);
    stack_push(cpu->flags, cpu);
    cpu_set_flag(B, 0, cpu);

    Byte lo = bus_read(0xFFFE);
    Byte hi = bus_read(0xFFFF);
    cpu->PC = lo | (hi << 8);
    return 0;
}

/* --- Clear flags --- */
static Byte op_CLC(CPU *cpu) { cpu_set_flag(C, 0, cpu); return 0; }
static Byte op_CLD(CPU *cpu) { cpu_set_flag(D, 0, cpu); return 0; }
static Byte op_CLI(CPU *cpu) { cpu_set_flag(I, 0, cpu); return 0; }
static Byte op_CLV(CPU *cpu) { cpu_set_flag(V, 0, cpu); return 0; }

/* --- CMP --- */
static Byte op_CMP(CPU *cpu) {
    fetch(cpu);
    Word result = cpu->regs.A - cpu->fetched;
    set_CMP_flags(result, cpu);
    return 1;
}

/* --- CPX --- */
static Byte op_CPX(CPU *cpu) {
    fetch(cpu);
    Word result = cpu->regs.X - cpu->fetched;
    set_CMP_flags(result, cpu);
    return 0;
}

/* --- CPY --- */
static Byte op_CPY(CPU *cpu) {
    fetch(cpu);
    Word result = cpu->regs.Y - cpu->fetched;
    set_CMP_flags(result, cpu);
    return 0;
}

/* --- DEC --- */
static Byte op_DEC(CPU *cpu) {
    fetch(cpu);
    Byte result = cpu->fetched - 1;
    bus_write(cpu->addr_abs, result);
    set_NZ_from(result, cpu);
    return 0;
}

/* --- DEX --- */
static Byte op_DEX(CPU *cpu) {
    cpu->regs.X--;
    set_NZ_from(cpu->regs.X, cpu);
    return 0;
}

/* --- DEY --- */
static Byte op_DEY(CPU *cpu) {
    cpu->regs.Y--;
    set_NZ_from(cpu->regs.Y, cpu);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group A — Flag Sets                                                */
/* ------------------------------------------------------------------ */

/* SEC */
static Byte op_SEC(CPU *cpu) { cpu_set_flag(C, 1, cpu); return 0; }

/* SED */
static Byte op_SED(CPU *cpu) { cpu_set_flag(D, 1, cpu); return 0; }

/* SEI */
static Byte op_SEI(CPU *cpu) { cpu_set_flag(I, 1, cpu); return 0; }

/* ------------------------------------------------------------------ */
/*  Group B — NOP                                                      */
/* ------------------------------------------------------------------ */

/* NOP */
static Byte op_NOP(CPU *cpu) { (void)cpu; return 0; }

/* ------------------------------------------------------------------ */
/*  Group C — Register Transfers                                       */
/* ------------------------------------------------------------------ */

/* TAX */
static Byte op_TAX(CPU *cpu) {
    cpu->regs.X = cpu->regs.A;
    set_NZ_from(cpu->regs.X, cpu);
    return 0;
}

/* TAY */
static Byte op_TAY(CPU *cpu) {
    cpu->regs.Y = cpu->regs.A;
    set_NZ_from(cpu->regs.Y, cpu);
    return 0;
}

/* TXA */
static Byte op_TXA(CPU *cpu) {
    cpu->regs.A = cpu->regs.X;
    set_NZ_from(cpu->regs.A, cpu);
    return 0;
}

/* TYA */
static Byte op_TYA(CPU *cpu) {
    cpu->regs.A = cpu->regs.Y;
    set_NZ_from(cpu->regs.A, cpu);
    return 0;
}

/* TSX */
static Byte op_TSX(CPU *cpu) {
    cpu->regs.X = cpu->SP;
    set_NZ_from(cpu->regs.X, cpu);
    return 0;
}

/* TXS */
static Byte op_TXS(CPU *cpu) {
    cpu->SP = cpu->regs.X;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group D — INC/INX/INY                                              */
/* ------------------------------------------------------------------ */

/* INC */
static Byte op_INC(CPU *cpu) {
    fetch(cpu);
    Byte result = cpu->fetched + 1;
    bus_write(cpu->addr_abs, result);
    set_NZ_from(result, cpu);
    return 0;
}

/* INX */
static Byte op_INX(CPU *cpu) {
    cpu->regs.X++;
    set_NZ_from(cpu->regs.X, cpu);
    return 0;
}

/* INY */
static Byte op_INY(CPU *cpu) {
    cpu->regs.Y++;
    set_NZ_from(cpu->regs.Y, cpu);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group E — EOR, ORA, SBC                                            */
/* ------------------------------------------------------------------ */

/* EOR */
static Byte op_EOR(CPU *cpu) {
    fetch(cpu);
    cpu->regs.A ^= cpu->fetched;
    set_NZ_flags(cpu);
    return 1;
}

/* ORA */
static Byte op_ORA(CPU *cpu) {
    fetch(cpu);
    cpu->regs.A |= cpu->fetched;
    set_NZ_flags(cpu);
    return 1;
}

/* SBC */
static Byte op_SBC(CPU *cpu) {
    fetch(cpu);
    Byte operand = cpu->fetched ^ 0xFF;
    Word result  = cpu->regs.A + operand + cpu_read_flag(C, cpu);
    cpu_set_flag(C, result > 0xFF, cpu);
    cpu_set_flag(V, ((cpu->regs.A ^ result) & (operand ^ result) & 0x80) != 0, cpu);
    cpu->regs.A = result & 0xFF;
    set_NZ_flags(cpu);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Group F — LDX, LDY, STX, STY                                        */
/* ------------------------------------------------------------------ */

/* LDX */
static Byte op_LDX(CPU *cpu) {
    fetch(cpu);
    cpu->regs.X = cpu->fetched;
    set_NZ_from(cpu->regs.X, cpu);
    return 1;
}

/* LDY */
static Byte op_LDY(CPU *cpu) {
    fetch(cpu);
    cpu->regs.Y = cpu->fetched;
    set_NZ_from(cpu->regs.Y, cpu);
    return 1;
}

/* STX */
static Byte op_STX(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->regs.X);
    return 0;
}

/* STY */
static Byte op_STY(CPU *cpu) {
    bus_write(cpu->addr_abs, cpu->regs.Y);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group G — LSR, ROL, ROR                                            */
/* ------------------------------------------------------------------ */

/* LSR */
static Byte op_LSR(CPU *cpu) {
    fetch(cpu);
    cpu_set_flag(C, cpu->fetched & 0x01, cpu);
    Byte result = cpu->fetched >> 1;
    cpu_set_flag(N, 0, cpu);
    cpu_set_flag(Z, result == 0, cpu);
    if (cpu->addr_mode_id == AM_ACC) cpu->regs.A = result;
    else                             bus_write(cpu->addr_abs, result);
    return 0;
}

/* ROL */
static Byte op_ROL(CPU *cpu) {
    fetch(cpu);
    Byte old_c = cpu_read_flag(C, cpu);
    cpu_set_flag(C, (cpu->fetched >> 7) & 1, cpu);
    Byte result = (cpu->fetched << 1) | old_c;
    set_NZ_from(result, cpu);
    if (cpu->addr_mode_id == AM_ACC) cpu->regs.A = result;
    else                             bus_write(cpu->addr_abs, result);
    return 0;
}

/* ROR */
static Byte op_ROR(CPU *cpu) {
    fetch(cpu);
    Byte old_c = cpu_read_flag(C, cpu);
    cpu_set_flag(C, cpu->fetched & 0x01, cpu);
    Byte result = (old_c << 7) | (cpu->fetched >> 1);
    set_NZ_from(result, cpu);
    if (cpu->addr_mode_id == AM_ACC) cpu->regs.A = result;
    else                             bus_write(cpu->addr_abs, result);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group H — Stack Ops                                                */
/* ------------------------------------------------------------------ */

/* PHA */
static Byte op_PHA(CPU *cpu) {
    stack_push(cpu->regs.A, cpu);
    return 0;
}

/* PHP */
static Byte op_PHP(CPU *cpu) {
    stack_push(cpu->flags | 0x30, cpu);
    return 0;
}

/* PLA */
static Byte op_PLA(CPU *cpu) {
    cpu->regs.A = stack_pop(cpu);
    set_NZ_flags(cpu);
    return 0;
}

/* PLP */
static Byte op_PLP(CPU *cpu) {
    cpu->flags = (stack_pop(cpu) & 0xCF) | 0x20;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Group I — Jumps and Subroutines                                    */
/* ------------------------------------------------------------------ */

/* JMP */
static Byte op_JMP(CPU *cpu) {
    cpu->PC = cpu->addr_abs;
    return 0;
}

/* JSR */
static Byte op_JSR(CPU *cpu) {
    Word return_addr = cpu->PC - 1;
    stack_push((return_addr >> 8) & 0xFF, cpu);
    stack_push(return_addr & 0xFF, cpu);
    cpu->PC = cpu->addr_abs;
    return 0;
}

/* RTS */
static Byte op_RTS(CPU *cpu) {
    Byte lo = stack_pop(cpu);
    Byte hi = stack_pop(cpu);
    cpu->PC = ((hi << 8) | lo) + 1;
    return 0;
}

/* RTI */
static Byte op_RTI(CPU *cpu) {
    cpu->flags = (stack_pop(cpu) & 0xCF) | 0x20;
    Byte lo    = stack_pop(cpu);
    Byte hi    = stack_pop(cpu);
    cpu->PC    = (hi << 8) | lo;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Instruction lookup table                                           */
/* ------------------------------------------------------------------ */

static const Instruction INSTR_TABLE[256] = {
    /* BRK */
    [0x00] = { "BRK", op_BRK, am_IMP, 7 },

    /* AND */
    [OPC_AND_INDX]  = { "AND", op_AND, am_IZX, 6 },
    [OPC_AND_ZP]    = { "AND", op_AND, am_ZP0, 3 },
    [OPC_AND_IM]    = { "AND", op_AND, am_IMM, 2 },
    [OPC_AND_ABS]   = { "AND", op_AND, am_ABS, 4 },
    [OPC_AND_ABSY]  = { "AND", op_AND, am_ABY, 4 },
    [OPC_AND_ZPX]   = { "AND", op_AND, am_ZPX, 4 },
    [OPC_AND_ABSX]  = { "AND", op_AND, am_ABX, 4 },
    [OPC_AND_INDY]  = { "AND", op_AND, am_IZY, 5 },

    /* BIT */
    [OPC_BIT_ZP]    = { "BIT", op_BIT, am_ZP0, 3 },
    [OPC_BIT_ABS]   = { "BIT", op_BIT, am_ABS, 4 },

    /* Branches (base = 2) */
    [OPC_BPL_REL]   = { "BPL", op_BPL, am_REL, 2 },
    [OPC_BMI_REL]   = { "BMI", op_BMI, am_REL, 2 },
    [OPC_BVC_REL]   = { "BVC", op_BVC, am_REL, 2 },
    [OPC_BVS_REL]   = { "BVS", op_BVS, am_REL, 2 },
    [OPC_BCC_REL]   = { "BCC", op_BCC, am_REL, 2 },
    [OPC_BCS_REL]   = { "BCS", op_BCS, am_REL, 2 },
    [OPC_BNE_REL]   = { "BNE", op_BNE, am_REL, 2 },
    [OPC_BEQ_REL]   = { "BEQ", op_BEQ, am_REL, 2 },

    /* ASL */
    [OPC_ASL_ACC]   = { "ASL", op_ASL, am_ACC, 2 },
    [OPC_ASL_ZP]    = { "ASL", op_ASL, am_ZP0, 5 },
    [OPC_ASL_ZPX]   = { "ASL", op_ASL, am_ZPX, 6 },
    [OPC_ASL_ABS]   = { "ASL", op_ASL, am_ABS, 6 },
    [OPC_ASL_ABSX]  = { "ASL", op_ASL, am_ABX, 7 },

    /* ADC */
    [OPC_ADC_IM]    = { "ADC", op_ADC, am_IMM, 2 },
    [OPC_ADC_ZP]    = { "ADC", op_ADC, am_ZP0, 3 },
    [OPC_ADC_ZPX]   = { "ADC", op_ADC, am_ZPX, 4 },
    [OPC_ADC_ABS]   = { "ADC", op_ADC, am_ABS, 4 },
    [OPC_ADC_ABSX]  = { "ADC", op_ADC, am_ABX, 4 },
    [OPC_ADC_ABSY]  = { "ADC", op_ADC, am_ABY, 4 },
    [OPC_ADC_INDX]  = { "ADC", op_ADC, am_IZX, 6 },
    [OPC_ADC_INDY]  = { "ADC", op_ADC, am_IZY, 5 },

    /* LDA */
    [OPC_LDA_IM]    = { "LDA", op_LDA, am_IMM, 2 },
    [OPC_LDA_ZP]    = { "LDA", op_LDA, am_ZP0, 3 },
    [OPC_LDA_ZPX]   = { "LDA", op_LDA, am_ZPX, 4 },
    [OPC_LDA_ABS]   = { "LDA", op_LDA, am_ABS, 4 },
    [OPC_LDA_ABSX]  = { "LDA", op_LDA, am_ABX, 4 },
    [OPC_LDA_ABSY]  = { "LDA", op_LDA, am_ABY, 4 },
    [OPC_LDA_INDX]  = { "LDA", op_LDA, am_IZX, 6 },
    [OPC_LDA_INDY]  = { "LDA", op_LDA, am_IZY, 5 },

    /* STA -- no page-cross penalty, base cycles already account for it */
    [OPC_STA_ZP]    = { "STA", op_STA, am_ZP0, 3 },
    [OPC_STA_ZPX]   = { "STA", op_STA, am_ZPX, 4 },
    [OPC_STA_ABS]   = { "STA", op_STA, am_ABS, 4 },
    [OPC_STA_ABSX]  = { "STA", op_STA, am_ABX, 5 },
    [OPC_STA_ABSY]  = { "STA", op_STA, am_ABY, 5 },
    [OPC_STA_INDX]  = { "STA", op_STA, am_IZX, 6 },
    [OPC_STA_INDY]  = { "STA", op_STA, am_IZY, 6 },

    /* Clear flags */
    [OPC_CLC_IMP]   = { "CLC", op_CLC, am_IMP, 2 },
    [OPC_CLD_IMP]   = { "CLD", op_CLD, am_IMP, 2 },
    [OPC_CLI_IMP]   = { "CLI", op_CLI, am_IMP, 2 },
    [OPC_CLV_IMP]   = { "CLV", op_CLV, am_IMP, 2 },

    /* CMP */
    [OPC_CMP_IM]    = { "CMP", op_CMP, am_IMM, 2 },
    [OPC_CMP_ZP]    = { "CMP", op_CMP, am_ZP0, 3 },
    [OPC_CMP_ZPX]   = { "CMP", op_CMP, am_ZPX, 4 },
    [OPC_CMP_ABS]   = { "CMP", op_CMP, am_ABS, 4 },
    [OPC_CMP_ABSX]  = { "CMP", op_CMP, am_ABX, 4 },
    [OPC_CMP_ABSY]  = { "CMP", op_CMP, am_ABY, 4 },
    [OPC_CMP_INDX]  = { "CMP", op_CMP, am_IZX, 6 },
    [OPC_CMP_INDY]  = { "CMP", op_CMP, am_IZY, 5 },

    /* CPX */
    [OPC_CPX_IM]    = { "CPX", op_CPX, am_IMM, 2 },
    [OPC_CPX_ZP]    = { "CPX", op_CPX, am_ZP0, 3 },
    [OPC_CPX_ABS]   = { "CPX", op_CPX, am_ABS, 4 },

    /* CPY */
    [OPC_CPY_IM]    = { "CPY", op_CPY, am_IMM, 2 },
    [OPC_CPY_ZP]    = { "CPY", op_CPY, am_ZP0, 3 },
    [OPC_CPY_ABS]   = { "CPY", op_CPY, am_ABS, 4 },

    /* DEC */
    [OPC_DEC_ZP]    = { "DEC", op_DEC, am_ZP0, 5 },
    [OPC_DEC_ZPX]   = { "DEC", op_DEC, am_ZPX, 6 },
    [OPC_DEC_ABS]   = { "DEC", op_DEC, am_ABS, 6 },
    [OPC_DEC_ABSX]  = { "DEC", op_DEC, am_ABX, 7 },

    /* DEX, DEY */
    [OPC_DEX_IMP]   = { "DEX", op_DEX, am_IMP, 2 },
    [OPC_DEY_IMP]   = { "DEY", op_DEY, am_IMP, 2 },

    /* SEC, SED, SEI */
    [OPC_SEC_IMP]   = { "SEC", op_SEC, am_IMP, 2 },
    [OPC_SED_IMP]   = { "SED", op_SED, am_IMP, 2 },
    [OPC_SEI_IMP]   = { "SEI", op_SEI, am_IMP, 2 },

    /* NOP */
    [OPC_NOP_IMP]   = { "NOP", op_NOP, am_IMP, 2 },

    /* Register transfers */
    [OPC_TAX_IMP]   = { "TAX", op_TAX, am_IMP, 2 },
    [OPC_TAY_IMP]   = { "TAY", op_TAY, am_IMP, 2 },
    [OPC_TXA_IMP]   = { "TXA", op_TXA, am_IMP, 2 },
    [OPC_TYA_IMP]   = { "TYA", op_TYA, am_IMP, 2 },
    [OPC_TSX_IMP]   = { "TSX", op_TSX, am_IMP, 2 },
    [OPC_TXS_IMP]   = { "TXS", op_TXS, am_IMP, 2 },

    /* INC */
    [OPC_INC_ZP]    = { "INC", op_INC, am_ZP0, 5 },
    [OPC_INC_ZPX]   = { "INC", op_INC, am_ZPX, 6 },
    [OPC_INC_ABS]   = { "INC", op_INC, am_ABS, 6 },
    [OPC_INC_ABSX]  = { "INC", op_INC, am_ABX, 7 },

    /* INX, INY */
    [OPC_INX_IMP]   = { "INX", op_INX, am_IMP, 2 },
    [OPC_INY_IMP]   = { "INY", op_INY, am_IMP, 2 },

    /* EOR */
    [OPC_EOR_INDX]  = { "EOR", op_EOR, am_IZX, 6 },
    [OPC_EOR_ZP]    = { "EOR", op_EOR, am_ZP0, 3 },
    [OPC_EOR_IM]    = { "EOR", op_EOR, am_IMM, 2 },
    [OPC_EOR_ABS]   = { "EOR", op_EOR, am_ABS, 4 },
    [OPC_EOR_ZPX]   = { "EOR", op_EOR, am_ZPX, 4 },
    [OPC_EOR_ABSX]  = { "EOR", op_EOR, am_ABX, 4 },
    [OPC_EOR_ABSY]  = { "EOR", op_EOR, am_ABY, 4 },
    [OPC_EOR_INDY]  = { "EOR", op_EOR, am_IZY, 5 },

    /* ORA */
    [OPC_ORA_INDX]  = { "ORA", op_ORA, am_IZX, 6 },
    [OPC_ORA_ZP]    = { "ORA", op_ORA, am_ZP0, 3 },
    [OPC_ORA_IM]    = { "ORA", op_ORA, am_IMM, 2 },
    [OPC_ORA_ABS]   = { "ORA", op_ORA, am_ABS, 4 },
    [OPC_ORA_ZPX]   = { "ORA", op_ORA, am_ZPX, 4 },
    [OPC_ORA_ABSX]  = { "ORA", op_ORA, am_ABX, 4 },
    [OPC_ORA_ABSY]  = { "ORA", op_ORA, am_ABY, 4 },
    [OPC_ORA_INDY]  = { "ORA", op_ORA, am_IZY, 5 },

    /* SBC */
    [OPC_SBC_IM]    = { "SBC", op_SBC, am_IMM, 2 },
    [OPC_SBC_ZP]    = { "SBC", op_SBC, am_ZP0, 3 },
    [OPC_SBC_ZPX]   = { "SBC", op_SBC, am_ZPX, 4 },
    [OPC_SBC_ABS]   = { "SBC", op_SBC, am_ABS, 4 },
    [OPC_SBC_ABSX]  = { "SBC", op_SBC, am_ABX, 4 },
    [OPC_SBC_ABSY]  = { "SBC", op_SBC, am_ABY, 4 },
    [OPC_SBC_INDX]  = { "SBC", op_SBC, am_IZX, 6 },
    [OPC_SBC_INDY]  = { "SBC", op_SBC, am_IZY, 5 },

    /* LDX */
    [OPC_LDX_IM]    = { "LDX", op_LDX, am_IMM, 2 },
    [OPC_LDX_ZP]    = { "LDX", op_LDX, am_ZP0, 3 },
    [OPC_LDX_ZPY]   = { "LDX", op_LDX, am_ZPY, 4 },
    [OPC_LDX_ABS]   = { "LDX", op_LDX, am_ABS, 4 },
    [OPC_LDX_ABY]   = { "LDX", op_LDX, am_ABY, 4 },

    /* LDY */
    [OPC_LDY_IM]    = { "LDY", op_LDY, am_IMM, 2 },
    [OPC_LDY_ZP]    = { "LDY", op_LDY, am_ZP0, 3 },
    [OPC_LDY_ZPX]   = { "LDY", op_LDY, am_ZPX, 4 },
    [OPC_LDY_ABS]   = { "LDY", op_LDY, am_ABS, 4 },
    [OPC_LDY_ABX]   = { "LDY", op_LDY, am_ABX, 4 },

    /* STX */
    [OPC_STX_ZP]    = { "STX", op_STX, am_ZP0, 3 },
    [OPC_STX_ZPY]   = { "STX", op_STX, am_ZPY, 4 },
    [OPC_STX_ABS]   = { "STX", op_STX, am_ABS, 4 },

    /* STY */
    [OPC_STY_ZP]    = { "STY", op_STY, am_ZP0, 3 },
    [OPC_STY_ZPX]   = { "STY", op_STY, am_ZPX, 4 },
    [OPC_STY_ABS]   = { "STY", op_STY, am_ABS, 4 },

    /* LSR */
    [OPC_LSR_ACC]   = { "LSR", op_LSR, am_ACC, 2 },
    [OPC_LSR_ZP]    = { "LSR", op_LSR, am_ZP0, 5 },
    [OPC_LSR_ZPX]   = { "LSR", op_LSR, am_ZPX, 6 },
    [OPC_LSR_ABS]   = { "LSR", op_LSR, am_ABS, 6 },
    [OPC_LSR_ABSX]  = { "LSR", op_LSR, am_ABX, 7 },

    /* ROL */
    [OPC_ROL_ACC]   = { "ROL", op_ROL, am_ACC, 2 },
    [OPC_ROL_ZP]    = { "ROL", op_ROL, am_ZP0, 5 },
    [OPC_ROL_ZPX]   = { "ROL", op_ROL, am_ZPX, 6 },
    [OPC_ROL_ABS]   = { "ROL", op_ROL, am_ABS, 6 },
    [OPC_ROL_ABSX]  = { "ROL", op_ROL, am_ABX, 7 },

    /* ROR */
    [OPC_ROR_ACC]   = { "ROR", op_ROR, am_ACC, 2 },
    [OPC_ROR_ZP]    = { "ROR", op_ROR, am_ZP0, 5 },
    [OPC_ROR_ZPX]   = { "ROR", op_ROR, am_ZPX, 6 },
    [OPC_ROR_ABS]   = { "ROR", op_ROR, am_ABS, 6 },
    [OPC_ROR_ABSX]  = { "ROR", op_ROR, am_ABX, 7 },

    /* Stack ops */
    [OPC_PHA_IMP]   = { "PHA", op_PHA, am_IMP, 3 },
    [OPC_PHP_IMP]   = { "PHP", op_PHP, am_IMP, 3 },
    [OPC_PLA_IMP]   = { "PLA", op_PLA, am_IMP, 4 },
    [OPC_PLP_IMP]   = { "PLP", op_PLP, am_IMP, 4 },

    /* Jumps and subroutines */
    [OPC_JMP_ABS]   = { "JMP", op_JMP, am_ABS, 3 },
    [OPC_JMP_IND]   = { "JMP", op_JMP, am_IND, 5 },
    [OPC_JSR_ABS]   = { "JSR", op_JSR, am_ABS, 6 },
    [OPC_RTS_IMP]   = { "RTS", op_RTS, am_IMP, 6 },
    [OPC_RTI_IMP]   = { "RTI", op_RTI, am_IMP, 6 },
};

/* ------------------------------------------------------------------ */
/*  Dispatch loop                                                      */
/* ------------------------------------------------------------------ */

void cpu_step(CPU *cpu) {
    static uint64_t instruction_id = 0;
    static Word pc_ring[32];
    static Byte op_ring[32];
    static int ring_idx = 0;
    static int trap_fff0_logged = 0;

    if (cpu->PC == 0xFFF0 && !trap_fff0_logged) {
        fprintf(stderr,
                "CPU_TRAP_FFF0: A=%02X X=%02X Y=%02X P=%02X SP=%02X | stack_top=%02X %02X %02X %02X %02X %02X\n",
                cpu->regs.A, cpu->regs.X, cpu->regs.Y, cpu->flags, cpu->SP,
                bus_read(0x0100 + ((cpu->SP + 1) & 0xFF)),
                bus_read(0x0100 + ((cpu->SP + 2) & 0xFF)),
                bus_read(0x0100 + ((cpu->SP + 3) & 0xFF)),
                bus_read(0x0100 + ((cpu->SP + 4) & 0xFF)),
                bus_read(0x0100 + ((cpu->SP + 5) & 0xFF)),
                bus_read(0x0100 + ((cpu->SP + 6) & 0xFF)));
        fprintf(stderr, "  Recent flow:\n");
        for (int i = 0; i < 32; i++) {
            int ri = (ring_idx + i) & 31;
            fprintf(stderr, "    PC=%04X OP=%02X\n", pc_ring[ri], op_ring[ri]);
        }
        trap_fff0_logged = 1;
    }

    pc_ring[ring_idx & 31] = cpu->PC;
    op_ring[ring_idx & 31] = bus_read(cpu->PC);
    ring_idx++;

    bus_set_cpu_instruction_id(++instruction_id);
    cpu->opcode = fetch_program_byte(cpu);
    const Instruction *ins = &INSTR_TABLE[cpu->opcode];
    if (ins->op == NULL) {
        Word bad_pc = (Word)(cpu->PC - 1);
        if (bad_pc != 0xFFF0) {
            fprintf(stderr, "CPU_UNKNOWN_OPCODE: PC=%04X opcode=%02X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n",
                    bad_pc, cpu->opcode, cpu->regs.A, cpu->regs.X, cpu->regs.Y, cpu->flags, cpu->SP);
        }
        cpu->cycles = 2; /* avoid zero-cycle stalls while diagnosing */
        return;
    }
    cpu->cycles = 0;
    Byte am_extra = ins->mode(cpu);
    Byte op_extra = ins->op(cpu);
    cpu->cycles += ins->cycles + (am_extra & op_extra);

    /* NMI check at end of cpu_step */
    if (cpu->nmi_pending) {
        cpu->nmi_pending = 0;
        stack_push((cpu->PC >> 8) & 0xFF, cpu);
        stack_push(cpu->PC & 0xFF, cpu);
        Byte p = cpu->flags;
        p &= ~(1 << B);
        p |=  (1 << U);
        stack_push(p, cpu);
        cpu_set_flag(I, 1, cpu);
        cpu->PC = (Word)bus_read(0xFFFA) | ((Word)bus_read(0xFFFB) << 8);
        cpu->cycles += 7;
    }
}

void cpu_execute(Word cycles, CPU *cpu) {
    while (cycles > 0) {
        cpu_step(cpu);
        cycles -= cpu->cycles;
    }
}
