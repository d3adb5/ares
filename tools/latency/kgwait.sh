#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"
# Waits until the device is past the lock screen, up to 10 minutes.
for i in $(seq 1 300); do
  S=$(adb shell dumpsys window 2>/dev/null | grep -c 'isKeyguardShowing=true'); F=$(adb shell dumpsys window 2>/dev/null | grep -m1 mCurrentFocus)
  case "$F" in *NotificationShade*|*Keyguard*) ;; *) [ "$S" = 0 ] && { echo unlocked; exit 0; };; esac; sleep 2
done; echo still-locked; exit 1
