#!/usr/bin/env python3
"""
tools/build_os_v4.py - Lupin Bare-Metal OS v4 Sovereign Builder & Signer
Erradica completamente o TWRP e interfaces de terceiros da imagem de boot.
Substitui a recuperação pelo Guardião Nativo e pelo HUD Traseiro Soberano.
Gera imagem assinada com AVB 2.0 (192 MB exatos) para o Slot B.
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

def purge_twrp_and_inject_sovereign(ramdisk_gz_path, guardian_bin, hud_bin, work_dir):
    print("[*] Desempacotando ramdisk para purga completa do TWRP e injeção soberana...")
    ramdisk_root = os.path.join(work_dir, "ramdisk_root")
    os.makedirs(ramdisk_root, exist_ok=True)
    
    # 1. Extrai o cpio.gz
    subprocess.check_call(f"zcat '{ramdisk_gz_path}' | cpio -idm 2>/dev/null", shell=True, cwd=ramdisk_root)
    
    # 2. PURGA TOTAL: Arranca pastas e recursos do TWRP
    twrp_purge_targets = [
        os.path.join(ramdisk_root, "twres"),
        os.path.join(ramdisk_root, "system", "bin", "recovery"),
    ]
    for target in twrp_purge_targets:
        if os.path.islink(target) or os.path.isfile(target):
            os.remove(target)
            print(f"[x] Removido arquivo legado: {os.path.basename(target)}")
        elif os.path.isdir(target):
            shutil.rmtree(target)
            print(f"[x] Purga de diretório legado: {os.path.basename(target)}")

    # 3. Injeta binários nativos em /sbin
    sbin_dir = os.path.join(ramdisk_root, "sbin")
    os.makedirs(sbin_dir, exist_ok=True)
    
    dest_guard = os.path.join(sbin_dir, "lupinos_guard")
    shutil.copy2(guardian_bin, dest_guard)
    os.chmod(dest_guard, 0o755)
    print(f"[+] Guardião nativo soberano: {dest_guard}")
    
    if hud_bin and os.path.exists(hud_bin):
        dest_hud = os.path.join(sbin_dir, "lupin_subdisplay_hud")
        shutil.copy2(hud_bin, dest_hud)
        os.chmod(dest_hud, 0o755)
        print(f"[+] HUD AMOLED traseiro nativo: {dest_hud}")

    # 4. Substitui /system/bin/recovery por symlink para lupinos_guard
    sys_bin = os.path.join(ramdisk_root, "system", "bin")
    os.makedirs(sys_bin, exist_ok=True)
    rec_symlink = os.path.join(sys_bin, "recovery")
    if os.path.exists(rec_symlink) or os.path.islink(rec_symlink):
        os.remove(rec_symlink)
    os.symlink("/sbin/lupinos_guard", rec_symlink)
    print(f"[+] /system/bin/recovery redirecionado soberanamente para /sbin/lupinos_guard")

    # 5. Reescreve init.recovery.service.rc para boot 100% LupinOS
    service_rc = os.path.join(ramdisk_root, "init.recovery.service.rc")
    sovereign_rc_content = """# ==============================================================================
# Lupin Bare-Metal OS v4 - Sovereign System Init
# 100% Código Nativo da Matilha Lupin (Zero TWRP / Zero Bloat)
# ==============================================================================

on boot
    # 0. Verifica contador de falhas persistente para rollback
    start lupin_check_rollback

    # 1. Aciona o Guardião Mestre do LupinOS
    start lupinos_guard

    # 2. Aciona o Driver do Display Traseiro AMOLED (HUD Silício Puro)
    start lupin_hud

    # 3. Dispara a carga de ambiente autônomo (SSH, Redis, Qwen 14B)
    start cavalo_ai_fallback

    # 4. Confirma boot bem-sucedido (mark-success) e zera falhas
    start lupin_mark_success

# Guardião de Silício (ZRAM 12GB, Thermal Watchdog, WakeLock, DSP)
service lupinos_guard /sbin/lupinos_guard
    user root
    group root
    seclabel u:r:recovery:s0
    oneshot

# HUD AMOLED Traseiro 126x294 (DRM_MASTER soberano)
service lupin_hud /sbin/lupin_subdisplay_hud
    user root
    group root
    seclabel u:r:recovery:s0

# Valida falhas consecutivas antes do user-space
service lupin_check_rollback /sbin/lupin_slot_ctl check-rollback
    user root
    group root
    seclabel u:r:recovery:s0
    oneshot

# Bootstrap de Raiz Linux (Alpine/Debian Chroot + Llama + Net)
service cavalo_ai_fallback /system/bin/sh /data/start_chroot_auto.sh
    user root
    group root
    seclabel u:r:recovery:s0
    oneshot

# Marco zero-regressão, limpa as falhas se deu sucesso
service lupin_mark_success /sbin/lupin_slot_ctl mark-success
    user root
    group root
    seclabel u:r:recovery:s0
    oneshot
"""
    with open(service_rc, "w", encoding="utf-8") as f:
        f.write(sovereign_rc_content)
    print("[+] init.recovery.service.rc reescrito para Soberania Total")

    # 6. Permissões de root na raiz do ramdisk
    os.chmod(ramdisk_root, 0o755)

    # 7. Reempacota ramdisk_patched.cpio.gz garantindo root:root
    out_ramdisk = os.path.join(work_dir, "ramdisk_sovereign_v4.cpio.gz")
    cmd = f"find . | cpio -o -H newc -R 0:0 2>/dev/null | gzip -9 > '{out_ramdisk}'"
    subprocess.check_call(cmd, shell=True, cwd=ramdisk_root)
    print(f"[+] Ramdisk v4 Soberano reempacotado: {os.path.getsize(out_ramdisk)} bytes")
    return out_ramdisk

def assemble_and_sign_boot_v4(kernel_path, ramdisk_path, out_img_path, cmdline=b"twrpfastboot=1\x00"):
    print("[*] Montando imagem Boot v4 Soberana...")
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
        
    print(f"[+] Boot v4 bruto gerado: {os.path.getsize(out_img_path)} bytes")
    
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
    print(f"[✓] SUCESSO! Imagem LupinOS v4 Soberana pronta:")
    print(f"    Arquivo: {out_img_path}")
    print(f"    Tamanho: {final_size} bytes ({final_size // (1024*1024)} MB)")
    print(f"    SHA-256: {h}")
    return h

def main():
    parser = argparse.ArgumentParser(description="Lupin Bare-Metal OS v4 Sovereign Builder")
    parser.add_argument("--boot", help="Imagem boot original base", required=True)
    parser.add_argument("--guardian", help="Binário lupinos_guard", default="src/guardian/lupinos_guard")
    parser.add_argument("--hud", help="Binário lupin_subdisplay_hud", default="bin/lupin_subdisplay_hud")
    parser.add_argument("--out", help="Imagem assinada final v4", default="lupin_baremetal_star_v4_sovereign.img")
    args = parser.parse_args()
    
    with tempfile.TemporaryDirectory() as work_dir:
        kernel_file, ramdisk_file, cmdline = extract_kernel_and_ramdisk(args.boot, work_dir)
        new_ramdisk = purge_twrp_and_inject_sovereign(ramdisk_file, args.guardian, args.hud, work_dir)
        assemble_and_sign_boot_v4(kernel_file, new_ramdisk, args.out, cmdline)

if __name__ == "__main__":
    main()
