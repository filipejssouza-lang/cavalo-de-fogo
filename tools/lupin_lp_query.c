/*
 * tools/lupin_lp_query.c - Leitor Soberano de LP Metadata (Android Dynamic Partitions)
 * Lê diretamente /dev/block/by-name/super em modo somente-leitura e retorna
 * "<offset_bytes> <size_bytes>" de qualquer partição lógica linear (ex: vendor_a, system_a).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define LP_METADATA_HEADER_MAGIC 0x414C5030U /* "0PLA" */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <super_dev> <part_name>\n", argv[0]);
        return 1;
    }

    const char *dev_path = argv[1];
    const char *want_name = argv[2];

    int fd = open(dev_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open super");
        return 1;
    }

    uint8_t *buf = calloc(1, 131072);
    if (!buf) {
        close(fd);
        return 1;
    }

    ssize_t n = pread(fd, buf, 131072, 0);
    close(fd);
    if (n < 0x4000) {
        fprintf(stderr, "Erro: leitura curta da super (%zd)\n", n);
        free(buf);
        return 1;
    }

    /* Procura cabeçalho 0PLA nos slots padrão (0x3000, 0x13000, etc.) */
    size_t slots[] = { 0x3000, 0x13000, 0x2000, 0x1000 };
    uint8_t *hdr = NULL;
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        if (slots[i] + 256 <= (size_t)n) {
            uint32_t magic = 0;
            memcpy(&magic, buf + slots[i], 4);
            if (magic == LP_METADATA_HEADER_MAGIC) {
                hdr = buf + slots[i];
                break;
            }
        }
    }

    if (!hdr) {
        fprintf(stderr, "Erro: LP Metadata (0PLA) não encontrado em %s\n", dev_path);
        free(buf);
        return 1;
    }

    uint32_t hdr_sz = 0;
    uint32_t p_off = 0, p_num = 0, p_entsz = 0;
    uint32_t e_off = 0, e_num = 0, e_entsz = 0;

    memcpy(&hdr_sz,  hdr + 8,  4);
    memcpy(&p_off,   hdr + 80, 4);
    memcpy(&p_num,   hdr + 84, 4);
    memcpy(&p_entsz, hdr + 88, 4);
    memcpy(&e_off,   hdr + 92, 4);
    memcpy(&e_num,   hdr + 96, 4);
    memcpy(&e_entsz, hdr + 100, 4);

    for (uint32_t i = 0; i < p_num; i++) {
        uint8_t *pe = hdr + hdr_sz + p_off + (size_t)i * p_entsz;
        char pname[37];
        memcpy(pname, pe, 36);
        pname[36] = '\0';

        if (strcmp(pname, want_name) == 0) {
            uint32_t first_ext = 0, num_ext = 0;
            memcpy(&first_ext, pe + 40, 4);
            memcpy(&num_ext,   pe + 44, 4);
            if (num_ext != 1 || first_ext >= e_num) {
                fprintf(stderr, "Erro: partição %s possui %u extents (não contígua)\n", pname, num_ext);
                free(buf);
                return 2;
            }
            uint8_t *ee = hdr + hdr_sz + e_off + (size_t)first_ext * e_entsz;
            uint64_t num_sectors = 0, target_data = 0;
            uint32_t target_type = 0;
            memcpy(&num_sectors, ee + 0,  8);
            memcpy(&target_type, ee + 8,  4);
            memcpy(&target_data, ee + 12, 8);
            if (target_type != 0 || num_sectors == 0) {
                fprintf(stderr, "Erro: partição %s vazia ou não linear\n", pname);
                free(buf);
                return 2;
            }
            printf("%llu %llu\n",
                   (unsigned long long)(target_data * 512ULL),
                   (unsigned long long)(num_sectors * 512ULL));
            free(buf);
            return 0;
        }
    }

    fprintf(stderr, "Partição %s não encontrada\n", want_name);
    free(buf);
    return 1;
}
