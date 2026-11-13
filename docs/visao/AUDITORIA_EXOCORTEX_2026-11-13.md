# 🐺 AUDITORIA DA VISÃO "EXOCÓRTEX SOBERANO v6" vs. O ESTADO REAL (2026-11-13)

> Audita os 3 documentos da visão (`01-`, `02-`, `03-`) contra o **estado medido** do cluster
> e do repo `cavalo-de-fogo`. Fonte das medições: §1.33–§1.39 do `estado_atual.md`.
> Verdadeiro = reproduzido com comando; Falso = contradito por medição.

---

## 1. ✅ O QUE A VISÃO ACERTA (verificado)

| Afirmação | Verificação |
|---|---|
| `src/display/lupin_subdisplay_hud.c` existe | ✅ `ls src/display/` → `drm_subdisplay.c`, `logo_bitmap.h`, `lupin_subdisplay_hud.c`, `subdisplay_hud.c/.h` |
| `crates/lupinos-display` fala com `/dev/dri/card0` em DRM KMS | ✅ `lib.rs`: `CREATE_DUMB`, `ADDFB`, `MAP_DUMB`, `SETCRTC`, `DIRTYFB`, `PAGE_FLIP`; Connector 64 |
| `crates/lupinos-daemon/src/lora_manager.rs` existe | ✅ |
| `src/audio/lupin_music.c` (CS35L41) existe | ✅ `lupin_audio.c`, `lupin_audio_play.c`, `lupin_music.c/.h` |
| `src/mojo/lupin_mojo_kernels.mojo` e `src/vulkan/vulkan_adreno_gemm.cpp` existem | ✅ + `uma_allocator.cpp`, `gemm.comp` |
| `tools/lupin-cam` existe | ✅ |
| **`lupin-guard` L1 = 7/7** | ✅ `./build/test_neuro_pc` → 28 testes, **7** de guard |
| **`lupin-store` = 13/13** | ✅ os outros 13 são de store (8 de engine) |
| **Cap V/f FROZEN `cpu4-6: 1728000` / `cpu7: 1843200`** | ✅ decisão real do cluster (out/2026, `diario_de_bordo.md`): sem o cap, **94,9 °C em 4 s** de rajada |
| Layout do LPS1 do blueprint Python | ✅ bate com o motor C: magic `LPS1` em 0, checksum FNV-1a **32-bit** em 16, payload em 20 |
| Travas: Mimir não compila pesado, Fenrir sob demanda, zero pip no Fenrir | ✅ são as regras reais da Matilha |

---

## 2. ⚠️ O QUE A VISÃO ACERTOU MAS JÁ SUBIU DE VERSÃO

| Na visão | Real hoje |
|---|---|
| golden test **14/14** | **18/18** (subiu com o bignum/digit file: `bn_add`, `bn_mul`, `bn_sub`, `bn_edge`) |
| commit `594b366` | HEAD `c195a04` |
| "Etapa A: baixe o checkpoint `divagr1925/QwenPU` (26,7 GB)" | o checkpoint disponível e **validado** é o `Qwen3.5-4B-Qwenputer-CHIP8` (28 GB em `~/colab_checkpoints/qwen35-chip8/`). O QwenPU 26,7 GB **nunca foi baixado** |
| "Fase 4 do Lupin Neuro-OS" = guard + store | ⚠️ **conflito de numeração**: no canônico (F1/F2/F2.5/F3/F4) a **F4 = UOP-64/ARM64**. O guard in-weights é a **F3** (pendência dela) |

---

## 3. ❌ O QUE A VISÃO MOSTRA COMO PRONTO E NÃO ESTÁ

Afirmações de estado que a visão desenha na HUD **como se fossem fatos** — e não são:

