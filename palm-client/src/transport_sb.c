/*
 * Transport over the SB-LoRa Springboard module (sb_spi.c). Polled: the
 * SC16IS740's 64-byte RX FIFO fills in about 5.5 ms at 115200, so bytes that
 * arrive while the app isn't inside TransportRecv can be lost. An interrupt
 * handler on the slot IRQ* (wired to the chip's IRQ) would fix that; it needs
 * the Handspring card API.
 */
#include <PalmOS.h>
#include "transport.h"
#include "sb_spi.h"

#define SB_SEND_TICKS 200   /* give up if the TX FIFO doesn't drain for 2 s */

static Boolean gOpen = false;

Err SbTransportOpen(void)
{
	if (gOpen)
		return errNone;
	if (!SbProbe())
		return vmErrNoModule;
	SbUartInit();
	gOpen = true;
	return errNone;
}

void SbTransportClose(void)
{
	if (!gOpen)
		return;
	SbSleep(true);
	gOpen = false;
}

Err SbTransportSend(const UInt8 *buf, UInt16 len)
{
	UInt32 deadline = TimGetTicks() + SB_SEND_TICKS;
	UInt16 n;

	if (!gOpen)
		return serErrNotOpen;
	while (len) {
		n = SbReadReg(SB_TXLVL);
		if (n == 0) {
			if (TimGetTicks() >= deadline)
				return serErrTimeOut;
			continue;
		}
		if (n > len)
			n = len;
		SbWriteFifo(buf, n);
		buf += n;
		len -= n;
		deadline = TimGetTicks() + SB_SEND_TICKS;
	}
	return errNone;
}

/*
 * Busy-polls RXLVL until data or the timeout, then keeps reading while the
 * FIFO has more, up to max. Busy rather than SysTaskDelay: a one-tick sleep
 * is longer than the FIFO lasts.
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
