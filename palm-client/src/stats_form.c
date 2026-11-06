/*
 * Radio Stats: what the companion radio reports about itself (battery,
 * storage, uptime, queue, last signal, airtime, packet counts) and a graph
 * of its noise floor, read every few seconds while the screen is open. The
 * noise samples are kept for the session, so the graph carries on when the
 * screen is opened again.
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "protocol2.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "stats_form.h"

static void Read(void) VM_SECT5;
static void FormatSecs(Char *out, UInt32 s) VM_SECT5;
static void DrawText(Coord y, const Char *s) VM_SECT5;
static void DrawGraph(const RectangleType *g) VM_SECT5;
static void Draw(void) VM_SECT5;

#define STATS_EVERY  (5 * SysTicksPerSecond())
#define NOISE_MAX    60      /* samples: 5 minutes */
#define GRAPH_BOTTOM 143     /* just above Done */
#define GRAPH_MIN_H  16

static VmStats   gStats;
static VmBattery gBatt;
static Boolean   gHasBatt;
static Err       gErr;
static UInt32    gReadAt;
static Int16     gNoise[NOISE_MAX];
static UInt16    gNoiseCount = 0, gNoiseNext = 0;

static void Read(void)
{
	UInt8 frame[VM_HEADER_LEN + 2], reply[32];
	UInt16 len, got, t;
	Err err;

	gReadAt = TimGetTicks();
	MemSet(&gStats, sizeof(gStats), 0);
	gErr = errNone;
	if (!SessionIsConnected()) {
		gErr = serErrNotOpen;
		return;
	}
	len = VmBuildCode(frame, sizeof(frame), VM_CMD_GET_BATT_AND_STORAGE);
	gHasBatt = SessionQuery(frame, len, VM_RESP_BATT_AND_STORAGE, reply, sizeof(reply), &got) == errNone &&
	           VmParseBattery(reply, got, &gBatt);
	for (t = VM_STATS_CORE; t <= VM_STATS_PACKETS; t++) {
		len = VmBuildGetStats(frame, sizeof(frame), (UInt8)t);
		err = SessionQuery(frame, len, VM_RESP_STATS, reply, sizeof(reply), &got);
		if (err == errNone)
			VmParseStats(reply, got, &gStats);
		else
			gErr = err;
	}
	if (gStats.hasRadio) {
		gNoise[gNoiseNext] = gStats.noiseFloor;
		gNoiseNext = (gNoiseNext + 1) % NOISE_MAX;
		if (gNoiseCount < NOISE_MAX)
			gNoiseCount++;
	}
}

/* "45s", "12m", "3h 20m", "2d 4h" */
static void FormatSecs(Char *out, UInt32 s)
{
	if (s < 60)
		StrPrintF(out, "%lus", s);
	else if (s < 3600)
		StrPrintF(out, "%lum", s / 60);
	else if (s < 86400UL)
		StrPrintF(out, "%luh %lum", s / 3600, s / 60 % 60);
	else
		StrPrintF(out, "%lud %luh", s / 86400UL, s / 3600 % 24);
}

static void DrawText(Coord y, const Char *s)
{
	DrawCut(s, 3, y, 154);    /* never past the edge, whatever the font */
}

/* The noise floor samples, oldest at the left; the scale fits what's there, at least 10 dB. */
static void DrawGraph(const RectangleType *g)
{
	Int16 lo = 0, hi = 0, v, i, k;
	Coord x, y, w = g->extent.x, h = g->extent.y;
	Char s[12];

	for (i = 0; i < (Int16)gNoiseCount; i++) {
		v = gNoise[i];
		if (i == 0 || v < lo)
			lo = v;
		if (i == 0 || v > hi)
			hi = v;
	}
	if (hi - lo < 10) {
		lo = (lo + hi) / 2 - 5;
		hi = lo + 10;
	}
	WinDrawGrayLine(g->topLeft.x, g->topLeft.y + h - 1, g->topLeft.x + w - 1, g->topLeft.y + h - 1);
	WinDrawGrayLine(g->topLeft.x, g->topLeft.y, g->topLeft.x + w - 1, g->topLeft.y);
	StrPrintF(s, "%d", hi);
	DrawRight(s, g->topLeft.x + w + 22, g->topLeft.y - 4);
	StrPrintF(s, "%d", lo);
	DrawRight(s, g->topLeft.x + w + 22, g->topLeft.y + h - 7);
	for (i = 0; i < (Int16)gNoiseCount; i++) {
		k = (Int16)((gNoiseNext + NOISE_MAX - gNoiseCount + i) % NOISE_MAX);
		x = g->topLeft.x + w - ((Int16)gNoiseCount - i) * (w / NOISE_MAX);
		y = g->topLeft.y + h - 1 - (Coord)((Int32)(gNoise[k] - lo) * (h - 1) / (hi - lo));
		WinDrawLine(x, y, x, g->topLeft.y + h - 1);
		if (w / NOISE_MAX > 1)
			WinDrawLine(x + 1, y, x + 1, g->topLeft.y + h - 1);
	}
}

