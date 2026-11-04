#!/usr/bin/env python3
import os, sys, base64, subprocess, json

REPO_ROOT = "/home/filipe/cavalo-de-fogo"

files = [
    "Cargo.toml",
    "crates/lupinos-fastrpc/Cargo.toml",
    "crates/lupinos-fastrpc/src/lib.rs",
    "crates/lupinos-display/Cargo.toml",
    "crates/lupinos-display/src/lib.rs",
    "crates/lupinos-daemon/Cargo.toml",
    "crates/lupinos-daemon/src/main.rs"
]

sources = {}
for f in files:
    with open(os.path.join(REPO_ROOT, f), "r", encoding="utf-8") as fh:
        sources[f] = fh.read()

payload_b64 = base64.b64encode(json.dumps(sources).encode("utf-8")).decode("ascii")

colab_script = f"""import os, sys, base64, subprocess, json

print('=== 1. Setting up Rustup + Musl AArch64 in Cloud ===')
subprocess.run('curl --proto "=https" --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --target aarch64-unknown-linux-musl', shell=True, check=True)
subprocess.run('apt-get update -qq && apt-get install -y -qq musl-tools gcc-aarch64-linux-gnu', shell=True, check=True)

sources = json.loads(base64.b64decode('{payload_b64}').decode('utf-8'))
for path, code in sources.items():
    os.makedirs(os.path.dirname(path) if os.path.dirname(path) else '.', exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(code)

print('=== 2. Compiling Static lupinos-daemon for AArch64 Musl ===')
env = os.environ.copy()
cargo_bin = os.path.expanduser('~/.cargo/bin')
env['PATH'] = f"{{cargo_bin}}:{{env['PATH']}}"
env['CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER'] = 'aarch64-linux-gnu-gcc'

cmd = ['cargo', 'build', '--release', '--target', 'aarch64-unknown-linux-musl']
p = subprocess.run(cmd, env=env, capture_output=True, text=True)
print(p.stdout)
print(p.stderr)

if p.returncode != 0:
    sys.exit(p.returncode)

bin_path = 'target/aarch64-unknown-linux-musl/release/lupinos-daemon'
with open(bin_path, 'rb') as f:
    bin_b64 = base64.b64encode(f.read()).decode('ascii')

print('BEGIN_LUPINOS_DAEMON_B64')
print(bin_b64)
print('END_LUPINOS_DAEMON_B64')
print('=== Compilation Success! ===')
"""

with open("/tmp/colab_rust_job.py", "w") as f:
    f.write(colab_script)

print("Dispatching Static Rust Musl Compilation Job to Google Colab Forge...")
res = subprocess.run(["colab-gpu-runner", "--file", "/tmp/colab_rust_job.py", "--gpu", "CPU", "--task", "compile_rust_musl_daemon"], capture_output=True, text=True)
print(res.stdout)
if "BEGIN_LUPINOS_DAEMON_B64" in res.stdout:
    b64_data = res.stdout.split("BEGIN_LUPINOS_DAEMON_B64")[1].split("END_LUPINOS_DAEMON_B64")[0].strip()
    out_path = os.path.join(REPO_ROOT, "lupinos-daemon")
    with open(out_path, "wb") as f:
        f.write(base64.b64decode(b64_data))
    os.chmod(out_path, 0o755)
    print(f"SUCCESS: Generated {out_path} ({os.path.getsize(out_path)} bytes)")
else:
    print(res.stderr)
