#!/usr/bin/python3 -I
# Steady latency: the last frame before each pause, on both sides. Host pauses are 100 ms apart from 3-6 s of steady frames.
import sys,bisect,statistics as st
L,off=sys.argv[1],int(sys.argv[2])
host=[int(l.split()[1]) for l in open(f'hp-{L}.log') if len(l.split())==2]
rows=set()
for l in open(f'sf-{L}.txt'):
    p=l.split()
    if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: rows.add(int(p[1]))
rows=sorted(rows)
hp=[host[i-1] for i in range(1,len(host)) if 40e6<host[i]-host[i-1]<140e6]
cp=[];last=0
for i in range(1,len(rows)):
    if 30e6<rows[i]-rows[i-1]<140e6:
        if rows[i-1]-last>500e6: cp.append(rows[i-1])
        last=rows[i-1]
lat=[]
for c in cp:
    t=c-off; k=bisect.bisect_left(hp,t)
    m=[j for j in (k-1,k) if 0<=j<len(hp) and 0<t-hp[j]<80e6]
    if m: lat.append((t-hp[min(m,key=lambda j:t-hp[j])])/1e6)
g=[round((b-a)/8351000) for a,b in zip(rows,rows[1:])]; v=sum(y for y in g if y<=3)
s=f"mean {st.mean(lat):5.2f} min {min(lat):5.2f} p90 {sorted(lat)[len(lat)*9//10]:5.2f}" if lat else "none"
print(f"{L:<10} n={len(lat):3d} steady latency {s} | shown {sum(1 for y in g if y<=3)/v*100:.1f}% doubled {sum(1 for y in g if y==2)} of {len(rows)}")
