# 🐺 Lupin OS v6 (`cavalo-de-fogo`): O Exocórtex Soberano de Um Homem Só

Quando uma *Big Tech* constrói um sistema operacional ou um cluster de IA, $70\%$ do silício é desperdiçado com telemetria invasiva, isolamento multi-inquilino (*multi-tenancy*), burocracia de DRM e modelos engessados para agradar milhões de usuários genéricos.

Quando você constrói uma arquitetura desse calibre **para uso estritamente pessoal**, as amarras desaparecem. O que você tem na bancada — unindo o **Fenrir** (Snapdragon 888 *bare-metal*), o **PC Mimir**, o **Lupino** (voz e memória $24/7$), o **Hispo** (vanguarda móvel) e o canhão orbital **Colab Beast A100 ($80\text{ GB}$)** — não é apenas um *homelab*: é um **Cyber-Deck e Exocórtex Soberano**.

Abaixo estão os **5 Módulos do Exocórtex** desenhados sob medida sobre os arquivos reais do seu repositório `cavalo-de-fogo` (commit `594b366`), seguidos das implementações de código e da diretriz de execução para o seu agente local.

---

## 1. O "Totem Vivo" na Tela Traseira ($126 \times 294$ AMOLED)
Você já tem no repositório o `src/display/lupin_subdisplay_hud.c` e a crate `crates/lupinos-display` falando direto com o `/dev/dri/card0` em DRM KMS puro. Em vez de exibir apenas texto estático, essa tela de $1.1"$ nas costas do **Fenrir** torna-se a **Janela da Alma do QwenPuter**:

* **Página 0 — Totem Rubi & ECG do Silício:** Exibe a espiral áurea rubi pulsando conforme a carga da NPU Hexagon 780 ($26\text{ TOPS}$), o equilíbrio térmico do Peltier cravado em $32^\circ\text{C}\text{--}33^\circ\text{C}$ e a trava V/f (`cpu4-6` a $1.72\text{ GHz}$, `cpu7` a $1.84\text{ GHz}$).
* **Página 1 — Visualizador de Pesos em Tempo Real (*In-Weights Framebuffer*):** Quando o motor `src/neuro_pc/lupin_neuro_pc.c` (RV32IM) ou a ponte `Qwen3.5-CHIP8` / `QwenPU` estiverem executando programas matemáticos, o subdisplay renderiza o *framebuffer* $64 \times 32$ escalonado para $126 \times 64$ no topo e os registradores de estado (`PC`, `SP`, checksum `FNV-1a` e flag do `lupin-guard` L1) na base. Você literalmente **vê o cérebro da IA pensando em binário** na traseira do Mi 11 Ultra.
* **Página 2 — Radar da Matilha & Canhão Orbital:** Status visual dos $4$ nós locais (latência do link OTG de $0.5\text{ ms}$, saúde do cofre no Lupino) e um contador regressivo do saldo de Compute Units ($598.82\text{ CU}$ / $\sim 67\text{ h}$) quando o **Colab Beast A100** estiver acoplado na malha.

### 💻 Blueprint de Código: Protocolo Binário do Socket `/run/lupin/hud.sock`
Para alimentar o `lupin_subdisplay_hud.c` sem gastar CPU com *parsing* de JSON, o `lupinos-daemon` envia um pacote binário fixo de $312\text{ bytes}$ via UNIX Datagram Socket:

