#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: powercell.sh <label> <fps> <mode>: stream steady pan content, then sample
./setfps.sh $2; ./setpace.sh $3; adb shell am force-stop $ATHENA; pkill -x probe-app; sleep 3
adb shell am start -n $ATHENA/com.limelight.ShortcutTrampoline --es Name "$HOST_NAME" --es AppUUID a0000000-0000-4000-8000-000000000001 >/dev/null; sleep 12
printf "%-22s " "$1"; ./sample.sh 40 | cut -c1-130; adb shell am force-stop $ATHENA; pkill -x probe-app; sleep 1
