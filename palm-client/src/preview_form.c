/*
 * Font Preview (Options menu): sample screens in the small font, to judge how
 * readable it is on the Visor's LCD before the real screens are built on it.
 */
#include <PalmOS.h>
#include "resources.h"
#include "ui/draw.h"
#include "preview_form.h"

/* In the fourth code segment: see sections.h for the rules. */
static void Row(Coord y, Char mark, const Char *name, const Char *badge, const Char *age, const Char *preview) VM_SECT4;
static void DrawPreview(void) VM_SECT4;

#define TOP 16

static void Row(Coord y, Char mark, const Char *name, const Char *badge, const Char *age, const Char *preview)
{
	Coord ageX = 88, previewX = 106;

	WinDrawChars(&mark, 1, 1, y);
	DrawCut(name, 8, y, 52);
	if (badge)
		DrawRight(badge, 80, y);
	if (age)
		DrawRight(age, previewX - 3, y);
	(void)ageX;
	if (preview)
		DrawCut(preview, previewX, y, 160 - previewX);
}

static void DrawPreview(void)
{
	Char line[48];
	Coord y = TOP;
	FontID old = FntSetFont(vmSmallFont);

	DrawSection("Channels", 0, y, 160);                                     y += VM_ROW_H;
	StrPrintF(line, "%c2", GLYPH_COMPANION);
	Row(y, '@', "Public", line, "5m", "Steve: spare antenna?"); y += VM_ROW_H;
	Row(y, '#', "phillymesh", NULL, "1h", "ok see you there");       y += VM_ROW_H;
	DrawSection("Direct", 0, y, 160);                                       y += VM_ROW_H;
	StrPrintF(line, "%c", GLYPH_FAILED);
	Row(y, GLYPH_COMPANION, "Rich K.", line, "23h", "test from the visor"); y += VM_ROW_H;
	Row(y, GLYPH_REPEATER, "Hilltop Repeater", NULL, "2d", NULL);   y += VM_ROW_H;
	Row(y, GLYPH_ROOM, "Rittenhouse Room", NULL, "3d", NULL);       y += VM_ROW_H;
	Row(y, GLYPH_SENSOR, "Weather Sensor", NULL, NULL, NULL);       y += VM_ROW_H;

	DrawSection("Thu Nov 1", 0, y, 160);                                    y += VM_ROW_H + 1;
	DrawChip("Steve K.", 1, y);                                     y += VM_CHIP_H + 1;
	WinDrawChars("11:30", 5, 4, y);
	WinDrawChars("I'll bring a spare antenna", 26, 26, y);         y += VM_ROW_H;
	WinDrawChars("for anyone who needs one", 24, 26, y);      y += VM_ROW_H + 1;
	StrPrintF(line, "%c VisorNeo", GLYPH_SELF);
	DrawChip(line, 1, y);                                           y += VM_CHIP_H + 1;
	WinDrawChars("11:33", 5, 4, y);
	StrPrintF(line, "on my way, caf%c at 3  %c", (Char)0xE9, GLYPH_ACKED);
	WinDrawChars(line, StrLen(line), 26, y);                        y += VM_ROW_H + 2;

	StrPrintF(line, "%c%c%c%c%c%c%c%c%c%c%c ABCxyz 0123 %c",
	          GLYPH_SELF, GLYPH_COMPANION, GLYPH_REPEATER, GLYPH_ROOM, GLYPH_SENSOR, GLYPH_UNKNOWN,
	          GLYPH_ACKED, GLYPH_FAILED, GLYPH_PENDING, GLYPH_LOCK, GLYPH_UP, GLYPH_BACK);
	WinDrawChars(line, StrLen(line), 1, y);
	FntSetFont(old);
}

Boolean FontPreviewFormHandleEvent(EventType *event)
{
	switch (event->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		DrawPreview();
		return true;
	case ctlSelectEvent:
		if (event->data.ctlSelect.controlID == FontPreviewDoneButton) {
			FrmGotoForm(MainForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
