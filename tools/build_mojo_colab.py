#!/usr/bin/env python3
"""
🐺 LupinOS Mojo Cloud Forge Script
Uses Google Colab (via colab-gpu-runner) to compile Mojo source code for AArch64 Cortex-X1
and automatically lowers/extracts zero-dependency static C-ABI objects and shared libraries.
"""

import os
import sys
import subprocess
import argparse

MOJO_SRC_DEFAULT = os.path.join(os.path.dirname(__file__), "../src/mojo/lupin_mojo_kernels.mojo")

COLAB_WRAPPER_CODE = """
import subprocess, sys, os, base64

print("=== 1. Setting up Mojo Toolchain in Cloud ===")
subprocess.run([sys.executable, "-m", "pip", "install", "-q", "mojo"], check=True)

mojo_code = \"\"\"{mojo_src_escaped}\"\"\"

with open("lupin_mojo_kernels.mojo", "w") as f:
    f.write(mojo_code)

print("=== 2. Compiling Mojo to AArch64 Cortex-X1 Object ===")
p_build = subprocess.run([
    "mojo", "build",
    "--target-triple", "aarch64-linux-gnu",
    "--mcpu", "cortex-x1",
    "--target-features", "",
    "-O3",
    "--emit", "object",
    "-o", "lupin_mojo_cortex_x1.o",
    "lupin_mojo_kernels.mojo"
], capture_output=True, text=True)

print("Mojo exit code:", p_build.returncode)
if p_build.returncode != 0:
    print(p_build.stderr)
    sys.exit(1)

print("=== 3. Linking Shared Library (C-ABI) ===")
subprocess.run(["apt-get", "update", "-qq"])
subprocess.run(["apt-get", "install", "-y", "-qq", "gcc-aarch64-linux-gnu"])

subprocess.run([
    "aarch64-linux-gnu-gcc", "-shared", "-O3",
    "-o", "libmojokernels.so", "lupin_mojo_cortex_x1.o", "-lm"
], check=True)

print("=== 4. Emitting Artifacts for Automatic Extraction ===")
with open("libmojokernels.so", "rb") as f_so:
    b64_so = base64.b64encode(f_so.read()).decode("ascii")
    print(f"__FILE_BASE64:libmojokernels.so:{b64_so}__")

with open("lupin_mojo_cortex_x1.o", "rb") as f_o:
    b64_o = base64.b64encode(f_o.read()).decode("ascii")
    print(f"__FILE_BASE64:lupin_mojo_cortex_x1.o:{b64_o}__")
"""

def main():
    parser = argparse.ArgumentParser(description="Build Mojo kernels for Snapdragon 888 Cortex-X1 via Cloud Colab")
    parser.add_argument("--src", default=MOJO_SRC_DEFAULT, help="Path to .mojo kernel source file")
    parser.add_argument("--out-dir", default=".", help="Directory to extract generated .so and .o files")
    args = parser.parse_args()

    if not os.path.exists(args.src):
        print(f"Error: Mojo source file not found at {args.src}")
        sys.exit(1)

    with open(args.src, "r") as f:
        src_content = f.read()

    escaped_src = src_content.replace("\\", "\\\\").replace('"""', '\\"\\"\\"')
    code_to_run = COLAB_WRAPPER_CODE.replace("{mojo_src_escaped}", escaped_src)

    cmd = [
        "colab-gpu-runner",
        "--task", "mojo_cortex_x1_forge",
        "--code", code_to_run,
        "--output-dir", os.path.abspath(args.out_dir),
        "--timeout", "240",
        "--json"
    ]
    print(f"🚀 Dispatching Mojo Cloud Forge compilation to Google Colab...")
    p = subprocess.run(cmd)
    sys.exit(p.returncode)

if __name__ == "__main__":
    main()
