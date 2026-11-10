/*
 * Testes unitários do motor Lupin Neuro-PC.
 *
 * O teste de equivalência real (QEMU × ref.py × motor C) vive em
 * tests/golden_test.py — este arquivo cobre a API e as bordas do motor.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/neuro_pc/lupin_neuro_pc.h"

/* Monta um programa RV32I direto na RAM e o executa. */
static void load_words(neuro_pc_state_t *pc, const uint32_t *words, size_t n) {
    /* O motor carrega ELF; para testes, injetamos palavras no ROM. */
    for (size_t i = 0; i < n; i++) {
        pc->rom[(NEURO_PC_ROM_BASE - NEURO_PC_ROM_BASE) / 4 + i] = words[i];
    }
    pc->pc = NEURO_PC_ROM_BASE;
}

static void test_init_free(void) {
    neuro_pc_state_t pc;
    assert(lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES) == 0);
    assert(pc.ram != NULL);
    assert(pc.drom != NULL);
    assert(pc.rom != NULL);
    assert(pc.ram_size == NEURO_PC_RAM_BYTES);
    /* O stack pointer nasce no topo da RAM, como no circuito. */
    assert(pc.x[2] == NEURO_PC_SP_INIT);
    assert(pc.fault == NEURO_PC_OK);
    assert(pc.halted == 0);

    lupin_neuro_pc_free(&pc);
    assert(pc.ram == NULL);
    assert(pc.drom == NULL);
    assert(pc.rom == NULL);
    printf("test_init_free passed\n");
}

static void test_rv32i_execution(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    /* ADDI x1,x0,12 ; ADDI x2,x0,34 ; ADD x3,x1,x2 ; ECALL */
    uint32_t prog[] = {
        0x00c00093, /* addi x1, x0, 12 */
        0x02200113, /* addi x2, x0, 34 */
        0x002081b3, /* add  x3, x1, x2 */
        0x00000073  /* ecall */
    };
    load_words(&pc, prog, 4);
    pc.x[17] = 93; /* exit(0) */
    pc.x[10] = 0;

    lupin_neuro_pc_step(&pc, 10);

    assert(pc.x[1] == 12);
    assert(pc.x[2] == 34);
    assert(pc.x[3] == 46);
    assert(pc.halted == 1);
    assert(pc.exit_code == 0);
    assert(pc.fault == NEURO_PC_OK);

    lupin_neuro_pc_free(&pc);
    printf("test_rv32i_execution passed\n");
}

static void test_ecall_putchar(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    /* a7=1 (putchar) ; ADDI a0,x0,'H' ; ECALL ; ADDI a0,x0,'I' ; ECALL ; a7=93 ; ECALL */
    uint32_t prog[] = {
        0x00100893, /* addi a7, x0, 1 (putchar) */
        0x04800513, /* addi a0, x0, 'H' */
        0x00000073, /* ecall (putchar) */
        0x04900513, /* addi a0, x0, 'I' */
        0x00000073, /* ecall (putchar) */
        0x05d00893, /* addi a7, x0, 93 (exit) */
        0x00000073  /* ecall (exit) */
    };
    load_words(&pc, prog, 7);

    lupin_neuro_pc_step(&pc, 20);

    assert(strcmp(pc.stdout_buf, "HI") == 0);
    assert(pc.halted == 1);

    lupin_neuro_pc_free(&pc);
    printf("test_ecall_putchar passed\n");
}

static void test_branch_does_not_write_rd(void) {
    /* Regressão: no formato B os bits [11:7] são do imediato, não rd.
     * Um motor que escreva rd=0 num branch corrompe x13 silenciosamente. */
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    /* addi x13,x0,7 ; bne x0,x0,+8 (não taken, rd bits = 13) ; ecall(exit) */
    uint32_t prog[] = {
        0x00700693, /* addi x13, x0, 7 */
        0x00000663, /* bne x0, x0, +8  (rd field = 13 nos bits [11:7]) */
        0x00000073  /* ecall */
    };
    load_words(&pc, prog, 3);
    pc.x[17] = 93;
    pc.x[10] = 0;

    lupin_neuro_pc_step(&pc, 10);
    assert(pc.x[13] == 7); /* não pode ter virado 0 */
    assert(pc.halted == 1);

    lupin_neuro_pc_free(&pc);
    printf("test_branch_does_not_write_rd passed\n");
}

static void test_sign_extended_immediates(void) {
    /* Regressão: addi sp,sp,-16 deve decrementar, não incrementar por 4080. */
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    uint32_t prog[] = {
        0xff010113, /* addi sp, sp, -16 */
        0x00000073  /* ecall */
    };
    load_words(&pc, prog, 2);
    pc.x[17] = 93;
    pc.x[10] = 0;

    lupin_neuro_pc_step(&pc, 5);
    assert(pc.x[2] == NEURO_PC_SP_INIT - 16);

    lupin_neuro_pc_free(&pc);
    printf("test_sign_extended_immediates passed\n");
}

static void test_memory_regions(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    /* .data na RAM, .rodata no DROM, .text no ROM */
    uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t rodata[4] = {0x11, 0x22, 0x33, 0x44};
    assert(lupin_neuro_pc_load_image(&pc, NEURO_PC_RAM_BASE + 0x100, data, 4) == 0);
    assert(lupin_neuro_pc_load_image(&pc, NEURO_PC_DROM_BASE, rodata, 4) == 0);

    assert(pc.ram[0x100] == 0xDE && pc.ram[0x103] == 0xEF);
    assert(pc.drom[0] == 0x11 && pc.drom[3] == 0x44);
    assert(pc.rom[0] == 0); /* nada foi escrito no ROM */

    lupin_neuro_pc_free(&pc);
    printf("test_memory_regions passed\n");
}

static void test_elf_rejects_garbage(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    uint8_t junk[64];
    memset(junk, 0xAB, sizeof(junk));
    assert(lupin_neuro_pc_load_elf(&pc, junk, sizeof(junk)) != 0);
    assert(lupin_neuro_pc_load_elf(&pc, junk, 4) != 0);

    lupin_neuro_pc_free(&pc);
    printf("test_elf_rejects_garbage passed\n");
}

static void test_alu_edge_cases(void) {
    /* Casos de overflow/divisão que a ISA define explicitamente. */
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);

    /* li x1, -1 ; li x2, 0 ; div x3,x1,x2  -> 0xFFFFFFFF (div por zero) */
    uint32_t prog[] = {
        0xfff00093, /* addi x1, x0, -1 */
        0x00000113, /* addi x2, x0, 0  */
        0x02c0c1b3, /* div  x3, x1, x2 */
        0x00000073  /* ecall */
    };
    load_words(&pc, prog, 4);
    pc.x[17] = 93;
    pc.x[10] = 0;
    lupin_neuro_pc_step(&pc, 10);
    assert(pc.x[3] == 0xFFFFFFFFu);

    lupin_neuro_pc_free(&pc);
    printf("test_alu_edge_cases passed\n");
}

int main(void) {
    test_init_free();
    test_rv32i_execution();
    test_ecall_putchar();
    test_branch_does_not_write_rd();
    test_sign_extended_immediates();
    test_memory_regions();
    test_elf_rejects_garbage();
    test_alu_edge_cases();
    printf("All neuro_pc tests passed.\n");
    return 0;
}
