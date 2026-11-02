# Fonts

`4x6.bdf` is X.org's `misc-fixed` 4x6 font (ISO 10646 build), from https://gitlab.freedesktop.org/xorg/font/misc-misc. It was contributed by Janne V. Kujala and is in the public domain ("Public domain font. Share and enjoy.", per its COPYRIGHT property).

`tools/gen_font.py` turns it, plus hand-drawn UI symbols, into the app's small font (`resources/smallfont.txt`, registered as app font 0x80).

`5x8.bdf` is the same family's 5x8 font (ISO 10646 build, also public domain per its COPYRIGHT property), converted from XQuartz's `/opt/X11/share/fonts/misc/5x8.pcf.gz` with `tools/pcf2bdf.py`. It's the larger font behind the Text size option (`resources/largefont.nfnt`, the same app font ID, swapped at runtime).
