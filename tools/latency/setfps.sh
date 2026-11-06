#!/bin/bash
. "$(dirname "$(readlink -f "$0")")/env.sh"; $RSH >/dev/null <<X
am force-stop $ATHENA
sed -i 's#<string name="list_fps">[0-9.]*</string>#<string name="list_fps">$1</string>#' /data/data/$ATHENA/shared_prefs/${ATHENA}_preferences.xml
X
