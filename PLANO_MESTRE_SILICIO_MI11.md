# 🐺 PLANO MESTRE: EVOLUÇÃO EXTREMA DO SILÍCIO (MI 11 ULTRA / LUPINOS V3)

> **DIRETRIZ ZEUSDIN (MODO MONSTRO ATIVO):**
> Zero código simulado. Zero stubs vazios de nuvem. Engenharia direta de baixo nível extraindo 100% da capacidade física do Snapdragon 888 (SM8350 "star").

---

## 🏛️ PILAR 1: BOOT REAL E SOBERANO NO SLOT B (SEM TWRP)
- **Objetivo:** Inicializar o hardware frio diretamente no **Lupin Bare-Metal OS v3** gravado em `/dev/block/by-name/boot_b`.
- **Componentes Ativos:**
  1. Supervisor nativo em C (`/sbin/lupinos_guard`) compilado estático para AArch64.
  2. Gerenciamento imediato de energia: Tela AMOLED desligada (`brightness = 0`), USB autosuspend desativado (`-1`), wake_lock `lupinos_usb`.
  3. ZRAM provida em 12.0 GB (`12884901888` bytes) com prioridade 100 via direct sysfs/syscalls.
  4. Sentinela de montagem de `/data` e disparo do chroot Alpine Linux com SSH na porta 2222.
- **Plano de Rollback:** Slot A permanece 100% intocado no TWRP como ancoradouro seguro caso ocorra qualquer instabilidade.

---

## ⚡ PILAR 2: EXTRAÇÃO DOS DRIVERS REAIS DO HARDWARE QUALCOMM
- **Problema Detectado:** Pipelines genéricos em nuvem tentam usar stubs vazios (`touch dummy libQnnHtp.so`) porque não possuem os binários da Qualcomm.
- **Solução de Silício Local:**
  1. Os blobs binários de 64 bits da Qualcomm **já existem no próprio Mi 11**:
     - Biblioteca FastRPC: `/vendor_real/lib64/libcdsprpc.so` (258 KB real ELF da Qualcomm)
     - Biblioteca ADSP RPC: `/vendor_real/lib64/libadsprpc.so` (258 KB real ELF da Qualcomm)
     - Firmwares do cDSP: `/data/alpine/dsp/cdsp` (arquivos de microcódigo binário do Hexagon 780)
  2. **Device Nodes Ativos:**
     - `/dev/msm_fastrpc-2` (canal do cDSP Hexagon 780 v68)
     - `/dev/kgsl-3d0` (GPU Adreno 660, já operacional no Mesa Turnip 24.3.0)
  3. **Ação:** Montar as bibliotecas reais de `/vendor_real` dentro do ambiente de execução do Alpine/LupinOS (`LD_LIBRARY_PATH=/vendor_real/lib64`), subir o daemon `cdsprpcd` e linkar os executáveis contra o `libcdsprpc.so` real.

---

## 🔥 PILAR 3: COMPUTAÇÃO HETEROGÊNEA DE IA (CPU X1 + GPU ADRENO + HEXAGON NPU)
- **Divisão Científica de Trabalho no Silício:**
  1. **Prefill / Prompt Ingestion:**
     - **Motor:** GPU Adreno 660 via Mesa Turnip Vulkan 1.3 (`ggml-vulkan`).
     - **Capacidade:** 159 tokens/s em batch, utilizando os 8.3 GB de VRAM compartilhada e os 51.2 GB/s de largura de banda da memória LPDDR5.
  2. **Geração Token-a-Token (Single Token Latency):**
     - **Motor:** CPU Kryo 680 Prime Core 7 (Cortex-X1 @ 2.84 GHz).
     - **Instruções:** Vetorização ARM NEON 128-bit + instruções ARMv8.4-A Dot Product nativo (`sdot`/`udot`).
     - **Kernels Mojo Lowering:** RMSNorm e SwiGLU compilados para Cortex-X1 com C-ABI limpa (AAPCS64).
  3. **Gating & Especialistas MoE (Mixture of Experts):**
     - **Motor:** Hexagon 780 NPU (v68 HTP) via FastRPC.
     - **Capacidade:** 26 TOPS de INT8/INT16 operando a apenas 2-3 Watts de consumo térmico.
     - **Memória de Trabalho:** 12 GB RAM física + 12 GB ZRAM = 24 GB de espaço de trabalho para alocação de especialistas ativos.

---

## 🛠️ PILAR 4: PROTOCOLO DE EXECUÇÃO E REGRAS DE BANCADA
1. **Zero Sobrecarga no Mimir:** Toda compilação PESADA é feita na nuvem (GitHub Actions ou Google Colab efêmero consumindo <0.1 CU por build). O PC Mimir apenas orquestra via scripts leves.
2. **Registro Criptográfico Perpétuo:** Todo binário executado no Mi 11 tem seu SHA-256 aferido e gravado no log do cofre (`estado_atual.md`).
3. **Controle Térmico Rígido:** Operação com Peltier ativa mantendo bateria <35°C e CPU <42°C.
