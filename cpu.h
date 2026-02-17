#ifndef CPU_H
#define CPU_H

#include "types.h"

typedef struct {
  Byte A;
  Byte X;
  Byte Y;
} Regs;

typedef enum {
  C = 0,  // Carry
  Z = 1,  // Zero
  I = 2,  // IRQ disable
  D = 3,  // Dec
  B = 4,  // Break
  U = 5,  // Unused/Ignored
  V = 6,  // Overflow
  N = 7   // Negative
} Flags;

typedef struct {
  Word PC;
  Byte SP;

  Regs regs;

  Byte flags;

} CPU;

void cpu_reset(CPU *cpu);

void cpu_execute(Byte cycles, CPU *cpu);

Byte cpu_read_flag(Flags flag, CPU *cpu);
void cpu_set_flag(Flags flag, Byte value, CPU *cpu);
void cpu_toggle_flag(Flags flag, CPU *cpu);

#endif
