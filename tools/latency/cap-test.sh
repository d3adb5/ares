#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
S=c0000000-0000-4000-8000-000000000001
for fps in 120 60; do ./setfps.sh $fps; for r in 1 2; do ./run-sparse.sh cap-$fps-$r $S 2>&1 | tail -1 | cut -c1-95; done; done; ./setfps.sh 120
adb shell am force-stop $ATHENA; pkill -x probe-app; sleep 3
