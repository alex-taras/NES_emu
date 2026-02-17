#include <string.h>

#include "cpu.h"

#include "bus.h"
#include "opcodes.h"

void cpu_reset(CPU *cpu) {
    cpu->PC = 0x0100; // at least after ZP end
    cpu->SP = 0x00FD;

    cpu->regs.A = 0x00;
    cpu->regs.X = 0x00;
    cpu->regs.Y = 0x00;
                 //NVUBDIZC
    cpu->flags = 0b00100100;

    bus_reset();
}

//  --- FLAGS ---

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

    // force U on 1
    mask = 0x01 << U;
    cpu->flags |= mask;
}

void cpu_toggle_flag(Flags flag, CPU *cpu) {
    Byte mask = 0x01 << flag;
    cpu->flags ^= mask;
}

//  --- MEM OPS ---

Byte fetch_program_byte(CPU *cpu) {
    Byte data = bus_read(cpu->PC);
    cpu->PC++;

    return data;
}

Word fetch_program_word(CPU *cpu) {
    Byte data_lo = fetch_program_byte(cpu);
    Byte data_hi = fetch_program_byte(cpu);

    Word data = data_lo | (data_hi << 8);

    return data;
}

//  --- CONVENIENCE ---

void indexed_LDA_ABS(Byte *cycles, CPU *cpu, Byte index) {
    Word base = fetch_program_word(cpu);
    Word addr = base + index;

    Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);

    cpu->regs.A = bus_read(addr);
    *cycles -= (4 + extra_cycle);
}

void indexed_STA_ABS(CPU *cpu, Byte index) {
    Word base = fetch_program_word(cpu);
    Word addr = base + index;

    bus_write(addr, cpu->regs.A);
}

void set_flags_LDA(CPU *cpu) {
    cpu_set_flag(Z, cpu->regs.A == 0, cpu);
    cpu_set_flag(N, (cpu->regs.A >> 7) & 1, cpu);
}

void cpu_execute(Byte cycles, CPU *cpu) {
    while (cycles > 0) {
        Byte opcode = fetch_program_byte(cpu);

        switch (opcode) {

            //  --- LDA ---
            case OPC_LDA_IM: {
                Byte operand = fetch_program_byte(cpu);
                cpu->regs.A = operand;
                cycles -= 2;
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_ZP: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_value = bus_read(operand & 0xFF);
                cpu->regs.A = zero_page_value;
                cycles -= 3;
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_ZPX: {
                Byte operand = fetch_program_byte(cpu);
                Byte addr = operand + cpu->regs.X;
                cpu->regs.A = bus_read(addr & 0xFF);
                cycles -= 4;
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_ABS: {
                Word operand = fetch_program_word(cpu);
                cpu->regs.A = bus_read(operand);
                cycles -= 4;
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_ABSX: {
                indexed_LDA_ABS(&cycles, cpu, cpu->regs.X);
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_ABSY: {
                indexed_LDA_ABS(&cycles, cpu, cpu->regs.Y);
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_INDX: {
                Byte operand = fetch_program_byte(cpu);
                Byte wrapped = (operand + cpu->regs.X) & 0xFF;
                Byte lo = bus_read(wrapped & 0xFF);
                Byte hi = bus_read((wrapped + 1) & 0xFF);
                Word addr = lo | (hi << 8);
                cpu->regs.A = bus_read(addr);
                cycles -= 5;
                set_flags_LDA(cpu);
                break;
            }

            case OPC_LDA_INDY: {
                Byte operand = fetch_program_byte(cpu);
                Byte lo = bus_read(operand);
                Byte hi = bus_read((operand + 1) & 0xFF);
                Word base = lo | (hi << 8);
                Word addr = base + cpu->regs.Y;
                Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);
                cpu->regs.A = bus_read(addr);
                cycles -= (5 + extra_cycle);
                set_flags_LDA(cpu);
                break;
            }

            //  --- STA ---

            case OPC_STA_ZP: {
                Byte operand = fetch_program_byte(cpu);
                bus_write(operand, cpu->regs.A);
                cycles -= 3;
                break;
            }

            case OPC_STA_ZPX: {
                Byte operand = fetch_program_byte(cpu);
                Byte addr = (operand + cpu->regs.X) & 0xFF;
                bus_write(addr, cpu->regs.A);
                cycles -=4;
                break;
            }

            case OPC_STA_ABS: {
                Word operand = fetch_program_word(cpu);
                bus_write(operand, cpu->regs.A);
                cycles -=4;
                break;
            }

            case OPC_STA_ABSX: {
                indexed_STA_ABS(cpu, cpu->regs.X);
                cycles -= 5;
                break;
            }

            case OPC_STA_ABSY: {
                indexed_STA_ABS(cpu, cpu->regs.Y);
                cycles -= 5;
                break;
            }

            case OPC_STA_INDX: {
                Byte operand = fetch_program_byte(cpu);
                Byte wrapped = (operand + cpu->regs.X) & 0xFF;
                Byte lo = bus_read(wrapped & 0xFF);
                Byte hi = bus_read((wrapped + 1) & 0xFF);
                Word addr = lo | (hi << 8);
                bus_write(addr, cpu->regs.A);
                cycles -= 6;
                break;
            }

            case OPC_STA_INDY: {
                Byte operand = fetch_program_byte(cpu);
                Byte lo = bus_read(operand & 0xFF);
                Byte hi = bus_read((operand + 1) & 0xFF);
                Word base = lo | (hi << 8);
                Word addr = base + cpu->regs.Y;
                bus_write(addr, cpu->regs.A);
                cycles -= 6;
                break;
            }

            // --- ADC ---

            case OPC_ADC_IM: {
                Byte operand = fetch_program_byte(cpu);
                Word result = cpu->regs.A + operand + cpu_read_flag(C, cpu);    // do math on word

                cpu_set_flag(C, result > 0xFF, cpu);            // detect a carry out
                cpu_set_flag(Z, (result & 0xFF) == 0, cpu);     // detect if zero
                cpu_set_flag(N, result  & 0x80, cpu);           // set sign flag (bit 7)
                cpu_set_flag(V, ((cpu->regs.A ^ result) & (operand ^ result) & 0x80) != 0, cpu);    // detect overflow detect sign change
                
                cpu->regs.A = result & 0xFF;    // store truncated result

                cycles -= 2;
                break;
            }

            default:
                cycles--;
                break;
            }
    }
}