/*
 * Heard Packets: the last packets the radio received (its RX log pushes),
 * newest first, with what kind, the route, the signal and who it was from or
 * came through. All, Chat (direct and channel messages) or Adverts. A tap
 * shows one in full. Kept in memory while the app runs.
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "contacts.h"
#include "info_fmt.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "ui/rowlist.h"
#include "packets_form.h"

static Boolean Shown(const VmRxLog *rx) VM_SECT5;
static void    Filter(void) VM_SECT5;
static const Char *TypeName(UInt8 type) VM_SECT5;
static void    DrawRow(UInt16 row, const RectangleType *b, void *ctx) VM_SECT5;
static void    Details(UInt16 row) VM_SECT5;
static void    Reload(Boolean draw) VM_SECT5;

#define PKT_MAX 40

#define SHOW_ALL     0
#define SHOW_CHAT    1
#define SHOW_ADVERTS 2

typedef struct {
	UInt32  time;
	VmRxLog rx;
} Packet;

static Packet  gPkts[PKT_MAX];
static UInt16  gCount = 0, gNext = 0;
static UInt32  gNoted = 0;          /* bumped per packet, so an open screen knows to reload */
static UInt32  gShownNoted;
static UInt8   gShow = SHOW_ALL;
static UInt8   gRows[PKT_MAX];      /* shown rows: indexes into gPkts, newest first */
static UInt16  gRowCount;
static RowList gList;

void PacketsNote(const VmRxLog *rx, UInt32 now)
{
	gPkts[gNext].time = now;
	gPkts[gNext].rx = *rx;
	gNext = (gNext + 1) % PKT_MAX;
	if (gCount < PKT_MAX)
		gCount++;
	gNoted++;
}

static Boolean Shown(const VmRxLog *rx)
{
	switch (gShow) {
	case SHOW_CHAT:    return rx->payloadType == VM_PAYLOAD_TXT || rx->payloadType == VM_PAYLOAD_GRP_TXT;
	case SHOW_ADVERTS: return rx->payloadType == VM_PAYLOAD_ADVERT;
	}
	return true;
}

static void Filter(void)
{
	UInt16 i, k;

	gRowCount = 0;
	for (i = 0; i < gCount; i++) {
		k = (gNext + PKT_MAX - 1 - i) % PKT_MAX;
		if (Shown(&gPkts[k].rx))
			gRows[gRowCount++] = (UInt8)k;
	}
}

/* MeshCore's payload types (Packet.h). A switch, not a table: sections.h. */
static const Char *TypeName(UInt8 type)
{
	switch (type) {
	case 0:  return "Request";
	case 1:  return "Response";
	case 2:  return "DM";
	case 3:  return "Ack";
	case 4:  return "Advert";
	case 5:  return "Channel";
	case 6:  return "Ch data";
	case 7:  return "Anon req";
	case 8:  return "Path";
	case 9:  return "Trace";
	case 10: return "Multipart";
	case 11: return "Control";
	case 15: return "Raw";
	}
	return "Other";
}

