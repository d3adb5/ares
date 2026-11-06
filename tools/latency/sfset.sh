#!/bin/bash
# usage: sfset.sh <percent at 120 Hz> | off : bind an edited offsets table, restart SF and wait for Android.
. "$(dirname "$(readlink -f "$0")")/env.sh"; P=$1
if [ "$P" = off ]; then
$RSH >/dev/null <<X
umount /vendor/etc/display/advanced_sf_offsets.xml 2>/dev/null
setprop ctl.restart surfaceflinger
X
else
$RSH >/dev/null <<X
umount /vendor/etc/display/advanced_sf_offsets.xml 2>/dev/null
cp /vendor/etc/display/advanced_sf_offsets.xml /data/local/tmp/sfo.xml
V=\$(getprop vendor.display.target.version)
sed -i "/Device version=\"\$V\"/,/<\/Device>/s/fps=\"120\" SfDurationPercentage=\"[0-9]*\"/fps=\"120\" SfDurationPercentage=\"$P\"/" /data/local/tmp/sfo.xml
chmod 644 /data/local/tmp/sfo.xml
mount --bind /data/local/tmp/sfo.xml /vendor/etc/display/advanced_sf_offsets.xml
setprop ctl.restart surfaceflinger
X
fi
sleep 8
for i in $(seq 1 60); do
  F=$(adb shell dumpsys window 2>/dev/null | grep -m1 mCurrentFocus | tr -d '\r'); case "$F" in *launcher*|*Launcher*) break;; esac; sleep 2
done
sleep 12; adb shell input keyevent KEYCODE_WAKEUP; adb shell dumpsys SurfaceFlinger | grep -m1 'SF duration' | cut -c1-90; adb shell dumpsys window | grep -m1 mCurrentFocus | cut -c1-90; adb shell cat /vendor/etc/display/advanced_sf_offsets.xml | grep 'fps="120"' | cut -c1-70
