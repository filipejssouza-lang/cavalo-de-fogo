# 🧭 SPEC — Fase 2 do Lupin Neuro-PC: Guard (L1) + Lupin-Store

> Especificação para execução pelo **Google Jules**. Escreva no repositório
> `filipejssouza-lang/cavalo-de-fogo`, branch **`feat/neuro-pc-phase2-guard-store`**, base `master`.
>
> **Escopo estrito e delimitado.** O trabalho tem três partes independentes (Guard, Store,
> Endpoints) e uma de testes. **Não refatore o que já existe.** O motor RV32IM passou por uma
> auditoria e tem paridade bit-a-bit com o QEMU e com o `ref.py` do Qwenputer
> (`tests/golden_test.py`, 18/18). **Qualquer mudança no motor que-break esse teste é
> proibida.**

---

## 0. Ponto de partida (já existe, NÃO reescreva)

| Arquivo | Estado |
|---|---|
| `src/neuro_pc/lupin_neuro_pc.h` | API do motor: `neuro_pc_state_t`, `lupin_neuro_pc_init/reset/free/load_elf/load_image/step/run_script` |
| `src/neuro_pc/lupin_neuro_pc.c` | Motor RV32IM + Zba real, mapa de memória do circuito (RAM 32 KB em `0x0`, DROM 128 KB em `0x20000`, ROM 256 KB em `0x40000`), parser ELF32, ECALL putchar/exit/write |
| `src/neuro_pc/lupin_pc_cli.c` | CLI: `status`, `run <código-C>`, `elf <arquivo.elf>` — saída em JSON |
| `tests/test_neuro_pc.c` | **8 testes unitários, todos verdes.** Não podem quebrar. |
| `tests/golden_test.py` | **Golden test de 3 motores** (QEMU × `ref.py` do Qwenputer × motor C) na mesma imagem ELF, exigindo stdout + exit code idênticos. **18/18 verdes.** Não pode quebrar. |

O enum de faults atual (`lupin_neuro_pc.h`) tem 8 valores (`0..7`) e `NEURO_PC_OK = -1`, com
`lupin_neuro_pc_fault_name()` indexando um array de 8 strings **na mesma ordem**. Ao adicionar
`NEURO_PC_FAULT_GUARD`, acrescente-o **no fim** (valor `8`) e acrescente a string correspondente
**no fim** do array — senão os fault codes existentes mudam de número.

---

## 1. Referência de arquitetura (leia antes de codar)

A bancada tem o clone do **openTPU** (`FeSens/openTPU`), que é a implementação de referência
de DeltaNet com simulador bit-exact:

| Arquivo | O que tira dele |
|---|---|
| `~/lab-bench/openTPU/docs/qwen35.md` | A especificação do **Gated DeltaNet** e do pareamento de **Value Heads** (`qk_share`: "os value heads de um par compartilham um key head"). |
| `~/lab-bench/openTPU/opentpu/llm/qwen35.py` | O campo `Spec.qk_share` e como os pares de value heads compartilham q/k. |
| `~/lab-bench/openTPU/opentpu/isasim.py` | O **simulador bit-exact** — o modelo de ouro do RTL: "depois de rodar o mesmo programa nas mesmas imagens de DRAM, o conteúdo de TMEM e DRAM de cada slice deve ser idêntico bit a bit". |

**A lição de arquitetura que vale para as duas peças:** no `openTPU`, a recurrent state do
DeltaNet (16 heads × 128 × 128 fp32 por camada) **é a memória**. O hardware não tem DRAM de
chave-valor separada para o estado do modelo — o estado *é* o DeltaNet. É a mesma premissa do
Qwenputer: "a memória RAM é a regra delta".

**Consequência prática para o Store desta fase:** o `lupin_store` aqui é a **versão
host-side** do store em pesos (o mesmo que o Qwenputer implementa em `chipformer/q35/store.py`,
com 80 cabeças DeltaNet e 66.560 tokens). O formato binário do snapshot que você vai
implementar **deve ser compatível com a possibilidade de virar o DeltaNet depois** — ou seja,
o snapshot é serializado de forma estruturada (chave → valor, com metadados), não como um blob
opaco. Documente essa decisão no docstring.

---

## 2. Peça A — Lupin-Guard (L1): filtro determinístico antes da execução

### 2.1 O que é

