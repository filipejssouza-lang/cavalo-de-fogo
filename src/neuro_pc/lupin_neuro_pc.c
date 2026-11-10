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
#include "lupin_neuro_pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define M32 0xFFFFFFFFu

/* --- helpers de sinalização (idênticos a ref.sx) --- */
static int32_t sx32(uint32_t v) {
    return (v & 0x80000000u) ? (int32_t)(v - 0x100000000ull) : (int32_t)v;
}

/* Extensão de sinal, idêntica a ref.sx(v, bits): o bit de sinal é o bit (bits-1). */
static uint32_t sx(uint32_t v, int bits) {
    v &= (1u << bits) - 1u;
    return (v & (1u << (bits - 1))) ? (v | ~((1u << bits) - 1u)) : v;
}

/* Imediatos com extensão de sinal. As posições de bit são as do RISC-V:
 * I = w[31:20], S = w[31:25]|w[11:7], B = w[31]|w[7]|w[30:25]|w[11:8], J = w[31]|w[19:12]|w[20]|w[30:21]. */
static uint32_t imm_i(uint32_t w) { return sx(w >> 20, 12); }
static uint32_t imm_s(uint32_t w) { return sx((w >> 25) << 5 | ((w >> 7) & 31), 12); }
static uint32_t imm_b(uint32_t w) {
    return sx((w >> 31 & 1) << 12 | (w >> 7 & 1) << 11 | (w >> 25 & 63) << 5 | (w >> 8 & 15) << 1, 13);
}
static uint32_t imm_j(uint32_t w) {
    return sx((w >> 31 & 1) << 20 | (w >> 12 & 0xFF) << 12 | (w >> 20 & 1) << 11 |
              (w >> 21 & 0x3FF) << 1, 21);
}

/* --- ciclo de vida --- */

int lupin_neuro_pc_init(neuro_pc_state_t *pc, size_t ram_size_bytes) {
    if (!pc) return -1;
    memset(pc, 0, sizeof(*pc));
    /* O mapa do circuito é fixo: 32 KB de RAM. O parâmetro é aceito por
     * compatibilidade com a API anterior, mas o mapa manda. */
    (void)ram_size_bytes;
    pc->ram = (uint8_t *)calloc(1, NEURO_PC_RAM_BYTES);
    pc->drom = (uint8_t *)calloc(1, NEURO_PC_DROM_BYTES);
    pc->rom = (uint32_t *)calloc(1, NEURO_PC_ROM_BYTES);
    if (!pc->ram || !pc->drom || !pc->rom) {
        lupin_neuro_pc_free(pc);
        return -1;
    }
    pc->ram_size = NEURO_PC_RAM_BYTES;
    pc->fault = NEURO_PC_OK;
    pc->x[2] = NEURO_PC_SP_INIT; /* sp: o stack cresce para baixo desde o topo da RAM */
    pc->pc = NEURO_PC_ROM_BASE;
    return 0;
}

void lupin_neuro_pc_reset(neuro_pc_state_t *pc) {
    if (!pc) return;
    pc->pc = NEURO_PC_ROM_BASE;
    memset(pc->x, 0, sizeof(pc->x));
    pc->x[2] = NEURO_PC_SP_INIT;
    pc->cycles = 0;
    pc->halted = 0;
    pc->exit_code = 0;
    pc->fault = NEURO_PC_OK;
    pc->fault_pc = 0;
    memset(pc->stdout_buf, 0, sizeof(pc->stdout_buf));
    if (pc->ram) memset(pc->ram, 0, NEURO_PC_RAM_BYTES);
    if (pc->drom) memset(pc->drom, 0, NEURO_PC_DROM_BYTES);
    if (pc->rom) memset(pc->rom, 0, NEURO_PC_ROM_BYTES);
}

void lupin_neuro_pc_free(neuro_pc_state_t *pc) {
    if (!pc) return;
    free(pc->ram); pc->ram = NULL;
    free(pc->drom); pc->drom = NULL;
    free(pc->rom); pc->rom = NULL;
}

/* --- memória --- */

