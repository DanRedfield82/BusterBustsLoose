
#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#include "statehandler.h"

typedef uint8_t (*CpuReadHandler)(void* mem, uint32_t adr);
typedef void (*CpuWriteHandler)(void* mem, uint32_t adr, uint8_t val);
typedef void (*CpuIdleHandler)(void* mem, bool waiting);

typedef struct Cpu Cpu;

struct Cpu {
  // reference to memory handler, pointers to read/write/idle handlers
  void* mem;
  CpuReadHandler read;
  CpuWriteHandler write;
  CpuIdleHandler idle;
  // registers
  uint16_t a;
  uint16_t x;
  uint16_t y;
  uint16_t sp;
  uint16_t pc;
  uint16_t dp; // direct page (D)
  uint8_t k; // program bank (PB)
  uint8_t db; // data bank (B)
  // flags
  bool c;
  bool z;
  bool v;
  bool n;
  bool i;
  bool d;
  bool xf;
  bool mf;
  bool e;
  // power state (WAI/STP)
  bool waiting;
  bool stopped;
  // interrupts
  bool irqWanted;
  bool nmiWanted;
  bool intWanted;
  bool intDelay;
  bool resetWanted;
  // optional execution hooks: hookFn(hookCtx, pc) runs before an opcode whose
  // 16-bit address is marked in hookBits (the callback checks the bank)
  uint8_t hookBits[0x2000];
  void (*hookFn)(void* ctx, uint32_t pc);
  void* hookCtx;
};

static inline void cpu_setHook(Cpu* cpu, uint32_t pc) { cpu->hookBits[(pc & 0xffff) >> 3] |= 1 << (pc & 7); }

Cpu* cpu_init(void* mem, CpuReadHandler read, CpuWriteHandler write, CpuIdleHandler idle);
void cpu_free(Cpu* cpu);
void cpu_reset(Cpu* cpu, bool hard);
void cpu_handleState(Cpu* cpu, StateHandler* sh);
void cpu_runOpcode(Cpu* cpu);
// debug hook, called before every opcode fetch (NULL when unused)
extern void (*cpu_dbgExec)(Cpu* cpu);
void cpu_nmi(Cpu* cpu);
void cpu_setIrq(Cpu* cpu, bool state);

#endif
