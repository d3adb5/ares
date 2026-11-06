#!/bin/bash
# usage: run-sparse.sh <label>: 50 s, a marker every 3-6 s. Prints latency of the frame before and after each pause.
. "$(dirname "$(readlink -f "$0")")/env.sh"; L=$1
adb shell am force-stop $ATHENA; pkill -x probe-app; sleep 3; : > host-present.log
adb shell am start -n $ATHENA/com.limelight.ShortcutTrampoline --es Name "$HOST_NAME" --es AppUUID ${2:-a0000000-0000-4000-8000-000000000006} >/dev/null
for i in $(seq 1 40); do sleep 1; N=$(adb shell dumpsys SurfaceFlinger --list 2>/dev/null | grep -F '(BLAST)' | grep -i 'SurfaceView.*athena' | head -1 | tr -d '\r'); [ -n "$N" ] && [ -s host-present.log ] && break; done; sleep 4
O1=$(python3 -I clockoff.py | cut -d' ' -f1)
: > sf-$L.txt; for i in $(seq 1 85); do adb shell "dumpsys SurfaceFlinger --latency '$N'" >> sf-$L.txt; sleep 0.45; done
O2=$(python3 -I clockoff.py | cut -d' ' -f1); cp host-present.log hp-$L.log
python3 -I sparse.py $L $(( (O1+O2)/2 )); adb shell am force-stop $ATHENA; pkill -x probe-app
