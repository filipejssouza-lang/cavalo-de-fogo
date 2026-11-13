# 🤝 COORDENAÇÃO — não mexer sem ler

> Há **duas agentes** trabalhando no Neuro-OS. Este arquivo é o canal de coordenação.
> Ele está **versionado no repo** justamente para ser lido por qualquer agente que
> chegue aqui. O espelho completo está em `~/.agents/memory/coordenacao_neuro_os.md`.

## 🗺️ PRIMEIRO: os nomes são o MESMO PROJETO
**Fenrir = Cavalo de Fogo = Lupin OS = Neuro-PC = Neuro-OS.**
**Fenrir** = o nó (Mi 11 Ultra, `192.168.1.9`) · **Lupin OS v6** = o SO bare-metal que roda
nele · **Cavalo de Fogo** = este repo · **Neuro-PC** (`lupin-pc`) = o motor RV32IM+Zba em
`src/neuro_pc/` · **Neuro-OS** = o computador em pesos em cima do motor
(Qwenputer/QwenPU + Lupin-Guard fundido nos pesos), em `~/lupin-neuro-os/`.

## Estado atual (2026-11-13 09:40 BRT)

| Frente | Dono | Estado |
|---|---|---|
| Motor RV32IM real (F1) | Orquestrador | ✅ `f5511fa` |
| Guard (L1) + Store + endpoints (F2) | Orquestrador | ✅ **mergeado na master** `03a2cae` — `audit_phase2.sh` **APROVADO tudo verde** |
| **Bignum ops (uops 64-69) + golden test cobrindo (F2.5)** | Claude Code | ✅ **CONCLUÍDO** — `ac85dc7`, golden test **18/18** (inclui bn_add/bn_mul/bn_sub/bn_edge) |
| **Port AArch64 do motor no Fenrir** | Orquestrador | ✅ **CONCLUÍDO 2026-11-13** — `lupin-pc` AArch64 instalado em `/usr/local/bin/lupin-pc` via `tools/deploy_fenrir.sh`; **15/15 idêntico** ao build x86_64 |
| **Fase 3 — ponte real no Colab L4** | Orquestrador | ✅ **VALIDADO** (inferência real no checkpoint CHIP8) — o que falta é o **Guard in-weights** |
| **Guard in-weights — entrega completa** | Orquestrador | 🔴 **ÚNICO GAP** — `tools/audit_delivery.py` = REPROVADO 3/8; faltam `guard/test_guard_q35.py`, `tests/test_acceptance.py`, `colab/build_neuro_os.py`, `colab/verify_neuro_os.py` (3 são CPU-only) |
| F4 — UOP-64/ARM64 | — | ⬜ |
| F5 — Automação Custo Zero | — | 🟡 |

> **Causa do gap:** a sessão Jules `6167182409992609950` entregou mas **não deu push**.
> Material preservado em `~/lupin-neuro-os/wip_sessao_6167182409992609950/`.
> Spec do que falta: `~/lupin-neuro-os/docs/SPEC.md`.

## Regra de ouro

**UM dono por arquivo por vez.** Se duas agentes vão mexer no mesmo arquivo, uma
entrega primeiro e a outra espera o merge.

### Escrita exclusiva do Claude Code AGORA
```
src/neuro_pc/lupin_neuro_pc.c
src/neuro_pc/lupin_neuro_pc.h
tests/golden_test.py
```

### Escrita exclusiva do Orquestrador
```
~/lupin-neuro-os/        (specs, guias, harness)
~/lab-bench/divagr18/    (leitura — NÃO fork do upstream)
~/.agents/memory/        (estado_atual.md, diario_de_bordo.md)
```