static uint32_t mem_read32(neuro_pc_state_t *pc, uint32_t addr) {
    if (addr + 3 < NEURO_PC_RAM_BASE + NEURO_PC_RAM_BYTES) {
        uint32_t o = addr - NEURO_PC_RAM_BASE;
        return (uint32_t)pc->ram[o] | (uint32_t)pc->ram[o + 1] << 8 |
               (uint32_t)pc->ram[o + 2] << 16 | (uint32_t)pc->ram[o + 3] << 24;
    }
    if (addr >= NEURO_PC_DROM_BASE && addr + 3 < NEURO_PC_DROM_BASE + NEURO_PC_DROM_BYTES) {
        uint32_t o = addr - NEURO_PC_DROM_BASE;
        return (uint32_t)pc->drom[o] | (uint32_t)pc->drom[o + 1] << 8 |
               (uint32_t)pc->drom[o + 2] << 16 | (uint32_t)pc->drom[o + 3] << 24;
    }
    return 0;
}

static void mem_write32(neuro_pc_state_t *pc, uint32_t addr, uint32_t val) {
    if (addr + 3 < NEURO_PC_RAM_BASE + NEURO_PC_RAM_BYTES) {
        uint32_t o = addr - NEURO_PC_RAM_BASE;
        pc->ram[o] = (uint8_t)(val & 0xFF);
        pc->ram[o + 1] = (uint8_t)((val >> 8) & 0xFF);
        pc->ram[o + 2] = (uint8_t)((val >> 16) & 0xFF);
        pc->ram[o + 3] = (uint8_t)((val >> 24) & 0xFF);
    }
}

static uint8_t mem_read8(neuro_pc_state_t *pc, uint32_t addr) {
    if (addr < NEURO_PC_RAM_BASE + NEURO_PC_RAM_BYTES)
        return pc->ram[addr - NEURO_PC_RAM_BASE];
    if (addr >= NEURO_PC_DROM_BASE && addr < NEURO_PC_DROM_BASE + NEURO_PC_DROM_BYTES)
        return pc->drom[addr - NEURO_PC_DROM_BASE];
    return 0;
}

static int addr_is_ram(uint32_t addr) {
    return addr < NEURO_PC_RAM_BASE + NEURO_PC_RAM_BYTES;
}
static int addr_is_drom(uint32_t addr) {
    return addr >= NEURO_PC_DROM_BASE && addr < NEURO_PC_DROM_BASE + NEURO_PC_DROM_BYTES;
}
static int addr_is_rom(uint32_t addr) {
    return addr >= NEURO_PC_ROM_BASE && addr < NEURO_PC_ROM_BASE + NEURO_PC_ROM_BYTES;
}

/* --- carga de imagens --- */

int lupin_neuro_pc_load_image(neuro_pc_state_t *pc, uint32_t base, const uint8_t *bytes, size_t len) {
    if (!pc || !bytes) return -1;
    for (size_t i = 0; i < len; i++) {
        uint32_t a = base + (uint32_t)i;
        if (addr_is_ram(a)) pc->ram[a - NEURO_PC_RAM_BASE] = bytes[i];
        else if (addr_is_drom(a)) pc->drom[a - NEURO_PC_DROM_BASE] = bytes[i];
        else if (addr_is_rom(a)) {
            uint32_t o = (a - NEURO_PC_ROM_BASE) & ~3u;
            uint32_t w = pc->rom[o >> 2];
            uint32_t sh = 8u * (a & 3u);
            w = (w & ~(0xFFu << sh)) | ((uint32_t)bytes[i] << sh);
            pc->rom[o >> 2] = w;
        } else return -1;
    }
    return 0;
}

/* Carrega uma imagem ELF32 little-endian RISC-V estática.
 * Cada segmento PT_LOAD é copiado byte a byte para a região que seu vaddr indica:
 *   .text    -> ROM  (0x40000)
 *   .rodata  -> DROM (0x20000)
 *   .data/.bss -> RAM (0x00000)
 * O entry point (e_entry) vira o pc inicial. */
