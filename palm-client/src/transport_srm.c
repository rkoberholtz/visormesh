#include <PalmOS.h>
#include "transport.h"

#define VM_BAUD 115200

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

	if (gPortOpen)
		return errNone;
	err = SrmOpen(gPort, VM_BAUD, &gPortId);
	if (err == errNone)
		gPortOpen = true;
	return err;
}

void TransportClose(void)
{
	if (!gPortOpen)
		return;
	SrmClose(gPortId);
	gPortOpen = false;
}

Err TransportSend(const UInt8 *buf, UInt16 len)
{
	Err err = errNone;

	if (!gPortOpen)
		return serErrNotOpen;
	/* No SrmSendWait: over USB it can block indefinitely if no host is reading. */
	SrmSend(gPortId, (void *)buf, len, &err);
	return err;
}

UInt16 TransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP)
{
	UInt32 avail = 0;
	UInt32 n;

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
