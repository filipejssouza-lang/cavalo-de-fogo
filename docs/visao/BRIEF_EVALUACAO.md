# 🧾 BRIEF DE AVALIAÇÃO — Repo `cavalo-de-fogo` (Lupin OS v6 / Neuro-OS)

> Para o **agente sênior** que vai avaliar este repositório.
> Data: **2026-11-13**. Tudo abaixo foi **medido**, não inferido.
> Fonte primária dos números: `~/.agents/memory/estado_atual.md` (§1.33–§1.40).

---

## 1. O QUE ESTÁ SENDO AVALIADO (leia primeiro, evita mal-entendido)

**Fenrir = Cavalo de Fogo = Lupin OS v6 = Neuro-PC = Neuro-OS — é UM projeto só.**
São 5 nomes para 5 camadas, não 5 projetos:

| Nome | Camada | Onde |
|---|---|---|
| **Fenrir** | o **nó** (Xiaomi Mi 11 Ultra, SD888, `192.168.1.9`, ssh `2222`) | a bancada |
| **Lupin OS v6** | o **SO bare-metal** que roda nele | este repo → imagem `boot_b` assinada AVB 2.0 |
| **Cavalo de Fogo** | o **repo/projeto** | este repo |
| **Neuro-PC** (`lupin-pc`) | o **motor**: emulador RV32IM+Zba | `src/neuro_pc/` → `build/lupin-pc` |
| **Neuro-OS** | o **computador em pesos** sobre o motor: Qwenputer/QwenPU + **Lupin-Guard** fundido nos pesos | `~/lupin-neuro-os/` (repo separado) |

**Pipeline:**
```
Qwen3.5-4B-CHIP8 (28 GB, guard fundido na Camada 1)
  └─ NPU Hexagon 780 via FastRPC (/dev/msm_fastrpc-2)   [crates/lupinos-fastrpc]
      └─ fallback llama.cpp :8080
          └─ o modelo emite <tool_call> com programa RISC-V (uops do digit file)
              └─ Lupin-PC (motor C AArch64) executa o programa RV32IM+Zba
                  └─ Guard L1 (Camada 1) decide ALLOW/BLOCK (política do nó)
                      └─ stdout volta ao modelo
```

---

## 2. ESTADO MEDIDO (na data do brief)

| Item | Estado | Como reproduzir |
|---|---|---|
| Golden test (3 motores) | 🟢 **18/18** — stdout + exit code + instruções | `python3 tests/golden_test.py` |
| Testes do motor C | 🟢 **28/28** (8 engine · 7 guard · 13 store) | `make build/test_neuro_pc && ./build/test_neuro_pc` |
| Auditoria da Fase 2 (C) | 🟢 **APROVADO — tudo verde** | `bash ~/lupin-neuro-os/tools/audit_phase2.sh` |
| Auditoria da entrega do guard | 🔴 **REPROVADO 3/8** | `python3 ~/lupin-neuro-os/tools/audit_delivery.py` |
| Port AArch64 no Fenrir | 🟢 **15/15 idêntico** ao build x86_64 | `bash ~/lupin-neuro-os/tools/deploy_fenrir.sh --test` |
| Ponte real (inferência no Colab L4) | 🟢 validada — `62468 × 9558 = 597069144` | `~/lupin-neuro-os/colab_chip8_demo.py` |
| Guard in-weights (circuito) | 🟡 implementado; oráculo **24/24**; **entrega incompleta** | `pytest ~/lupin-neuro-os/guard/test_guard_ref.py` |
| `cargo build` das crates | 🟢 passa (1 warning `dead_code` em `get_active`) | `cargo build --manifest-path crates/lupinos-daemon/Cargo.toml` |

**O gap real e único:** a **entrega** do Guard in-weights. O circuito existe
(`~/lupin-neuro-os/guard/lupin_guard_q35.py`, FSM de `slot_head` de chave endógena) e o
oráculo passa 24/24, mas faltam 4 arquivos — a sessão Jules entregou e **não deu push**
(`colab/` e `tests/` ficaram vazios):
`guard/test_guard_q35.py` · `tests/test_acceptance.py` · `colab/build_neuro_os.py` ·
`colab/verify_neuro_os.py`. **3 são CPU-only.**

