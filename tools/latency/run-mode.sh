#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: run-mode.sh <tag> : steady count + markers x2, with HWC misses
m() { adb shell dumpsys SurfaceFlinger | grep -m1 "HWC missed" | tr -dc 0-9; }
for r in 1 2; do a=$(m); ./run-steady.sh $1s$r a0000000-0000-4000-8000-000000000001 | tail -1; b=$(m); ./run-e2e.sh $1m$r 55555555-5555-4555-8555-555555555555 2>&1 | tail -1 | cut -c1-175; c=$(m); echo "   HWC missed: steady +$((b-a)) markers +$((c-b))"; done
adb shell am force-stop $ATHENA; pkill -x probe-app