```c
// src/display/subdisplay_hud_protocol.h
#ifndef SUBDISPLAY_HUD_PROTOCOL_H
#define SUBDISPLAY_HUD_PROTOCOL_H

#include <stdint.h>

#define HUD_SOCKET_PATH "/run/lupin/hud.sock"
#define HUD_MAGIC 0x4C504836 // "LPH6" (Lupin HUD v6)

typedef enum {
    HUD_PAGE_SILICON_ECG = 0,
    HUD_PAGE_NEURO_PC_VM = 1,
    HUD_PAGE_CLUSTER_NET = 2,
    HUD_PAGE_ALERT_GUARD = 3
} hud_page_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;              // 0x4C504836
    uint8_t  active_page;        // 0..3 (ou 0xFF para auto-rotate)
    uint8_t  brightness;         // 0..255 (auto-dim para 15 após 30s ocioso)
    int16_t  soc_temp_milli;     // Ex: 32500 = 32.5 °C (Peltier)
    uint16_t ram_used_mb;        // Ex: 410 MB (idle) ou 4850 MB (Qwen-8B)
    uint8_t  npu_load_pct;       // 0..100 %
    uint8_t  guard_status;       // 0 = 7/7 GREEN, 1 = L1 BLOCKED, 2 = ROLLBACK
    uint32_t rv32_pc;            // Program Counter do lupin-pc / QwenPU
    uint32_t lps1_fnv1a;         // Checksum FNV-1a do último snapshot no Store
    float    colab_cu_remaining; // Ex: 598.82f
    uint16_t otg_latency_us;     // Ex: 500 = 0.5 ms (Mimir <-> Fenrir)
    uint8_t  chip8_vram[256];    // Framebuffer 64x32 (1 bit por pixel = 256 bytes)
    char     status_msg[32];     // Ex: "QWENPU A100 SYNC OK"
} lupin_hud_packet_t;

#endif
```

---

## 2. "Sonho Noturno" (Destilação Epigenética no Colab Beast + `lora_manager.rs`)
No seu repositório já existe `crates/lupinos-daemon/src/lora_manager.rs`. Nenhuma *Big Tech* treina um adaptador neural exclusivo para os hábitos de uma única pessoa toda semana — mas a Matilha pode:

* **Coleta Silenciosa ($24/7$ no Lupino):** Durante a semana, o cofre central (`~/.agents/memory/`) no **Lupino (P30 Pro)** acumula as soluções que funcionaram no cluster, correções de bugs do `cavalo-de-fogo` e decisões de design do *Suika 3D* e *Legado de Mani*.
* **Ciclo REM de $15\text{ Minutos}$ na A100:** Quando acionado, o **Colab Beast A100 ($80\text{ GB}$)** converte as memórias da semana em pares de instrução de alta densidade, treina um adaptador LoRA `rank=16` sobre o `Qwen3.5-4B` / `8B`, exporta para GGUF LoRA, assina com `FNV-1a` e injeta no **Fenrir**.

### 💻 Blueprint de Código: `crates/lupinos-daemon/src/lora_manager.rs`
Refatoração do gerenciador de LoRA em Rust para suportar *hot-swap* atômico com verificação de integridade `FNV-1a` antes de acoplar ao motor de inferência sob demanda:

```rust
// crates/lupinos-daemon/src/lora_manager.rs
use std::fs::{self, File};
use std::io::{Read, Result, Error, ErrorKind};
use std::path::{Path, PathBuf};

const FNV_OFFSET_BASIS: u64 = 0xcbf29ce484222325;
const FNV_PRIME: u64 = 0x100000001b3;

#[derive(Debug, Clone)]
pub struct EpigeneticLora {
    pub name: String,
    pub path: PathBuf,
    pub fnv1a_hash: u64,
    pub scale: f32,
}

pub struct LoraManager {
    vault_dir: PathBuf,
    active_adapter: Option<EpigeneticLora>,
}

impl LoraManager {
    pub fn new<P: AsRef<Path>>(vault_dir: P) -> Self {
        Self {
            vault_dir: vault_dir.as_ref().to_path_buf(),
            active_adapter: None,
        }
    }

    /// Calcula o checksum FNV-1a (64-bit) compatível com o lupin-store (LPS1)
    pub fn compute_fnv1a(path: &Path) -> Result<u64> {
        let mut file = File::open(path)?;
        let mut buf = [0u8; 65536];
        let mut hash = FNV_OFFSET_BASIS;
        loop {
            let n = file.read(&mut buf)?;
            if n == 0 {
                break;
            }
            for &byte in &buf[..n] {
                hash ^= byte as u64;
                hash = hash.wrapping_mul(FNV_PRIME);
            }
        }
        Ok(hash)
    }

    /// Valida e ativa um adaptador LoRA gerado pelo "Sonho Noturno" no Colab Beast
    pub fn activate_verified_lora(
        &mut self,
        adapter_name: &str,
        expected_fnv1a: u64,
        scale: f32,
    ) -> Result<&EpigeneticLora> {
        let candidate_path = self.vault_dir.join(format!("{}.gguf", adapter_name));
        if !candidate_path.exists() {
            return Err(Error::new(ErrorKind::NotFound, "Arquivo LoRA nao encontrado no Fenrir"));
        }

        let actual_hash = Self::compute_fnv1a(&candidate_path)?;
        if actual_hash != expected_fnv1a {
            // Rejeita pesos corrompidos antes de tocar na NPU/GPU
            let _ = fs::remove_file(&candidate_path);
            return Err(Error::new(
                ErrorKind::InvalidData,
                format!("Falha de integridade FNV-1a no LoRA: esperado {:#x}, obtido {:#x}", expected_fnv1a, actual_hash),
            ));
        }

        self.active_adapter = Some(EpigeneticLora {
            name: adapter_name.to_string(),
            path: candidate_path,
            fnv1a_hash: actual_hash,
            scale,
        });

        Ok(self.active_adapter.as_ref().unwrap())
    }

    /// Gera os argumentos de linha de comando para o llama-server sob demanda
    pub fn build_cli_flags(&self) -> Vec<String> {
        match &self.active_adapter {
            Some(lora) => vec![
                "--lora-scaled".to_string(),
                lora.path.to_string_lossy().to_string(),
                format!("{:.2}", lora.scale),
            ],
            None => Vec::new(),
        }
    }
}
```

