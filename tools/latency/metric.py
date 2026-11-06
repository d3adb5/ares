import json,os,ssl,sys,urllib.request
t=open(os.environ['XDG_RUNTIME_DIR']+'/hermes/local-api.token').read().strip()
c=ssl._create_unverified_context()
r=urllib.request.urlopen(urllib.request.Request('https://localhost:47990/api/metrics',headers={'Authorization':'Bearer '+t}),context=c,timeout=3)
p=json.load(r).get('pipeline') or {}
print(f"host fps={p.get('fps',0):.1f} Mbit={p.get('bitrate_kbps',0)/1000:.1f} encode={p.get('encode_ms',0):.2f}ms cap->enc={p.get('capture_to_encode_ms',0):.2f}ms dropped={p.get('frames_dropped')}")