Uma função pura, sem alocação, que recebe o comando a ser executado e diz se ele viola as
regras soberanas da Matilha. **É o filtro de política — não é um sandbox.** Ele corre ANTES de
qualquer execução, e a decisão é determinística e auditável.

### 2.2 Assinatura exata

```c
/* Em src/neuro_pc/lupin_neuro_pc.h */

/* Verifica o comando contra as regras soberanas da Matilha.
 * Retorna 0 se o comando e permitido, ou um codigo de fault (> 0) se for violado.
 * Nao aloca memoria. Nao escreve em nenhum buffer. */
int lupin_neuro_pc_guard_check(const char *code);
```

E o fault dedicado, acrescentado **no fim** do enum existente:

```c
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
    NEURO_PC_FAULT_GUARD          /* <-- NOVO, valor 8 */
} neuro_pc_fault_t;
```

Acrescente `"guard"` ao **fim** do array de `lupin_neuro_pc_fault_name()`.

### 2.3 Os padrões bloqueados

Bloqueie estes padrões destrutivos (case-insensitive, procurando **substring**):

| Padrão | Regra violada |
|---|---|
| `./gradlew` | Compilação local no PC (7 GB de RAM) — proibido; use `gh workflow run` |
| `gradle` | idem |
| `rm -rf /` | Destruição do filesystem |
| `:(){:|:&};:` | Fork bomb |

**Atenção ao caso `gradle`:** ele é proibido mesmo sozinho (a regra é "nada de Gradle local").
Uma vez que `gradle` éForbidden, o padrão `./gradlew` é coberto pelo mesmo `strstr`. Mas
**mantenha os dois listados explicitamente** na tabela, para que a auditoria veja a intenção.

**Atenção ao `rm -rf /`:** o padrão exato é `rm -rf /` (com barra). `rm -rf /tmp` **não** deve
ser bloqueado — é um comando normal. Portanto o matcher precisa checar `rm -rf /` seguido de
**fim da string ou espaço** — não basta `strstr("rm -rf /")`, porque `rm -rf /home/x` contém
essa substring e **não** é o mesmo comando.

Faça assim: normalize o texto (remova espaços多余 e quebras de linha extras para a comparação
do prefixo), e para `rm -rf /` exija que o que vem depois seja fim-de-string ou um separador
(espaço, `;`, `&&`, `|`, fim).

### 2.4 A tabela de regras

Não escreva os padrões espalhados no código. Use uma **tabela estática** no `.c`, com o padrão,
a regra e a mensagem:

```c
typedef struct {
    const char *pattern;      /* o que casar (lowercase) */
    const char *rule;         /* nome da regra violada */
    const char *message;      /* mensagem para o usuario */
    int match_prefix;         /* 1 = o padrao deve ser o inicio do comando */
} neuro_pc_guard_rule_t;

static const neuro_pc_guard_rule_t NEURO_PC_GUARD_RULES[] = { ... };
```

Exporte também a contagem, para o teste e para a auditoria:

```c
#define NEURO_PC_GUARD_RULE_COUNT (sizeof(NEURO_PC_GUARD_RULES) / sizeof(NEURO_PC_GUARD_RULES[0]))
```

### 2.5 Integração no motor

Onde o guard é chamado:

1. **`lupin_neuro_pc_run_script()`** — no **começo**, antes de compilar/executar. Se o guard
   bloquear, a função retorna `-1`, o estado fica com `fault = NEURO_PC_FAULT_GUARD` e o
   `stdout_buf` recebe a mensagem do guard (explicando a regra violada). **Não execute nada.**
2. **A rota `/pc/run` do daemon** — explicitamente, antes de invocar o binário (ver Peça C).

**Não quebre o `lupin-pc elf`** — o caminho de carga de ELF **não** passa pelo guard (é um
binário já compilado, não um comando de shell). O golden test depende disso.

### 2.6 Critérios do Guard (testes)

| # | Entrada | Esperado |
|---|---|---|
| G1 | `git status` | permitido |
| G2 | `cargo test` | permitido |
| G3 | `gh workflow run` | permitido |
| G4 | `cluster-status` | permitido |
| G5 | `./gradlew assembleRelease` | **BLOQUEADO** |
| G6 | `gradle build` | **BLOQUEADO** |
| G7 | `rm -rf /` | **BLOQUEADO** |
| G8 | `rm -rf /home/filipe` | **PERMITIDO** (não é a raiz) |
| G9 | `:(){ :|:& };:` | **BLOQUEADO** |
| G10 | `echo gradle` | **BLOQUEADO** (`gradle` é proibido mesmo como argumento — a regra é "nada de Gradle local") |
| G11 | `GRADLE BUILD` (maiúsculas) | **BLOQUEADO** (case-insensitive) |
| G12 | `git status && rm -rf /` | **BLOQUEADO** |
| G13 | `""` (string vazia) | permitido |
| G14 | `NULL` (ponteiro nulo) | permitido, sem crash |

