#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
./setpace.sh keep; ./setfps.sh 120
for load in 200 300; do echo $load > load.n; for r in 1 2; do
  for v in "LF3 c0000000-0000-4000-8000-000000000004" "LF2 c0000000-0000-4000-8000-000000000005"; do
    set -- $v; L=$1-$load-$r; ./run-sparse.sh $L $2 2>&1 | tail -1 | cut -c1-90; python3 -I fps.py $L; done; done; done
