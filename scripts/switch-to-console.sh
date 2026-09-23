#!/bin/sh
# 1. Libera recursos de IA no Alpine
echo "[+] Suspendendo serviços de IA..."
chroot /data/alpine /bin/sh -c "pkill -f llama-server; pkill -f sd-cli" 2>/dev/null || true
sync && echo 3 > /proc/sys/vm/drop_caches

# 2. Crava CPU e GPU no talo (Aproveitando o Peltier)
echo "[+] Travando clocks no máximo (Modo Peltier Gaming)..."
for i in 4 5 6 7; do
  echo performance > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor 2>/dev/null || true
done
echo 0 > /sys/class/kgsl/kgsl-3d0/min_pwrlevel 2>/dev/null || true
echo performance > /sys/class/kgsl/kgsl-3d0/devfreq/governor 2>/dev/null || true
echo 1 > /sys/class/kgsl/kgsl-3d0/force_clk_on 2>/dev/null || true

# 3. Prepara o Debian e inicializa o display virtual + Sunshine
echo "[+] Subindo Modo Console no Debian..."
mkdir -p /data/debian/dev /data/debian/proc /data/debian/sys /data/debian/dev/pts /data/debian/dev/shm /data/debian/tmp /data/debian/data/local/tmp
mount -o bind /dev /data/debian/dev 2>/dev/null || true
mount -t devpts devpts /data/debian/dev/pts 2>/dev/null || true
mount -t proc proc /data/debian/proc 2>/dev/null || true
mount -t sysfs sys /data/debian/sys 2>/dev/null || true
mount -t tmpfs tmpfs /data/debian/dev/shm 2>/dev/null || true
chmod 1777 /data/debian/tmp /data/debian/data/local/tmp 2>/dev/null || true

# Inicia Xvfb e Sunshine desacoplados de sessão
chroot /data/debian /bin/bash -c "
  export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
  export TMPDIR=/tmp
  export DISPLAY=:0
  pkill -f Xvfb 2>/dev/null || true
  pkill -f sunshine 2>/dev/null || true
  nohup Xvfb :0 -screen 0 1280x720x24 -nocursor </dev/null >/tmp/xvfb.log 2>&1 &
  sleep 1
  nohup sunshine </dev/null >/tmp/sunshine.log 2>&1 &
  service ssh restart 2>/dev/null || nohup /usr/sbin/sshd </dev/null >/dev/null 2>&1 &
" </dev/null >/dev/null 2>&1 &

echo "[✓] Modo Console ATIVO! Painel Sunshine: https://localhost:47990"