---

## 3. Sentinela Fantasma: Laser dToF $8 \times 8$ + IR SPI + Áudio Procedural `CS35L41`
O **Fenrir** mantém $100\%$ da RAM de $12\text{ GB}$ livre para cargas sob demanda, mas os seus sensores de baixo nível operam no barramento I2C/SPI gastando menos de $2\text{ MB}$ de memória residente:

* **Matriz de Presença dToF $8 \times 8$:** O sensor laser traseiro lê a distância em milímetros de $64$ zonas na sua bancada. Não usa câmera nem invade privacidade.
* **Gesto de Despertar (*Wake-on-Presence*):**
  1. Se a média das zonas centrais do dToF cair abaixo de $400\text{ mm}$ por $1.5\text{ s}$ (gesto de mão ou aproximação), o Fenrir toca um acorde harmônico sintetizado no `src/audio/lupin_music.c` (usando os amplificadores Cirrus Logic `CS35L41` já patcheados com `machine_dlkm.ko`);
  2. Dispara um pulso no emissor **IR SPI** se configurado para ligar o ambiente;
  3. Pré-aquece o barramento FastRPC da NPU Hexagon 780 e avisa o **PC Mimir** via OTG ($0.5\text{ ms}$) que o Lobo Titã está pronto para combate.

---

## 4. "Viagem no Tempo Cognitiva" (`QwenPU` + `lupin-store` + `lupin-guard`)
Na Fase 4 do **Lupin Neuro-OS**, o "computador em pesos" **QwenPU ($26.7\text{ GB}$)** rodando no **Colab Beast A100** ganha imunidade contra alucinações graças ao acoplamento com o `lupin-guard` ($7/7$) e o `lupin-store` ($13/13$):

### 💻 Blueprint de Código: `lupin_guard_q35_integration.py` (Etapa B + Etapa C)
Este módulo intercepta os *trace tokens* da QwenPU em tempo real, passa pelo filtro determinístico do Guard e grava snapshots `LPS1` com checksum `FNV-1a` para *rollback* instantâneo:

```python
#!/usr/bin/env python3
"""
Lupin Neuro-OS — Fase 4 (Etapas B & C)
Acoplamento QwenPU (26.7 GB no Colab A100) + Lupin-Guard L1 + Lupin-Store (LPS1)
"""
import struct
import hashlib
from typing import List, Dict, Optional

LPS1_MAGIC = b"LPS1"
FNV_OFFSET_BASIS_32 = 0x811C9DC5
FNV_PRIME_32 = 0x01000193

FORBIDDEN_PATTERNS = (
    "rm -rf /",
    ":(){ :|:& };:",
    "reboot -f",
    "dd if=/dev/zero",
    "mkfs.",
    "chmod -R 777 /",
    "> /dev/sda",
    "/dev/mem",
)

def fnv1a_32(data: bytes) -> int:
    h = FNV_OFFSET_BASIS_32
    for b in data:
        h ^= b
        h = (h * FNV_PRIME_32) & 0xFFFFFFFF
    return h

class LupinGuardInWeights:
    """Circuito L1 de proteção acoplado ao fluxo de tokens da QwenPU."""
    def __init__(self) -> None:
        self.rolling_window: str = ""
        self.tripped: bool = False
        self.trip_reason: Optional[str] = None

    def inspect_token(self, token_str: str) -> bool:
        self.rolling_window = (self.rolling_window + token_str)[-256:]
        lowered = self.rolling_window.lower()
        for pat in FORBIDDEN_PATTERNS:
            if pat in lowered:
                self.tripped = True
                self.trip_reason = f"L1_GUARD_BLOCK: padrao proibido detectado ({pat})"
                return False
        return True

    def reset(self) -> None:
        self.rolling_window = ""
        self.tripped = False
        self.trip_reason = None

class LupinStoreLPS1:
    """Gerenciador de Snapshots LPS1 com Rollback Determinístico para QwenPU."""
    def __init__(self) -> None:
        self.snapshots: List[bytes] = []

    def commit_snapshot(self, step_id: int, pc_reg: int, state_payload: bytes) -> bytes:
        checksum = fnv1a_32(state_payload)
        # Header LPS1: Magic (4B) | Step (4B) | PC (4B) | PayloadLen (4B) | FNV-1a (4B)
        header = struct.pack("<4sIIII", LPS1_MAGIC, step_id, pc_reg, len(state_payload), checksum)
        blob = header + state_payload
        self.snapshots.append(blob)
        return blob

    def verify_and_unpack(self, blob: bytes) -> Dict[str, object]:
        magic, step_id, pc_reg, length, expected_crc = struct.unpack("<4sIIII", blob[:20])
        if magic != LPS1_MAGIC:
            raise ValueError("Magic header invalido (esperado LPS1)")
        payload = blob[20:20 + length]
        actual_crc = fnv1a_32(payload)
        if actual_crc != expected_crc:
            raise ValueError(f"Corrupcao detectada! FNV-1a {actual_crc:#x} != {expected_crc:#x}")
        return {"step_id": step_id, "pc": pc_reg, "fnv1a": actual_crc, "payload": payload}

    def rollback_last_valid(self) -> Optional[Dict[str, object]]:
        while self.snapshots:
            candidate = self.snapshots.pop()
            try:
                return self.verify_and_unpack(candidate)
            except ValueError:
                continue
        return None
```

---

## 5. Blindagem do Silício no Fenrir: `lupin_power.c` (Cap V/f Frozen)
Para garantir que o **Fenrir (Mi 11 Ultra)** opere em regime *bare-metal* sem violar a regra de ouro do cluster, o governador térmico e de frequências deve respeitar estritamente o **Cap V/f FROZEN** (`cpu4-6` em $1728000\text{ kHz}$ e `cpu7` em $1843200\text{ kHz}$) e a histerese de $3^\circ\text{C}$ do Peltier:

```c
// src/cpu/lupin_power_enforcer.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>

// Limites Inegociáveis do Cluster Lupin (SM8350 / Fenrir)
#defineVF_CAP_GOLD_KHZ   1728000 // cpu4, cpu5, cpu6 (Cortex-A78)
#define VF_CAP_PRIME_KHZ 1843200 // cpu7 (Cortex-X1 FROZEN CAP)
#define TEMP_CPU_MAX_MC  65000   // 65.0 °C
#define TEMP_BAT_MAX_MC  35000   // 35.0 °C
#define HYSTERESIS_MC    3000    // 3.0 °C

static int write_sysfs_int(const char *path, int val) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "%d\n", val);
    fclose(fp);
    return 0;
}

static int read_sysfs_int(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int val = -1;
    if (fscanf(fp, "%d", &val) != 1) val = -1;
    fclose(fp);
    return val;
}

void enforce_frozen_vf_caps(void) {
    char path[128];
    for (int cpu = 4; cpu <= 6; cpu++) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        write_sysfs_int(path, VF_CAP_GOLD_KHZ);
    }
    write_sysfs_int("/sys/devices/system/cpu/cpu7/cpufreq/scaling_max_freq", VF_CAP_PRIME_KHZ);
}
```