---

## 3. O QUE MUDOU NESTE REPO EM 2026-11-13 (commits a avaliar)

| Commit | O quê |
|---|---|
| `c195a04` | `COORDENACAO.md`: unifica os nomes + atualiza o estado das fases (F2.5 ✅ 18/18, F3 ✅ validada, port AArch64 ✅, gap único = guard in-weights). Marca o gap do bignum como **RESOLVIDO** (registro histórico mantido para contexto). |
| `8400a17` | `Makefile`: `CFLAGS`/`LDFLAGS` de `=` para `?=` (permite sobrescrita para cross-compile). |
| `ac85dc7` | `feat(neuro-pc)`: **digit file + bignum ops (uops 64-69)**, paridade 18/18. |
| `c0f0f2a` | `tools/build_os_v6.py`: **injeta o motor `lupin-pc` na imagem de boot** (`/opt/lupinos/lupin-pc`) — antes a imagem subia **sem** o motor do Neuro-PC, embora o CI já o compilasse e o daemon Rust já o procurasse (`crates/lupinos-daemon/src/main.rs:824-833`). Novo flag `--neuro-pc`. |
| `docs/visao/*` | Arquiva as 3 iterações da visão "Exocórtex Soberano v6" + a **auditoria contra o estado real** (`AUDITORIA_EXOCORTEX_2026-11-13.md`). |
| `README.md` | Seção "O QUE É O QUÊ" (o mapa de nomes), seção do Motor do Neuro-PC, `--neuro-pc` na montagem, caminho oficial de deploy. |
| `docs/SPEC_FASE2_GUARD_STORE.md` | `14/14` → `18/18` (3 lugares). |

---

## 4. O QUE A VISÃO PROPORCIONA (e a auditoria dela)

Em `docs/visao/`:
- `01-exocortex-visao-conceito.md` — os 5 módulos do Exocórtex em prosa.
- `02-exocortex-blueprints-codigo.md` — blueprints: protocolo binário do HUD, `lora_manager.rs`
  com FNV-1a, integração do guard, enforcer de Cap V/f.
- `03-exocortex-hud-desenho-visual.md` — o desenho visual das 4 páginas da telinha 126×294,
  a paleta "Totem Rubi" e o renderizador procedural.
- `AUDITORIA_EXOCORTEX_2026-11-13.md` — **o que a visão acerta, o que já subiu de versão, o que
  ela mostra como pronto e não está, e 6 erros técnicos concretos nos blueprints** (incluindo
  `#defineVF_CAP_GOLD_KHZ` que não compila, e o pacote do HUD que é **314 bytes**, não 312).

---

## 5. O QUE PEDIMOS QUE SEJA AVALIADO

**A. Correção e honestidade do estado**
1. Os números e veredictos acima estão certos? Se algo estiver inflado, diga qual e por quê.
2. O `audit_delivery.py` (REPROVADO 3/8) está medindo a coisa certa? Os 8 critérios fazem sentido?

**B. Motor e portabilidade**
3. O port AArch64 está realmente equivalente ao x86_64, ou o teste de 15 ELFs é fraco demais
   para provar isso? **O que mais deveria entrar na bateria** — qualifique casos que
   quebrariam um port malfeito (overflow de `pc`, `ram_size` no limite, guard com comando
   >2048 bytes, snapshot LPS1 corrompido, syscall não mapeada)?
4. O `lupin-pc` está no lugar certo na imagem (`/opt/lupinos/lupin-pc`)? A lista de busca do
   daemon Rust cobre o caminho do boot real?
5. A fronteira "cross-compiler na estação / ELF no nó" está bem desenhada? Ou o Fenrir
   deveria poder compilar guest sozinho?

