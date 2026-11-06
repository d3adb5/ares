#!/usr/bin/python3 -I
# CPU seconds per second over 10 s for host processes of the session, by name.
import os,time
def snap():
    out={}
    for pid in filter(str.isdigit,os.listdir('/proc')):
        try:
            s=open(f'/proc/{pid}/stat').read(); name=s[s.index('(')+1:s.rindex(')')]; f=s[s.rindex(')')+2:].split()
            if name.startswith(('sunshine','gamescope','Xwayland','probe-app','gamescope-wl')): out[(pid,name)]=int(f[11])+int(f[12])
        except Exception: pass
    return out
a=snap();t=time.time();time.sleep(10);b=snap();dt=time.time()-t
tot={}
for k,v in b.items():
    tot[k[1]]=tot.get(k[1],0)+(v-a.get(k,v))/100/dt*100
print(' '.join(f"{n}={v:.1f}%" for n,v in sorted(tot.items())))
