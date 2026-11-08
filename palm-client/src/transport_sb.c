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
 */
#include <PalmOS.h>
#include "transport.h"
#include "sb_spi.h"
#include "hs_card.h"

#define SB_SEND_TICKS 200   /* give up if the TX FIFO doesn't drain for 2 s */

static Boolean gOpen = false;
static Boolean gIrq = false;     /* the interrupt handler is installed */
static SbRing  gRing;

/* Interrupt context: no globals (gRing comes in as cardParam), no system calls. */
static void SbIntHandler(UInt32 cardParam, Boolean *sysAwakeP)
{
	(void)sysAwakeP;   /* draining needs only bus reads, so the Visor can stay asleep */
	SbDrainRx((SbRing *)cardParam);
}

static Boolean ModuleInstalled(void)
{
	UInt8 installed = 0;

	return HsCardAttrGet(hsCardSlot, hsCardAttrHwInstalled, &installed) == errNone && installed;
}

/*
 * Asks the OS whether a module is in the slot and where its chip selects
 * are. A bus read with the slot empty is a bus error, which resets the Visor.
 */
static Boolean ModulePresent(void)
{
	UInt32 version, base, size;

	if (FtrGet(hsFtrCreator, hsFtrIDVersion, &version) != errNone)
		return false;   /* not a Handspring device: no slot */
	if (!ModuleInstalled())
		return false;
	if (HsCardAttrGet(hsCardSlot, hsCardAttrCsBase, &base) != errNone ||
	    HsCardAttrGet(hsCardSlot, hsCardAttrCsSize, &size) != errNone)
		return false;
	SbSetBase(base, size);
	return true;
}

static void IrqEnable(Boolean on)
{
	UInt8 enable = on;

	if (gIrq)
		HsCardAttrSet(hsCardSlot, hsCardAttrIntEnable, &enable);
}

static Boolean InstallHandler(void)
{
	UInt32 param = (UInt32)&gRing;
	HsCardIntHandlerPtr handler = SbIntHandler;
	UInt8 enable = true;

	gRing.bus = *SbCurrentBus();
	gRing.head = gRing.tail = 0;
	gRing.overflow = false;
	if (HsCardAttrSet(hsCardSlot, hsCardAttrCardParam, &param) != errNone ||
	    HsCardAttrSet(hsCardSlot, hsCardAttrIntHandler, &handler) != errNone)
		return false;
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
