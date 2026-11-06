#!/bin/bash
# usage: run-e2e.sh <label> <app uuid>
. "$(dirname "$(readlink -f "$0")")/env.sh"; L=$1; U=$2
adb shell am force-stop $ATHENA; pkill -x probe-app; pkill -x probe-noise; pkill -x probe-pan; pkill -x probe-load; sleep 3
: > host-present.log
adb shell am start -n $ATHENA/com.limelight.ShortcutTrampoline --es Name "$HOST_NAME" --es AppUUID $U >/dev/null
for i in $(seq 1 40); do sleep 1; N=$(adb shell dumpsys SurfaceFlinger --list 2>/dev/null | grep -F '(BLAST)' | grep -i 'SurfaceView.*athena' | head -1 | tr -d '\r'); [ -n "$N" ] && [ -s host-present.log ] && break; done
echo "layer: $N"; sleep 4
O1=$(python3 -I clockoff.py | cut -d' ' -f1)
: > sf-$L.txt; for i in $(seq 1 24); do adb shell "dumpsys SurfaceFlinger --latency '$N'" >> sf-$L.txt; sleep 0.5; done
O2=$(python3 -I clockoff.py | cut -d' ' -f1); echo "offsets $O1 $O2"
cp host-present.log hp-$L.log
adb exec-out screencap -p > shot-$L.png
python3 -I align.py $L hp-$L.log sf-$L.txt $(( (O1+O2)/2 ))
adb shell am force-stop $ATHENA

