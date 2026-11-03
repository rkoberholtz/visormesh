#!/bin/sh
# Renders the app icon BMPs in resources/icon/ from an SVG or PNG (needs
# ImageMagick, and rsvg-convert for SVG: brew install librsvg imagemagick). The BMPs are committed, so
# the build itself doesn't need these tools.
#
#   tools/make_icon.sh [icon.svg|icon.png]
#
# Large icon 32x22 and small (list view) 15x9, each at 1 bit (threshold 85%: keeps line art's thin strokes),
# 4 bits (16 grays: the Visor Neo) and 8 bits (colour; pilrc maps it to the
# Palm palette). White is transparent.
set -e
cd "$(dirname "$0")/.."
SRC="${1:-../meshcore-visor-hair-1.png}"
OUT=resources/icon
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

case "$SRC" in
*.svg) rsvg-convert -w 512 -h 512 "$SRC" -o "$TMP/full.png" ;;
*)     magick "$SRC" -background white -alpha remove "$TMP/full.png" ;;
esac
magick "$TMP/full.png" -trim +repage "$TMP/trim.png"

for spec in large:32x22 small:15x9; do
	name=${spec%%:*}
	size=${spec#*:}
	magick "$TMP/trim.png" -resize "$size" -background white -gravity center -extent "$size" \
		-alpha remove "$TMP/$name.png"
	magick "$TMP/$name.png" -colorspace Gray -threshold 85% -type Bilevel -compress None "BMP3:$OUT/${name}1.bmp"
	magick "$TMP/$name.png" -colorspace Gray -dither None -colors 16 -type Palette -compress None "BMP3:$OUT/${name}4.bmp"
	magick "$TMP/$name.png" -dither None -colors 64 -type Palette -compress None "BMP3:$OUT/${name}8.bmp"
done
ls -l "$OUT"
