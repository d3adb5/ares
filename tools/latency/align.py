#!/usr/bin/python3 -I
# usage: align.py <label> <host log> <sf dump> <offset ns> [gap_min_ms]
# Per-marker latency: panel present of the frame after a gap minus the app's present of that frame.
import sys,bisect
L,hp,sf,off=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4])
host=[int(l.split()[1]) for l in open(hp) if len(l.split())==2]
rs=set()
for l in open(sf):
    p=l.split()
    if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: rs.add((int(p[1]),int(p[0]),int(p[2])))
rs=sorted(rs); rows=[r[0] for r in rs]
hg=[b for a,b in zip(host,host[1:]) if 33e6<b-a<60e6]
cg=[b for a,b in zip(rows,rows[1:]) if 33e6<b-a<60e6]
best=(0,0)
for c in cg:
    for h in hg:
        d=c-h
        n=0
        for x in cg:
            k=bisect.bisect_left(hg,x-d)
            if any(0<=m<len(hg) and abs(hg[m]-(x-d))<6e6 for m in (k-1,k)): n+=1
        if n>best[0]: best=(n,d)
n,d=best
ds=[]
for x in cg:
    k=bisect.bisect_left(hg,x-d)
    m=[j for j in (k-1,k) if 0<=j<len(hg)]
    if m:
        j=min(m,key=lambda j:abs(hg[j]-(x-d)))
        if abs(hg[j]-(x-d))<8e6: ds.append((x-off-hg[j])/1e6)
ds.sort()
rel=sorted((r[0]-r[1])/1e6 for r in rs)
span=(rows[-1]-rows[0])/1e9
q=lambda v,p:v[min(int(len(v)*p),len(v)-1)]
print(f"{L:<9} client {len(rows)/span:5.1f}fps host {len(host)/((host[-1]-host[0])/1e9):5.1f}fps markers {len(ds)}/{len(cg)} | present->panel ms p10={q(ds,.1):6.2f} p50={q(ds,.5):6.2f} p90={q(ds,.9):6.2f} mean={sum(ds)/len(ds):6.2f} | release->present p50={q(rel,.5):5.2f}")
