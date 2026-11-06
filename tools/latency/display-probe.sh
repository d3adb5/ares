#!/bin/bash
# Read-only: what this handheld's display and SurfaceFlinger do. Changes nothing. With one
# device attached and RSH (README.md), while something animates on it (a stream, or scrolling), or the rates cannot be measured.
# usage: display-probe.sh [seconds, default 30]
N=${1:-30}
"${RSH:-rsh}" <<X | tr -d '\r'
g() { getprop "\$1"; }
echo "device: \$(g ro.product.manufacturer) \$(g ro.product.model), \$(g ro.soc.model) (\$(g ro.board.platform)), Android \$(g ro.build.version.release), \$(g ro.build.display.id)"
echo "panel modes: \$(cat /sys/class/drm/card0-DSI-1/modes 2>/dev/null | tr '\n' ' ')"
echo "active mode: \$(dumpsys SurfaceFlinger 2>/dev/null | grep -m1 'refresh-rate' | tr -s ' ')"
dumpsys display 2>/dev/null | grep -m1 mActiveSfDisplayMode | grep -oE 'refreshRate=[0-9.]+|presentationDeadlineNanos=[0-9]+' | tr '\n' ' '; echo
T=\$(g vendor.display.target.version); X=/vendor/etc/display/advanced_sf_offsets.xml
echo "lead table: file \$([ -f \$X ] && echo present || echo absent), target version \${T:-none}, advanced offsets \$(g debug.sf.enable_advanced_sf_phase_offset)"
[ -f \$X ] && [ -n "\$T" ] && sed -n "/Device version=\"\$T\"/,/<\/Device>/p" \$X | grep FpsOffsetMap | tr -s ' '
echo "lead props: late.sf \$(g debug.sf.late.sf.duration) early.sf \$(g debug.sf.early.sf.duration) earlyGl.sf \$(g debug.sf.earlyGl.sf.duration) durations \$(g debug.sf.use_phase_offsets_as_durations)"
dumpsys SurfaceFlinger 2>/dev/null | grep -E 'app duration|present offset' | head -2 | tr -s ' '
echo "bind-mounted table: \$(grep -c advanced_sf_offsets /proc/mounts)"
f=\$(grep -lE 'mode: (command|video)' /sys/kernel/debug/dri/0/encoder*/status 2>/dev/null | while read e; do grep -qE 'vsync: *[1-9]' "\$e" && echo "\$e"; done | tail -1)
c() { [ -n "\$f" ] && grep -E 'mode: (command|video)' "\$f" | grep -oE 'vsync: *[0-9]+' | grep -oE '[0-9]+\$' | sort -n | tail -1; }
echo "vsync counter: \${f:-not found} \$(grep -oE 'mode: [a-z]+' "\$f" 2>/dev/null | head -1)"
a=\$(c); t1=\$(date +%s%N); m1=\$(dumpsys SurfaceFlinger 2>/dev/null | grep -E 'HWC missed|GPU missed' | tr -dc '0-9\n' | tr '\n' ' ')
sleep $N
b=\$(c); t2=\$(date +%s%N); m2=\$(dumpsys SurfaceFlinger 2>/dev/null | grep -E 'HWC missed|GPU missed' | tr -dc '0-9\n' | tr '\n' ' ')
[ -n "\$a" ] && echo "hardware vsyncs: \$((b-a)) in \$(awk "BEGIN{printf \\"%.0f\\", (\$t2-\$t1)/1e6}") ms"
echo "SF missed, HWC then GPU: \$m1-> \$m2"
dumpsys SurfaceFlinger 2>/dev/null | grep 'For ideal period' | head -2 | tr -s '\t ' ' '
echo "temperature: \$(cat /sys/class/thermal/thermal_zone0/temp)"
X
