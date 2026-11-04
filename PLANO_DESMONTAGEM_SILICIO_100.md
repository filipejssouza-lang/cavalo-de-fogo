# 🔬 PLANO OPERACIONAL DE DESMONTAGEM E ENGENHARIA REVERSA DE SILÍCIO (100% DA PLACA)
## Alvo: Xiaomi Mi 11 Ultra (Snapdragon 888 "star" SM8350)
### Matilha Lupin — Modo Monstro de Engenharia Bare-Metal

---

## 🏛️ OBJETIVO E DIRETRIZ
Desmontar, mapear e submeter a engenharia reversa profunda **100% dos subsistemas físicos da placa-mãe do Mi 11 Ultra**, eliminando caixas-pretas e conquistando soberania total sobre os registradores, barramentos, displays e aceleradores de silício.

A infraestrutura pesada de análise estática e descompilação AST será executada no **Google Colab Cloud Forge** (Ghidra 11 Headless + REA Engine), devolvendo as estruturas limpas em C diretamente para a bancada.

---

## 🗺️ AS 5 ZONAS FUNDAMENTAIS DE ATAQUE

```
  ┌────────────────────────────────────────────────────────────────────────┐
  │                   XIAOMI MI 11 ULTRA (SM8350 "STAR")                   │
  ├────────────────────────────────────────────────────────────────────────┤
  │ [ZONA 1] Hexagon 780 cDSP / NPU      │ [ZONA 2] Sub-Display Traseiro   │
  │  - /dev/msm_fastrpc-2                │  - card0-DSI-2 (126x294 AMOLED) │
  │  - libcdsprpc.so (FastRPC ioctls)    │  - panel1-backlight (0-255)     │
  │  - Firmwares cDSP (v68 HTP - 26 TOPS)│  - Sequência DCS de Init        │
  ├──────────────────────────────────────┼─────────────────────────────────┤
  │ [ZONA 3] Adreno 660 GPU (Vulkan)     │ [ZONA 4] SPMI PMIC & Térmicas   │
  │  - /dev/kgsl-3d0 (Ringbuffer UMA)    │  - 28 Thermal Zones do SoC      │
  │  - Mesa Turnip 24.3.0 + Freedreno    │  - PM8350/C SPMI Bus Control    │
  ├──────────────────────────────────────┴─────────────────────────────────┤
  │ [ZONA 5] Memória LPDDR5 (51.2 GB/s) & UFS 3.1 Direct Flash             │
  │  - 12 GB RAM Física + 12 GB ZRAM (24 GB Pool)                          │
  │  - GPT UFS 3.1 LUN 0-6 (73 partições de hardware mapeadas)             │
  └────────────────────────────────────────────────────────────────────────┘
```

---

### 📍 ZONA 1: HEXAGON 780 cDSP / NPU (26 TOPS @ 2-3W)
- **Alvo Físico:** Coprocessador de inteligência artificial Hexagon 780 (v68 HTP).
- **Vetores de Extração no Dispositivo:**
  - Biblioteca binária: `/vendor_real/lib64/libcdsprpc.so` (258 KB)
  - Microcódigos de DSP: `/data/alpine/dsp/cdsp/*.so` e firmware em `/vendor/firmware_mnt`
  - Device Node: `/dev/msm_fastrpc-2` e `/dev/adsprpc-smd`
- **Operação no Ghidra Headless (Colab):**
  1. Descompilação completa das funções `fastrpc_invoke()`, `remote_handle_open()`, `rpcmem_alloc()` e `ioctl` dispatchers.
  2. Extração de structs de IPC: `fastrpc_ioctl_invoke_crc`, `fastrpc_ioctl_init`, `fastrpc_ioctl_mmap`.
- **Entrega na Bancada:**
  - Header C 100% puro e aberto: `fastrpc_uapi.h` (elimina a dependência dos blobs da Qualcomm).
  - Driver zero-copy direto da memória LPDDR5 para o cDSP.

---