static void Draw(void)
{
	FontID old = FntSetFont(vmSmallFont);
	RectangleType r;
	Char line[48], a[16], b[16], snr[8];
	Coord y = 18, step = VM_ROW_H;

	RctSetRectangle(&r, 0, 16, 160, 127);
	WinEraseRectangle(&r, 0);
	if (gErr == serErrNotOpen) {
		DrawText(y, "Connect to the radio first (Node).");
		FntSetFont(old);
		return;
	}
	if (gHasBatt) {
		StrPrintF(line, "Battery %u.%02u V", gBatt.battMv / 1000, (gBatt.battMv % 1000) / 10);
		DrawText(y, line);
		y += step;
		if (gBatt.hasStorage) {
			StrPrintF(line, "Storage %lu of %lu KB used", gBatt.usedKb, gBatt.totalKb);
			DrawText(y, line);
			y += step;
		}
	}
	if (gStats.hasCore) {
		FormatSecs(a, gStats.uptimeSecs);
		StrPrintF(line, "Up %s, %u queued", a, gStats.queueLen);
		if (gStats.errFlags)
			StrPrintF(line + StrLen(line), ", errors %x", gStats.errFlags);
		DrawText(y, line);
		y += step;
	}
	if (gStats.hasRadio) {
		StrPrintF(line, "Noise floor %d dBm", gStats.noiseFloor);
		DrawText(y, line);
		y += step;
		TxtFormatSnr(snr, gStats.lastSnrQ);
		StrPrintF(line, "Last heard %d dBm, %s", (Int16)gStats.lastRssi, snr);
		DrawText(y, line);
		y += step;
		FormatSecs(a, gStats.txAirSecs);
		FormatSecs(b, gStats.rxAirSecs);
		StrPrintF(line, "Airtime: sent %s, heard %s", a, b);
		DrawText(y, line);
		y += step;
	}
	if (gStats.hasPackets) {
		StrPrintF(line, "Heard %lu: %lu flood, %lu direct", gStats.recv, gStats.recvFlood, gStats.recvDirect);
		DrawText(y, line);
		y += step;
		StrPrintF(line, "Sent %lu: %lu flood, %lu direct", gStats.sent, gStats.sentFlood, gStats.sentDirect);
		DrawText(y, line);
		y += step;
	}
	if (!gStats.hasCore && !gStats.hasRadio && !gStats.hasPackets) {
		DrawText(y, gErr == vmErrRadio ? "This firmware doesn't report stats." : "The radio didn't answer.");
		FntSetFont(old);
		return;
	}
	/* the graph takes what the lines leave, which depends on the font */
	y += 2;
	if (GRAPH_BOTTOM - (y + 12) < GRAPH_MIN_H) {
		FntSetFont(old);
		return;
	}
	DrawSection("noise floor, dBm, last 5 min", 0, y, 160);
	RctSetRectangle(&r, 3, y + 12, 130, GRAPH_BOTTOM - (y + 12));
	if (gNoiseCount)
		DrawGraph(&r);
	FntSetFont(old);
}

void StatsFormIdle(void)
{
	if (TimGetTicks() - gReadAt >= STATS_EVERY) {
		Read();
		Draw();
	}
}

Boolean StatsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		FrmDrawForm(form);
		Read();
		Draw();
		return true;
	case frmUpdateEvent:
		/* new messages and contacts don't change what's shown: the next read does */
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			FrmDrawForm(form);
			Draw();
		}
		return true;
	case ctlSelectEvent:
		if (event->data.ctlSelect.controlID == StatsDoneButton) {
			FrmGotoForm(MainForm);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