int lupin_neuro_pc_load_elf(neuro_pc_state_t *pc, const uint8_t *elf_bytes, size_t len) {
    if (!pc || !elf_bytes || len < 52) return -1;
    if (memcmp(elf_bytes, "\x7f" "ELF", 4) != 0) return -1;
    if (elf_bytes[4] != 1) return -1;              /* ELFCLASS32 */
    if (elf_bytes[5] != 1) return -1;              /* ELFDATA2LSB */

    uint32_t entry = (uint32_t)elf_bytes[24] | (uint32_t)elf_bytes[25] << 8 |
                     (uint32_t)elf_bytes[26] << 16 | (uint32_t)elf_bytes[27] << 24;
    uint32_t phoff = (uint32_t)elf_bytes[28] | (uint32_t)elf_bytes[29] << 8 |
                     (uint32_t)elf_bytes[30] << 16 | (uint32_t)elf_bytes[31] << 24;
    uint16_t phentsize = (uint16_t)(elf_bytes[42] | (elf_bytes[43] << 8));
    uint16_t phnum = (uint16_t)(elf_bytes[44] | (elf_bytes[45] << 8));

    for (uint16_t i = 0; i < phnum; i++) {
        size_t off = (size_t)phoff + (size_t)i * phentsize;
        if (off + 32 > len) return -1;
        const uint8_t *ph = elf_bytes + off;
        uint32_t p_type = (uint32_t)ph[0] | (uint32_t)ph[1] << 8 |
                          (uint32_t)ph[2] << 16 | (uint32_t)ph[3] << 24;
        uint32_t p_offset = (uint32_t)ph[4] | (uint32_t)ph[5] << 8 |
                            (uint32_t)ph[6] << 16 | (uint32_t)ph[7] << 24;
        uint32_t p_vaddr = (uint32_t)ph[8] | (uint32_t)ph[9] << 8 |
                           (uint32_t)ph[10] << 16 | (uint32_t)ph[11] << 24;
        uint32_t p_filesz = (uint32_t)ph[16] | (uint32_t)ph[17] << 8 |
                            (uint32_t)ph[18] << 16 | (uint32_t)ph[19] << 24;
        uint32_t p_memsz = (uint32_t)ph[20] | (uint32_t)ph[21] << 8 |
                           (uint32_t)ph[22] << 16 | (uint32_t)ph[23] << 24;
        if (p_type != 1) continue;                /* PT_LOAD */
        if ((size_t)p_offset + p_filesz > len) return -1;
        /* arquivos: copia; bss: zera (o calloc já zera, mas ser explícito) */
        for (uint32_t k = 0; k < p_filesz; k++) {
            uint32_t a = p_vaddr + k;
            if (addr_is_ram(a)) pc->ram[a - NEURO_PC_RAM_BASE] = elf_bytes[p_offset + k];
            else if (addr_is_drom(a)) pc->drom[a - NEURO_PC_DROM_BASE] = elf_bytes[p_offset + k];
            else if (addr_is_rom(a)) {
                uint32_t o = (a - NEURO_PC_ROM_BASE) & ~3u;
                uint32_t w = pc->rom[o >> 2];
                uint32_t sh = 8u * (a & 3u);
                w = (w & ~(0xFFu << sh)) | ((uint32_t)elf_bytes[p_offset + k] << sh);
                pc->rom[o >> 2] = w;
            } else return -1;
        }
        for (uint32_t k = p_filesz; k < p_memsz; k++) {
            uint32_t a = p_vaddr + k;
            if (addr_is_ram(a)) pc->ram[a - NEURO_PC_RAM_BASE] = 0;
        }
    }
    pc->pc = entry;
    return 0;
}

/* --- aritmética (idêntica a ref.alu) --- */

