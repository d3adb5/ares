#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: cell.sh <tag> <uuid> <fps> <mode>: one steady count
./setfps.sh $3; ./setpace.sh $4; H=$(bash -c "./run-steady.sh $1 $2" 2>&1 | head -1 | cut -d' ' -f2); printf "%-22s %s | " "$1" "$H"; python3 -I shown.py $1 | cut -d: -f2-
