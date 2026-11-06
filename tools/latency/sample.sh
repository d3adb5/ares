#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: sample.sh <seconds> : battery draw, CPU and GPU with charging paused; charging is restored on the way out.
$RSH <<X | tr -d '\r' | python3 -I sample.py $1
B=/sys/class/power_supply/battery; Q=/sys/class/qcom-battery
trap 'echo 0 > \$Q/restrict_chg' EXIT
echo 1 > \$Q/restrict_chg; sleep 6
stat1(){ for p in \$(pidof surfaceflinger) \$(pidof vendor.qti.hardware.display.composer-service) \$(pidof $ATHENA); do awk '{print \$14+\$15}' /proc/\$p/stat; done | tr '\n' ' '; echo; }
echo "A \$(head -1 /proc/stat | cut -d' ' -f3-)"; echo "PA \$(stat1)"
i=0; while [ \$i -lt $(( $1 * 2 )) ]; do echo "S \$(cat \$B/current_now) \$(cat \$B/voltage_now)"; sleep 0.5; i=\$((i+1)); done
echo "Z \$(head -1 /proc/stat | cut -d' ' -f3-)"; echo "PZ \$(stat1)"
echo "G \$(cat /sys/class/kgsl/kgsl-3d0/gpu_busy_percentage | tr -dc 0-9) \$(cat /sys/class/thermal/thermal_zone0/temp)"
echo 0 > \$Q/restrict_chg
X
