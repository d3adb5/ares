# Sourced first by every script: runs from this directory, with the settings below (see README.md).
cd "$(dirname "$(readlink -f "$0")")" || exit 1
ATHENA=${ATHENA:-local.retroid.athena}
HOST_NAME=${HOST_NAME:-$(uname -n)}
RSH=${RSH:-rsh}
GAMESCOPE=${GAMESCOPE:-gamescope}
