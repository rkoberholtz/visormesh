#include <PalmOS.h>
#include "transport.h"

#define VM_BAUD       115200
#define VM_SEND_TRIES 5      /* SrmSend attempts for one buffer (USB re-attach) */
#define VM_HOST_WAIT  SysTicksPerSecond()   /* for the USB host to come back (TransportSend) */

static UInt16  gPortId;
static Boolean gPortOpen = false;
static UInt32  gPort = VM_PORT_USB;

void TransportSetPort(UInt32 port)
{
	gPort = port;
}

UInt32 TransportGetPort(void)
{
	return gPort;
}

Err TransportOpen(void)
{
	Err err;

	if (gPort == VM_PORT_SBLORA)
		return SbTransportOpen();
	if (gPortOpen)
		return errNone;

	err = SrmOpen(gPort, VM_BAUD, &gPortId);
	if (err == errNone)
		gPortOpen = true;
	return err;
}

/*
 * Over USB, CTS is on while the host reads what the Visor sends (the bridge
 * running) and off while nothing does. Then SrmSend blocks for 10 s whatever
 * the CTS timeout is set to (measured on the Neo), so don't send at all.
 * Waits up to wait ticks for it; any other port, or no status, counts as yes.
 */
static Boolean HostReading(Int32 wait)
{
	UInt32 status, until = TimGetTicks() + wait;
	UInt16 lineErrs;

	if (gPort != VM_PORT_USB)
		return true;
	for (;;) {
		if (SrmGetStatus(gPortId, &status, &lineErrs) != errNone || (status & srmStatusCtsOn))
			return true;
		if ((Int32)(TimGetTicks() - until) >= 0)
			return false;
		SysTaskDelay(SysTicksPerSecond() / 20);
	}
}

void TransportClose(void)
{
	SbTransportClose();
	if (!gPortOpen)
		return;
	SrmClose(gPortId);
	gPortOpen = false;
}

Err TransportSend(const UInt8 *buf, UInt16 len)
{
	Err err = errNone;
	UInt32 sent = 0;
	UInt16 tries;

	if (gPort == VM_PORT_SBLORA)
		return SbTransportSend(buf, len);
	if (!gPortOpen)
		return serErrNotOpen;
	if (!HostReading(VM_HOST_WAIT))
		return serErrTimeOut;
	/*
	 * Over USB, SrmSend can time out while the host re-attaches and still
	 * deliver the bytes it accepted, so only ever resend the remainder;
	 * resending the whole frame duplicated commands. No SrmSendWait: it can
	 * block indefinitely if no host is reading.
	 */
	for (tries = 0; tries < VM_SEND_TRIES; tries++) {
		sent += SrmSend(gPortId, (void *)(buf + sent), len - sent, &err);
		if (sent >= len)
			return errNone;
		if (err != serErrTimeOut)
			return err;
		SrmClearErr(gPortId);
	}
	return serErrTimeOut;
}

Err TransportSendOnce(const UInt8 *buf, UInt16 len)
{
	Err err = errNone;

	if (gPort == VM_PORT_SBLORA)
		return SbTransportSend(buf, len);
	if (!gPortOpen)
		return serErrNotOpen;
	if (!HostReading(0))
		return serErrTimeOut;
	if (SrmSend(gPortId, (void *)buf, len, &err) == len)
		return errNone;
	SrmClearErr(gPortId);
	return err != errNone ? err : serErrTimeOut;
}

UInt16 TransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP)
{
	UInt32 avail = 0;
	UInt32 n;

	if (gPort == VM_PORT_SBLORA)
		return SbTransportRecv(buf, max, timeoutTicks, errP);
	*errP = errNone;
	if (!gPortOpen) {
		*errP = serErrNotOpen;
		return 0;
	}

	/* without waiting: only if something's there (a read that can't wait still costs the USB driver time) */
	if (timeoutTicks == 0 && (SrmReceiveCheck(gPortId, &avail) != errNone || avail == 0))
		return 0;
	/* Block for the first byte, then take whatever else is already buffered. */
	n = SrmReceive(gPortId, buf, 1, timeoutTicks, errP);
	if (n == 0) {
		if (*errP == serErrLineErr)
			SrmClearErr(gPortId);
		return 0;
	}
	if (SrmReceiveCheck(gPortId, &avail) == errNone && avail > 0) {
		if (avail > (UInt32)(max - 1))
			avail = max - 1;
		n += SrmReceive(gPortId, buf + 1, avail, 0, errP);
	}
	if (*errP == serErrLineErr)
		SrmClearErr(gPortId);
	return (UInt16)n;
}
