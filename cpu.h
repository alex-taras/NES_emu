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
  Byte nmi_pending;   /* set by PPU; cleared by CPU after NMI handler entry */
  Byte irq_pending;   /* set by mapper; cleared by CPU after IRQ handler entry */
  int  cycles_remaining;  /* dots remaining for current instruction (counts down to 0) */

  /* scratch fields used by the lookup-table dispatcher */
  Byte opcode;
  Word addr_abs;
  Byte fetched;
  Byte page_crossed;
  Byte addr_mode_id;
  Byte cycles;

} CPU;

void cpu_reset(CPU *cpu);

void stack_push(Byte value, CPU *cpu);
Byte stack_pop(CPU *cpu);

void cpu_execute(Word cycles, CPU *cpu);
void cpu_step(CPU *cpu);

Byte cpu_read_flag(Flags flag, CPU *cpu);
void cpu_set_flag(Flags flag, Byte value, CPU *cpu);
void cpu_toggle_flag(Flags flag, CPU *cpu);

#endif
