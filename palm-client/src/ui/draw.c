#include <PalmOS.h>
#include "../resources.h"
#include "draw.h"
#include "text.h"

/* In the second code segment: see sections.h for the rules. */


static MemHandle gFontH = 0;
UInt16 gUiFontHeight = VM_SMALL_FONT_HEIGHT;

Err UiFontInit(Boolean large)
{
	if (gFontH)
		return errNone;
	gFontH = DmGetResource('NFNT', large ? LargeFontRes : SmallFontRes);
	if (!gFontH)
		return DmGetLastErr();
	gUiFontHeight = large ? VM_LARGE_FONT_HEIGHT : VM_SMALL_FONT_HEIGHT;
	gTxtLarge = large;
	return FntDefineFont(vmSmallFont, (FontPtr)MemHandleLock(gFontH));
}

/* FntDefineFont replaces the font at that ID; the old resource is released after. */
Err UiFontSetLarge(Boolean large)
{
	MemHandle old = gFontH;
	Err err;

	gFontH = 0;
	err = UiFontInit(large);
	if (err != errNone) {
		gFontH = old;
		return err;
	}
	if (old) {
		MemHandleUnlock(old);
		DmReleaseResource(old);
	}
	return errNone;
}

void UiFontRelease(void)
{
	if (gFontH) {
		MemHandleUnlock(gFontH);
		DmReleaseResource(gFontH);
		gFontH = 0;
	}
}

void DrawSection(const Char *label, Coord x0, Coord y, Coord width)
{
	Char rule = GLYPH_RULE;
	FontID old = FntSetFont(vmSmallFont);
	Coord ruleW = FntCharWidth(rule);
	Coord labelW = FntCharsWidth(label, StrLen(label));
	Coord x = x0;
	UInt16 i;

	for (i = 0; i < 2; i++, x += ruleW)
		WinDrawChars(&rule, 1, x, y);
	x += 2;
	WinDrawChars(label, StrLen(label), x, y);
	for (x += labelW + 2; x + ruleW <= x0 + width; x += ruleW)
		WinDrawChars(&rule, 1, x, y);
	FntSetFont(old);
}

/* 2 px of black either side and 1 px above and below: inverted 4x6 text smears with less. */
#define CHIP_PAD_X 2
#define CHIP_PAD_Y 1

Coord ChipWidth(const Char *text)
{
	return FntCharsWidth(text, StrLen(text)) + 2 * CHIP_PAD_X;
}

Coord DrawChip(const Char *text, Coord x, Coord y)
{
	UInt16 len = StrLen(text);
	Coord w = FntCharsWidth(text, len) + 2 * CHIP_PAD_X;
	RectangleType r;

	RctSetRectangle(&r, x, y, w, FntLineHeight() + 2 * CHIP_PAD_Y);
	WinDrawRectangle(&r, 0);
	WinDrawInvertedChars(text, len, x + CHIP_PAD_X, y + CHIP_PAD_Y);
	return w;
}

Coord DrawCut(const Char *text, Coord x, Coord y, Coord maxW)
{
	Int16 w = maxW;
	Int16 len = StrLen(text);
	Boolean fits;
	Char ell = GLYPH_ELLIPSIS;

	if (FntCharsWidth(text, len) <= maxW) {
		WinDrawChars(text, len, x, y);
		return FntCharsWidth(text, len);
	}
	w = maxW - FntCharWidth(ell);
	FntCharsInWidth(text, &w, &len, &fits);
	WinDrawChars(text, len, x, y);
	WinDrawChars(&ell, 1, x + w, y);
	return w + FntCharWidth(ell);
}

Coord DrawRight(const Char *text, Coord xRight, Coord y)
{
	UInt16 len = StrLen(text);
	Coord x = xRight - FntCharsWidth(text, len);

	WinDrawChars(text, len, x, y);
	return x;
}
