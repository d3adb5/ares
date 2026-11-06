#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# Steady latency of a free-running vsync game by mitigation, at each stream rate.
./setpace.sh keep
for fps in 120 60; do ./setfps.sh $fps; for r in 1 2; do
  for v in "F3 c0000000-0000-4000-8000-000000000001" "F2 c0000000-0000-4000-8000-000000000002" "MB c0000000-0000-4000-8000-000000000003"; do
    set -- $v; ./run-sparse.sh $1-$fps-$r $2 2>&1 | tail -1 | cut -c1-125
  done; done; done
./setfps.sh 120
