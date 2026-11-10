#ifndef LUPIN_NEURO_PC_H
#define LUPIN_NEURO_PC_H

#include <stdint.h>
#include <stddef.h>

#define NEURO_PC_STDOUT_BUF_SIZE 1024

typedef struct {
    uint32_t pc;
    uint32_t x[32];
    uint32_t cycles;
    size_t ram_size;
    int halted;
    char stdout_buf[NEURO_PC_STDOUT_BUF_SIZE];
    uint8_t *ram;
} neuro_pc_state_t;

int lupin_neuro_pc_init(neuro_pc_state_t *pc, size_t ram_size_bytes);
int lupin_neuro_pc_load_elf(neuro_pc_state_t *pc, const uint8_t *elf_bytes, size_t len);
int lupin_neuro_pc_step(neuro_pc_state_t *pc, uint32_t max_cycles);
int lupin_neuro_pc_run_script(neuro_pc_state_t *pc, const char *code, char *out_buf, size_t out_max);
void lupin_neuro_pc_reset(neuro_pc_state_t *pc);
void lupin_neuro_pc_free(neuro_pc_state_t *pc);

#endif // LUPIN_NEURO_PC_H
