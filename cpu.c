#include <string.h>

#include "cpu.h"

#include "bus.h"
#include "opcodes.h"

void cpu_reset(CPU *cpu) {
    cpu->PC = 0x01FF; // at least after ZP end
    cpu->SP = 0xFF;

    cpu->regs.A = 0x00;
    cpu->regs.X = 0x00;
    cpu->regs.Y = 0x00;
                 //NVUBDIZC
    cpu->flags = 0b00100100;

    bus_reset();
}

// --- STACK ---

void stack_push(Byte value, CPU *cpu) {
    bus_write(0x100 + cpu->SP, value);
    cpu->SP -= 1;
}

Byte stack_pop(CPU *cpu) {
    cpu->SP += 1;
    return bus_read(0x100 + cpu->SP);
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

    Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);  // Page crossing penalty

    cpu->regs.A = bus_read(addr);
    *cycles -= (4 + extra_cycle);
}

void indexed_STA_ABS(CPU *cpu, Byte index) {
    Word base = fetch_program_word(cpu);
    Word addr = base + index;

    bus_write(addr, cpu->regs.A);
}

void indexed_AND_ABS(Byte *cycles, CPU *cpu, Byte index) {
    Word base = fetch_program_word(cpu);
    Word addr = base + index;

    Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);

    cpu->regs.A &= bus_read(addr);
    *cycles -= (4 + extra_cycle);
}

void branch(Byte *cycles, CPU *cpu, Byte flag, Byte carry_val) {
    Byte operand = fetch_program_byte(cpu);
    int8_t signed_offset = operand;

    Byte opc_cycles = 2;

    if (flag == carry_val) {
        Word new_PC = cpu->PC + signed_offset;
        opc_cycles += 1;

        Byte extra_cycle = (cpu->PC & 0xFF00) != (new_PC & 0xFF00);
        opc_cycles += extra_cycle;

        cpu->PC = new_PC;
    }

    *cycles -= opc_cycles;
}

void set_NZ_flags(CPU *cpu) {
    cpu_set_flag(Z, cpu->regs.A == 0, cpu);
    cpu_set_flag(N, (cpu->regs.A >> 7) & 1, cpu);
}

Byte read_bit(Byte value, Byte position) {
    Byte result = (value >> position) & 0x01;
    return result;
}

