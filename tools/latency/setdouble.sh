#!/bin/bash
# usage: setdouble.sh true|false : Athena's "Limit vsync to double buffering"
. "$(dirname "$(readlink -f "$0")")/env.sh"; $RSH >/dev/null <<X
am force-stop $ATHENA
f=/data/data/$ATHENA/shared_prefs/${ATHENA}_preferences.xml
sed -i '/name="checkbox_vsync_double_buffer"/d' \$f
sed -i 's#</map>#    <boolean name="checkbox_vsync_double_buffer" value="$1" />\n</map>#' \$f
X
