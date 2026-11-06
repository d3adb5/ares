#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
s=0; for i in $(seq 1 20); do s=$((s+$(cat /sys/class/drm/card1/device/gpu_busy_percent))); sleep 0.5; done; echo "gpu=$((s/20))%"
