<p align="center">
  <img src="docs/assets/lupin_os_logo.png" width="240" alt="Lupin OS v6 Logo" />
</p>

# 🐺 Lupin OS v6 — Soberania Autônoma
> **Sistema Operacional Bare-Metal para Silício Qualcomm Snapdragon 888 (SM8350)**  
> **Nó Principal do Cluster Lupin:** Xiaomi Mi 11 Ultra (*star*) — 12 GB LPDDR5 | 256 GB UFS 3.1 | Cooler Peltier Ativo

---

## 🗺️ O QUE É O QUÊ (os nomes são o MESMO PROJETO)

Este repositório é **o Fenrir**. Os nomes designam **camadas**, não projetos diferentes:

| Nome | Camada | Onde |
|---|---|---|
| **Fenrir** | o **nó** (Mi 11 Ultra, SD888, `192.168.1.9`, ssh `2222`) | a bancada |
| **Lupin OS v6** | o **SO bare-metal** que roda nele | este repo → `lupin_baremetal_star_v6_sovereign.img` |
| **Cavalo de Fogo** | o **repo/projeto** do SO | este repo |
| **Neuro-PC** (`lupin-pc`) | o **motor**: emulador RV32IM+Zba que executa os ELFs do guest | `src/neuro_pc/` → `build/lupin-pc` |
| **Neuro-OS** | o **computador em pesos** em cima do motor: Qwenputer/QwenPU + **Lupin-Guard** fundido nos pesos | `~/lupin-neuro-os/` |

**O pipeline completo:**

```
Qwen3.5-4B-CHIP8 (28 GB, guard fundido na Camada 1)
  └─ NPU Hexagon 780 via FastRPC (/dev/msm_fastrpc-2)   [crates/lupinos-fastrpc]
      └─ fallback llama.cpp :8080
          └─ o modelo emite <tool_call> com programa RISC-V (uops do digit file)
              └─ Lupin-PC (motor C AArch64) executa o programa RV32IM+Zba
                  └─ Guard L1 (Camada 1) decide ALLOW/BLOCK (política do nó)
                      └─ stdout volta ao modelo
```

> Estado real e medido: `~/.agents/memory/etapas_neuro_os.md`.
> Visão de produto + auditoria contra o estado real: [`docs/visao/`](docs/visao/).

---

## ⚡ O que é o Lupin OS v6?

O **Lupin OS** é um sistema operacional bare-metal de alta performance projetado para transformar hardware mobile topo de linha em nós soberanos de inteligência artificial, computação heterogênea e telecomunicações edge, **eliminando 100% o overhead do Android**.

Na versão **v6 ("Soberania Autônoma")**, o sistema atinge independência completa de ferramentas legadas:
- ❌ **Zero Android:** Sem Zygote, sem SurfaceFlinger, sem JVM e sem Telemetria OEM.
- ❌ **Zero TWRP (Expurgo Definitivo):** O antigo hack de inicialização via TWRP foi **completamente eliminado**. Todas as 14 bibliotecas e binários do TWRP foram arrancados do ramdisk.
- ✅ **Boot Bare-Metal Nativo:** O Lupin OS inicializa diretamente da partição `boot_b` via Linux Initramfs puro, com assinatura **AVB 2.0 (192 MB exatos)** e **Rollback Automático A/B**.
- ✅ **Tri-Silicon Heterogêneo:** Alocação de tarefas distribuída entre Cortex-X1, NPU Hexagon 780 e GPU Adreno 660.

---

## 🏛️ Arquitetura de Silício Heterogêneo