static uint32_t alu_add(uint32_t a, uint32_t b) { return a + b; }
static uint32_t alu_sub(uint32_t a, uint32_t b) { return a - b; }
static uint32_t alu_sll(uint32_t a, uint32_t b) { return a << (b & 31); }
static uint32_t alu_srl(uint32_t a, uint32_t b) { return a >> (b & 31); }
static uint32_t alu_sra(uint32_t a, uint32_t b) { return (uint32_t)(sx32(a) >> (b & 31)); }
static uint32_t alu_slt(uint32_t a, uint32_t b) { return (uint32_t)(sx32(a) < sx32(b)); }
static uint32_t alu_sltu(uint32_t a, uint32_t b) { return (uint32_t)(a < b); }
static uint32_t alu_mul(uint32_t a, uint32_t b) { return a * b; }
static uint32_t alu_mulh(uint32_t a, uint32_t b) {
    return (uint32_t)(((int64_t)sx32(a) * (int64_t)sx32(b)) >> 32);
}
static uint32_t alu_mulhsu(uint32_t a, uint32_t b) {
    return (uint32_t)(((int64_t)sx32(a) * (int64_t)(uint64_t)b) >> 32);
}
static uint32_t alu_mulhu(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}
static uint32_t alu_div(uint32_t a, uint32_t b) {
    int32_t sa = sx32(a), sb = sx32(b);
    if (sb == 0) return M32;
    if (sa == (int32_t)0x80000000 && sb == -1) return (uint32_t)sa;
    return (uint32_t)(sa / sb);
}
static uint32_t alu_divu(uint32_t a, uint32_t b) { return b == 0 ? M32 : a / b; }
static uint32_t alu_rem(uint32_t a, uint32_t b) {
    int32_t sa = sx32(a), sb = sx32(b);
    if (sb == 0) return (uint32_t)sa;
    if (sa == (int32_t)0x80000000 && sb == -1) return 0;
    return (uint32_t)(sa % sb);
}
static uint32_t alu_remu(uint32_t a, uint32_t b) { return b == 0 ? a : a % b; }
static uint32_t alu_sh1add(uint32_t a, uint32_t b) { return (a << 1) + b; }
static uint32_t alu_sh2add(uint32_t a, uint32_t b) { return (a << 2) + b; }
static uint32_t alu_sh3add(uint32_t a, uint32_t b) { return (a << 3) + b; }

/* --- execução --- */

static void put_char(neuro_pc_state_t *pc, uint8_t c) {
    size_t len = strnlen(pc->stdout_buf, sizeof(pc->stdout_buf) - 1);
    if (len + 1 < sizeof(pc->stdout_buf)) {
        pc->stdout_buf[len] = (char)c;
        pc->stdout_buf[len + 1] = '\0';
    }
}

static void do_fault(neuro_pc_state_t *pc, int cause, uint32_t pc_addr) {
    pc->halted = 1;
    pc->fault = cause;
    pc->fault_pc = pc_addr;
}

