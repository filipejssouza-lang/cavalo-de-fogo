# 🧠 Qualcomm Hexagon 780 cDSP / NPU Integration (SM8350 / Mi 11 Ultra)

Este documento descreve a integração arquitetural do subsistema de inteligência artificial da NPU **Qualcomm Hexagon 780** (26 TOPS de capacidade computacional tensorial HMX) no ambiente **Lupin Bare-Metal OS v3**.

---

## 1. Visão Geral do Silício (SM8350)
- **Aparelho:** Xiaomi Mi 11 Ultra (Codinome: `star`)
- **SoC:** Qualcomm Snapdragon 888 5G (SM8350 / Lahaina)
- **Acelerador NPU:** Hexagon 780 DSP com Hexagon Tensor Accelerator (HTP v68)
- **Topologia de IPC:** Qualcomm FastRPC sobre GLINK (`/dev/adsprpc-smd` e canais `/dev/msm_fastrpc-*`)

---

## 2. Bibliotecas de Runtime e Stubs no Sistema
Para executar inferência na NPU Hexagon em ambiente bare-metal / chroot, o nó utiliza as bibliotecas já mapeadas na partição do fornecedor e preservadas no sistema:

* `libcdsprpc.so`: Interface de chamada remota (FastRPC) entre o userspace Linux e o processador de sinal digital (cDSP).
* `libQnnHtp.so`: Backend do Qualcomm Neural Network (QNN) para o Hexagon Tensor Processor.
* `libsnpe_htp_v68_skel.so`: Skeleton executável compilado especificamente para o core Hexagon v68 (presente em `/data/alpine/dsp/cdsp/`).
* `cdsprpcd`: Daemon servidor de listener FastRPC (presente em `/vendor_real/bin/cdsprpcd`).

---

## 3. Rota de Compilação & Integração com llama.cpp

### A. Pré-requisitos de Toolchain:
Diferente da compilação padrão para CPU/Vulkan, o backend QNN do `llama.cpp` requer:
1. **Android NDK (r26c ou r27c)** com target `aarch64-linux-android`.
2. **Qualcomm AI Engine Direct SDK (QNN SDK)** instalado na máquina de compilação.
3. Variável `QNN_SDK_ROOT` apontando para o diretório descompactado do SDK.

### B. Comando de Compilação Cruzada:
```bash
cmake -B build_qnn \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-31 \
    -DGGML_QNN=ON \
    -DQNN_SDK_ROOT=$QNN_SDK_ROOT

cmake --build build_qnn --config Release -j$(nproc)
```

### C. Execução Otimizada no Fenrir:
Ao despachar modelos quantizados (`Q4_0`, `IQ4_NL`, `MXFP4`) para o executável `llama-cli` com backend QNN:
```bash
export LD_LIBRARY_PATH=/vendor_real/lib64:/data/alpine/dsp/cdsp:$LD_LIBRARY_PATH
export ADSP_LIBRARY_PATH="/data/alpine/dsp/cdsp;/vendor_real/lib/rfsa/adsp"

./bin/llama-cli -m modelo_qnn.gguf -p "Prompt do sistema" -n 64 --device HTP0
```
O consumo elétrico medido na NPU varia entre **2.0W e 3.5W**, operando com calor residual mínimo na caixa Peltier (31°C).