## O gap do bignum (F2.5) — RESOLVIDO em 2026-11-13, não refaça
> ✅ **Fechado** pelo commit `ac85dc7` ("implementa digit file e bignum ops (uops 64-69) com
> paridade 18/18"). O golden test subiu de 14/14 para **18/18** com os 4 casos novos
> (bn_add, bn_mul, bn_sub, bn_edge), que exercitam de fato o digit file.
> O texto abaixo é o **registro histórico** do gap — mantido para contexto.

<details><summary>Registro histórico do gap (F2.5) — ler só por curiosidade</summary>

O `ref.py` do Qwenputer implementa o **digit file**: 8 registradores `d0..d7` de 128
dígitos decimais, com as instruções custom:

| uop | opcode/f3 | o que faz |
|---|---|---|
| `BMAC` | 0x0B / f3=0 | `dD[i+off] = (ovw?0:dD[i+off]) + dS[i]*m + c`, carry `c` em `rd` |
| `BMSB` | 0x0B / f3=1 | idem, subtraindo |
| `BLD` | 0x0B / f3=2 | carrega ASCII de `mem8[(rs1-1-i) & 0x7FFF]` no digit file |
| `BGET` | 0x0B / f3=3 | lê um dígito |
| `BSCAN` | 0x0B / f3=4 | conta dígitos ASCII numa janela de memória |
| `BPRD` | 0x2B / f3=0 | imprime `'0' + dígito` |

O motor C **não tem nenhum deles** (0 ocorrências de `0x0B`/`0x2B` no decode), e o
**golden test 14/14 não pega** — os 14 programas são C simples e nenhum toca o digit file.
A paridade verificada é real, mas **restrita ao domínio coberto**.

### Ordem sugerida

1. **Escrever um caso de teste autocontido** que use as custom ops (BLD + BMAC + BPRD),
   sem depender de `mini-gmp` (que não está no checkout). Montar em assembly ou bytes crus.
2. **Acrescentá-lo ao `tests/golden_test.py`** → o motor C passa a **falhar**
   (illegal instruction). Isso é a prova de que o teste pega o gap.
3. **Implementar o digit file** no motor C (8 × 128 dígitos, os 6 uops).
4. O golden test volta a **15/15**, agora com cobertura real do domínio do Qwenputer.

### Spec — cite a linha, não invente

`~/lab-bench/divagr18/qwenputer/experimental/riscv/chipformer/rv/ref.py`:
- linhas ~22-52: o formato do digit file e o encode de `rs2`
  (`rs2 = dD | dS << 3 | ovw << 6 | n << 7 | off << 13 | m << 20`)
- linhas ~296-310: `BMSB` / `BMAC` no emulador plano
- linhas ~519-530: os handlers no `Machine.record()` (a forma do **circuito**)
- `spec.py` linhas ~43-59: o mapa de memória e o formato dos tokens

**Atenção:** o `Machine` (circuito) e o `Rv32` (emulador plano) têm **semântica
diferente** para o digit file — o `Machine` emite tokens `BD`/`SC`/`F0`/`NT` para o
circuito; o `Rv32` só simula. O motor C é **host-side**, então siga o `Rv32`
(linhas ~296-310) para a semântica e use o `Machine` só como referência do *formato*
do registro no circuito.

### ⚠️ O bignum NÃO é otimização — é essencial

Não aceite a conclusão de que "o escopo RV32IM+Zba atual já é suficiente". Isso está
**errado**:

- O firmware `calc` do Qwenputer (`experimental/riscv/env/calc/calc.c`) avalia
  `+ - * / // % ** gcd` **sobre o digit file**, com valores de **até 62 dígitos**
  (`docs/rv32.md:24-31`).
- Sem as uops 64-69, o computador **em pesos** não faz aritmética de precisão
  arbitrária, e o motor C fica **incompleto em relação à spec do `ref.py`**.
- O `README.md:16` do `experimental/riscv` diz: *"A `calc` firmware with bignum
  `+ - * / // % ** gcd` is exact on 10,000 random cases."*

Ou seja: o golden test 14/14 hoje prova a paridade **no domínio coberto** (aritmética
de 32 bits), e o domínio do Qwenputer é **justamente** o de precisão arbitrária. Sem
as custom ops, a Fase 3 (merge real) entrega um computador que não calcula.

</details>

## Numeração canônica das fases

**F1** motor real ✅ · **F2** Guard+Store+endpoints ✅ · **F2.5** bignum ✅ **18/18** ·
**F3** ponte real no Colab ✅ (falta o Guard in-weights) · **F4** UOP-64/ARM64 ⬜

A "Fase 2 = portar ref.py→C" do plano original foi **absorvida pela F1**.

## Memória compartilhada

`estado_atual.md` e `diario_de_bordo.md` são de **duas agentes**. Cada uma insere a
sua seção **no topo**, com o próximo número, e **nunca reescreve seções alheias**.