```
                      ┌────────────────────────────────────────┐
                      │        Lupin OS v6 Bare-Metal          │
                      └───────────────────┬────────────────────┘
                                          │
            ┌─────────────────────────────┼─────────────────────────────┐
            ▼                             ▼                             ▼
   ┌──────────────────┐          ┌──────────────────┐          ┌──────────────────┐
   │  Prime Core 7    │          │  Hexagon 780     │          │   Adreno 660     │
   │  Cortex-X1       │          │  cDSP / NPU      │          │   GPU Vulkan     │
   │  @ 2.84 GHz      │          │  (v68 HTP)       │          │  (Mesa Turnip)   │
   ├──────────────────┤          ├──────────────────┤          ├──────────────────┤
   │ • NEON SIMD F32  │          │ • FastRPC Linux  │          │ • Vulkan 1.3 UMA │
   │ • lupin-power    │          │ • DMA-BUF Zero-  │          │ • Zero-Copy KV   │
   │   idle/bal/burst │          │   Copy I/O       │          │ • SPIR-V GEMM    │
   │ • Closed-Loop    │          │ • HTTP API :8091 │          │ • /dev/kgsl-3d0  │
   │   Thermal Guard  │          │ • Fallback :8080 │          │                  │
   └──────────────────┘          └──────────────────┘          └──────────────────┘
            ▲                             ▲                             ▲
            └─────────────────────────────┼─────────────────────────────┘
                                          │
                      ┌───────────────────┴────────────────────┐
                      │    Display Traseiro AMOLED 1.1"        │
                      │    (126x294 DRM KMS - 4 Páginas)       │
                      └────────────────────────────────────────┘
```

### 1. Prime Core Cortex-X1 @ 2.84 GHz (`lupin-power`)
* Controle fino de frequência e governança de energia (`lupin-power idle`, `balanced`, `burst`).
* **Idle:** Núcleos LITTLE (A55) a 300 MHz, A78/X1 em powersave (consumo $< 0.8\text{W}$).
* **Burst:** Disparo instantâneo do Cortex-X1 no clock máximo (2.84 GHz) com afinidade de processo via `sched_setaffinity`.
* **Governador Térmico Closed-Loop:** Monitoramento contínuo em malha fechada. Limiares de segurança (bateria $< 35^\circ\text{C}$, CPU $< 65^\circ\text{C}$), controle de histerese e acionamento ativo da caixa Peltier.

### 2. NPU Qualcomm Hexagon 780 cDSP (`crates/lupinos-fastrpc`)
* Comunicação direta via ioctl FastRPC Linux (`/dev/msm_fastrpc-2`, cDSP Domain 3).
* Memória compartilhada zero-copy via DMA-BUF (`/dev/dma_heap/system` e `/dev/ion`).
* Servidor HTTP nativo na porta `:8091` com endpoints compatíveis com OpenAI:
  * `GET /health` — Status da NPU e modo de operação (real/simulado).
  * `GET /metrics` — Latência, tokens/s e uso de memória.
  * `POST /v1/chat/completions` — Inferência local na NPU com fallback automático para `llama-server` na porta `:8080`.

### 3. GPU Adreno 660 Vulkan 1.3 UMA
* Driver open-source Mesa Turnip mapeado diretamente em `/dev/kgsl-3d0`.
* Alocador de Memória Unificada (`uma_allocator.cpp`) com buffers `HOST_VISIBLE` e `DEVICE_LOCAL`, permitindo que tensores e KV-cache sejam compartilhados sem tráfego de barramento PCI/RAM.

---

## 📺 HUD AMOLED Traseiro 1.1" (126x294)

O display secundário traseiro opera através de DRM KMS atômico puro (`/dev/dri/card0`, Connector 64, CRTC 208) sem depender de bibliotecas gráficas pesadas:

* **4 Páginas Rotativas com Indicador `[X/4]`:**
  * **Página 1 `[1/4]` — Sistema:** Temperaturas (CPU/Bateria), RAM livre, ZRAM 12GB e Load Average.
  * **Página 2 `[2/4]` — IA & NPU:** Estado do Hexagon 780, inferências/s, modo de execução e LoRA ativo.
  * **Página 3 `[3/4]` — Rede:** IP local, IP Tailscale, taxa de transferência e status de failover anti-flap.
  * **Página 4 `[4/4]` — Status:** Governador de energia ativo, tempo de atividade (uptime) e limites térmicos.