| Na HUD (páginas 2 e 3) | Real (medido 2026-11-13) |
|---|---|
| `☄️ COLAB BEAST (A100) VRAM: 80.0 GB [ONLINE]` | **Sessão encerrada.** Efêmera. Os **598,82 CU estão guardados**, mas não há sessão rodando |
| `QWENPU 26.7G: CARREGADO` | **Não existe localmente.** `~/colab_checkpoints/` contém **só** `qwen35-chip8` (28 GB) |
| `GOLDEN: 14/14 PARIDADE` | **18/18** |
| `[SONDA :8091 -> OK]` | O daemon Rust **não está rodando** no Fenrir: sem processo, sem porta 8091, `target/` sem binário compilado |
| `🔄 SYNC: P30 + NEO9 OK` | **Hispo offline** (sem LAN/Tailscale/NetBird/USB) |
| `OTG Gigabit: 0.5 ms` | O cabo OTG do Fenrir está **desconectado** (nó em bancada) — não há `eth0`, só `wlan0` |
| `GGUF 4-bit Qwen3.5-4B (~2,3 GB)` e `8B (~4,5 GB)` | **Não existem.** 11 tentativas de quantização no Colab falharam (§1.32) |

> **Regra que decorre disto:** a HUD desenha **estado desejado**, não estado medido. Se ela
> mostrar "ONLINE" para o Colab e para a Sonda, o agente que a lê vai tomar decisões sobre
> recursos que não existem. **A HUD precisa ser alimentada por leitura real** (o daemon
> consultando o cluster), não por constante.

---

## 4. 🐛 ERROS TÉCNICOS NOS BLUEPRINTS

1. **Não compila:** `#defineVF_CAP_GOLD_KHZ` e `#define VF_CAP_PRIME_KHZ` — falta o espaço
   entre `#define` e o nome. O arquivo `lupin_power_enforcer.c` do blueprint morre no
   preprocessador.
2. **Tamanho do pacote errado:** o doc diz *"pacote binário de **312 bytes**"*. Medido com
   `gcc` sobre o struct exato do blueprint: **`sizeof(lupin_hud_packet_t) = 314`**.
   (4+1+1+2+2+1+1+4+4+4+2+256+32 = 314.) Se o daemon e o HUD usarem structs diferentes,
   o pacote se desalinha — e o HUD é **packed**, então não há padding que perdoe.
3. **Três medidas diferentes para a mesma VRAM:** o header fala em `chip8_vram[256]` = 64×32;
   a função desenha `vx < 56` a 2× (112×64); o comentário diz *"escalonada 1.9x (122x61)"*.
   Escolher **uma** e propagar.
4. **O blueprint do guard é um *downgrade*:** `lupin_guard_q35_integration.py` casa **strings**
   numa janela deslizante de 256 chars. O que já existe (`guard/lupin_guard_q35.py`) é um
   **circuito em tensores** que casa **token a token** com FSM de `slot_head` de chave
   endógena — e o motor C já tem guard próprio com **streaming** (o bug do truncamento em
   2048 bytes foi justamente corrigido assim). **Não trocar o circuito pelo blueprint.**
5. **Os 8 `FORBIDDEN_PATTERNS` divergem da tabela soberana.** A fonte da verdade é
   `guard/commands.json`; o motor C aplica 5 regras
   (`./gradlew`, `gradle`, fork bomb, `rm -rf /`, `reboot -f`). O blueprint acrescenta
   `dd if=/dev/zero`, `mkfs.`, `chmod -R 777 /`, `> /dev/sda`, `/dev/mem` — bom, mas tem que
   **entrar no `commands.json`**, não numa lista paralela.
6. **FNV-1a em 32 e 64 bits no mesmo documento.** `lora_manager.rs` usa 64-bit
   (`0xcbf29ce484222325`); o LPS1 usa 32-bit (`0x811C9DC5`). Isso é **intencional** (hash de
   arquivo vs. hash de payload), mas precisa estar escrito, senão vira bug de interoperabilidade.

---

## 5. ✅ O QUE OS BLUEPRINTS ENTREGAM DE NOVO (e vale incorporating)

