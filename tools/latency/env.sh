# Sourced first by every script: runs from this directory, with the settings below (see README.md).
cd "$(dirname "$(readlink -f "$0")")" || exit 1
ATHENA=${ATHENA:-local.retroid.athena}
HOST_NAME=${HOST_NAME:-$(uname -n)}
RSH=${RSH:-rsh}
GAMESCOPE=${GAMESCOPE:-gamescope}
# The stream's layer, as --latency wants it. Android 14+ lists it as RequestedLayerState{<name> parentId=...}.
stream_layer() { adb shell dumpsys SurfaceFlinger --list 2>/dev/null | grep -F '(BLAST)' | grep -i 'SurfaceView.*athena' | head -1 | tr -d '\r' | sed -E 's/^RequestedLayerState\{//; s/ (parentId|relativeParentId|z)=.*//; s/\}$//'; }
