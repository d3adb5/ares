#!/bin/bash
# usage: vsynccount.sh <seconds> <label>: hardware vsync counter of the DSI interface against the device clock, and that clock against the host's.
. "$(dirname "$(readlink -f "$0")")/env.sh"; N=$1
O1=$(python3 -I clockoff.py | cut -d' ' -f1); T1=$(date +%s%N)
$RSH <<X | tr -d '\r'
f=\$(grep -l "intf:1 .*mode: command" /sys/kernel/debug/dri/0/encoder*/status | head -1)
a=\$(grep -o "intf:1 *vsync: *[0-9]*" \$f | grep -o "[0-9]*\$"); u1=\$(cut -d' ' -f1 /proc/uptime)
sleep $N
b=\$(grep -o "intf:1 *vsync: *[0-9]*" \$f | grep -o "[0-9]*\$"); u2=\$(cut -d' ' -f1 /proc/uptime)
echo "$2 file \$f delta \$((b-a)) uptime \$u1 \$u2 under \$(grep -o 'underrun: *[0-9]*' \$f | head -1)"
X
T2=$(date +%s%N); O2=$(python3 -I clockoff.py | cut -d' ' -f1); echo "device-host clock drift: $(python3 -I -c "print(round(($O2-($O1))/(($T2-$T1)/1e9)/1e3,1),'ppm')")"