void cpu_execute(Byte cycles, CPU *cpu) {
    while (cycles > 0) {
        Byte opcode = fetch_program_byte(cpu);

        switch (opcode) {

            //  --- LDA ---
            case OPC_LDA_IM: {
                Byte operand = fetch_program_byte(cpu);

                cpu->regs.A = operand;

                set_NZ_flags(cpu);

                cycles -= 2;
                break;
            }

            case OPC_LDA_ZP: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_value = bus_read(operand & 0xFF);

                cpu->regs.A = zero_page_value;

                set_NZ_flags(cpu);

                cycles -= 3;
                break;
            }

            case OPC_LDA_ZPX: {
                Byte operand = fetch_program_byte(cpu);
                Byte addr = operand + cpu->regs.X;  // Let it wrap naturally
                
                cpu->regs.A = bus_read(addr & 0xFF);

                set_NZ_flags(cpu);

                cycles -= 4;
                break;
            }

            case OPC_LDA_ABS: {
                Word operand = fetch_program_word(cpu);

                cpu->regs.A = bus_read(operand);

                set_NZ_flags(cpu);

                cycles -= 4;
                break;
            }

            case OPC_LDA_ABSX: {
                indexed_LDA_ABS(&cycles, cpu, cpu->regs.X);

                set_NZ_flags(cpu);
                break;
            }

            case OPC_LDA_ABSY: {
                indexed_LDA_ABS(&cycles, cpu, cpu->regs.Y);

                set_NZ_flags(cpu);
                break;
            }

            case OPC_LDA_INDX: {    // pre-indexed as in adding X before the actual address lookup
                Byte operand = fetch_program_byte(cpu);
                Byte wrapped = (operand + cpu->regs.X) & 0xFF;
                
                Byte lo = bus_read(wrapped & 0xFF);
                Byte hi = bus_read((wrapped + 1) & 0xFF);
                Word addr = lo | (hi << 8);

                cpu->regs.A = bus_read(addr);

                set_NZ_flags(cpu);

                cycles -= 5;
                break;
            }

            case OPC_LDA_INDY: {
                Byte operand = fetch_program_byte(cpu);
                
                Byte lo = bus_read(operand);
                Byte hi = bus_read((operand + 1) & 0xFF);
                Word base = lo | (hi << 8);
                Word addr = base + cpu->regs.Y; // post-indexd as in adding Y to the lookup addess
                
                Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);  // page crossing penalty (FF)

                cpu->regs.A = bus_read(addr);

                set_NZ_flags(cpu);

                cycles -= (5 + extra_cycle);
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

                set_NZ_flags(cpu);
                cpu_set_flag(C, result > 0xFF, cpu);            // detect a carry out
                cpu_set_flag(V, ((cpu->regs.A ^ result) & (operand ^ result) & 0x80) != 0, cpu);    // detect overflow detect sign change
                
                cpu->regs.A = result & 0xFF;    // store truncated result

                cycles -= 2;
                break;
            }

            // --- AND ---

            case OPC_AND_IM: {
                Byte operand = fetch_program_byte(cpu);
                
                cpu->regs.A &= operand;

                set_NZ_flags(cpu);

                cycles -= 2;
                break;
            }

            case OPC_AND_ZP: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_value = bus_read(operand & 0xFF);

                cpu->regs.A &= zero_page_value;

                set_NZ_flags(cpu);

                cycles -= 3;
                break;
            }

            case OPC_AND_ZPX: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_addr = operand + cpu->regs.X; // Let it wrap naturally
                Byte zero_page_value = bus_read(zero_page_addr & 0xFF);

                cpu->regs.A &= zero_page_value;

                set_NZ_flags(cpu);

                cycles -= 4;
                break;
            }

            case OPC_AND_ABS: {
                Word operand = fetch_program_word(cpu);

                cpu->regs.A &= bus_read(operand);

                set_NZ_flags(cpu);

                cycles -= 4;
                break;
            }

            case OPC_AND_ABSX: {
                indexed_AND_ABS(&cycles, cpu, cpu->regs.X);

                set_NZ_flags(cpu);
                break;
            }

            case OPC_AND_ABSY: {
                indexed_AND_ABS(&cycles, cpu, cpu->regs.Y);

                set_NZ_flags(cpu);
                break;
            }

            case OPC_AND_INDX: {
                Byte operand = fetch_program_byte(cpu);
                Byte wrapped = (operand + cpu->regs.X) & 0xFF;
                
                Byte lo = bus_read(wrapped & 0xFF);
                Byte hi = bus_read((wrapped + 1) & 0xFF);
                Word addr = lo | (hi << 8);
                
                cpu->regs.A &= bus_read(addr);
                
                set_NZ_flags(cpu);

                cycles -= 6;
                break;
            }

            case OPC_AND_INDY: {
                Byte operand = fetch_program_byte(cpu);
                
                Byte lo = bus_read(operand);
                Byte hi = bus_read((operand + 1) & 0xFF);
                Word base = lo | (hi << 8);
                
                Word addr = base + cpu->regs.Y;
                Byte extra_cycle = (addr & 0xFF00) != (base & 0xFF00);
                
                cpu->regs.A &= bus_read(addr);

                set_NZ_flags(cpu);

                cycles -= (5 + extra_cycle);
                break;
            }

            // --- ASL ---

            case OPC_ASL_ACC: {
                Word shifted_value = cpu->regs.A << 1;
                
                cpu->regs.A = shifted_value & 0xFF;
                
                set_NZ_flags(cpu);
                cpu_set_flag(C, shifted_value > 0xFF, cpu);

                cycles -= 2;
                break;
            }

            case OPC_ASL_ZP: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_value = bus_read(operand & 0xFF);
                Word shifted_value = zero_page_value << 1;

                bus_write(operand, shifted_value & 0xFF);

                cpu_set_flag(Z, shifted_value == 0, cpu);
                cpu_set_flag(N, ((shifted_value & 0xFF) >> 7) & 1, cpu);
                cpu_set_flag(C, shifted_value > 0xFF, cpu);

                cycles -= 5;
                break;
            }

            case OPC_ASL_ZPX: {
                Byte operand = fetch_program_byte(cpu);
                Byte addr = operand + cpu->regs.X;
                Byte zero_page_value = bus_read(operand & 0xFF);
                Word shifted_value = zero_page_value << 1;

                bus_write(operand, shifted_value & 0xFF);

                cpu_set_flag(Z, shifted_value == 0, cpu);
                cpu_set_flag(N, ((shifted_value & 0xFF) >> 7) & 1, cpu);
                cpu_set_flag(C, shifted_value > 0xFF, cpu);

                cycles -= 6;
                break;
            }

            case OPC_ASL_ABS: {
                Word operand = fetch_program_word(cpu);
                Byte abs_value = bus_read(operand);
                Word shifted_value = abs_value << 1;

                bus_write(operand, shifted_value & 0xFF);

                cpu_set_flag(Z, shifted_value == 0, cpu);
                cpu_set_flag(N, ((shifted_value & 0xFF) >> 7) & 1, cpu);
                cpu_set_flag(C, shifted_value > 0xFF, cpu);

                cycles -= 6;
                break;
            }

            case OPC_ASL_ABSX: {
                Word operand = fetch_program_word(cpu);
                Word addr = operand + cpu->regs.X;
                Byte abs_value = bus_read(addr);
                Word shifted_value = abs_value << 1;

                bus_write(addr, shifted_value & 0xFF);

                cpu_set_flag(Z, shifted_value == 0, cpu);
                cpu_set_flag(N, ((shifted_value & 0xFF) >> 7) & 1, cpu);
                cpu_set_flag(C, shifted_value > 0xFF, cpu);

                cycles -= 7;
                break;
            }

            // --- BRANCHING ---

            case OPC_BCC_REL: {
                branch(&cycles, cpu, cpu_read_flag(C, cpu), 0);
                break;
            }

            case OPC_BCS_REL: {
                branch(&cycles, cpu, cpu_read_flag(C, cpu), 1);
                break;
            }

            case OPC_BNE_REL: {
                branch(&cycles, cpu, cpu_read_flag(Z, cpu), 0);
            }

            case OPC_BEQ_REL: {
                branch(&cycles, cpu, cpu_read_flag(Z, cpu), 1);
            }

            case OPC_BPL_REL: {
                branch(&cycles, cpu, cpu_read_flag(N, cpu), 0);
            }

            case OPC_BMI_REL: {
                branch(&cycles, cpu, cpu_read_flag(N, cpu), 1);
            }

            case OPC_BVC_REL: {
                branch(&cycles, cpu, cpu_read_flag(V, cpu), 0);
            }

            case OPC_BVS_REL: {
                branch(&cycles, cpu, cpu_read_flag(V, cpu), 1);
            }

            case OPC_BIT_ZP: {
                Byte operand = fetch_program_byte(cpu);
                Byte zero_page_value = bus_read(operand & 0xFF);

                cpu_set_flag(Z, (cpu->regs.A & zero_page_value) == 0, cpu);

                Byte N_val = read_bit(zero_page_value, 7);
                cpu_set_flag(N, N_val, cpu);

                Byte V_val = read_bit(zero_page_value, 6);
                cpu_set_flag(V, V_val, cpu);

                cycles -= 3;
                break;
            }

            case OPC_BIT_ABS: {
                Word operand = fetch_program_word(cpu);
                Byte abs_value = bus_read(operand);

                cpu_set_flag(Z, (cpu->regs.A & abs_value) == 0, cpu);

                Byte N_val = read_bit(abs_value, 7);
                cpu_set_flag(N, N_val, cpu);

                Byte V_val = read_bit(abs_value, 6);
                cpu_set_flag(V, V_val, cpu);

                cycles -= 4;
                break;
            }

            // --- BRK ---

            case OPC_BRK_IMP: {
                Word stack_PC = cpu->PC + 2;
                Byte stack_PC_lo = stack_PC & 0xFF;
                Byte stack_PC_hi = (stack_PC >> 8) & 0xFF;
                stack_push(stack_PC_hi, cpu); // Stack is LIFO so hi is first to get them right on pop (reverse order of hi/lo)
                stack_push(stack_PC_lo, cpu);
                
                cpu_set_flag(I, 1, cpu);
                cpu_set_flag(B, 1, cpu);
                stack_push(cpu->flags, cpu);
                cpu_set_flag(B, 0, cpu);

                Byte lo = bus_read(0xFFFE);
                Byte hi = bus_read(0xFFFF);
                Word addr = lo | (hi << 8);

                cpu->PC = addr;
                cycles -= 7;
                break;
            }

            default:
                cycles--;
                break;
            }
    }
}