#!/bin/bash
# During a running stream: hardware vsync counter and present fences over the same window.
. "$(dirname "$(readlink -f "$0")")/env.sh"; N=$(adb shell dumpsys SurfaceFlinger --list 2>/dev/null | grep -F '(BLAST)' | grep -i 'SurfaceView.*athena' | head -1 | tr -d '\r')
S=$($RSH <<X | tr -d '\r'
f=\$(grep -l "intf:1 .*mode: command" /sys/kernel/debug/dri/0/encoder*/status | head -1)
echo \$(grep -o "intf:1 *vsync: *[0-9]*" \$f | grep -o "[0-9]*\$") \$(date +%s%N)
X
)
: > pr.txt
for i in $(seq 1 ${1:-50}); do adb shell "dumpsys SurfaceFlinger --latency '$N'" >> pr.txt; sleep 0.45; done
E=$($RSH <<X | tr -d '\r'
f=\$(grep -l "intf:1 .*mode: command" /sys/kernel/debug/dri/0/encoder*/status | head -1)
echo \$(grep -o "intf:1 *vsync: *[0-9]*" \$f | grep -o "[0-9]*\$") \$(date +%s%N) \$(cat /sys/class/thermal/thermal_zone0/temp)
X
)
python3 -I - "$S" "$E" <<'P'
import sys,statistics as st
a=sys.argv[1].split();b=sys.argv[2].split()
hw=(int(b[0])-int(a[0]))/((int(b[1])-int(a[1]))/1e9)
t=set()
for l in open('pr.txt'):
    p=l.split()
    if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: t.add(int(p[1]))
t=sorted(t); g=[y-x for x,y in zip(t,t[1:]) if y-x<4e7]; m=st.median([x for x in g if x<1.1e7])
n=sum(round(x/m) for x in g); sp=sum(g)
print(f"hardware counter {hw:.3f} Hz | present fences {1e9*n/sp:.3f} Hz | window {(int(b[1])-int(a[1]))/1e9:.0f}s | temp {int(b[2])/1000:.1f}C")
P
