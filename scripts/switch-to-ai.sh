#!/bin/sh
# 1. Encerra o display virtual e streaming de jogos
echo "[+] Finalizando ambiente de jogos no Debian..."
killall -9 sunshine Xvfb wine box64 2>/dev/null || true
pkill -9 -f sunshine 2>/dev/null || true
pkill -9 -f Xvfb 2>/dev/null || true
chroot /data/debian /bin/bash -c "pkill -9 -f sunshine; pkill -9 -f Xvfb; pkill -9 -f wine; pkill -9 -f box64" 2>/dev/null || true
sync && echo 3 > /proc/sys/vm/drop_caches

# 2. Retorna a CPU e GPU para gerenciamento dinâmico
echo "[+] Retornando clocks para modo econômico/dinâmico..."
for i in 0 1 2 3 4 5 6 7; do
  echo schedutil > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor 2>/dev/null || true
done
echo 1 > /sys/class/kgsl/kgsl-3d0/min_pwrlevel 2>/dev/null || true
echo msm-adreno-tz > /sys/class/kgsl/kgsl-3d0/devfreq/governor 2>/dev/null || true
echo 0 > /sys/class/kgsl/kgsl-3d0/force_clk_on 2>/dev/null || true

# 3. Reinicia os motores de IA no Alpine
echo "[+] Religando serviços do Cavalo de Fogo (Alpine)..."
chroot /data/alpine /bin/sh -c "
  export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin
  pgrep sshd >/dev/null || /usr/sbin/sshd
  /root/watchdog.sh </dev/null >/dev/null 2>&1 &
" 2>/dev/null || true

echo "[✓] Modo Servidor de IA RESTAURADO."