* **Backlight Adaptativo Auto-Dim:**
  * $100\%$ de brilho durante atividade ou comandos.
  * Auto-dim para $30\%$ após 60 segundos de inatividade.
  * Auto-off ($0\%$) após 300 segundos de inatividade.
* **Controle por Socket UNIX (`/run/lupin/hud.sock`):**
  * `lupinctl hud next` / `prev` — Troca manual de página.
  * `lupinctl hud dim` / `on` — Ajuste de iluminação.
  * `lupinctl hud status` — Leitura do estado atual do display.

---

## 🧬 Gerenciador de Adaptadores LoRA em Rust

Implementado nativamente em `crates/lupinos-daemon/src/lora_manager.rs`:
* Carregamento a quente e troca de adaptadores LoRA sem reiniciar o modelo base.
* Conversão automática on-the-fly de adaptadores PEFT (`.bin` / `.safetensors`) para formato GGUF compatível com `llama.cpp`.
* Endpoints HTTP no daemon:
  * `POST /lora/load` — `{ "name": "...", "path": "..." }`
  * `GET /lora/list` — Retorna adaptadores carregados e ativo.
  * `POST /lora/unload` — `{ "name": "..." }`
* CLI dedicado: `lupinctl lora load|list|unload`.

---

## 🛡️ Boot A/B Endurecido com Rollback Automático

Para garantir que o dispositivo nunca sofra *brick* ou fique inacessível:
* Utilitário nativo de baixo nível `tools/lupin_slot_ctl.c`.
* Contador atômico persistente de falhas de boot.
* Se o boot falhar por 3 vezes consecutivas antes de alcançar o marco de userspace, o sistema executa **rollback automático para o slot alternativo bootável**.
* Verificação rigorosa de integridade SHA-256 da imagem antes de autorizar a gravação.
* Chamada automática de `lupin_slot_ctl mark-success` pelo `init.recovery.service.rc` assim que os daemons essenciais sobem com sucesso.

---

## 🛠️ Plano de Controle do Lupin OS (`lupinctl`)

O `lupinctl` é a interface unificada de comando do operador e dos agentes:

```bash
# Telemetria geral e status de silício (formato legível ou JSON)
lupinctl status
lupinctl status --json

# Controle de energia e governors
lupinctl power idle
lupinctl power balanced
lupinctl power burst "meu_comando_pesado"

# Controle térmico e leitura de sensores
lupinctl thermal status

# Controle do display traseiro
lupinctl hud next
lupinctl hud on
lupinctl hud dim

# Gerenciamento de adaptadores LoRA
lupinctl lora list
lupinctl lora load coder_v1 /data/models/lora_coder.gguf
lupinctl lora unload coder_v1
```

O daemon também expõe métricas de telemetria compatíveis com Prometheus na porta `:9100/metrics`.

---

## 🏗️ Compilação e Deploy (CI/CD na Nuvem)

Por diretriz estrita da Orquestra Lupin, **nenhuma compilação pesada é realizada na máquina de desenvolvimento local**. Todo o processo de build é executado via GitHub Actions:

```
[Git Push Tag v*] ──► [GitHub Actions Ubuntu AArch64]
                              │
                              ├─► Compilação cruzada dos binários C (make)
                              ├─► Testes de unidade em Rust (cargo test)
                              ├─► Testes de regressão host-runnable (test_sovereign_control.sh)
                              ├─► Montagem da imagem de boot (build_os_v6.py)
                              ├─► Assinatura AVB 2.0 (192 MB exatos)
                              │
                              ▼
                  [Artifacts / GitHub Release v6]
```

### Disparo Manual via GitHub CLI:
```bash
gh workflow run build-lupinos-v6.yml
```

