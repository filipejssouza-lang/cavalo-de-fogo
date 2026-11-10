/*
 * Lupin Neuro-PC — Motor RV32IM + Zba em C puro.
 *
 * Este motor é o ESPELHO EXECUTÁVEL do computador que o Qwenputer compila nos
 * pesos (~/lab-bench/divagr18/qwenputer/experimental/riscv/chipformer/rv/ref.py).
 * Ambos implementam a mesma ISA e o mesmo mapa de memória, de modo que uma
 * saída divergente é um bug de um dos dois, nunca um "quase igual".
 *
 * Mapa de memória (idêntico ao do circuito — ver rv/spec.py):
 *
 *   RAM   0x0000_0000 +  32 KB   dados / bss / heap a partir de 0x100, stack abaixo de 0x8000
 *   DROM  0x0002_0000 + 128 KB   dados somente-leitura (.rodata), bytes
 *   ROM   0x0004_0000 + 256 KB   código somente-leitura (.text)
 *
 * Syscalls (ecall a7):
 *   1  -> putchar(a0)     imprime um byte (0..255)
 *   93 -> exit(a0)        encerra o programa com o código em a0
 *   64 -> write(fd,buf,n) variante Linux, para binários compilados sem o crt0 do circuito
 *
 * O motor NÃO é o circuito: ele roda no silício do host. A paridade bit-a-bit
 * com o circuito é verificada pelo golden test (tests/golden_test.py), que
 * compara QEMU × ref.py × este motor na mesma imagem.
 */
#ifndef LUPIN_NEURO_PC_H
#define LUPIN_NEURO_PC_H

#include <stdint.h>
#include <stddef.h>

/* --- mapa de memória do circuito (rv/spec.py) --- */
#define NEURO_PC_RAM_BASE    0x00000000u
#define NEURO_PC_RAM_WORDS   (1u << 13)          /* 32 KB */
#define NEURO_PC_RAM_BYTES   (NEURO_PC_RAM_WORDS * 4u)
#define NEURO_PC_DROM_BASE   0x00020000u
#define NEURO_PC_DROM_WORDS  (1u << 15)          /* 128 KB */
#define NEURO_PC_DROM_BYTES  (NEURO_PC_DROM_WORDS * 4u)
#define NEURO_PC_ROM_BASE    0x00040000u
#define NEURO_PC_ROM_WORDS   (1u << 16)          /* 256 KB */
#define NEURO_PC_ROM_BYTES   (NEURO_PC_ROM_WORDS * 4u)
#define NEURO_PC_SP_INIT     (NEURO_PC_RAM_BASE + 4u * NEURO_PC_RAM_WORDS) /* 0x8000 */

#define NEURO_PC_STDOUT_BUF_SIZE 4096

/* --- causas de falha (mesma ordem de rv/spec.py: FAULTS) --- */
typedef enum {
    NEURO_PC_OK = -1,
    NEURO_PC_FAULT_ILLEGAL = 0,
    NEURO_PC_FAULT_EBREAK,
    NEURO_PC_FAULT_MISALIGNED_LOAD,
    NEURO_PC_FAULT_MISALIGNED_STORE,
    NEURO_PC_FAULT_LOAD_OUT_OF_RANGE,
    NEURO_PC_FAULT_STORE_OUT_OF_RANGE,
    NEURO_PC_FAULT_BAD_JUMP,
    NEURO_PC_FAULT_ECALL
} neuro_pc_fault_t;

typedef struct {
    uint32_t pc;                 /* endereço byte dentro do ROM */
    uint32_t x[32];              /* x0 é sempre zero */
    uint32_t cycles;
    size_t ram_size;             /* bytes de RAM alocados */
    int halted;
    int exit_code;
    int fault;                   /* neuro_pc_fault_t ou NEURO_PC_OK */
    uint32_t fault_pc;
    char stdout_buf[NEURO_PC_STDOUT_BUF_SIZE];
    uint8_t *ram;                /* 32 KB */
    uint8_t *drom;               /* 128 KB */
    uint32_t *rom;               /* 65536 palavras */
} neuro_pc_state_t;

int lupin_neuro_pc_init(neuro_pc_state_t *pc, size_t ram_size_bytes);
void lupin_neuro_pc_reset(neuro_pc_state_t *pc);
void lupin_neuro_pc_free(neuro_pc_state_t *pc);

/* Carrega uma imagem ELF32 little-endian estática (PT_LOAD) no mapa do circuito.
 * Devolve 0 em sucesso, -1 em erro. O entry point é gravado em pc. */
int lupin_neuro_pc_load_elf(neuro_pc_state_t *pc, const uint8_t *elf_bytes, size_t len);

/* Injeta bytes crus numa região (NEURO_PC_ROM_BASE para código, RAM_BASE para dados).
 * Usado por testes e pelo carregamento de snapshots. */
int lupin_neuro_pc_load_image(neuro_pc_state_t *pc, uint32_t base, const uint8_t *bytes, size_t len);

/* Executa até max_cycles instruções ou até halt. Devolve o número de ciclos
 * executados, ou -1 em erro de uso. */
int lupin_neuro_pc_step(neuro_pc_state_t *pc, uint32_t max_cycles);

/* Compila um trecho C para RV32 e o executa. Devolve 0 em sucesso.
 * Exige um cross-compiler riscv64-linux-gnu-gcc no PATH. */
int lupin_neuro_pc_run_script(neuro_pc_state_t *pc, const char *code, char *out_buf, size_t out_max);

const char *lupin_neuro_pc_fault_name(int fault);

#endif /* LUPIN_NEURO_PC_H */
