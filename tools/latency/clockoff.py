#!/usr/bin/python3 -I
# Prints device_monotonic - host_monotonic in ns, from the lowest round trips over USB.
import subprocess,time
p=subprocess.Popen(['adb','exec-in','/data/local/tmp/ares-clk'],stdin=subprocess.PIPE,stdout=subprocess.PIPE) if False else subprocess.Popen(['adb','shell','/data/local/tmp/ares-clk'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,bufsize=0)
s=[]
for i in range(400):
    a=time.monotonic_ns(); p.stdin.write(b'x'); l=p.stdout.readline(); b=time.monotonic_ns()
    if not l.strip(): continue
    s.append((b-a,int(l)-(a+b)//2)); 
    p.stdout.readline() if False else None
    time.sleep(0.002)
p.kill()
s.sort(); best=s[:20]
print(int(sorted(x[1] for x in best)[10]), best[0][0], best[19][0])