/* "17:32:05 Advert  3 hops  6.2 dB" and who it's from or came through. */
static void DrawRow(UInt16 row, const RectangleType *b, void *ctx)
{
	const Packet *p = &gPkts[gRows[row]];
	const VmRxLog *rx = &p->rx;
	FontID old = FntSetFont(vmSmallFont);
	Coord x = b->topLeft.x + 1, y = b->topLeft.y, right = b->topLeft.x + b->extent.x - 1;
	DateTimeType dt;
	VmContact c;
	Char line[48], snr[8];
	Coord left;

	TimSecondsToDateTime(SessionLocalPalm(p->time), &dt);
	StrPrintF(line, "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
	WinDrawChars(line, StrLen(line), x, y);
	WinDrawChars(TypeName(rx->payloadType), StrLen(TypeName(rx->payloadType)), x + 34, y);
	if (!rx->flood)
		StrCopy(line, "direct");
	else
		StrPrintF(line, "%u hop%s", rx->hops, rx->hops == 1 ? "" : "s");
	WinDrawChars(line, StrLen(line), x + 72, y);
	TxtFormatSnr(snr, (Int8)rx->snrQ);
	left = DrawRight(snr, right, y);
	/* an advert's node, else the last relay, by name if we know it */
	line[0] = 0;
	if (rx->isAdvert && ContactsFindByPrefixN(rx->advertKey, 6, &c))
		StrCopy(line, c.name);
	else if (rx->fromLen && rx->fromLen <= 3)
		InfoHop(line, sizeof(line), rx->from, rx->fromLen);
	if (line[0])
		DrawCut(line, x + 104, y, left - 3 - (x + 104));
	FntSetFont(old);
}

static void Details(UInt16 row)
{
	const Packet *p = &gPkts[gRows[row]];
	const VmRxLog *rx = &p->rx;
	VmContact c;
	Char text[240], snr[8], hex[16];

	StrCopy(text, TypeName(rx->payloadType));
	if (rx->isAdvert) {
		TxtHex(rx->advertKey, 6, hex);
		StrCat(text, " from ");
		if (ContactsFindByPrefixN(rx->advertKey, 6, &c)) {
			StrNCat(text, c.name, StrLen(text) + 20);
			StrCat(text, " ");
		}
		StrCat(text, hex);
	} else if (rx->payloadType == VM_PAYLOAD_TXT && rx->payloadLen >= 2) {
		/* a DM names its ends by the first byte of their keys */
		TxtHex(rx->payload, 1, hex);
		StrPrintF(text + StrLen(text), " to %s", hex);
		TxtHex(rx->payload + 1, 1, hex);
		StrPrintF(text + StrLen(text), " from %s", hex);
	}
	StrCat(text, "\n");
	InfoClock(text, sizeof(text), "Heard", p->time);
	TxtFormatSnr(snr, (Int8)rx->snrQ);
	StrPrintF(text + StrLen(text), "\nSNR %s, RSSI %d dBm\n", snr, (Int16)rx->rssi);
	if (!rx->flood) {
		StrCat(text, "Sent direct");
		if (rx->hops) {
			StrCat(text, ", route left:");
			InfoVia(text, sizeof(text), rx->path, rx->pathBytes, rx->hashSize, rx->hops);
		}
	} else if (rx->hops == 0) {
		StrCat(text, "Flood, heard from its sender");
	} else {
		StrPrintF(text + StrLen(text), "Flood, %u hop%s via", rx->hops, rx->hops == 1 ? "" : "s");
		InfoVia(text, sizeof(text), rx->path, rx->pathBytes, rx->hashSize, rx->hops);
	}
	FrmCustomAlert(PacketAlert, text, "", "");
}

static void Reload(Boolean draw)
{
	Filter();
	gShownNoted = gNoted;
	RowListSetCount(&gList, gRowCount);
	if (draw)
		RowListDraw(&gList);
}

Boolean PacketsFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();
	Int16 tapped = -1;
	UInt16 id;

	switch (event->eType) {
	case frmOpenEvent:
		RowListInit(&gList, PacketsGadget, PacketsScroll, VM_ROW_H, DrawRow, NULL, NULL);
		id = gShow == SHOW_CHAT ? PacketsMsgPush : gShow == SHOW_ADVERTS ? PacketsAdvertPush : PacketsAllPush;
		FrmSetControlGroupSelection(form, PacketsFilterGroup, id);
		Reload(false);
		FrmDrawForm(form);
		RowListDraw(&gList);
		return true;
	case frmUpdateEvent:
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode)
			FrmDrawForm(form);
		Reload(true);
		return true;
	case ctlSelectEvent:
		switch (event->data.ctlSelect.controlID) {
		case PacketsAllPush:    gShow = SHOW_ALL;     Reload(true); return true;
		case PacketsMsgPush:    gShow = SHOW_CHAT;    Reload(true); return true;
		case PacketsAdvertPush: gShow = SHOW_ADVERTS; Reload(true); return true;
		case PacketsDoneButton:
			FrmGotoForm(HomeForm);
			return true;
		}
		break;
	default:
		break;
	}
	if (RowListHandleEvent(&gList, event, &tapped)) {
		if (tapped >= 0)
			Details((UInt16)tapped);
		return true;
	}
	return false;
}

void PacketsFormIdle(void)
{
	/* newest first: keep the top in view as packets arrive */
	if (gShownNoted != gNoted && gList.top == 0)
		Reload(true);
}
