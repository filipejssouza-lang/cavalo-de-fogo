/*
 * Testes do Lupin-Store — key-value in-memory acoplado ao estado da maquina.
 *
 * A semantica espelha o store em pesos do Qwenputer: a chave decide a linha,
 * o valor e a celula. O snapshot tem magic, versao e checksum, para que um
 * snapshot corrompido seja REJEITADO em vez de carregado em silencio.
 */
#include <string.h>

static void test_store_put_get(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[512];

    assert(lupin_store_put(&pc, "a", "1") == 0);
    assert(lupin_store_get(&pc, "a", out, sizeof(out)) == 0);
    assert(strcmp(out, "1") == 0);
    assert(lupin_store_count(&pc) == 1);

    lupin_neuro_pc_free(&pc);
    printf("test_store_put_get passed\n");
}

static void test_store_put_sobrescreve(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[512];

    assert(lupin_store_put(&pc, "a", "1") == 0);
    assert(lupin_store_put(&pc, "a", "2") == 0);
    assert(lupin_store_get(&pc, "a", out, sizeof(out)) == 0);
    assert(strcmp(out, "2") == 0);
    assert(lupin_store_count(&pc) == 1); /* nao duplica */

    lupin_neuro_pc_free(&pc);
    printf("test_store_put_sobrescreve passed\n");
}

static void test_store_get_inexistente(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[512];
    out[0] = 'X';

    assert(lupin_store_get(&pc, "nao_existe", out, sizeof(out)) == -1);
    assert(out[0] == '\0');
    assert(lupin_store_count(&pc) == 0);

    lupin_neuro_pc_free(&pc);
    printf("test_store_get_inexistente passed\n");
}

static void test_store_delete(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[512];

    assert(lupin_store_put(&pc, "a", "1") == 0);
    assert(lupin_store_delete(&pc, "a") == 0);
    assert(lupin_store_get(&pc, "a", out, sizeof(out)) == -1);
    assert(lupin_store_count(&pc) == 0);
    assert(lupin_store_delete(&pc, "a") == -1); /* ja estava removida */
    assert(lupin_store_delete(&pc, "nunca_existiu") == -1);

    lupin_neuro_pc_free(&pc);
    printf("test_store_delete passed\n");
}

static void test_store_capacidade(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char key[32];

    for (int i = 0; i < NEURO_PC_STORE_MAX_ENTRIES; i++) {
        snprintf(key, sizeof(key), "k%02d", i);
        assert(lupin_store_put(&pc, key, "v") == 0);
    }
    assert(lupin_store_count(&pc) == NEURO_PC_STORE_MAX_ENTRIES);

    /* a 65a entrada nao cabe */
    assert(lupin_store_put(&pc, "overflow", "v") == -1);
    assert(lupin_store_count(&pc) == NEURO_PC_STORE_MAX_ENTRIES); /* nao corrompe */

    /* todas ainda legiveis */
    for (int i = 0; i < NEURO_PC_STORE_MAX_ENTRIES; i++) {
        char out[64];
        snprintf(key, sizeof(key), "k%02d", i);
        assert(lupin_store_get(&pc, key, out, sizeof(out)) == 0);
        assert(strcmp(out, "v") == 0);
    }

    lupin_neuro_pc_free(&pc);
    printf("test_store_capacidade passed\n");
}

static void test_store_list(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[1024];

    assert(lupin_store_put(&pc, "alpha", "1") == 0);
    assert(lupin_store_put(&pc, "beta", "2") == 0);
    assert(lupin_store_put(&pc, "gama", "3") == 0);

    int n = lupin_store_list(&pc, out, sizeof(out));
    assert(n == 3);
    /* uma chave por linha */
    int lines = 0;
    for (char *p = out; *p; p++) if (*p == '\n') lines++;
    assert(lines == 3);
    assert(strstr(out, "alpha") != NULL);
    assert(strstr(out, "beta") != NULL);
    assert(strstr(out, "gama") != NULL);
    /* lista as CHAVES, nao os valores */
    assert(strstr(out, "1") == NULL);

    lupin_neuro_pc_free(&pc);
    printf("test_store_list passed\n");
}

static void test_store_list_vazio(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[64];
    out[0] = 'X';
    assert(lupin_store_list(&pc, out, sizeof(out)) == 0);
    assert(out[0] == '\0');
    lupin_neuro_pc_free(&pc);
    printf("test_store_list_vazio passed\n");
}

