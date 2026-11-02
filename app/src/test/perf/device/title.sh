#!/system/bin/sh
# Prints "title" while Sky's title screen shows its Play button, else "no".
# Checks two pixels: the white play triangle and the dark button around it.
f=${1:-/data/local/tmp/perfdiag/s.raw}
screencap "$f" || { echo no; exit; }
set -- $(od -An -tu4 -N8 "$f")
w=$1
h=$2
[ "$w" -gt "$h" ] || { echo no; exit; }
px() {
    od -An -tu1 -j $((16 + ($2 * w + $1) * 4)) -N3 "$f" | awk '{ print int(($1 + $2 + $3) / 3) }'
}
tri=$(px $((w * 5017 / 10000)) $((h * 4796 / 10000)))
btn=$(px $((w * 4750 / 10000)) $((h * 5222 / 10000)))
echo "lum tri=$tri btn=$btn" >&2
if [ "$tri" -gt 190 ] && [ "$btn" -lt 90 ]; then echo title; else echo no; fi
