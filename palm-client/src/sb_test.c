/*
 * SB-LoRa Test: each step's result is a line on the screen, and a step that
 * fails ends the test, since the ones after it depend on it. The bus is only
 * touched once the OS says a module is in the slot: with the slot empty, a
 * bus access is a bus error that resets the Visor.
 */
#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "transport.h"
#include "sb_spi.h"
#include "protocol.h"
#include "ui/draw.h"
#include "ui/text.h"
#include "sb_test.h"

static void Add(const Char *s) VM_SECT5;
static void Hex32(Char *out, UInt32 v) VM_SECT5;
static Boolean TestSlot(void) VM_SECT5;
static Boolean TestMapping(void) VM_SECT5;
static Boolean TestUart(void) VM_SECT5;
static Boolean TestLoopback(void) VM_SECT5;
static Boolean TestInterrupt(void) VM_SECT5;
static void TestRadio(void) VM_SECT5;
static void Run(void) VM_SECT5;
static void Draw(void) VM_SECT5;

#define TEST_LINES   10
#define TEST_LINE    48
#define TEST_BYTES   SB_FIFO_SIZE
#define IRQ_WAIT     (SysTicksPerSecond() / 2)
#define RADIO_WAIT   (2 * SysTicksPerSecond())

static Char  gLines[TEST_LINES][TEST_LINE];
static UInt16 gCount;
static UInt8 gPattern[TEST_BYTES], gBack[TEST_BYTES];
static VmFrameDecoder gDecoder;

static void Add(const Char *s)
{
	if (gCount < TEST_LINES)
		StrNCopy(gLines[gCount++], s, TEST_LINE - 1);
	gLines[gCount - 1][TEST_LINE - 1] = 0;
	Draw();
}

/* StrPrintF's %lx prints zeros on the Visor */
static void Hex32(Char *out, UInt32 v)
{
	UInt8 b[4];

	b[0] = (UInt8)(v >> 24);
	b[1] = (UInt8)(v >> 16);
	b[2] = (UInt8)(v >> 8);
	b[3] = (UInt8)v;
	TxtHex(b, 4, out);
}

static Boolean TestSlot(void)
{
	UInt32 base = 0, size = 0;
	Char line[TEST_LINE], hex[10];

	switch (SbSlotState(&base, &size)) {
	case SB_SLOT_NONE:
		Add("No Springboard slot on this device.");
		return false;
	case SB_SLOT_EMPTY:
		Add("Slot: empty. Insert the module and Run.");
		return false;
	case SB_SLOT_NO_CS:
		Add("Slot: a module, but no chip selects.");
		return false;
	default:
		break;
	}
	Hex32(hex, base);
	StrPrintF(line, "Slot: CS0 at %s, %lu KB", hex, size / 1024);
	Add(line);
	return true;
}

/* Both mappings, so a wrong one shows what it read; the one that works is kept. */
static Boolean TestMapping(void)
{
	UInt8 got[2];
	Char line[TEST_LINE], hex[6];
	Boolean ok[2];
	Int16 shift;

	for (shift = 1; shift >= 0; shift--) {
		ok[shift] = SbTryMapping((UInt8)shift, got);
		TxtHex(got, 2, hex);
		StrPrintF(line, "A0 = CPU A%d: 55AA read %s%s", shift, hex, ok[shift] ? ", ok" : "");
		Add(line);
	}
	if (!ok[0] && !ok[1]) {
		Add("No answer from the SC16IS740.");
		return false;
	}
	if (!ok[1])
		SbTryMapping(0, got);
	return true;
}

/* After a reset: 8N1, both FIFOs empty, the transmitter idle. */
static Boolean TestUart(void)
{
	UInt8 r[3];
	Char line[TEST_LINE], hex[8];
	Boolean ok;

	SbUartInit();
	r[0] = SbReadReg(SB_LCR);
	r[1] = SbReadReg(SB_LSR);
	r[2] = SbReadReg(SB_TXLVL);
	ok = r[0] == 0x03 && (r[1] & 0x60) == 0x60 && r[2] == SB_FIFO_SIZE;
	TxtHex(r, 2, hex);
	StrPrintF(line, "UART: LCR LSR %s, TX room %u%s", hex, r[2], ok ? ", ok" : "");
	Add(line);
	if (!ok)
		Add("Expected 0360 and 64.");
	return ok;
}

static Boolean TestLoopback(void)
{
	UInt16 got, wrong = 0, i;
	Char line[TEST_LINE];

	for (i = 0; i < TEST_BYTES; i++)
		gPattern[i] = (UInt8)(i * 37 + 1);
	got = SbLoopback(gPattern, TEST_BYTES, gBack);
	for (i = 0; i < got; i++)
		if (gBack[i] != gPattern[i])
			wrong++;
	if (got == TEST_BYTES && wrong == 0) {
		Add("Loopback: 64 bytes back, ok");
		return true;
	}
	StrPrintF(line, "Loopback: %u of 64 back, %u wrong", got, wrong);
	Add(line);
	return false;
}

