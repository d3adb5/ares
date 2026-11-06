#!/bin/bash
# usage: run-steady.sh <label> <uuid> ; panel vs host frame counts over 14 s
. "$(dirname "$(readlink -f "$0")")/env.sh"; L=$1; U=$2
adb shell am force-stop $ATHENA; pkill -x probe-app; sleep 3; : > host-present.log
adb shell am start -n $ATHENA/com.limelight.ShortcutTrampoline --es Name "$HOST_NAME" --es AppUUID $U >/dev/null
for i in $(seq 1 40); do sleep 1; N=$(stream_layer); [ -n "$N" ] && [ -s host-present.log ] && break; done; sleep 5
: > sf-$L.txt; for i in $(seq 1 22); do adb shell "dumpsys SurfaceFlinger --latency '$N'" >> sf-$L.txt; sleep 0.4; done
cp host-present.log hp-$L.log; python3 -I metric.py 2>&1 | cut -c1-90; python3 -I gaps.py $L hp-$L.log sf-$L.txt
adb logcat -d -t 2000 2>/dev/null | grep -E "Limelight|Game|MediaCodec.*(Dropped|drop)" | grep -iE "frames|drop" | tail -2 | cut -c1-120 >/dev/null
adb shell am force-stop $ATHENA; pkill -x probe-app
