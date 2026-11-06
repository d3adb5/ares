#!/bin/bash
# usage: setpace.sh latency|keep|balanced|smoothness|cap-fps
. "$(dirname "$(readlink -f "$0")")/env.sh"; $RSH >/dev/null <<X
am force-stop $ATHENA
sed -i 's#<string name="frame_pacing">[a-z0-9-]*</string>#<string name="frame_pacing">$1</string>#' /data/data/$ATHENA/shared_prefs/${ATHENA}_preferences.xml
X
