# Each stream frame is matched to the newest present before its fence signalled.
import sys,bisect
app=[int(l.split()[1]) for l in open(sys.argv[1]) if len(l.split())==2]
rows=[l.split() for l in open(sys.argv[2]) if len(l.split())==4]
lat=[];sub=[];gaps=[];last=None;seen=set()
for r in rows:
    ready=int(r[1]);submit=int(r[2])
    if last is not None:gaps.append((ready-last)/1e6)
    last=ready
    i=bisect.bisect_right(app,submit)-1
    if i<0 or i in seen:continue
    seen.add(i);lat.append((ready-app[i])/1e6);sub.append((submit-app[i])/1e6)
lat=sorted(lat[30:]);sub=sorted(sub[30:]);gaps=sorted(gaps[30:])
q=lambda v,p:v[min(int(len(v)*p),len(v)-1)]
print(f"{sys.argv[3]:<28} frames={len(rows):5d} matched={len(lat):5d} | present->composited ms p50={q(lat,.5):6.2f} p95={q(lat,.95):6.2f} p99={q(lat,.99):6.2f} max={lat[-1]:6.2f} | frame gap p50={q(gaps,.5):6.2f} p99={q(gaps,.99):6.2f}")
