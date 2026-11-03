#!/bin/sh
# Renders the app icon BMPs in resources/icon/ (needs ImageMagick, and
# rsvg-convert for SVG: brew install librsvg imagemagick). The BMPs are committed,
# so the build itself doesn't need these tools.
#
#   tools/make_icon.sh                      # from ../meshcore-visor-hair-1.png
#   tools/make_icon.sh icon.svg|icon.png    # any big source, resized down
#   tools/make_icon.sh --art large.png small.png
#                                           # pixel art already drawn at device
#                                           # size: never resampled, only centred
#
# Large icon 32x22 and small (list view) 15x9, each at 1 bit, 4 bits (16 grays:
# the Visor Neo) and 8 bits (colour; pilrc maps it to the Palm palette). White
# (255) is the transparency key -- keep reversed art at most 238.
#
# Downscaling a 512px render to 22px is what the plain mode does, and it turns
# into gray mush; --art takes a 22x22 drawing made for the screen instead.
set -e
cd "$(dirname "$0")/.."
OUT=resources/icon
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# threshold for the 1-bit fallback: art is pre-tuned (hair 204 / outline 102), a
# big colour source needs a higher cut to keep thin strokes
MONO_ART=75%
MONO_SRC=85%

# fail when art is bigger than the frame instead of silently shrinking it
check_fits() {
	set -- "$1" "$2" "$3" $(magick identify -format '%w %h' "$3")
	if [ "$4" -gt "$1" ] || [ "$5" -gt "$2" ]; then
		echo "$3 is ${4}x${5}; the frame is $1x$2 -- draw it at that size or smaller" >&2
		exit 2
	fi
}

if [ "$1" = "--art" ]; then
	[ -n "$2" ] && [ -n "$3" ] || { echo "usage: $0 --art large.png small.png" >&2; exit 2; }
	LARGE=$2
	SMALL=$3
	# refuse art that would have to be resampled: that is the bug this mode exists to avoid
	check_fits 32 22 "$LARGE"
	check_fits 15 9 "$SMALL"
	magick "$LARGE" -background white -alpha remove -gravity center -extent 32x22 \
		-alpha off "$TMP/large.png"
	magick "$SMALL" -background white -alpha remove -gravity center -extent 15x9 \
		-alpha off "$TMP/small.png"
	MONO=$MONO_ART
	MODE=art
else
	SRC="${1:-../meshcore-visor-hair-1.png}"
	case "$SRC" in
	*.svg) rsvg-convert -w 512 -h 512 "$SRC" -o "$TMP/full.png" ;;
	*)     magick "$SRC" -background white -alpha remove "$TMP/full.png" ;;
	esac
	magick "$TMP/full.png" -trim +repage "$TMP/trim.png"
	for spec in large:32x22 small:15x9; do
		name=${spec%%:*}
		size=${spec#*:}
		magick "$TMP/trim.png" -resize "$size" -background white -gravity center \
			-extent "$size" -alpha remove "$TMP/$name.png"
	done
	MONO=$MONO_SRC
	MODE=source
fi

# A 256-entry gray palette: with 16 or fewer colours in the art the BMP writer
# would otherwise drop the 8-bit icon to 4 bits (a duplicate of large4/small4)
# would otherwise drop the "8-bit" icon to 4 bits (same file as ${name}4.bmp)
magick -size 256x1 gradient:'#000000-#FFFFFF' -colorspace Gray "$TMP/pal.png"

for name in large small; do
	# 8-bit first, from the true gray levels, so the palette isn't built from
	# an already-quantised image
	if [ "$MODE" = art ]; then
		magick "$TMP/$name.png" -colorspace Gray -dither None -remap "$TMP/pal.png" \
			-type Palette -compress None "BMP3:$OUT/${name}8.bmp"
	else
		magick "$TMP/$name.png" -dither None -colors 64 -type Palette -compress None \
			"BMP3:$OUT/${name}8.bmp"
	fi
	magick "$TMP/$name.png" -colorspace Gray -dither None -colors 16 -type Palette -compress None \
		"BMP3:$OUT/${name}4.bmp"
	magick "$TMP/$name.png" -colorspace Gray -threshold "$MONO" -type Bilevel -compress None \
		"BMP3:$OUT/${name}1.bmp"
done
magick identify "$OUT"/large?.bmp "$OUT"/small?.bmp
