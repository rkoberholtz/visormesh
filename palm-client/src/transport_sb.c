/*
 * Transport over the SB-LoRa Springboard module (sb_spi.c).
 *
 * Receiving is interrupt-driven: the SC16IS740 raises the slot IRQ* when its
 * RX FIFO reaches 8 bytes or goes quiet, and SbIntHandler drains it into
 * gRing. Without the handler the 64-byte FIFO lasts about 5.5 ms at 115200,
 * so anything that arrived while the app was busy elsewhere was lost. If the
 * handler can't be installed, it falls back to polling the FIFO.
 *
 * The handler and the app share the bus, so the app masks the slot interrupt
 * around its own SPI transactions (sends, close).
 *
 * The handler is SbIrq (sb_spi.c), which also wakes the Visor when the radio
 * says a message is waiting. SbWakeArm leaves a copy of it running after the
 * app closes, in memory of its own, so a message starts the closed-app check
 * at once instead of at the next alarm.
 */
#include <PalmOS.h>
#include "transport.h"
#include "sb_spi.h"
#include "hs_card.h"
#include "app.h"

#define SB_SEND_TICKS 200   /* give up if the TX FIFO doesn't drain for 2 s */

static Boolean gOpen = false;
static Boolean gIrq = false;     /* the interrupt handler is installed */
static SbRing  gRing;


static Boolean ModuleInstalled(void)
{
	UInt8 installed = 0;

	return HsCardAttrGet(hsCardSlot, hsCardAttrHwInstalled, &installed) == errNone && installed;
}

/*
 * Asks the OS whether a module is in the slot and where its chip selects
 * are. A bus read with the slot empty is a bus error, which resets the Visor.
 */
UInt8 SbSlotState(UInt32 *base, UInt32 *size)
{
	UInt32 version;

	if (FtrGet(hsFtrCreator, hsFtrIDVersion, &version) != errNone)
		return SB_SLOT_NONE;   /* not a Handspring device: no slot */
	if (!ModuleInstalled())
		return SB_SLOT_EMPTY;
	if (HsCardAttrGet(hsCardSlot, hsCardAttrCsBase, base) != errNone ||
	    HsCardAttrGet(hsCardSlot, hsCardAttrCsSize, size) != errNone)
		return SB_SLOT_NO_CS;
	SbSetBase(*base, *size);
	return SB_SLOT_READY;
}

static Boolean ModulePresent(void)
{
	UInt32 base, size;

	return SbSlotState(&base, &size) == SB_SLOT_READY;
}

Boolean SbTransportIrq(void)
{
	return gIrq;
}

static void IrqEnable(Boolean on)
{
	UInt8 enable = on;

	if (gIrq)
		HsCardAttrSet(hsCardSlot, hsCardAttrIntEnable, &enable);
}

void SbTransportLoopback(Boolean on)
{
	if (!gOpen)
		return;
	IrqEnable(false);
	SbWriteReg(SB_MCR, on ? SB_MCR_LOOPBACK : 0);
	IrqEnable(true);
}

static Boolean InstallHandler(void)
{
	UInt32 param = (UInt32)&gRing;
	HsCardIntHandlerPtr handler = SbIrq;
	HsCardEvtHandlerPtr evt = NULL;
	UInt8 enable = true;

	MemSet(&gRing, sizeof(gRing) - SB_RING_SIZE, 0);
	gRing.bus = *SbCurrentBus();
	gRing.ier = SB_IER_RHR;
	if (HsCardAttrSet(hsCardSlot, hsCardAttrCardParam, &param) != errNone ||
	    HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler) != errNone)
		return false;
	HsCardAttrSet(hsCardSlot, hsCardAttrEvtHandler, &evt);
	SbWriteReg(SB_IER, SB_IER_RHR);
	/* interrupts start disabled; the guide says to enable them after installing */
	if (HsCardAttrSet(hsCardSlot, hsCardAttrIntEnable, &enable) != errNone) {
		SbWriteReg(SB_IER, 0);
		handler = NULL;
		HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler);
		return false;
	}
	return true;
}

static void RemoveHandler(void)
{
	HsCardIntHandlerPtr handler = NULL;

	IrqEnable(false);
	HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler);
	gIrq = false;
}

Err SbTransportOpen(void)
{
	if (gOpen)
		return errNone;
	SbWakeArm(false);   /* the app's own handler takes over */
	if (!ModulePresent() || !SbProbe())
		return vmErrNoModule;
	SbUartInit();
	gIrq = InstallHandler();
	gOpen = true;
	return errNone;
}

void SbTransportClose(void)
{
	if (!gOpen)
		return;
	if (gIrq)
		RemoveHandler();
	if (ModuleInstalled())     /* pulled out: the bus is gone */
		SbSleep(true);
	gOpen = false;
}

