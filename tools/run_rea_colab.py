#!/usr/bin/env python3
import os, sys, base64, subprocess

SO_PATH = "/home/filipe/cavalo-de-fogo/tools/libcdsprpc.so"
with open(SO_PATH, "rb") as f:
    b64_so = base64.b64encode(f.read()).decode("ascii")

colab_script = f"""import subprocess, sys, os, base64

print('=== 1. Setting up Reverse Engineering Engine in Cloud ===')
subprocess.run('apt-get update -qq && apt-get install -y -qq radare2', shell=True, check=True)

with open('libcdsprpc.so', 'wb') as f:
    f.write(base64.b64decode('{b64_so}'))

print('=== 2. Analyzing libcdsprpc.so (Snapdragon 888) ===')
cmd = "r2 -q -c 'aaa; afl' libcdsprpc.so"
p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
funcs = [line for line in p.stdout.splitlines() if any(k in line for k in ['remote_handle', 'rpcmem', 'dspqueue', 'fastrpc', 'ioctl'])]

print(f'Key functions identified: {{len(funcs)}}')
for f in funcs[:15]:
    print('  ->', f)

print('=== 3. Disassembling Key IPC Dispatchers ===')
cmd_decomp = "r2 -q -c 'aaa; s sym.remote_handle_open; pdf; s sym.rpcmem_alloc; pdf; s sym.remote_handle_invoke; pdf' libcdsprpc.so"
p_decomp = subprocess.run(cmd_decomp, shell=True, capture_output=True, text=True)

print('BEGIN_REA_OUTPUT')
print(p_decomp.stdout[:6000])
print('END_REA_OUTPUT')
"""

with open("/tmp/colab_rea_job.py", "w") as f:
    f.write(colab_script)

print("Dispatching REA Job to Google Colab Forge (CPU / Cloud Engine)...")
res = subprocess.run(["colab-gpu-runner", "--file", "/tmp/colab_rea_job.py", "--gpu", "CPU", "--task", "rea_fastrpc_dissection"], capture_output=True, text=True)
print(res.stdout)
if "BEGIN_REA_OUTPUT" in res.stdout:
    report = res.stdout.split("BEGIN_REA_OUTPUT")[1].split("END_REA_OUTPUT")[0].strip()
    out_file = "/home/filipe/cavalo-de-fogo/docs/FASTRPC_DISASSEMBLY_REPORT.txt"
    with open(out_file, "w") as f:
        f.write(report)
    print(f"SUCCESS: Analysis saved to {out_file} ({len(report)} chars)")
else:
    print(res.stderr)
