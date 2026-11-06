#!/bin/bash
# usage: run-game.sh <label> <uuid> <process name, exact case>: steady latency of a real game, with GAMESCOPE_WSI_PRESENT_LOG=<here>/host-present.log
# in its env. Freezes it for 100 ms every 3-6 s as markers: that shows on screen, so leave it at a menu or standing still.
. "$(dirname "$(readlink -f "$0")")/env.sh"; L=$1; U=$2; G=$3
adb shell am force-stop $ATHENA; sleep 3; : > host-present.log
adb shell am start -n $ATHENA/com.limelight.ShortcutTrampoline --es Name "$HOST_NAME" --es AppUUID $U >/dev/null
P=; N=
for i in $(seq 1 150); do sleep 2
  N=$(adb shell dumpsys SurfaceFlinger --list 2>/dev/null | grep -F '(BLAST)' | grep -i 'SurfaceView.*athena' | head -1 | tr -d '\r')
  P=$(pgrep -x "$G" | head -1)
  [ -n "$N" ] && [ -n "$P" ] && [ -s host-present.log ] && break
done
[ -z "$P" ] && { echo "$L: game did not start"; exit 1; }
sleep 25; echo "$P" > game.pid
O1=$(python3 -I clockoff.py | cut -d' ' -f1); : > sf-$L.txt
trap 'kill -CONT $P 2>/dev/null' EXIT
( e=$((SECONDS+75)); while [ $SECONDS -lt $e ]; do sleep $((3 + RANDOM % 4)); kill -STOP $P; sleep 0.1; kill -CONT $P; done ) &
for i in $(seq 1 170); do adb shell "dumpsys SurfaceFlinger --latency '$N'" >> sf-$L.txt; sleep 0.45; done
wait; O2=$(python3 -I clockoff.py | cut -d' ' -f1); cp host-present.log hp-$L.log
python3 -I sparse.py $L $(( (O1+O2)/2 ))