static void test_store_limites_de_tamanho(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char big_key[512];
    char big_val[512];

    memset(big_key, 'x', sizeof(big_key));
    memset(big_val, 'x', sizeof(big_val));

    /* chave 1 byte acima do limite -> rejeitada */
    big_key[NEURO_PC_STORE_KEY_MAX + 1] = '\0';
    assert(lupin_store_put(&pc, big_key, "v") == -1);

    /* valor 1 byte acima do limite -> rejeitado */
    big_val[NEURO_PC_STORE_VAL_MAX + 1] = '\0';
    assert(lupin_store_put(&pc, "k", big_val) == -1);

    /* exatamente no limite: aceito */
    big_key[NEURO_PC_STORE_KEY_MAX] = '\0';
    assert(lupin_store_put(&pc, big_key, "v") == 0);
    big_val[NEURO_PC_STORE_VAL_MAX] = '\0';
    assert(lupin_store_put(&pc, "k2", big_val) == 0);

    lupin_neuro_pc_free(&pc);
    printf("test_store_limites_de_tamanho passed\n");
}

static void test_store_null(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[64];
    assert(lupin_store_put(&pc, NULL, "v") == -1);
    assert(lupin_store_put(&pc, "k", NULL) == -1);
    assert(lupin_store_get(&pc, NULL, out, sizeof(out)) == -1);
    assert(lupin_store_delete(&pc, NULL) == -1);
    lupin_neuro_pc_free(&pc);
    printf("test_store_null passed\n");
}

static void test_store_snapshot_restore(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[512];
    uint8_t buf[4096];

    assert(lupin_store_put(&pc, "alpha", "valor-a") == 0);
    assert(lupin_store_put(&pc, "beta", "valor-b") == 0);
    assert(lupin_store_put(&pc, "gama", "valor-c") == 0);

    int n = lupin_store_snapshot(&pc.store, buf, sizeof(buf));
    assert(n > 20);

    neuro_pc_state_t pc2;
    lupin_neuro_pc_init(&pc2, NEURO_PC_RAM_BYTES);
    assert(lupin_store_count(&pc2) == 0);
    assert(lupin_store_restore(&pc2.store, buf, (size_t)n) == 0);
    assert(lupin_store_count(&pc2) == 3);
    assert(lupin_store_get(&pc2, "alpha", out, sizeof(out)) == 0);
    assert(strcmp(out, "valor-a") == 0);
    assert(lupin_store_get(&pc2, "gama", out, sizeof(out)) == 0);
    assert(strcmp(out, "valor-c") == 0);

    lupin_neuro_pc_free(&pc);
    lupin_neuro_pc_free(&pc2);
    printf("test_store_snapshot_restore passed\n");
}

static void test_store_snapshot_corrompido(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    uint8_t buf[4096];

    assert(lupin_store_put(&pc, "alpha", "valor-a") == 0);
    int n = lupin_store_snapshot(&pc.store, buf, sizeof(buf));
    assert(n > 20);

    neuro_pc_state_t pc2;
    lupin_neuro_pc_init(&pc2, NEURO_PC_RAM_BYTES);

    /* byte corrompido no payload -> checksum diverge -> REJEITA */
    uint8_t bad[4096];
    memcpy(bad, buf, (size_t)n);
    bad[25] ^= 0xFF;
    assert(lupin_store_restore(&pc2.store, bad, (size_t)n) == -1);
    assert(lupin_store_count(&pc2) == 0); /* nada foi carregado */

    /* magic errado -> rejeita */
    memcpy(bad, buf, (size_t)n);
    bad[0] = 'X';
    assert(lupin_store_restore(&pc2.store, bad, (size_t)n) == -1);
    assert(lupin_store_count(&pc2) == 0);

    /* versao desconhecida -> rejeita */
    memcpy(bad, buf, (size_t)n);
    bad[4] = 0xFF;
    assert(lupin_store_restore(&pc2.store, bad, (size_t)n) == -1);

    /* buffer curto -> rejeita */
    assert(lupin_store_restore(&pc2.store, buf, 8) == -1);

    lupin_neuro_pc_free(&pc);
    lupin_neuro_pc_free(&pc2);
    printf("test_store_snapshot_corrompido passed\n");
}

static void test_store_snapshot_buffer_pequeno(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    uint8_t buf[64];
    char big[NEURO_PC_STORE_VAL_MAX + 1];

    /* payload grande de proposito: o header sao 20 bytes + 1+len(chave) + 1+len(valor) */
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(lupin_store_put(&pc, "alpha", big) == 0);
    assert(lupin_store_snapshot(&pc.store, buf, sizeof(buf)) == -1);

    lupin_neuro_pc_free(&pc);
    printf("test_store_snapshot_buffer_pequeno passed\n");
}

static void test_store_reset_limpa(void) {
    neuro_pc_state_t pc;
    lupin_neuro_pc_init(&pc, NEURO_PC_RAM_BYTES);
    char out[64];
    assert(lupin_store_put(&pc, "a", "1") == 0);
    assert(lupin_store_count(&pc) == 1);
    lupin_neuro_pc_reset(&pc);
    assert(lupin_store_count(&pc) == 0);
    assert(lupin_store_get(&pc, "a", out, sizeof(out)) == -1);
    lupin_neuro_pc_free(&pc);
    printf("test_store_reset_limpa passed\n");
}