**C. O motor RV32IM contra a spec do Qwenputer**
6. A paridade 18/18 cobre o **domínio** que a spec exige? (O `ref.py` implementa o digit file
   com 8 registradores × 128 dígitos e as 6 uops custom.) O que ainda **não** é coberto?
7. O guard L1 do motor (5 regras, streaming) e o guard in-weights (circuito em tensores) estão
   **duplicando** responsabilidade? Onde deve ficar a fronteira?

**D. Segurança**
8. O guard L1 é uma barreira de segurança real ou apenas uma convenção? Ele roda **antes**
   de qualquer execução? A tabela `commands.json` é a fonte única ou há listas paralelas
   (a visão propõe uma lista de 8 `FORBIDDEN_PATTERNS` no Python — isso duplicaria)?
9. O bypass por truncamento está de fato fechado? Existe outro caminho de bypass
   (normalização de Unicode, symlink, `sh -c`, case folding em locale diferente)?

**E. Escopo do `lupin-guard`**
10. O `lupin-guard` L1 de `src/neuro_pc/` e o `lupin_guard_q35.py` de `~/lupin-neuro-os/` são
    o mesmo produto em dois lugares, ou dois produtos? A documentação atual diz "a tabela
    soberana é `guard/commands.json`" — e o motor C tem as suas 5 regras **hard-coded** em
    `lupin_neuro_pc.c:561`. **Isso é uma divergência real.** Como unificar?

**F. Roadmap**
11. A ordem de incorporação proposta na auditoria (fechar o guard → HUD binário → HUD com
    estado real → Colab) faz sentido? O que está fora de ordem?

---

## 6. RESTRIÇÕES QUE O AVALIADOR DEVE RESPEITAR

- **Nunca** compilar APK Android localmente (builds vão pelo GitHub Actions).
- **Nunca** rodar torch/modelos pesados no Mimir (7 GB de RAM). Tudo com torch vai ao Colab.
- **Nunca** deixar o Fenrir com serviço pesado 24/7 — regime **sob demanda**.
- **Custo zero é lei:** Colab só quando a tarefa justificar.
- **Um dono por arquivo por vez** (regra de ouro do `COORDENACAO.md`).
- A máquina-alvo do motor é **AArch64** (SD888), não x86_64. O Mimir é x86_64 e serve de
  estação de cross e de orquestrador.

---

## 7. COMO REPRODUZIR TUDO (sem depender da nuvem)

```bash
cd ~/cavalo-de-fogo

# 1. Golden test dos 3 motores (QEMU ≡ ref.py ≡ motor C)
python3 tests/golden_test.py            # esperado: PARIDADE TOTAL 18/18

# 2. Testes do motor C (engine + guard + store)
make build/test_neuro_pc && ./build/test_neuro_pc   # esperado: 28/28

# 3. Auditoria da Fase 2 (camada C)
bash ~/lupin-neuro-os/tools/audit_phase2.sh          # esperado: APROVADO

# 4. Auditoria da entrega do guard in-weights
python3 ~/lupin-neuro-os/tools/audit_delivery.py     # esperado: REPROVADO 3/8 (o gap real)

# 5. Port AArch64 no Fenrir (só se ele estiver acessível)
bash ~/lupin-neuro-os/tools/deploy_fenrir.sh --test

# 6. Crates Rust
cargo build --manifest-path crates/lupinos-daemon/Cargo.toml
cargo test  --manifest-path crates/lupinos-daemon/Cargo.toml

# 7. Visão + auditoria
less docs/visao/AUDITORIA_EXOCORTEX_2026-11-13.md
```

**Sem GPU e sem Colab**, os passos 1, 2, 3, 4, 6 e 7 rodam inteiros na máquina.

---

## 8. RESUMO EM UMA LINHA

O motor está **funcional e verificado** (18/18 entre três motores, 28/28 no C, port AArch64
byte a byte idêntico); o que **falta** é a entrega do **Guard in-weights** (4 arquivos, 3
CPU-only) e a Colab Beast para o merge real nos pesos.