- **`subdisplay_hud_protocol.h` — genuinamente novo.** O protocolo atual é um socket UNIX
  **Stream** (`/run/lupin/hud.sock`, sem header binário definido). Um pacote binário
  `packed` de tamanho fixo elimina o *parsing* de JSON e o retrabalho de protocolo.
  **Ao adotar, corrigir para 314 bytes** e padronizar a geometria da VRAM.
- **Palette "Totem Rubi"** (`0x00E0115F` / `0x007A082E` / bronze `0x00CD7F32` /
  ciano Peltier `0x0000F0FF` / verde Guardião `0x0000FF66`) — específica, legível e
  com contraste correto em OLED.
- **`activate_verified_lora` com FNV-1a** — *hot-swap* atômico que **rejeita pesos corrompidos
  antes de tocar na NPU**. É a ordem correta (verificar → acoplar).
- **Render procedural da Espiral Áurea** — determinístico, sem asset, com *pixel-shift*
  anti-burn-in declarado para o AMOLED.

---

## 6. 📌 O QUE A VISÃO **OMITE** (e o estado real mostra)

1. **O gap real do projeto:** o Guard in-weights está com o **circuito implementado** e o
   oráculo em **24/24**, mas a **entrega está incompleta** — `tools/audit_delivery.py` dá
   **REPROVADO 3/8**. Faltam `guard/test_guard_q35.py`, `tests/test_acceptance.py`,
   `colab/build_neuro_os.py` e `colab/verify_neuro_os.py` (a sessão Jules entregou mas
   **não deu push**; `colab/` e `tests/` estão vazios).
2. **O port AArch64 do motor já foi feito** (2026-11-13): `lupin-pc` instalado em
   `/usr/local/bin/lupin-pc` no Fenrir, **15/15 idêntico** ao build x86_64.
3. **O guard in-weights vive nos pesos, não no motor.** A visão trata "guard" e "motor" como
   a mesma coisa. São **dois lugares**: `src/neuro_pc/` (motor C, política do nó, já
   implantado) e `~/lupin-neuro-os/guard/` (circuito fundido nos pesos, pendente de merge).
4. **O Fenrir está em bancada**, não em produção: sem OTG, sem daemon `:8091`, sem serviço
   LLM `:8080`. Tudo que a visão desenha na HUD depende de serviços que **não estão de pé**.

---

## 7. 🎯 PLANO DE INCORPORAÇÃO (do que a visão tem de útil, em ordem)

| # | Item | Onde | Custo |
|---|---|---|---|
| 1 | **Adotar o pacote binário do HUD** (corrigido: 314 B, geometria única) | `src/display/subdisplay_hud_protocol.h` + `lupinos-display` | médio |
| 2 | **Paleta Totem Rubi** no renderizador existente | `src/display/lupin_subdisplay_hud.c` | baixo |
| 3 | **Fechar a entrega do guard** (os 4 arquivos) — **o gap que bloqueia a F3** | `~/lupin-neuro-os/` | médio, CPU-only (3 de 4) |
| 4 | **Convergir `FORBIDDEN_PATTERNS` → `guard/commands.json`** (fonte única) | `~/lupin-neuro-os/guard/commands.json` | baixo |
| 5 | **`lupin_power_enforcer.c`** com o `#define` corrigido | `src/cpu/` | baixo |
| 6 | **HUD alimentada por estado real** (consultar o cluster, não constantes) | `lupinos-daemon` | médio |
| 7 | **Colab Beast**: GGUF Q4_0 + merge do guard nos pesos | Colab A100 | alto (CU) |

> **A ordem importa:** o item 3 (fechar o guard) é o que destrava a F3 e, por consequência,
> os itens 6 e 7. O item 1 é o que dá forma visível ao produto, mas é o que mais rende
> agora sem depender de nuvem.

---

*Auditoria de 2026-11-13, por conta própria. Números de estado: §1.33–§1.39 do `estado_atual.md`.*
