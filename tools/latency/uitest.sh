#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# Normal Android UI with blur and GPU composition: shade and recents, then HWC and GPU misses.
m() { adb shell dumpsys SurfaceFlinger | grep -E "HWC missed|GPU missed" | tr -dc '0-9\n' | tr '\n' ' '; }
a=$(m); for i in 1 2 3 4 5 6 7 8; do adb shell cmd statusbar expand-settings; sleep 0.8; adb shell input swipe 900 800 900 300 300; sleep 0.3; adb shell cmd statusbar collapse; sleep 0.6; adb shell input keyevent KEYCODE_APP_SWITCH; sleep 0.8; adb shell input keyevent KEYCODE_HOME; sleep 0.6; done; b=$(m); echo "UI test HWC/GPU missed before: $a after: $b"
