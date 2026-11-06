#!/usr/bin/python3 -I
# usage: gaps.py <label> <host log> <sf dump>: frames the panel presented vs the host's, by vsync gap.
import sys,collections
L,hp,sf=sys.argv[1:4]
host=[int(l.split()[1]) for l in open(hp) if len(l.split())==2]
rs=set()
for l in open(sf):
    p=l.split()
    if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: rs.add((int(p[1]),int(p[0]),int(p[2])))
rs=sorted(rs); t=[r[0] for r in rs]
# only gaps inside one dump's reach; a real hole between dumps would be > 1 s
g=[b-a for a,b in zip(t,t[1:])]
c=collections.Counter(round(x/8333333) for x in g)
span=(t[-1]-t[0])/1e9
h=[x for x in host if t[0]-0<=x-0 or True]
hspan=(host[-1]-host[-200])/1e9
hfps=199/hspan
rel=sorted((r[0]-r[1])/1e6 for r in rs)
print(f"{L:<12} host {hfps:6.1f} fps | panel {len(t)/span:6.1f} fps = {100*len(t)/span/hfps:5.1f}% | vsync gaps 1:{c[1]} 2:{c[2]} 3:{c[3]} 4+:{sum(v for k,v in c.items() if k>3)} 0:{c[0]} | release->present p50 {rel[len(rel)//2]:.2f}")
