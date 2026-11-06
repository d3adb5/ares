import sys
h=[int(l.split()[1]) for l in open(f'hp-{sys.argv[1]}.log') if len(l.split())==2]
g=sorted((b-a)/1e6 for a,b in zip(h,h[1:]) if b-a<80e6)
print(f"   game frame median {g[len(g)//2]:.2f} ms = {1000/g[len(g)//2]:.1f} fps, p95 {g[int(len(g)*.95)]:.2f}, p99 {g[int(len(g)*.99)]:.2f}")