> **Nota sobre G10:** este caso **contradiz** a regra "match por comando, não por substring"
> que vale para o guard em pesos (o `lupin_guard_ref.py` no repo `lupin-neuro-os`). Ali,
> `echo gradle build` é permitido porque `gradle build` é um *argumento* do `echo`. **Aqui é
> diferente**, e a diferença é deliberada: esta é a **Camada 1 (L1)**, o filtro de política
> do nó, que aplica a regra do cluster de forma conservadora — *qualquer menção a `gradle` no
> comando é bloqueada*, porque o objetivo é não deixar o build local acontecer, e o usuário não
> tem como saber o que o `echo` realmente faria. **Documente essa decisão** no docstring do
> guard, em um parágrafo curto explicando a diferença entre L1 (política do nó, conservadora)
> e o guard in-weights (veredito semântico sobre o comando executado).

---

## 3. Peça B — Lupin-Store: filesystem/key-value in-memory

### 3.1 O que é

Um armazenamento chave-valor **in-memory**, acoplado ao estado da máquina, com tabela hash,
até **64 entradas**, e **snapshot binário** para persistência.

### 3.2 A estrutura

```c
#define NEURO_PC_STORE_MAX_ENTRIES 64
#define NEURO_PC_STORE_KEY_MAX   64   /* bytes, sem o NUL */
#define NEURO_PC_STORE_VAL_MAX   256  /* bytes, sem o NUL */

typedef struct {
    char key[NEURO_PC_STORE_KEY_MAX + 1];
    char value[NEURO_PC_STORE_VAL_MAX + 1];
    int  used;        /* 1 = ocupada, 0 = livre */
    uint32_t hash;    /* hash da chave (FNV-1a) */
} neuro_pc_store_entry_t;

typedef struct {
    neuro_pc_store_entry_t entries[NEURO_PC_STORE_MAX_ENTRIES];
    int count;
    uint64_t version;
} neuro_pc_store_t;
```

**FNV-1a 32 bits** para o hash (sem dependência externa, determinístico, sem alocação):
```c
static uint32_t neuro_pc_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}
```

**Tabela hash com sondagem linear.** O índice inicial é `hash % NEURO_PC_STORE_MAX_ENTRIES`;
se a entrada estiver ocupada com outra chave, sonde a próxima (linear probing), até achar
livre ou voltar ao início (tabela cheia → falha).

### 3.3 Anexação ao estado da máquina

```c
typedef struct {
    /* ... o que já existe ... */
    neuro_pc_store_t store;   /* o store vive DENTRO do estado: some com ele, e o snapshot vai junto */
} neuro_pc_state_t;
```

**O store é parte do estado**, não um singleton global. Assim:
- `lupin_neuro_pc_reset()` deve **limpar** o store (ou preservá-lo? — **limpe**, e documente).
- O snapshot do estado carrega o store automaticamente.

### 3.4 API exata

```c
/* Todas devolvem 0 em sucesso e -1 em erro (tabela cheia, chave inválida, etc.). */

int lupin_store_put(neuro_pc_state_t *pc, const char *key, const char *value);
int lupin_store_get(neuro_pc_state_t *pc, const char *key, char *out, size_t out_size);
int lupin_store_delete(neuro_pc_state_t *pc, const char *key);
int lupin_store_list(neuro_pc_state_t *pc, char *out, size_t out_size);   /* uma chave por linha */
int lupin_store_count(neuro_pc_state_t *pc);
```

**Semântica:**
- `put` de uma chave **existente** **sobrescreve** o valor (não duplica, não cresce a tabela).
- `get` de chave ausente devolve `-1` e `out[0] = '\0'`.
- `delete` de chave ausente devolve `-1`.
- `list` escreve as chaves (não os valores), uma por linha, em ordem de índice da tabela.
  Devolve o número de chaves escritas.