### 📍 ZONA 2: SUB-DISPLAY TRASEIRO (ILHA DE CÂMERAS AMOLED 126x294)
- **Alvo Físico:** Painel OLED de 1.1" integrado na traseira, agora visível pelo corte da caixa Peltier.
- **Vetores de Extração no Dispositivo:**
  - Nó DRM: `/sys/class/drm/card0-DSI-2` (Modo nativo: `126x294x60x3697cmd`)
  - Backlight: `/sys/class/backlight/panel1-backlight` (Range: 0 a 255)
  - Device Tree: `qcom,mdss_dsi_j18_42_06_0a_sec_dsc_cmd`
- **Operação no Ghidra Headless (Colab):**
  1. Descompilação dos drivers do painel secundário no kernel (`dsi_panel.c`, `dsi_display.c`) para extrair a sequência DCS de power-on e refresh.
- **Entrega na Bancada:**
  - Daemon nativo em C de ultrabaixo consumo (`lupin_rear_hud`):
    * Desenha direto no DRM Dumb Buffer (`/dev/dri/card0` connector DSI-2).
    * Telemetria visual ao vivo: CPU Temp (Peltier), RAM livre, IP do cluster, carga da CPU e status do MoE.
    * Tela frontal mantida desligada (`panel0-backlight = 0`).

---

### 📍 ZONA 3: GPU ADRENO 660 (PIPELINE VULKAN 1.3 & UMA)
- **Alvo Físico:** Acelerador gráfico Adreno 660 a 840 MHz com suporte a Ray Tracing e FP16/INT8.
- **Vetores de Extração no Dispositivo:**
  - Device Node: `/dev/kgsl-3d0`
  - Drivers: Mesa Turnip 24.3.0 e `libvulkan_freedreno.so`
- **Operação de Bancada:**
  1. Mapeamento das chamadas ioctl do KGSL (`IOCTL_KGSL_GPUOBJ_ALLOC`, `IOCTL_KGSL_RINGBUFFER_ISSUE`).
  2. Alocador unificado de memória (UMA) compartilhado: CPU escreve tensores $\rightarrow$ GPU computa $\rightarrow$ zero cópia de memória.

---

### 📍 ZONA 4: SPMI PMIC, TÉRMICAS E CONTROLE DO PELTIER
- **Alvo Físico:** Células térmicas internas e circuitos de alimentação Qualcomm PM8350 + PM8350C.
- **Vetores de Extração no Dispositivo:**
  - 28 zonas térmicas: `/sys/class/thermal/thermal_zone[0-27]/temp`
  - Controlador de bateria e voltagem: `/sys/class/power_supply/battery/`
- **Operação de Bancada:**
  1. Script de telemetria de alta frequência (100 ms) monitorando os pontos quentes do silício (CPU Prime X1, GPU e modem).
  2. Ajuste dinâmico do cooler Peltier mantendo o núcleo estritamente abaixo de 40°C sob carga pesada.

---

### 📍 ZONA 5: ARMAZENAMENTO UFS 3.1 & HIERARQUIA DE MEMÓRIA LPDDR5
- **Alvo Físico:** 12 GB de memória RAM LPDDR5 operando a 6400 Mbps (51.2 GB/s) + 256 GB UFS 3.1.
- **Vetores de Extração no Dispositivo:**
  - Mapeamento das 73 partições da tabela GPT em `/dev/block/by-name/`
  - ZRAM de 12 GB montada com algoritmo `lz4`/`zstd` com prioridade 100
- **Entrega na Bancada:**
  - Espaço de trabalho de **24 GB utilizáveis** (12 GB físicos + 12 GB ZRAM comprimida).
  - Montagem de F2FS com flags de latência ultrabaixa (`fsync_mode=nobarrier`, `inlinecrypt`).

---

## ⚡ FLUXO DE EXECUÇÃO EM 3 PASSOS IMEDIATOS

1. **Passo 1 (Coleta dos Blobs):** Extrair `libcdsprpc.so`, `libadsprpc.so` e os trechos de Device Tree do Mi 11 e empacotar no Mimir.
2. **Passo 2 (Forja no Colab com Ghidra 11):** Disparar o `colab-gpu-runner` para executar a análise estática headless e descompilar os ioctls do FastRPC e do Display DSI-2.
3. **Passo 3 (Injeção na Bancada):**
   * Subir o daemon da tela traseira (`lupin_rear_hud`) no Mi 11 com o mini display aceso exibindo a telemetria ao vivo.
   * Compilar o driver nativo da NPU sem as dependências fechadas da Qualcomm.
