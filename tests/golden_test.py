#!/usr/bin/env python3
"""
Golden test do Lupin Neuro-PC — paridade bit-a-bit entre três motores.

    QEMU          (qemu-riscv32)         — referência do hardware real
    ref.py        (Qwenputer)            — a spec executável do circuito em pesos
    motor C       (src/neuro_pc)         — o motor que roda no Fenrir

Os três executam a MESMA imagem ELF e devem concordar em stdout, exit code
e (quando aplicável) no número de instruções. Qualquer divergência é um bug
de um dos motores — nunca um "quase igual".

Uso:
    python3 tests/golden_test.py [-v]

Requer: riscv64-linux-gnu-gcc, qemu-riscv32 e o checkout do Qwenputer
(rv/ref.py) em LUPIN_RV_REPO, ou no caminho padrão do laboratório.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RV_REPO = Path(os.environ.get("LUPIN_RV_REPO",
                              Path.home() / "lab-bench/divagr18/qwenputer/experimental/riscv"))
RV_ENV = RV_REPO / "chipformer/rv/env"
CROSS_CC = os.environ.get("LUPIN_RV_CROSS_CC", "riscv64-linux-gnu-gcc")

# ---------------------------------------------------------------------------
# Programas de teste. Cada um é compilado duas vezes: com o mapa do circuito
# (rom.ld) para o motor C e o ref.py, e com o mapa do QEMU (qemu.ld) para o
# QEMU. O código-fonte é o mesmo; só o layout de memória muda.
# ---------------------------------------------------------------------------

PROGRAMS = {
    "hello": r'''
#include "rt.h"
int main(void) { for (int i = 0; i < 5; i++) rv_putchar('A' + i); rv_putchar('\n'); return 0; }
''',
    "exit_code": r'''
#include "rt.h"
int main(void) { rv_putchar('X'); return 42; }
''',
    "arithmetic": r'''
#include "rt.h"
volatile int a = 1234, b = 5678;
int main(void) {
    volatile int p = a * b;
    for (int i = 0; i < 7; i++) rv_putchar('0' + (p / 10000000 % 10));
    rv_putchar('\n');
    return (p == 7006652) ? 0 : 1;
}
''',
    "division": r'''
#include "rt.h"
volatile int x = 1000000, y = 7;
int main(void) {
    volatile int q = x / y, r = x % y;
    for (int i = 0; i < 6; i++) rv_putchar('0' + (q / 100000 % 10));
    rv_putchar(',');
    rv_putchar('0' + (r % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "shifts_zba": r'''
#include "rt.h"
int main(void) {
    volatile int v = 3;
    volatile int s1 = v << 1, s2 = v << 2, s3 = v << 3;
    rv_putchar('0' + s1); rv_putchar('0' + s2); rv_putchar('0' + s3);
    rv_putchar('\n');
    return 0;
}
''',
    "memory_rw": r'''
#include "rt.h"
static volatile int buf[8];
int main(void) {
    for (int i = 0; i < 8; i++) buf[i] = i * 11;
    volatile int sum = 0;
    for (int i = 0; i < 8; i++) sum += buf[i];
    for (int i = 0; i < 4; i++) rv_putchar('0' + (sum / 100 % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "loop_branches": r'''
#include "rt.h"
int main(void) {
    volatile int total = 0;
    for (int i = 1; i <= 20; i++) {
        if (i % 3 == 0) continue;
        if (i == 17) break;
        total += i;
    }
    rv_putchar('0' + (total / 100 % 10));
    rv_putchar('0' + (total / 10 % 10));
    rv_putchar('0' + (total % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "signed_compare": r'''
#include "rt.h"
volatile int neg = -5, pos = 3;
int main(void) {
    rv_putchar(neg < pos ? 'L' : 'G');
    rv_putchar((unsigned)neg > (unsigned)pos ? 'U' : 'u');
    rv_putchar('\n');
    return 0;
}
''',
    # Regressão do bug dos branches: bits [11:7] do formato B são imediato.
    # Se o motor escrever rd=0 num branch, x13/a3 é zerado e o resultado muda.
    "branch_no_rd_write": r'''
#include "rt.h"
int main(void) {
    volatile int acc = 0, i = 0;
    while (i < 12) {
        if (i == 5) { i++; continue; }
        if (i == 9) { i++; break; }
        acc += i;
        i++;
    }
    for (int d = 1000; d; d /= 10) rv_putchar('0' + (acc / d % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "nested_loops": r'''
#include "rt.h"
int main(void) {
    volatile int t = 0;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            if ((i + j) % 2 == 0) t += i * j;
    for (int d = 1000; d; d /= 10) rv_putchar('0' + (t / d % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "mul_div_edge": r'''
#include "rt.h"
int main(void) {
    volatile int big = 2000000000;
    volatile int m = big * 3;           /* overflow de 32 bits */
    volatile int d = -7, q = big / d, r = big % d;
    rv_putchar('0' + (m % 10));
    rv_putchar('0' + (q < 0 ? 9 : 0));
    rv_putchar('0' + (r < 0 ? 9 : 0));
    rv_putchar('\n');
    return 0;
}
''',
    "stack_frame": r'''
#include "rt.h"
static volatile int deep(int n) { volatile int x = n; return n <= 0 ? 0 : x + deep(n - 1); }
int main(void) {
    volatile int s = deep(8);
    rv_putchar('0' + (s % 10));
    rv_putchar('\n');
    return 0;
}
''',
    "byte_halves": r'''
#include "rt.h"
int main(void) {
    volatile unsigned int w = 0x12345678u;
    for (int s = 24; s >= 0; s -= 8) rv_putchar('0' + ((w >> s) & 0xF) % 10);
    rv_putchar('\n');
    return 0;
}
''',
    "char_io": r'''
#include "rt.h"
int main(void) {
    const char *m = "LUPIN";
    for (int i = 0; m[i]; i++) rv_putchar(m[i]);
    rv_putchar('\n');
    return 0;
}
''',
}


def build(name: str, src: str, ld: str, out: Path, qemu: bool) -> bool:
    """Compila o programa com o mapa pedido. Retorna False em falha.

    O crt0.S do upstream chama exit(), que no upstream vem do sys.c (picolibc).
    Aqui fornecemos um exit() mínimo sobre ecall a7=93, o mesmo contrato do
    circuito: em ambos os mapas o código de saída é o retorno de main.
    """
    with tempfile.TemporaryDirectory() as td:
        cfile = Path(td) / "prog.c"
        extra = ["-DQEMU"] if qemu else []
        cfile.write_text(src)
        exit_src = Path(td) / "exit.c"
        exit_src.write_text(
            '#include "rt.h"\n'
            'void exit(int code) { for (;;) rv_ecall2(93, code, 0, 0); }\n'
        )
        cmd = [CROSS_CC, "-march=rv32im", "-mabi=ilp32", "-nostdlib", "-nostartfiles",
               "-static", "-O2", *extra, f"-I{RV_ENV}", "-T", str(ld),
               "-o", str(out), str(RV_ENV / "crt0.S"), str(exit_src), str(cfile)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  [BUILD FAIL] {name} ({'qemu' if qemu else 'rom'}):\n{r.stderr}")
            return False
    return True


def run_qemu(elf: Path) -> tuple[str, int]:
    r = subprocess.run(["qemu-riscv32", "-cpu", "rv32", str(elf)],
                       capture_output=True, timeout=60)
    return r.stdout.decode("latin-1"), r.returncode


def run_ref(elf: Path) -> tuple[str, int, int]:
    """Executa no ref.py do Qwenputer (numpy puro, sem torch)."""
    script = (
        "import sys; sys.path.insert(0, %r)\n"
        "from chipformer.rv import elf as E, ref\n"
        "prog = E.program(%r)\n"
        "m = ref.Machine.from_program(prog)\n"
        "for _ in range(2000000):\n"
        "    if m.halted: break\n"
        "    m.record()\n"
        "sys.stdout.write(bytes(m.out).decode('latin-1'))\n"
        "sys.exit(m.exit_code if m.exit_code is not None else 0)\n"
    ) % (str(RV_REPO), str(elf))
    r = subprocess.run([sys.executable, "-c", script], capture_output=True, timeout=300)
    if r.returncode not in (0, 1, 42, 255):
        # pode ser um exit code do programa; se stderr tem traceback, é bug
        if r.stderr:
            print(f"  [REF ERROR] {r.stderr.decode()[:400]}")
    return r.stdout.decode("latin-1"), r.returncode, r.stderr.decode()


def run_c(elf: Path) -> tuple[str, int, int]:
    """Executa no motor C (binário já compilado)."""
    r = subprocess.run([str(REPO / "build/lupin-pc"), "elf", str(elf)],
                       capture_output=True, timeout=120)
    import json
    try:
        data = json.loads(r.stdout.decode())
    except Exception:
        print(f"  [C ERROR] {r.stdout.decode()[:300]} {r.stderr.decode()[:200]}")
        return "", -1, -1
    return data.get("output", ""), data.get("exit_code", 0), data.get("cycles", 0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--only", help="roda apenas um programa")
    args = ap.parse_args()

    if not (RV_ENV / "rom.ld").exists():
        print(f"ERRO: checkout do Qwenputer não encontrado em {RV_ENV}")
        print("      defina LUPIN_RV_REPO ou clone divagr18/qwenputer")
        return 2
    cbin = REPO / "build/lupin-pc"
    if not cbin.exists():
        print(f"ERRO: {cbin} não existe. Rode: make build/lupin-pc")
        return 2

    names = [args.only] if args.only else list(PROGRAMS)
    failures = []
    print(f"Golden test: QEMU × ref.py × motor C   ({len(names)} programas)\n")

    for name in names:
        src = PROGRAMS[name]
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            rom_elf, qemu_elf = td / "rom.elf", td / "qemu.elf"
            if not build(name, src, RV_ENV / "rom.ld", rom_elf, qemu=False):
                failures.append((name, "build-rom"))
                continue
            if not build(name, src, RV_ENV / "qemu.ld", qemu_elf, qemu=True):
                failures.append((name, "build-qemu"))
                continue

            q_out, q_rc = run_qemu(qemu_elf)
            r_out, r_rc, r_err = run_ref(rom_elf)
            c_out, c_rc, c_cyc = run_c(rom_elf)

        ok = (q_out == r_out == c_out) and (q_rc == r_rc == c_rc)
        mark = "OK  " if ok else "FAIL"
        print(f"  [{mark}] {name:16s} qemu={q_rc} ref={r_rc} c={c_rc}  "
              f"out={c_out!r}")
        if args.verbose:
            print(f"         qemu={q_out!r} ref={r_out!r}")
        if not ok:
            failures.append((name, f"qemu={q_out!r}/{q_rc} ref={r_out!r}/{r_rc} "
                                   f"c={c_out!r}/{c_rc}"))
            if r_err:
                print(f"         ref stderr: {r_err[:300]}")

    print()
    if failures:
        print(f"FALHOU: {len(failures)}/{len(names)}")
        for n, why in failures:
            print(f"  - {n}: {why}")
        return 1
    print(f"PARIDADE TOTAL: {len(names)}/{len(names)} motores concordam "
          f"(stdout + exit code) — QEMU ≡ ref.py ≡ motor C")
    return 0


if __name__ == "__main__":
    sys.exit(main())
