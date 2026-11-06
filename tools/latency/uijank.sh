#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# usage: uijank.sh <label>: shade and recents, 3 rounds; SF misses and what apps saw (gfxinfo).
m() { adb shell dumpsys SurfaceFlinger | grep -E "HWC missed|GPU missed" | tr -dc '0-9\n' | tr '\n' ' '; }
J() { adb shell dumpsys gfxinfo $1 | grep -E "Total frames rendered|Janky frames:|90th percentile|Number Missed Vsync" | head -4 | tr -s ' ' | sed 's/Total frames rendered: /frames /;s/Janky frames: /janky /;s/90th percentile: /p90 /;s/Number Missed Vsync: /missedVsync /' | tr '\n' ' '; }
adb shell dumpsys gfxinfo com.android.systemui reset >/dev/null; adb shell dumpsys gfxinfo com.android.launcher3 reset >/dev/null; a=$(m)
for r in 1 2 3; do for i in 1 2 3 4 5 6 7 8; do adb shell cmd statusbar expand-settings; sleep 0.8; adb shell input swipe 900 800 900 300 300; sleep 0.3; adb shell cmd statusbar collapse; sleep 0.6; adb shell input keyevent KEYCODE_APP_SWITCH; sleep 0.8; adb shell input keyevent KEYCODE_HOME; sleep 0.6; done; done
b=$(m); echo "$1: SF HWC/GPU missed $a -> $b"; echo "  systemui: $(J com.android.systemui)"; echo "  launcher: $(J com.android.launcher3)"
