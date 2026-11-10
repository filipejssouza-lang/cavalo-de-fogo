/*
 * Lupin Neuro-PC CLI — interface do motor RV32 (weight-space computer).
 *
 *   lupin-pc status              estado do motor
 *   lupin-pc run <codigo-C>      compila para RV32 e executa
 *   lupin-pc elf <caminho.elf>   carrega uma imagem ELF32 e executa
 *
 * A saída é JSON para consumo programático (o daemon Rust fala com este binário).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lupin_neuro_pc.h"

static void print_usage(void) {
    fprintf(stderr,
            "Lupin Neuro-PC CLI (motor RV32IM + Zba)\n"
            "\n"
            "Uso:\n"
            "  lupin-pc status\n"
            "  lupin-pc run <codigo-C>\n"
            "  lupin-pc elf <caminho.elf>\n");
}

/* Escapa uma string para um literal JSON (aspas, barras, controles). */
static void json_escape(const char *in, char *out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 7 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
        case '"': out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        default:
            if (c < 0x20) { o += (size_t)snprintf(out + o, out_size - o, "\\u%04x", c); }
            else out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static void emit_result(const neuro_pc_state_t *pc, const char *status) {
    char escaped[NEURO_PC_STDOUT_BUF_SIZE * 6 + 64];
    json_escape(pc->stdout_buf, escaped, sizeof(escaped));
    printf("{\"status\": \"%s\", \"cycles\": %u, \"exit_code\": %d, \"fault\": \"%s\", "
           "\"output\": \"%s\"}\n",
           status, pc->cycles, pc->exit_code, lupin_neuro_pc_fault_name(pc->fault), escaped);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    neuro_pc_state_t pc;
    if (lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES) != 0) {
        fprintf(stderr, "{\"status\": \"error\", \"error\": \"init failed\"}\n");
        return 1;
    }

    int rc = 0;

    if (strcmp(argv[1], "status") == 0) {
        printf("{\"status\": \"ok\", \"pc\": %u, \"cycles\": %u, \"ram_size\": %zu, "
               "\"halted\": %d, \"fault\": \"%s\"}\n",
               pc.pc, pc.cycles, pc.ram_size, pc.halted, lupin_neuro_pc_fault_name(pc.fault));
    } else if (strcmp(argv[1], "run") == 0 && argc >= 3) {
        char out[NEURO_PC_STDOUT_BUF_SIZE];
        if (lupin_neuro_pc_run_script(&pc, argv[2], out, sizeof(out)) != 0) {
            char escaped[NEURO_PC_STDOUT_BUF_SIZE * 6 + 64];
            json_escape(out, escaped, sizeof(escaped));
            printf("{\"status\": \"error\", \"cycles\": %u, \"fault\": \"%s\", \"output\": \"%s\"}\n",
                   pc.cycles, lupin_neuro_pc_fault_name(pc.fault), escaped);
            rc = 1;
        } else {
            emit_result(&pc, "ok");
            rc = pc.exit_code != 0 ? 1 : 0;
        }
    } else if (strcmp(argv[1], "elf") == 0 && argc >= 3) {
        FILE *f = fopen(argv[2], "rb");
        if (!f) {
            printf("{\"status\": \"error\", \"error\": \"cannot open %s\"}\n", argv[2]);
            lupin_neuro_pc_free(&pc);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) {
            fclose(f);
            printf("{\"status\": \"error\", \"error\": \"empty file\"}\n");
            lupin_neuro_pc_free(&pc);
            return 1;
        }
        unsigned char *buf = (unsigned char *)malloc((size_t)sz);
        if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf);
            fclose(f);
            printf("{\"status\": \"error\", \"error\": \"read failed\"}\n");
            lupin_neuro_pc_free(&pc);
            return 1;
        }
        fclose(f);
        if (lupin_neuro_pc_load_elf(&pc, buf, (size_t)sz) != 0) {
            free(buf);
            printf("{\"status\": \"error\", \"error\": \"elf load failed\"}\n");
            lupin_neuro_pc_free(&pc);
            return 1;
        }
        free(buf);
        lupin_neuro_pc_step(&pc, 10000000);
        emit_result(&pc, pc.fault == NEURO_PC_OK ? "ok" : "error");
        rc = (pc.fault != NEURO_PC_OK || pc.exit_code != 0) ? 1 : 0;
    } else {
        print_usage();
        rc = 1;
    }

    lupin_neuro_pc_free(&pc);
    return rc;
}
