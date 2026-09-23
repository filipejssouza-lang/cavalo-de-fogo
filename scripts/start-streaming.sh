#!/bin/bash
# Executar dentro do chroot Debian
export XDG_RUNTIME_DIR=/tmp
export WLR_BACKENDS=headless
export WLR_LIBINPUT_NO_DEVICES=1
export WAYLAND_DISPLAY=wayland-1

pkill -f sway
pkill -f sunshine

# Inicia o Sway Headless (Resolução 1920x1080)
nohup sway --unsupported-gpu > /tmp/sway.log 2>&1 &
sleep 2

# Inicia o Sunshine apontando para o display Wayland gerado
nohup sunshine > /tmp/sunshine.log 2>&1 &