int lupin_neuro_pc_step(neuro_pc_state_t *pc, uint32_t max_cycles) {
    if (!pc || pc->halted) return -1;
    uint32_t executed = 0;
    while (executed < max_cycles && !pc->halted) {
        uint32_t cur = pc->pc;
        if (!addr_is_rom(cur)) { do_fault(pc, NEURO_PC_FAULT_BAD_JUMP, cur); break; }
        uint32_t word = pc->rom[(cur - NEURO_PC_ROM_BASE) >> 2];
        uint32_t opc = word & 0x7F;
        uint32_t rd = (word >> 7) & 31;
        uint32_t f3 = (word >> 12) & 7;
        uint32_t rs1 = (word >> 15) & 31;
        uint32_t rs2 = (word >> 20) & 31;
        uint32_t f7 = word >> 25;
        uint32_t a = pc->x[rs1], b = pc->x[rs2];
        uint32_t next = cur + 4;
        uint32_t res = 0;
        int writes_rd = 1;
        int taken = 0;
        uint32_t target = 0;

        switch (opc) {
        case 0x37: /* LUI */
            res = word & 0xFFFFF000u;
            break;
        case 0x17: /* AUIPC */
            res = cur + (word & 0xFFFFF000u);
            break;
        case 0x6F: /* JAL */
            res = next;
            next = cur + imm_j(word);
            target = next;
            taken = 1;
            break;
        case 0x67: /* JALR */
            if (f3 != 0) { do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur); break; }
            res = next;
            next = (a + imm_i(word)) & ~1u;
            target = next;
            taken = 1;
            break;
        case 0x63: /* BRANCH */
            /* Atenção: no formato B os bits [11:7] são parte do imediato, não
             * o campo rd. Um branch NUNCA escreve em registrador — escribir
             * rd=0 aqui corromperia x13/a3 silenciosamente. */
            writes_rd = 0;
            switch (f3) {
            case 0: taken = (a == b); break;
            case 1: taken = (a != b); break;
            case 4: taken = (sx32(a) < sx32(b)); break;
            case 5: taken = (sx32(a) >= sx32(b)); break;
            case 6: taken = (a < b); break;
            case 7: taken = (a >= b); break;
            default: do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur); break;
            }
            if (pc->halted) break;
            if (taken) { next = cur + imm_b(word); target = next; }
            break;
        case 0x03: /* LOAD */
        {
            uint32_t ea = a + imm_i(word);
            uint32_t n = (f3 == 0 || f3 == 4) ? 1 : (f3 == 1 || f3 == 5) ? 2 : 4;
            if (ea % n) { do_fault(pc, NEURO_PC_FAULT_MISALIGNED_LOAD, cur); break; }
            if (!addr_is_ram(ea) && !addr_is_drom(ea)) {
                do_fault(pc, NEURO_PC_FAULT_LOAD_OUT_OF_RANGE, cur);
                break;
            }
            uint32_t w = mem_read32(pc, ea & ~3u);
            uint32_t v = (w >> (8u * (ea & 3u))) & ((n == 4) ? 0xFFFFFFFFu : ((1u << (8u * n)) - 1u));
            if (f3 == 0 || f3 == 1) v = (uint32_t)sx32(v << (32 - 8u * n)) >> (32 - 8u * n);
            res = v;
            break;
        }
        case 0x23: /* STORE */
        {
            uint32_t ea = a + imm_s(word);
            uint32_t n = (f3 == 0) ? 1 : (f3 == 1) ? 2 : 4;
            if (ea % n) { do_fault(pc, NEURO_PC_FAULT_MISALIGNED_STORE, cur); break; }
            if (!addr_is_ram(ea)) { do_fault(pc, NEURO_PC_FAULT_STORE_OUT_OF_RANGE, cur); break; }
            uint32_t sh = 8u * (ea & 3u);
            uint32_t mask = (n == 4) ? 0xFFFFFFFFu : ((1u << (8u * n)) - 1u);
            uint32_t old = mem_read32(pc, ea & ~3u);
            uint32_t merged = (old & ~(mask << sh)) | ((b & mask) << sh);
            mem_write32(pc, ea & ~3u, merged);
            writes_rd = 0;
            break;
        }
        case 0x13: /* OP-IMM */
            switch (f3) {
            case 0: res = alu_add(a, imm_i(word)); break;
            case 2: res = alu_slt(a, imm_i(word)); break;
            case 3: res = alu_sltu(a, imm_i(word)); break;
            case 4: res = a ^ imm_i(word); break;
            case 6: res = a | imm_i(word); break;
            case 7: res = a & imm_i(word); break;
            case 1: if (f7 != 0) { do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur); break; }
                    res = alu_sll(a, rs2); break;
            case 5: if (f7 == 0) res = alu_srl(a, rs2);
                    else if (f7 == 0x20) res = alu_sra(a, rs2);
                    else { do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur); break; }
                    break;
            }
            break;
        case 0x33: /* OP */
            if (f7 == 0) {
                switch (f3) {
                case 0: res = alu_add(a, b); break;
                case 1: res = alu_sll(a, b); break;
                case 2: res = alu_slt(a, b); break;
                case 3: res = alu_sltu(a, b); break;
                case 4: res = a ^ b; break;
                case 5: res = alu_srl(a, b); break;
                case 6: res = a | b; break;
                case 7: res = a & b; break;
                }
            } else if (f7 == 0x20 && (f3 == 0 || f3 == 5)) {
                res = (f3 == 0) ? alu_sub(a, b) : alu_sra(a, b);
            } else if (f7 == 1) {
                switch (f3) {
                case 0: res = alu_mul(a, b); break;
                case 1: res = alu_mulh(a, b); break;
                case 2: res = alu_mulhsu(a, b); break;
                case 3: res = alu_mulhu(a, b); break;
                case 4: res = alu_div(a, b); break;
                case 5: res = alu_divu(a, b); break;
                case 6: res = alu_rem(a, b); break;
                case 7: res = alu_remu(a, b); break;
                }
            } else if (f7 == 0x10 && (f3 == 2 || f3 == 4 || f3 == 6)) {
                res = (f3 == 2) ? alu_sh1add(a, b) : (f3 == 4) ? alu_sh2add(a, b) : alu_sh3add(a, b);
            } else {
                do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur);
            }
            break;
        case 0x0F: /* FENCE */
            writes_rd = 0;
            break;
        case 0x73: /* SYSTEM */
            if (word == 0x00000073u) { /* ECALL */
                uint32_t nr = pc->x[17], arg0 = pc->x[10];
                if (nr == 93) { pc->halted = 1; pc->exit_code = (int)arg0; writes_rd = 0; }
                else if (nr == 1) { put_char(pc, (uint8_t)(arg0 & 0xFF)); writes_rd = 0; }
                else if (nr == 64) { /* write(fd, buf, n) */
                    uint32_t buf = pc->x[11], n = pc->x[12];
                    for (uint32_t i = 0; i < n; i++) put_char(pc, mem_read8(pc, buf + i));
                    writes_rd = 0;
                } else {
                    do_fault(pc, NEURO_PC_FAULT_ECALL, cur);
                }
            } else if (word == 0x00100073u) { /* EBREAK */
                do_fault(pc, NEURO_PC_FAULT_EBREAK, cur);
            } else {
                do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur);
            }
            break;
        default:
            do_fault(pc, NEURO_PC_FAULT_ILLEGAL, cur);
            break;
        }

        if (pc->halted) break;

        if (writes_rd && rd != 0) pc->x[rd] = res;
        pc->x[0] = 0;

        if (taken) {
            if (!addr_is_rom(target)) { do_fault(pc, NEURO_PC_FAULT_BAD_JUMP, cur); break; }
        }
        pc->pc = next;
        pc->cycles++;
        executed++;
    }
    return (int)executed;
}

