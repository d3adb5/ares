#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: matrix.sh <app uuid> <tag> <rounds> <fps...>: steady panel counts per host rate and pacing mode
U=$1; TAG=$2; R=$3; shift 3
for r in $(seq 1 $R); do for fps in "$@"; do for mode in latency keep; do
  ./setfps.sh $fps; ./setpace.sh $mode; L=$TAG-$fps-$mode-$r
  H=$(bash -c "./run-steady.sh $L $U" 2>&1 | head -1 | cut -d' ' -f2-3); printf "%-28s %s | " "$L" "$H"; python3 -I shown.py $L | cut -c3-; done; done; done