### Montagem Local da Imagem (Opcional):
```bash
python3 tools/build_os_v6.py \
    --boot base_boot.img \
    --guardian bin/lupinos_guard \
    --hud bin/lupin_subdisplay_hud \
    --power bin/lupin-power \
    --lupinctl bin/lupinctl \
    --slot-ctl bin/lupin_slot_ctl \
    --neuro-pc build/lupin-pc \
    --out lupin_baremetal_star_v6_sovereign.img
```
> `--neuro-pc` injeta o motor do Neuro-PC em `/opt/lupinos/lupin-pc` — caminho na lista
> de busca do daemon Rust, para a imagem subir **com** o motor Neuro-PC.

### Deploy do Motor no Fenrir (caminho oficial):
```bash
bash ~/lupin-neuro-os/tools/deploy_fenrir.sh --test
```
Compila o motor C **nativamente** no Alpine do Fenrir (o cross-compiler fica no Mimir;
o Fenrir executa), faz backup do binário anterior, instala em `/usr/local/bin/lupin-pc`
e valida no silício real. **Resultado medido (2026-11-13):** bateria de 15 ELFs RV32IM
com resultado **byte a byte idêntico** ao build x86_64 do Mimir.

---

## 🧠 Motor do Neuro-PC (`lupin-pc`) — o computador em pesos, executando RV32IM

O motor C em `src/neuro_pc/` é o **emulador RV32IM+Zba** que executa os programas RISC-V
emitidos pelo modelo de pesos. Ele é o que fecha o ciclo: o modelo não "fala" sobre
computação — ele **chama** o computador e usa o resultado.

* **ISA do guest:** RV32IM + Zba, com as **uops custom do digit file** (BMAC/BMSB/BLD/BGET/
  BSCAN/BPRD) que dão ao computador em pesos aritmética de precisão arbitrária.
* **Guard L1 (Camada 1):** filtro determinístico de política do nó, com **streaming** (janela
  deslizante, sem buffer fixo) — o bug do truncamento em 2048 bytes foi corrigido e há teste
  de regressão para ele (`test_guard_bypass_por_truncamento`).
* **Lupin-Store (LPS1):** KV in-memory com snapshot binário assinado em FNV-1a 32-bit.
* **Paridade bit-a-bit:** `tests/golden_test.py` compara **QEMU ≡ `ref.py` do Qwenputer ≡
  motor C** na mesma imagem ELF (stdout + exit code + número de instruções).
  **Estado medido: 18/18** — inclui os 4 casos de bignum (`bn_add`, `bn_mul`, `bn_sub`,
  `bn_edge`).
* **Testes do motor C:** `./build/test_neuro_pc` → **28/28** (8 de engine, 7 de guard,
  13 de store).
* **Rotas do daemon:** `POST /pc/run`, `GET /pc/status`, `POST /pc/reset`,
  `POST /pc/store/{put,get,delete,list,count}` em `:8091`, e `lupinctl pc <status|run|elf>`.

---

## 📜 Histórico de Versões

* **v1 — v3 (Fase Chroot / Legada):** Primeiro protótipo de Linux direto no metal ancorado no ramdisk do TWRP.
* **v4 — v5 (Fase de Transição):** Expurgo físico do TWRP, desenvolvimento dos drivers DRM KMS para a tela traseira e primeiros governadores de CPU.
* **v6 (Soberania Autônoma — Versão Atual):**
  * Eliminação de 100% dos resquícios legados (14 alvos TWRP removidos).
  * Boot A/B com proteção ativa e rollback automático.
  * Integração completa da NPU Hexagon 780 via FastRPC com API HTTP compatível com OpenAI.
  * Gerenciador dinâmico de LoRA em Rust.
  * HUD traseiro com 4 páginas rotativas, dirty-rects e auto-dim.
  * Governador térmico em malha fechada e integração com Peltier.

---
*Desenvolvido pela Matilha Lupin & Orquestra Autônoma de Silício.*
