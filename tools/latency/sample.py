import sys
cur=[];volt=[];d={}
secs=int(sys.argv[1])
for l in sys.stdin:
    p=l.split()
    if not p: continue
    if p[0]=='S' and len(p)==3: cur.append(int(p[1]));volt.append(int(p[2]))
    elif p[0] in('A','Z','PA','PZ','G'): d[p[0]]=[int(x) for x in p[1:] if x.lstrip('-').isdigit()]
c=sum(cur)/len(cur)/1000; v=sum(volt)/len(volt)/1e6
a,z=d['A'],d['Z']; tot=sum(z)-sum(a); idle=(z[3]-a[3])+(z[4]-a[4])
pa=d.get('PA',[])+[0,0,0]; pz=d.get('PZ',[])+[0,0,0]; g=d.get('G',[0,0])+[0,0]
print(f"draw {c:6.0f} mA {c*v:6.0f} mW | CPU {100*(tot-idle)/tot:5.1f}% all cores | SF {(pz[0]-pa[0])/secs:5.1f}% hwc {(pz[1]-pa[1])/secs:5.1f}% athena {(pz[2]-pa[2])/secs:5.1f}% of a core | GPU {g[0]}% | {g[1]/1000:.1f}C")