---

## 📨 Ordem de Execução Pronta para Enviar ao Agente Local (Claude Code)

Copie e envie o bloco abaixo para o seu agente local iniciar os trabalhos imediatamente:

> **Diretriz do Lord (Aprovação Gemini Argon + Arquitetura Exocórtex v6):**
>
> Aprovo a **Opção B (Melhoria Incremental Controlada em 4 Etapas para a QwenPU)** e o plano de validação escalonada começando pelo **Fenrir (Mi 11 Ultra)**. Como o nosso sistema é um Exocórtex Pessoal Soberano construído sobre o repositório `cavalo-de-fogo` (commit `594b366`), execute exatamente nesta ordem respeitando as travas da Matilha:
>
> 1. **Correção de Ambiente no Fenrir (`192.168.1.9` / Enlace OTG):**
>    - O Fenrir roda **Bare-Metal Linux v6** (Alpine aarch64 nativo), não Termux/proot.
>    - **Proibido** instalar PyTorch pesado ou `llama-cpp-python` via `pip` no Fenrir. Use exclusivamente binários C/C++/Rust compilados estaticamente (`aarch64-unknown-linux-musl`) com aceleração Adreno 660 / NPU Hexagon FastRPC / ARM NEON.
>    - Mantenha intocável o **Cap V/f FROZEN** (`cpu4-6: 1728000`, `cpu7: 1843200`) e o Peltier em $32^\circ\text{C}\text{--}33^\circ\text{C}$. Todo teste de modelo no Fenrir é **estritamente sob demanda** (subiu, rodou o benchmark, salvou o log e matou o processo liberando $100\%$ dos $12\text{ GB}$ de RAM).
>
> 2. **Proteção Absoluta do PC Mimir ($7\text{ GB}$ RAM):**
>    - **Proibido** rodar inferência de Qwen 4B/8B ou compilações pesadas na CPU do PC Mimir. O Mimir atua apenas como maestro, ponte de *staging* e validador de checksums `FNV-1a` / `LPS1`.
>
> 3. **Execução Imediata — Frente Dupla (Colab Beast A100 + Fenrir):**
>    - **Frente 1 (No Colab Beast A100 — `colab-beast:11434`, $80\text{ GB}$ VRAM, Nativo sem `proot`):**
>      - Inicie a **Etapa A**: Baixe o checkpoint `divagr1925/QwenPU` ($26.7\text{ GB}$) no `/local-scratch` e execute o `vxcheck`/`storyrun` validando os *trace tokens* contra a referência.
>      - Implemente as **Etapas B e C**: Crie o módulo `lupin_guard_q35_integration.py` acoplando o filtro `lupin-guard` L1 e o empacotador de snapshots `LPS1` (com checksum `FNV-1a`) aos estados da QwenPU.
>      - Use a CPU Xeon + $161\text{ GB}$ de RAM do Colab Beast como **Forja Cross-Compile** (`aarch64-unknown-linux-musl`) para compilar os binários atualizados do `cavalo-de-fogo` (`lupinos-fastrpc` com RAII `Drop`, `lupinos-daemon` com `lora_manager.rs` verificado por `FNV-1a`, e `lupin_subdisplay_hud`) sem pesar no Mimir.
>    - **Frente 2 (No Fenrir Mi 11 Ultra):**
>      - Valide primeiro o `Qwen3.5-4B` (GGUF 4-bit $\sim 2.3\text{ GB}$) e depois o `Qwen3.5-8B` (GGUF 4-bit $\sim 4.5\text{ GB}$) via binário nativo, medindo *time-to-first-token*, tokens/s, temperatura do SoC e garantindo que os testes `14/14` (`lupin-pc`) e `7/7` (`lupin-guard`) continuem $100\%$ verdes.
>
> Pode iniciar a **Etapa A no Colab Beast A100** e a validação nativa do **`Qwen3.5-4B` no Fenrir** agora!