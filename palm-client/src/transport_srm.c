#include <PalmOS.h>
#include "transport.h"

#define VM_BAUD       115200
#define VM_SEND_TRIES 8      /* SrmSend attempts for one buffer (USB re-attach) */
#define VM_SEND_WAIT  (SysTicksPerSecond() / 4)   /* each: 2 s in all */

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
	Int32 wait = VM_SEND_WAIT;
	UInt16 size = sizeof(wait);
	Err err;

	if (gPort == VM_PORT_SBLORA)
		return SbTransportOpen();
	if (gPortOpen)
		return errNone;

	err = SrmOpen(gPort, VM_BAUD, &gPortId);
	if (err != errNone)
		return err;
	gPortOpen = true;
	/*
	 * With nothing reading at the other end (the bridge stopped), each SrmSend
	 * waits out the CTS timeout, 5 s by default: a connect froze for over a
	 * minute. Short waits let the session look for the user between tries.
	 */
	SrmControl(gPortId, srmCtlSetCtsTimeout, &wait, &size);
	return errNone;
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
