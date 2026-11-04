#!/usr/bin/env python3
import os, sys, base64, subprocess

SO_PATH = "/home/filipe/cavalo-de-fogo/tools/libcdsprpc.so"
with open(SO_PATH, "rb") as f:
    b64_so = base64.b64encode(f.read()).decode("ascii")

colab_script = f"""import subprocess, sys, os, base64

subprocess.run('apt-get update -qq && apt-get install -y -qq radare2', shell=True, check=True)

with open('libcdsprpc.so', 'wb') as f:
    f.write(base64.b64decode('{b64_so}'))

print('=== 1. Finding All ioctl Call Sites ===')
cmd_xrefs = "r2 -q -c 'aaa; axt sym.imp.ioctl' libcdsprpc.so"
p_xrefs = subprocess.run(cmd_xrefs, shell=True, capture_output=True, text=True)
print(p_xrefs.stdout)

print('=== 2. Disassembling remote_handle_open Target (0x215cc) ===')
cmd_open = "r2 -q -c 'aaa; s 0x215cc; pd 35' libcdsprpc.so"
p_open = subprocess.run(cmd_open, shell=True, capture_output=True, text=True)

print('=== 3. Disassembling rpcmem_alloc Target (0x2daa4) ===')
cmd_alloc = "r2 -q -c 'aaa; s 0x2daa4; pd 35' libcdsprpc.so"
p_alloc = subprocess.run(cmd_alloc, shell=True, capture_output=True, text=True)

print('BEGIN_IOCTL_REPORT')
print('--- XREFS TO IOCTL ---')
print(p_xrefs.stdout)
print('--- FUNCTION 0x215cc (remote_handle_open) ---')
print(p_open.stdout)
print('--- FUNCTION 0x2daa4 (rpcmem_alloc) ---')
print(p_alloc.stdout)
print('END_IOCTL_REPORT')
"""

with open("/tmp/colab_ioctl_job.py", "w") as f:
    f.write(colab_script)

print("Dispatching Deep ioctl Extraction Job to Google Colab Forge...")
res = subprocess.run(["colab-gpu-runner", "--file", "/tmp/colab_ioctl_job.py", "--gpu", "CPU", "--task", "rea_ioctl_extraction"], capture_output=True, text=True)
print(res.stdout)
if "BEGIN_IOCTL_REPORT" in res.stdout:
    report = res.stdout.split("BEGIN_IOCTL_REPORT")[1].split("END_IOCTL_REPORT")[0].strip()
    out_file = "/home/filipe/cavalo-de-fogo/docs/FASTRPC_IOCTL_DEEP_DISASSEMBLY.txt"
    with open(out_file, "w") as f:
        f.write(report)
    print(f"SUCCESS: Deep analysis saved to {out_file} ({len(report)} chars)")
else:
    print(res.stderr)