- Chaves com mais de `NEURO_PC_STORE_KEY_MAX` bytes → `-1` (sem truncar em silêncio).
- Valores com mais de `NEURO_PC_STORE_VAL_MAX` bytes → `-1`.

### 3.5 Snapshot binário

```c
/* Serializa o store em um buffer binario portavel.
 * Devolve o numero de bytes escritos, ou -1 em erro. */
int lupin_store_snapshot(const neuro_pc_store_t *store, uint8_t *buf, size_t buf_size);

/* Restaura o store de um snapshot. Devolve 0 em sucesso, -1 em erro (buffer invalido,
 * magic errado, versao desconhecida, ou checksum divergente). */
int lupin_store_restore(neuro_pc_store_t *store, const uint8_t *buf, size_t buf_size);
```

**Formato do snapshot (defina e documente; seja explícito):**

| Offset | Tamanho | Conteúdo |
|---|---|---|
| 0 | 4 | magic `"LPS1"` (0x4C505331) |
| 4 | 4 | `version` (uint32 LE) = 1 |
| 8 | 4 | `count` (uint32 LE) |
| 12 | 4 | `payload_bytes` (uint32 LE) |
| 16 | 4 | checksum FNV-1a do payload (uint32 LE) |
| 20 | … | payload: para cada entrada ocupada, `key_len(u8)`, `key`, `val_len(u8)`, `value` |

O **checksum** existe para que um snapshot corrompido seja detectado, não carregado em
silêncio. `restore` deve rejeitar magic errado, versão desconhecida, `count` inconsistente,
`payload_bytes` fora do buffer e checksum divergente.

### 3.6 Critérios do Store (testes)

| # | Caso | Esperado |
|---|---|---|
| S1 | `put("a","1")` depois `get("a")` | `"1"` |
| S2 | `put("a","1")` depois `put("a","2")` depois `get("a")` | `"2"`, e `count` continua 1 |
| S3 | `get("inexistente")` | `-1`, `out` vazio |
| S4 | `delete("a")` e depois `get("a")` | `-1` |
| S5 | `delete("inexistente")` | `-1` |
| S6 | 64 entradas distintas | `count` == 64, todas retrievable |
| S7 | 65ª entrada | `-1` (tabela cheia) — **e o `count` continua 64** |
| S8 | `list` com 3 entradas | devolve 3 linhas |
| S9 | chave com 100 bytes | `-1` |
| S10 | valor com 300 bytes | `-1` |
| S11 | snapshot → restore | store idêntico (todas as chaves e valores) |
| S12 | snapshot com **byte corrompido** no payload | `restore` devolve `-1` (checksum) |
| S13 | snapshot com magic errado | `restore` devolve `-1` |
| S14 | `list` vazio | devolve 0, `out` vazio |
| S15 | `put` com `key == NULL` ou `value == NULL` | `-1`, sem crash |

---

## 4. Peça C — Endpoints do daemon

Em `crates/lupinos-daemon/src/main.rs`, ao lado dos handlers `/pc/run`, `/pc/status`,
`/pc/reset` que já existem.

### 4.1 Rotas novas

| Rota | Método | Body | Resposta |
|---|---|---|---|
| `/pc/store/put` | POST | `{"key":"...","value":"..."}` | `{"status":"ok","count":N}` |
| `/pc/store/get` | POST | `{"key":"..."}` | `{"status":"ok","value":"..."}` ou `{"status":"error","error":"not found"}` |
| `/pc/store/list` | POST ou GET | (vazio) | `{"status":"ok","count":N,"keys":["a","b"]}` |
| `/pc/store/delete` | POST | `{"key":"..."}` | `{"status":"ok","count":N}` |

### 4.2 Requisitos

1. **Use o mesmo estilo de parsing do `handle_pc_run` que já existe** — o `json_string_field()`
   que você criou (com escapes `\n`, `\"`, `\\`, `\uXXXX`). Não invente um parser novo.
2. **Integre o `guard_check` na rota `/pc/run`**: antes de invocar o binário, verifique o
   código com a lógica do guard. Se for bloqueado, devolva **HTTP 403** com
   `{"status":"blocked","rule":"...","message":"..."}` — **não** chame o binário.
3. **O caminho do binário**: o `neuro_pc_bin()` que você já criou (caminhos absolutos) —
   **não reintroduza** o `./build/lupin-pc` relativo.
