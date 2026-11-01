#include <PalmOS.h>
#include "../resources.h"
#include "draw.h"

static MemHandle gFontH = 0;

Err UiFontInit(void)
{
	if (gFontH)
		return errNone;
	gFontH = DmGetResource('NFNT', SmallFontRes);
	if (!gFontH)
		return DmGetLastErr();
	return FntDefineFont(vmSmallFont, (FontPtr)MemHandleLock(gFontH));
}

void UiFontRelease(void)
{
	if (gFontH) {
		MemHandleUnlock(gFontH);
		DmReleaseResource(gFontH);
		gFontH = 0;
	}
}

void DrawSection(const Char *label, Coord y)
{
	Char rule = GLYPH_RULE;
	FontID old = FntSetFont(vmSmallFont);
	Coord ruleW = FntCharWidth(rule);
	Coord labelW = FntCharsWidth(label, StrLen(label));
	Coord x = 0;
	UInt16 i;

	for (i = 0; i < 2; i++, x += ruleW)
		WinDrawChars(&rule, 1, x, y);
	x += 2;
	WinDrawChars(label, StrLen(label), x, y);
	for (x += labelW + 2; x + ruleW <= 160; x += ruleW)
		WinDrawChars(&rule, 1, x, y);
	FntSetFont(old);
}

Coord DrawChip(const Char *text, Coord x, Coord y)
{
	UInt16 len = StrLen(text);
	Coord w = FntCharsWidth(text, len) + 2;
	RectangleType r;

	RctSetRectangle(&r, x, y, w, FntLineHeight());
	WinDrawRectangle(&r, 0);
	WinDrawInvertedChars(text, len, x + 1, y);
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
