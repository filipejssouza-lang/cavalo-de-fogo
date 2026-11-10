#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/neuro_pc/lupin_neuro_pc.h"

void test_init_free() {
    neuro_pc_state_t pc;
    int res = lupin_neuro_pc_init(&pc, 1024);
    assert(res == 0);
    assert(pc.ram != NULL);
    assert(pc.ram_size == 1024);
    
    lupin_neuro_pc_free(&pc);
    assert(pc.ram == NULL);
    printf("test_init_free passed\n");
}

void test_rv32i_execution() {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, 4096);
    
    // Simple program:
    // ADDI x1, x0, 12
    // ADDI x2, x0, 34
    // ADD x3, x1, x2
    uint32_t prog[] = {
        0x00c00093, // addi x1, x0, 12
        0x02200113, // addi x2, x0, 34
        0x002081b3, // add x3, x1, x2
        0x00000073  // ecall (halt, but a7=0)
    };
    
    lupin_neuro_pc_load_elf(&pc, (uint8_t*)prog, sizeof(prog));
    
    lupin_neuro_pc_step(&pc, 10);
    
    assert(pc.x[1] == 12);
    assert(pc.x[2] == 34);
    assert(pc.x[3] == 46);
    
    lupin_neuro_pc_free(&pc);
    printf("test_rv32i_execution passed\n");
}

void test_ecall_sys_write() {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, 4096);
    
    // Write "OK" at address 100
    pc.ram[100] = 'O';
    pc.ram[101] = 'K';
    
    // ADDI a7 (x17), x0, 64
    // ADDI a0 (x10), x0, 1
    // ADDI a1 (x11), x0, 100
    // ADDI a2 (x12), x0, 2
    // ECALL
    uint32_t prog[] = {
        0x04000893, // addi x17, x0, 64
        0x00100513, // addi x10, x0, 1
        0x06400593, // addi x11, x0, 100
        0x00200613, // addi x12, x0, 2
        0x00000073  // ecall
    };
    
    lupin_neuro_pc_load_elf(&pc, (uint8_t*)prog, sizeof(prog));
    
    lupin_neuro_pc_step(&pc, 10);
    
    assert(strcmp(pc.stdout_buf, "OK") == 0);
    assert(pc.x[10] == 2);
    
    lupin_neuro_pc_free(&pc);
    printf("test_ecall_sys_write passed\n");
}

int main() {
    test_init_free();
    test_rv32i_execution();
    test_ecall_sys_write();
    printf("All neuro_pc tests passed.\n");
    return 0;
}
