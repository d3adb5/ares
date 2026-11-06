#!/bin/bash
# usage: headless.sh <load> : median game fps with nothing streaming
. "$(dirname "$(readlink -f "$0")")/env.sh"; echo $1 > load.n; : > hl.log
env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" timeout 14 "$GAMESCOPE" --backend headless --prefer-vk-device 1002:744c -W 1920 -H 1080 -r 120 -- ./probe-load -240 mailbox hl.log > gs-hl.log 2>&1 &
sleep 5; B=$(cat /sys/class/drm/card1/device/gpu_busy_percent); sleep 2; B2=$(cat /sys/class/drm/card1/device/gpu_busy_percent); wait
python3 -I - <<'P'
h=[int(l.split()[1]) for l in open('hl.log') if len(l.split())==2]
g=sorted((b-a)/1e6 for a,b in zip(h,h[1:]))
print(f"headless load frames={len(h)} median frame {g[len(g)//2]:.2f} ms = {1000/g[len(g)//2]:.1f} fps p95 {g[int(len(g)*.95)]:.2f}")
P
echo "gpu busy $B $B2"
