#!/usr/bin/python3 -I
import os,sys
for L in sys.argv[1:]:
    t=set()
    for l in open(f'sf-{L}.txt'):
        p=l.split()
        if len(p)==3 and p[0].isdigit() and 0<int(p[1])<2**62: t.add(int(p[1]))
    t=sorted(t);P=int(os.environ.get('PANEL_PERIOD_NS',8353500))
    g=[round((b-a)/P) for a,b in zip(t,t[1:])]
    v=sum(x for x in g if x<=3)
    print(f"  {L}: shown {sum(1 for x in g if x<=3)/v*100:.1f}% of vsyncs, doubled {sum(1 for x in g if x==2)}, tripled {sum(1 for x in g if x==3)}")
