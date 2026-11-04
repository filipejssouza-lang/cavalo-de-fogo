#!/usr/bin/env python3
"""
tools/build_os_v3.py - Lupin Bare-Metal OS v3 Boot Image Builder & Signer
Integrates Native C/Zig Guardian, Boot Header v3 packaging, and AVB 2.0 footer.
"""

import os
import sys
import struct
import shutil
import subprocess
import tempfile
import argparse
import hashlib

BOOT_HDR_MAGIC = b'ANDROID!'
BOOT_PAGE_SIZE = 4096
PARTITION_SIZE = 201326592  # 192 MB exact boot partition size for Mi 11 Ultra (star)

def pad_to_page(size, page_size=BOOT_PAGE_SIZE):
    return ((size + page_size - 1) // page_size) * page_size

def extract_kernel_and_ramdisk(orig_img_path, work_dir):
    print(f"[*] Extraindo imagem original: {orig_img_path}")
    if not os.path.exists(orig_img_path):
        raise FileNotFoundError(f"Imagem base não encontrada: {orig_img_path}")
        
    with open(orig_img_path, "rb") as f:
        hdr = f.read(BOOT_PAGE_SIZE)
        if not hdr.startswith(BOOT_HDR_MAGIC):
            raise ValueError(f"Cabeçalho inválido. Magic esperado: {BOOT_HDR_MAGIC}")
            
        magic, k_sz, r_sz, os_ver, hdr_sz, res0, res1, res2, res3, hdr_ver, cmdline = struct.unpack(
            "<8sIIII4II1536s", hdr[:8+16+16+4+1536]
        )
        
        k_padded = pad_to_page(k_sz)
        kernel_data = f.read(k_padded)[:k_sz]
        
        r_padded = pad_to_page(r_sz)
        ramdisk_data = f.read(r_padded)[:r_sz]
        
    kernel_file = os.path.join(work_dir, "kernel.bin")
    with open(kernel_file, "wb") as f:
        f.write(kernel_data)
        
    ramdisk_file = os.path.join(work_dir, "ramdisk.cpio.gz")
    with open(ramdisk_file, "wb") as f:
        f.write(ramdisk_data)
        
    print(f"[+] Kernel extraído: {k_sz} bytes -> {kernel_file}")
    print(f"[+] Ramdisk extraído: {r_sz} bytes -> {ramdisk_file}")
    return kernel_file, ramdisk_file, cmdline

def inject_guardian_and_repack(ramdisk_gz_path, guardian_bin, work_dir):
    print("[*] Desempacotando ramdisk para injeção do lupinos_guard...")
    ramdisk_root = os.path.join(work_dir, "ramdisk_root")
    os.makedirs(ramdisk_root, exist_ok=True)
    
    # Extrai o cpio.gz
    subprocess.check_call(f"zcat '{ramdisk_gz_path}' | cpio -idm 2>/dev/null", shell=True, cwd=ramdisk_root)
    
    # Injeta lupinos_guard em /sbin/lupinos_guard
    sbin_dir = os.path.join(ramdisk_root, "sbin")
    os.makedirs(sbin_dir, exist_ok=True)
    dest_bin = os.path.join(sbin_dir, "lupinos_guard")
    
    if not os.path.exists(guardian_bin):
        raise FileNotFoundError(f"Binário do guardião não encontrado: {guardian_bin}")
        
    shutil.copy2(guardian_bin, dest_bin)
    os.chmod(dest_bin, 0o755)
    print(f"[+] Guardião nativo injetado em: {dest_bin}")
    
    # Injeta serviço no init.recovery.qcom.rc ou init.recovery.service.rc
    rc_file = os.path.join(ramdisk_root, "init.recovery.qcom.rc")
    if os.path.exists(rc_file):
        with open(rc_file, "r", encoding="utf-8", errors="ignore") as f:
            rc_content = f.read()
        if "service lupinos_guard" not in rc_content:
            with open(rc_file, "a", encoding="utf-8") as f:
                f.write("\n# Lupin Bare-Metal OS v3 Guardian\n")
                f.write("service lupinos_guard /sbin/lupinos_guard\n")
                f.write("    user root\n")
                f.write("    group root\n")
                f.write("    seclabel u:r:recovery:s0\n")
                f.write("    oneshot\n\n")
                f.write("on boot\n")
                f.write("    start lupinos_guard\n")
            print("[+] Gatilho de boot configurado no init.recovery.qcom.rc")
            
    # Reempacota ramdisk_patched.cpio.gz
    out_ramdisk = os.path.join(work_dir, "ramdisk_patched.cpio.gz")
    cmd = f"find . -mindepth 1 | cpio -o -H newc 2>/dev/null | gzip -9 > '{out_ramdisk}'"
    subprocess.check_call(cmd, shell=True, cwd=ramdisk_root)
    print(f"[+] Ramdisk v3 reempacotado: {os.path.getsize(out_ramdisk)} bytes")
    return out_ramdisk

def assemble_and_sign_boot_v3(kernel_path, ramdisk_path, out_img_path, cmdline=b"twrpfastboot=1\x00"):
    print("[*] Montando imagem Boot v3 oficial...")
    with open(kernel_path, "rb") as f:
        kernel_data = f.read()
    with open(ramdisk_path, "rb") as f:
        ramdisk_data = f.read()
        
    k_sz = len(kernel_data)
    r_sz = len(ramdisk_data)
    
    cmdline_padded = cmdline + b"\x00" * (1536 - len(cmdline)) if len(cmdline) < 1536 else cmdline[:1536]
    hdr = struct.pack(
        "<8sIIII4II1536s",
        BOOT_HDR_MAGIC,
        k_sz,
        r_sz,
        0,     # os_version
        1580,  # header_size v3
        0, 0, 0, 0,
        3,     # header_version = 3
        cmdline_padded
    )
    hdr_padded = hdr + b"\x00" * (BOOT_PAGE_SIZE - len(hdr))
    k_padded = kernel_data + b"\x00" * (pad_to_page(k_sz) - k_sz)
    r_padded = ramdisk_data + b"\x00" * (pad_to_page(r_sz) - r_sz)
    
    with open(out_img_path, "wb") as f:
        f.write(hdr_padded)
        f.write(k_padded)
        f.write(r_padded)
        
    print(f"[+] Boot v3 bruto gerado: {os.path.getsize(out_img_path)} bytes")
    
    # Assinatura AVB 2.0 Hash Footer
    print("[*] Assinando com AVB 2.0 Hash Footer (192 MB exatos)...")
    avb_cmd = [
        "avbtool", "add_hash_footer",
        "--image", out_img_path,
        "--partition_name", "boot",
        "--partition_size", str(PARTITION_SIZE),
        "--prop", "com.android.build.boot.os_version:15",
        "--prop", "com.android.build.boot.security_patch:2099-12-31"
    ]
    subprocess.check_call(avb_cmd)
    final_size = os.path.getsize(out_img_path)
    assert final_size == PARTITION_SIZE, f"Tamanho incorreto: {final_size} != {PARTITION_SIZE}"
    
    h = hashlib.sha256(open(out_img_path, "rb").read()).hexdigest()
    print(f"[✓] SUCESSO! Imagem LupinOS v3 pronta para o Slot B:")
    print(f"    Arquivo: {out_img_path}")
    print(f"    Tamanho: {final_size} bytes ({final_size // (1024*1024)} MB)")
    print(f"    SHA-256: {h}")
    return h

def main():
    parser = argparse.ArgumentParser(description="Lupin Bare-Metal OS v3 Boot Builder")
    parser.add_argument("--boot", help="Imagem boot original base", required=True)
    parser.add_argument("--guardian", help="Binário estático lupinos_guard", default="src/guardian/lupinos_guard")
    parser.add_argument("--out", help="Imagem assinada final v3", default="lupin_baremetal_star_v3.img")
    args = parser.parse_args()
    
    with tempfile.TemporaryDirectory() as work_dir:
        kernel_file, ramdisk_file, cmdline = extract_kernel_and_ramdisk(args.boot, work_dir)
        new_ramdisk = inject_guardian_and_repack(ramdisk_file, args.guardian, work_dir)
        assemble_and_sign_boot_v3(kernel_file, new_ramdisk, args.out, cmdline)

if __name__ == "__main__":
    main()
