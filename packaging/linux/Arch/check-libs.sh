#!/bin/sh
# Report what Ares can no longer link against: a library a system update removed
# (soname bump) or a symbol it dropped. pacman runs it after every library change
# (ares.hook); exits 1 when broken.
#
#   check-libs.sh [bindir] [libdir]
bindir=${1:-/usr/bin}
libdir=${2:-/usr/lib/ares}
elf=$(printf '\177ELF')
broken=$(for file in "$bindir"/ares "$bindir"/ares-* "$libdir"/*.so; do
  [ -f "$file" ] && [ "$(head -c4 "$file")" = "$elf" ] || continue
  out=$(ldd -r "$file" 2>&1)
  # A missing library also leaves all its symbols undefined: name only the library then.
  missing=$(printf '%s\n' "$out" | sed -n 's/^[[:space:]]*\([^[:space:]]*\) => not found.*/\1/p')
  [ -n "$missing" ] && echo "$missing" || printf '%s\n' "$out" | sed -n 's/^undefined symbol: \([^[:space:]]*\).*/\1/p'
done | sort -u)
[ -z "$broken" ] && exit 0
echo "==> WARNING: ares must be rebuilt or updated, it misses:" $broken
echo "    Rebuild it from its checkout (makepkg -fsi at the repo root) or install a newer package."
exit 1