/*
 * The same loopback through the transport: with the handler installed, the
 * bytes only reach the ring if the slot interrupt fired.
 */
static Boolean TestInterrupt(void)
{
	UInt32 until;
	UInt16 got = 0, n;
	Err err;
	Char line[TEST_LINE];

	if (SbTransportOpen() != errNone) {
		Add("Interrupt: the link wouldn't open.");
		return false;
	}
	SbTransportLoopback(true);
	SysTaskDelay(2);
	while (SbTransportRecv(gBack, TEST_BYTES, 0, &err))
		;   /* whatever the radio sent before loopback was on */
	if (SbTransportSend(gPattern, TEST_BYTES) != errNone) {
		SbTransportLoopback(false);
		Add("Interrupt: sending stuck (TX FIFO).");
		return false;
	}
	until = TimGetTicks() + IRQ_WAIT;
	while (got < TEST_BYTES && (Int32)(until - TimGetTicks()) > 0) {
		n = SbTransportRecv(gBack + got, TEST_BYTES - got, until - TimGetTicks(), &err);
		got += n;
	}
	SbTransportLoopback(false);
	if (!SbTransportIrq())
		StrPrintF(line, "Polled (no interrupt handler): %u of 64", got);
	else if (got == 0)
		StrCopy(line, "Interrupt: none came. Check IRQ*.");
	else
		StrPrintF(line, "Interrupt: works, %u of 64 bytes", got);
	Add(line);
	return got == TEST_BYTES || !SbTransportIrq();
}

/* DEVICE_QUERY over the real UART: the radio's answer, nothing on the air. */
static void TestRadio(void)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt16 len = VmBuildDeviceQuery(frame, sizeof(frame)), n, i;
	UInt32 until;
	VmDeviceInfo info;
	Err err;
	Char line[TEST_LINE];

	while (SbTransportRecv(gBack, TEST_BYTES, 0, &err))
		;
	VmDecoderReset(&gDecoder);
	if (SbTransportSend(frame, len) != errNone) {
		Add("Radio: sending stuck (TX FIFO).");
		return;
	}
	until = TimGetTicks() + RADIO_WAIT;
	while ((Int32)(until - TimGetTicks()) > 0) {
		n = SbTransportRecv(gBack, TEST_BYTES, until - TimGetTicks(), &err);
		for (i = 0; i < n; i++) {
			if (!VmDecoderFeed(&gDecoder, gBack[i]) || gDecoder.len == 0 ||
			    gDecoder.payload[0] != VM_RESP_DEVICE_INFO)
				continue;
			if (VmParseDeviceInfo(gDecoder.payload, gDecoder.len, &info)) {
				StrPrintF(line, "Radio: %s %s", info.manufacturer, info.version);
				Add(line);
			} else {
				Add("Radio: answered, but the reply didn't parse.");
			}
			return;
		}
	}
	Add("Radio: no answer. Powered? Firmware?");
}

static void Run(void)
{
	gCount = 0;
	Draw();
	if (TransportGetPort() == VM_PORT_SBLORA && (SessionIsConnected() || SessionConnecting())) {
		Add("The Slot link is in use: Disconnect first.");
		return;
	}
	if (!TestSlot() || !TestMapping() || !TestUart() || !TestLoopback())
		return;
	if (TestInterrupt())
		TestRadio();
	SbTransportClose();
	Add("Done.");
}

static void Draw(void)
{
	FontID old = FntSetFont(vmSmallFont);
	RectangleType r;
	Coord y = 18;
	UInt16 i;

	RctSetRectangle(&r, 0, 16, 160, 128);
	WinEraseRectangle(&r, 0);
	if (gCount == 0)
		DrawCut("Testing...", 3, y, 154);
	for (i = 0; i < gCount; i++, y += VM_ROW_H)
		DrawCut(gLines[i], 3, y, 154);
	FntSetFont(old);
}

Boolean SbTestFormHandleEvent(EventType *event)
{
	FormType *form = FrmGetActiveForm();

	switch (event->eType) {
	case frmOpenEvent:
		FrmDrawForm(form);
		Run();
		return true;
	case frmUpdateEvent:
		if (event->data.frmUpdate.updateCode == frmRedrawUpdateCode) {
			FrmDrawForm(form);
			Draw();
		}
		return true;
	case ctlSelectEvent:
		if (event->data.ctlSelect.controlID == SbTestDoneButton) {
			FrmGotoForm(MainForm);
			return true;
		}
		if (event->data.ctlSelect.controlID == SbTestRunButton) {
			Run();
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}