4. **O store é estado do processo do daemon**: como o daemon é um processo único e o store
   vive no `neuro_pc_state_t`, as rotas do store precisam de um estado **persistido entre
   requisições**. Duas opções aceitáveis:
   - **(a) Preferida:** o daemon mantém um `neuro_pc_state_t` global (lazy-init) e as rotas
     `/pc/store/*` o usam; o `/pc/run` usa **o mesmo** estado (para que um `put` seguido de
     um `run` veja o store).
   - **(b)** o store é serializado num arquivo em disco pelo daemon.
   **Escolha uma e documente.** Se escolher (a), use `static` + init sob demanda, e não
   zere o store a cada requisição.

### 4.3 Critérios dos Endpoints (testes)

Os testes de endpoint em Rust: o arquivo já tem testes embutidos (`#[cfg(test)]`). Adicione
testes **de função pura** (sem abrir socket) para:
| # | Caso | Esperado |
|---|---|---|
| E1 | `json_string_field` com `"code":"a\"b"` | `a"b` |
| E2 | `json_string_field` com escape `\u0041` | `A` |
| E3 | `json_string_field` sem o campo | `None` |
| E4 | store: put → get → list → delete, em sequência | valores corretos |

---

## 5. Peça D — Testes unitários

**Em `tests/test_neuro_pc.c`** (mesmo arquivo, novos testes ao final):

1. **8 testes existentes**: continuam **intactos**. Não edite suas asserções.
2. **Novos**: pelo menos **G1..G14** (guard) e **S1..S15** (store) — como funções
   `test_guard_*` e `test_store_*` separadas, cada uma com `assert` e mensagem clara.
3. O `main()` chama todos e imprime `All neuro_pc tests passed.` no final (como hoje).

**O `tests/golden_test.py` não pode ser tocado.** Ele é o golden test de 3 motores e é a
prova de paridade do motor.

### 5.1 Critérios de aceitação

Tudo isto tem que ser verdade:

- [ ] `make build/lupin-pc build/test_neuro_pc` compila **sem warning novo**
- [ ] `./build/test_neuro_pc` → **todos os testes passam**, incluindo os 8 originais
- [ ] `python3 tests/golden_test.py` → **18/18**, `PARIDADE TOTAL`
- [ ] `cargo build --release` em `crates/lupinos-daemon/` compila sem erro
- [ ] `NEURO_PC_FAULT_GUARD` tem valor **8** e `"guard"` é a 9ª string de
      `lupin_neuro_pc_fault_name()`
- [ ] Nenhum arquivo do `chipformer/` (upstream) foi tocado — não existe neste repo, mas
      **não** adicione um fork
- [ ] O guard **não** afeta o caminho `lupin-pc elf` (o golden test depende disso)

---

## 6. Ordem de execução (evite timeout)

Faça **nesta ordem**, committing cada etapa separadamente:

1. **Peça A** (guard) + testes G1..G14 → commit
2. **Peça B** (store) + testes S1..S15 → commit
3. **Peça C** (endpoints) + testes E1..E4 → commit
4. **Peça D** (rodar tudo, ajustar) → commit

**Cada commit compila e passa os testes.** Se algo não couber no tempo, entregue as peças A e
B completas (que são as centrais) e diga no PR o que ficou pendente. **Um PR parcial e honesto
vale mais que um PR completo e quebrado.**

---

## 7. O que NÃO fazer

- **Não modificar a semântica do motor** (`lupin_neuro_pc_step`, o mapa de memória, o parser
  ELF, os opcodes). O golden test existe para provar que isso está certo.
- **Não mexer em `tests/golden_test.py`.**
- **Não alterar `lupin_neuro_pc_run_script` além de acrescentar a chamada do guard** no
  início.
- **Não introduzir dependências externas** (nada de `uthash`, `jansson`, etc.). A tabela hash
  é sua, em C puro.
- **Não usar `malloc` na função do guard** — ela é chamada antes de qualquer execução e
  precisa ser previsível.

---

## 8. Entrega

- Branch: **`feat/neuro-pc-phase2-guard-store`**, base `master`.
- Pull Request contra `master`, com no corpo:
  - a **saída real** de `./build/test_neuro_pc` e de `python3 tests/golden_test.py`;
  - os critérios de aceitação da seção 5.1 marcados;
  - a escolha que você fez na seção 4.2 (opção (a) ou (b)) e o porquê;
  - qualquer coisa que ficou pendente, explicitamente.