/* --- compilação e execução de um trecho C --- */

#ifndef LUPIN_RV_CROSS_CC
#define LUPIN_RV_CROSS_CC "riscv64-linux-gnu-gcc"
#endif

int lupin_neuro_pc_run_script(neuro_pc_state_t *pc, const char *code, char *out_buf, size_t out_max) {
    if (!pc || !code || !out_buf || out_max == 0) return -1;
    out_buf[0] = '\0';

    /* mkstemp substitui o XXXXXX no buffer; os caminhos definitivos saem
     * de snprintf (o template do mkstemp nao pode ter sufixo depois do XXXXXX). */
    char src_t[] = "/tmp/lupin_pc_XXXXXX";
    char elf_t[] = "/tmp/lupin_pc_XXXXXX";
    char ld_t[] = "/tmp/lupin_pc_XXXXXX";
    int fd1 = mkstemp(src_t);
    if (fd1 < 0) return -1;
    close(fd1);
    int fd2 = mkstemp(elf_t);
    if (fd2 < 0) { unlink(src_t); return -1; }
    close(fd2);
    int fd3 = mkstemp(ld_t);
    if (fd3 < 0) { unlink(src_t); unlink(elf_t); return -1; }
    close(fd3);
    char src[64], elf[64], ld[64];
    snprintf(src, sizeof(src), "%s.c", src_t);
    snprintf(elf, sizeof(elf), "%s.elf", elf_t);
    snprintf(ld, sizeof(ld), "%s.ld", ld_t);

    FILE *f = fopen(src, "w");
    if (!f) { unlink(src); unlink(elf); return -1; }
    /* O crt0 do circuito: _start chama main e depois exit(a0).
     * rt.h em rv/env define rv_putchar (ecall a7=1) e rv_ecall2. */
    fputs(
        "/* Prelude do circuito: sem libc, so ecalls. */\n"
        "static inline long ecall2(long n, long a0, long a1, long a2) {\n"
        "  register long r_a0 __asm__(\"a0\") = a0, r_a1 __asm__(\"a1\") = a1, r_a2 __asm__(\"a2\") = a2;\n"
        "  register long r_a7 __asm__(\"a7\") = n;\n"
        "  __asm__ volatile(\"ecall\" : \"+r\"(r_a0) : \"r\"(r_a1), \"r\"(r_a2), \"r\"(r_a7) : \"memory\");\n"
        "  return r_a0;\n"
        "}\n"
        "static inline void rv_putchar(char c) { ecall2(1, (long)(unsigned char)c, 0, 0); }\n"
        "static inline long rv_ecall2(long n, long a0, long a1, long a2) { return ecall2(n, a0, a1, a2); }\n"
        "void exit(int code) { for (;;) ecall2(93, (long)code, 0, 0); }\n"
        "int main(void) {\n",
        f);
    /* O corpo do usuário é o corpo de main: pode chamar rv_putchar, usar
     * aritmética de 32 bits e devolver o código de saída. */
    fputs(code, f);
    fputs("\n}\n", f);
    /* _start é o entry point do circuito: chama main e passa o retorno como
     * código de saída via ecall a7=93. */
    fputs("void _start(void) { ecall2(93, (long)main(), 0, 0); for (;;); }\n", f);
    fclose(f);

    /* O linker script do circuito (rv/env/rom.ld) define o mapa de memória.
     * Replicado aqui para não depender do repositório de laboratório. */
    FILE *lf = fopen(ld, "w");
    if (!lf) { unlink(src); unlink(elf); unlink(ld); return -1; }
    fputs(
        "ENTRY(_start)\n"
        "MEMORY {\n"
        "  RAM (rw) : ORIGIN = 0x100, LENGTH = 0x7F00\n"
        "  DROM (r) : ORIGIN = 0x20000, LENGTH = 128K\n"
        "  ROM (rx) : ORIGIN = 0x40000, LENGTH = 256K\n"
        "}\n"
        "SECTIONS {\n"
        "  .text : { *(.text.init) *(.text .text.*) } > ROM\n"
        "  .rodata : { *(.rodata .rodata.*) *(.init_array .init_array.*) "
        "*(.fini_array .fini_array.*) *(.eh_frame .eh_frame.*) } > DROM\n"
        "  .data : { *(.data .data.*) *(.sdata .sdata.*) } > RAM\n"
        "  .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } > RAM\n"
        "  __heap_start = .;\n"
        "  __heap_end = 0x7000;\n"
        "  _end = __heap_start;\n"
        "}\n",
        lf);
    fclose(lf);

    /* O cross-compiler fica na estação de trabalho (Mimir); o nó de execução
     * (Fenrir) recebe ELF prontos. Se não há compilador, dizemos isso
     * claramente em vez de um "compile failed" opaco. */
    char probe[256];
    snprintf(probe, sizeof(probe), "command -v %s >/dev/null 2>&1", LUPIN_RV_CROSS_CC);
    if (system(probe) != 0) {
        snprintf(out_buf, out_max,
                 "cross-compiler '%s' not found: build the ELF on the workstation "
                 "and use 'lupin-pc elf <file.elf>'",
                 LUPIN_RV_CROSS_CC);
        unlink(src);
        unlink(ld);
        unlink(elf);
        return -1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "%s -march=rv32im -mabi=ilp32 -nostdlib -nostartfiles -static -O2 "
             "-T %s -o %s %s 2>&1",
             LUPIN_RV_CROSS_CC, ld, elf, src);
    int rc = system(cmd);
    unlink(src);
    unlink(ld);

    if (rc != 0) {
        snprintf(out_buf, out_max, "compile failed");
        unlink(elf);
        return -1;
    }

    FILE *ef = fopen(elf, "rb");
    if (!ef) { snprintf(out_buf, out_max, "compile produced no elf"); unlink(elf); return -1; }
    fseek(ef, 0, SEEK_END);
    long sz = ftell(ef);
    fseek(ef, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(ef); unlink(elf); return -1; }
    if (fread(buf, 1, (size_t)sz, ef) != (size_t)sz) {
        free(buf); fclose(ef); unlink(elf);
        snprintf(out_buf, out_max, "elf read failed");
        return -1;
    }
    fclose(ef);

    lupin_neuro_pc_reset(pc);
    if (lupin_neuro_pc_load_elf(pc, buf, (size_t)sz) != 0) {
        free(buf); unlink(elf);
        snprintf(out_buf, out_max, "elf load failed");
        return -1;
    }
    free(buf);
    unlink(elf);

    lupin_neuro_pc_step(pc, 10000000);
    snprintf(out_buf, out_max, "%s", pc->stdout_buf);
    return pc->fault == NEURO_PC_OK ? 0 : -1;
}

const char *lupin_neuro_pc_fault_name(int fault) {
    static const char *names[] = {"illegal", "ebreak", "misaligned load", "misaligned store",
                                  "load out of range", "store out of range", "bad jump", "ecall"};
    if (fault < 0 || fault > 7) return "none";
    return names[fault];
}
