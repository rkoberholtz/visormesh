#include <PalmOS.h>
#include "protocol.h"
#include "transport.h"
#include "session.h"

#define VM_APP_NAME        "VisorMesh"
#define VM_REPLY_TIMEOUT   (2 * SysTicksPerSecond())
#define VM_CONNECT_TRIES   3     /* over USB the host bridge needs a moment to attach */

static VmFrameDecoder gDecoder;
static Boolean gConnected = false;
static Boolean gMsgWaiting = false;
static VmDeviceInfo gDevice;
static VmSelfInfo gSelf;

/*
 * Reads frames until one with the wanted code arrives. Pushes are noted and
 * skipped; RESP_ERR ends the wait. The reply is left in gDecoder.payload.
 */
static Err WaitFor(UInt8 code, Int32 timeout)
{
	UInt8 rx[32];
	UInt16 n, i;
	UInt32 deadline = TimGetTicks() + timeout;
	Err err;

	while (TimGetTicks() < deadline) {
		n = TransportRecv(rx, sizeof(rx), deadline - TimGetTicks(), &err);
		for (i = 0; i < n; i++) {
			UInt8 got;

			if (!VmDecoderFeed(&gDecoder, rx[i]) || gDecoder.len == 0)
				continue;
			got = gDecoder.payload[0];
			if (got == code)
				return errNone;
			if (got == VM_PUSH_MSG_WAITING)
				gMsgWaiting = true;
			else if (got == VM_RESP_ERR)
				return vmErrRadio;
			/* other pushes and stray replies are ignored */
		}
	}
	return vmErrNoReply;
}

/* Sends a request frame and waits for the reply code, retrying sends the host isn't reading yet. */
static Err Request(const UInt8 *frame, UInt16 len, UInt8 code, UInt16 tries)
{
	Err err = vmErrNoReply;
	UInt16 t;

	for (t = 0; t < tries; t++) {
		err = TransportSend(frame, len);
		if (err == serErrTimeOut)
			continue;
		if (err != errNone)
			return err;
		err = WaitFor(code, VM_REPLY_TIMEOUT);
		if (err != vmErrNoReply)
			return err;
	}
	return err == serErrTimeOut ? vmErrNoReply : err;
}

/* DEVICE_QUERY + APP_START into gDevice/gSelf. The first request gets extra tries (USB attach). */
static Err QueryRadio(UInt16 firstTries)
{
	UInt8 frame[VM_HEADER_LEN + 8 + VM_NAME_SIZE];
	UInt16 len;
	Err err;

	len = VmBuildDeviceQuery(frame, sizeof(frame));
	err = Request(frame, len, VM_RESP_DEVICE_INFO, firstTries);
	if (err == errNone && !VmParseDeviceInfo(gDecoder.payload, gDecoder.len, &gDevice))
		err = vmErrBadReply;
	if (err != errNone)
		return err;

	len = VmBuildAppStart(frame, sizeof(frame), VM_APP_NAME);
	err = Request(frame, len, VM_RESP_SELF_INFO, 1);
	if (err == errNone && !VmParseSelfInfo(gDecoder.payload, gDecoder.len, &gSelf))
		err = vmErrBadReply;
	return err;
}

Err SessionConnect(void)
{
	Err err;

	SessionDisconnect();
	err = TransportOpen();
	if (err != errNone)
		return err;
	VmDecoderReset(&gDecoder);
	gMsgWaiting = false;

	/* Over USB the Visor only appears to the host after SrmOpen; give the bridge time to attach. */
	if (TransportGetPort() == VM_PORT_USB)
		SysTaskDelay(SysTicksPerSecond());

	err = QueryRadio(VM_CONNECT_TRIES);
	if (err != errNone) {
		TransportClose();
		return err;
	}
	gConnected = true;
	return errNone;
}

Err SessionRefresh(void)
{
	if (!gConnected)
		return serErrNotOpen;
	return QueryRadio(1);
}

Err SessionCommand(const UInt8 *frame, UInt16 len)
{
	if (!gConnected)
		return serErrNotOpen;
	return Request(frame, len, VM_RESP_OK, 1);
}

const VmDeviceInfo *SessionDevice(void)
{
	return &gDevice;
}

const VmSelfInfo *SessionSelf(void)
{
	return &gSelf;
}

void SessionDisconnect(void)
{
	TransportClose();
	gConnected = false;
}

Boolean SessionIsConnected(void)
{
	return gConnected;
}

Boolean SessionMessageWaiting(void)
{
	return gMsgWaiting;
}
