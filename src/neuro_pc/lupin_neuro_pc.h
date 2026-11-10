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
    NEURO_PC_FAULT_ECALL,
    NEURO_PC_FAULT_GUARD          /* <-- NOVO, valor 8: violacao da politica do no */
} neuro_pc_fault_t;

/* ==========================================================================================
 * Lupin-Store — filesystem / key-value in-memory acoplado ao estado da maquina.
 * ==========================================================================================
 * A versao em pesos (Qwenputer, chipformer/q35/store.py) guarda as entradas em
 * cabecas do Gated DeltaNet, com a chave one-hot sobrescrevendo a linha exata.
 * Aqui a mesma semantica em C puro: tabela hash com sondagem linear, o valor
 * guardado e a celula, e a chave decide a linha.
 *
 * O snapshot binario tem magic, versao e checksum FNV-1a: um snapshot corrompido
 * e REJEITADO, nunca carregado em silencio. O formato e estruturado (chave ->
 * valor com metadados) para poder virar o DeltaNet depois.
 */
#define NEURO_PC_STORE_MAX_ENTRIES 64
#define NEURO_PC_STORE_KEY_MAX     64   /* bytes, sem o NUL */
#define NEURO_PC_STORE_VAL_MAX    256   /* bytes, sem o NUL */
#define NEURO_PC_STORE_MAGIC       0x4C505331u  /* "LPS1" */
#define NEURO_PC_STORE_VERSION     1u

typedef struct {
    char key[NEURO_PC_STORE_KEY_MAX + 1];
    char value[NEURO_PC_STORE_VAL_MAX + 1];
    int  used;        /* 1 = ocupada, 0 = livre */
    uint32_t hash;    /* hash FNV-1a da chave */
} neuro_pc_store_entry_t;

typedef struct {
    neuro_pc_store_entry_t entries[NEURO_PC_STORE_MAX_ENTRIES];
    int count;
    uint64_t version;
} neuro_pc_store_t;

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
    neuro_pc_store_t store;      /* o store vive DENTRO do estado */
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

/* Lupin-Guard (L1): filtro deterministico de politica do no.
 * Devolve 0 se o comando e permitido, ou NEURO_PC_FAULT_GUARD se viola uma
 * regra soberana. Nao aloca memoria, nao trunca o comando e nao escreve em
 * nenhum buffer do chamador. */
int lupin_neuro_pc_guard_check(const char *code);

/* A mensagem / o nome da regra violada pelo comando (NULL se nenhuma violou).
 * Vem da MESMA varredura que decidiu o bloqueio. */
const char *lupin_neuro_pc_guard_message(const char *code);
const char *lupin_neuro_pc_guard_rule(const char *code);

/* ==========================================================================================
 * Lupin-Store — API. Todas devolvem 0 em sucesso e -1 em erro.
 * ========================================================================================== */

/* Insere ou SOBRESCREVE a chave. Chave ou valor NULL, ou acima do limite,
 * ou tabela cheia -> -1. */
int lupin_store_put(neuro_pc_state_t *pc, const char *key, const char *value);

/* Le a chave para `out`. Chave ausente -> -1 e out[0] = '\0'. */
int lupin_store_get(neuro_pc_state_t *pc, const char *key, char *out, size_t out_size);

/* Remove a chave. Chave ausente -> -1. */
int lupin_store_delete(neuro_pc_state_t *pc, const char *key);

/* Escreve as chaves (nao os valores), uma por linha. Devolve quantas escreveu. */
int lupin_store_list(neuro_pc_state_t *pc, char *out, size_t out_size);

/* Quantas entradas estao ocupadas. */
int lupin_store_count(neuro_pc_state_t *pc);

/* Serializa o store. Devolve os bytes escritos, ou -1 se o buffer nao comporta. */
int lupin_store_snapshot(const neuro_pc_store_t *store, uint8_t *buf, size_t buf_size);

/* Restaura o store de um snapshot. Devolve 0, ou -1 (magic/version/taamanho/
 * checksum invalidos). */
int lupin_store_restore(neuro_pc_store_t *store, const uint8_t *buf, size_t buf_size);

/* --- persistencia em disco ---
 * O store e um arquivo: o binario e o DONO do estado, e o daemon so o
 * orquestra. Assim o store sobrevive a um restart do daemon — o que FFI com
 * estado em memoria NAO faria.
 * O caminho vem de LUPIN_STORE_PATH; o default e /var/lib/lupinos/neuro_store.snap
 * (veja o caminho padrão em NEURO_PC_STORE_PATH_DEFAULT). */
#define NEURO_PC_STORE_PATH_DEFAULT "/var/lib/lupinos/neuro_store.snap"

/* Salva o store em `path`. Devolve 0, ou -1 (erro de IO / snapshot invalido). */
int lupin_store_save_file(const neuro_pc_store_t *store, const char *path);

/* Carrega o store de `path`. Devolve 0, ou -1 se o arquivo nao existe ou esta
 * corrompido. Um store vazio e um store valido. */
int lupin_store_load_file(neuro_pc_store_t *store, const char *path);

/* O caminho de persistencia em uso (do ambiente, ou o default). */
const char *lupin_store_default_path(void);

const char *lupin_neuro_pc_fault_name(int fault);

#endif /* LUPIN_NEURO_PC_H */
