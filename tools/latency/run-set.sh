#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: run-set.sh <tag> : 2 steady counts and 2 marker latency runs
for r in 1 2; do ./run-steady.sh $1s$r a0000000-0000-4000-8000-000000000001 | tail -1; ./run-e2e.sh $1m$r 55555555-5555-4555-8555-555555555555 2>&1 | tail -1 | cut -c1-175; done
adb shell am force-stop $ATHENA; pkill -x probe-app