Err SbTransportSend(const UInt8 *buf, UInt16 len)
{
	UInt32 deadline = TimGetTicks() + SB_SEND_TICKS;
	UInt16 n, total = len;

	if (!gOpen)
		return serErrNotOpen;
	if (!ModuleInstalled())
		return vmErrNoModule;
	while (len) {
		/* one FIFO's worth per masked stretch, so the handler gets in between */
		IrqEnable(false);
		n = SbReadReg(SB_TXLVL);
		if (n > len)
			n = len;
		if (n)
			SbWriteFifo(buf, n);
		IrqEnable(true);
		if (n == 0) {
			if (TimGetTicks() >= deadline)
				return len < total ? vmErrPartSent : serErrTimeOut;
			continue;
		}
		buf += n;
		len -= n;
		deadline = TimGetTicks() + SB_SEND_TICKS;
	}
	return errNone;
}

static UInt16 RingRead(UInt8 *buf, UInt16 max)
{
	UInt16 head = gRing.head;
	UInt16 n = 0;

	while (gRing.tail != head && n < max) {
		buf[n++] = gRing.buf[gRing.tail & (SB_RING_SIZE - 1)];
		gRing.tail++;
	}
	/* a dropped stretch shows up as a bad frame, which the decoder skips */
	gRing.overflow = false;
	return n;
}

/*
 * With the handler, waits on the ring (a tick at a time: it fills on its own).
 * Without it, busy-polls RXLVL, since a one-tick sleep is longer than the FIFO
 * lasts, then keeps reading while the FIFO has more, up to max.
 */
UInt16 SbTransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP)
{
	UInt32 deadline = TimGetTicks() + (timeoutTicks > 0 ? timeoutTicks : 0);
	UInt16 got = 0;
	UInt16 n;

	*errP = errNone;
	if (!gOpen) {
		*errP = serErrNotOpen;
		return 0;
	}
	if (gIrq) {
		while (gRing.head == gRing.tail) {
			if (TimGetTicks() >= deadline)
				return 0;
			SysTaskDelay(1);
		}
		return RingRead(buf, max);
	}
	for (;;) {
		n = SbReadReg(SB_RXLVL);
		if (n)
			break;
		if (TimGetTicks() >= deadline)
			return 0;
	}
	while (n && got < max) {
		if (n > max - got)
			n = max - got;
		SbReadFifo(buf + got, n);
		got += n;
		n = SbReadReg(SB_RXLVL);
	}
	return got;
}

#define VM_FTR_WAKE 3   /* feature: the SbWakeArm block's address while it's in */

/*
 * The block is an SbRing (resident) followed by a copy of sb_spi.c's core,
 * in a dynamic-heap chunk owned by the system so it stays when the app
 * quits. The handlers point into the copy, not the app's code: a HotSync
 * can replace the app while the block runs.
 */
void SbWakeArm(Boolean on)
{
	/* through pointer variables: GCC 2.95 can't take PC-relative function addresses into arithmetic */
	void (*start)(void) = SbCoreStart, (*end)(void) = SbCoreEnd;
	HsCardIntHandlerPtr irq = SbIrq;
	HsCardEvtHandlerPtr evtFn = SbWakeEvt;
	UInt32 addr, param, coreLen;
	SbRing *r;
	UInt8 *code;
	UInt8 enable = false;
	HsCardIntHandlerPtr handler = NULL;
	HsCardEvtHandlerPtr evt = NULL;
	LocalID self;
	UInt16 card;

	if (FtrGet(VM_CREATOR, VM_FTR_WAKE, &addr) == errNone) {
		HsCardAttrSet(hsCardSlot, hsCardAttrIntEnable, &enable);
		HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler);
		HsCardAttrSet(hsCardSlot, hsCardAttrEvtHandler, &evt);
		MemPtrFree((MemPtr)addr);
		FtrUnregister(VM_CREATOR, VM_FTR_WAKE);
		if (!on && ModuleInstalled())
			SbSleep(true);
	}
	if (!on || gOpen || !ModulePresent() || !SbProbe())
		return;
	coreLen = (UInt32)end - (UInt32)start;
	r = (SbRing *)MemPtrNew(sizeof(SbRing) + coreLen);
	if (!r)
		return;
	MemPtrSetOwner(r, 0);
	MemSet(r, sizeof(SbRing) - SB_RING_SIZE, 0);
	r->bus = *SbCurrentBus();
	r->ier = SB_IER_RHR | SB_IER_SLEEP;
	r->resident = true;
	SysCurAppDatabase(&card, &self);
	r->appCard = card;
	DmDatabaseInfo(card, self, r->appName, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	code = (UInt8 *)(r + 1);
	MemMove(code, (void *)start, (Int32)coreLen);
	FtrSet(VM_CREATOR, VM_FTR_WAKE, (UInt32)r);

	SbUartInit();
	SbSleep(false);     /* sets EFR bit 4, so IER can take the sleep bit */
	SbWriteReg(SB_IER, r->ier);
	param = (UInt32)r;
	handler = (HsCardIntHandlerPtr)(code + ((UInt32)irq - (UInt32)start));
	evt = (HsCardEvtHandlerPtr)(code + ((UInt32)evtFn - (UInt32)start));
	enable = true;
	if (HsCardAttrSet(hsCardSlot, hsCardAttrCardParam, &param) != errNone ||
	    HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler) != errNone ||
	    HsCardAttrSet(hsCardSlot, hsCardAttrEvtHandler, &evt) != errNone ||
	    HsCardAttrSet(hsCardSlot, hsCardAttrIntEnable, &enable) != errNone)
		SbWakeArm(false);
}
