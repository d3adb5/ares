#!/usr/bin/python3 -I
# usage: e2e.py <label> <host-present.log> <sf-latency-dump> <device-host offset ns>
# A skipped host frame shows up as a gap on the panel; the gaps' random spacing fixes the alignment.
import sys
label,hp,sf,off=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4])
host=[int(l.split()[1]) for l in open(hp) if len(l.split())==2]
rows=set()
for l in open(sf):
    p=l.split()
    if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: rows.add((int(p[0]),int(p[1]),int(p[2])))
rows=sorted(rows,key=lambda r:r[1])
pres=[r[1]-off for r in rows]
hg=[b for a,b in zip(host,host[1:]) if 33e6<b-a<60e6]
# Only gaps seen inside continuous dumps count: a hole between dumps looks like a long gap.
cg=[b for a,b in zip(pres,pres[1:]) if 33e6<b-a<60e6]
import bisect
def near(L):
    out=[]
    for c in cg:
        x=c-L; i=bisect.bisect_left(hg,x)
        best=min((abs(hg[j]-x) for j in (i-1,i) if 0<=j<len(hg)),default=1e18)
        if best<5e6: out.append(x)
    return len(out)
best=max(range(0,120000000,250000),key=near)
lat=[]
for c in cg:
    x=c-best; i=bisect.bisect_left(hg,x)
    cand=[j for j in (i-1,i) if 0<=j<len(hg)]
    if cand:
        j=min(cand,key=lambda j:abs(hg[j]-x))
        if abs(hg[j]-x)<8e6: lat.append((c-hg[j])/1e6)
lat.sort()
rel=sorted((r[1]-r[0])/1e6 for r in rows); acq=sorted((r[0]-r[2])/1e6 for r in rows)
q=lambda v,p:v[min(int(len(v)*p),len(v)-1)]
span=(pres[-1]-pres[0])/1e9
print(f"{label:<9} client {len(rows)/span:5.1f}fps host {len(host)/((host[-1]-host[0])/1e9):5.1f}fps gaps host={len(hg)} client={len(cg)} matched={len(lat)} | present->panel ms p10={q(lat,.1):6.2f} p50={q(lat,.5):6.2f} p90={q(lat,.9):6.2f} mean={sum(lat)/len(lat):6.2f} | release->present p50={q(rel,.5):5.2f}")
