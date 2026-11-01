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
 * Received bytes not yet fed to the decoder. Kept across calls because one
 * read can hold the end of one frame and the start of the next (the contact
 * list arrives as back-to-back frames).
 */
static UInt8  gRx[64];
static UInt16 gRxPos, gRxLen;

/*
 * Reads frames until one whose code is code1 or code2 arrives (pass the same
 * code twice to wait for one). Pushes are noted and skipped; RESP_ERR ends the
 * wait. The reply is left in gDecoder.payload.
 */
static Err WaitForEither(UInt8 code1, UInt8 code2, Int32 timeout)
{
	UInt32 deadline = TimGetTicks() + timeout;
	Err err;

	for (;;) {
		while (gRxPos < gRxLen) {
			UInt8 got;

			if (!VmDecoderFeed(&gDecoder, gRx[gRxPos++]) || gDecoder.len == 0)
				continue;
			got = gDecoder.payload[0];
			if (got == code1 || got == code2)
				return errNone;
			if (got == VM_PUSH_MSG_WAITING)
				gMsgWaiting = true;
			else if (got == VM_RESP_ERR)
				return vmErrRadio;
			/* other pushes and stray replies are ignored */
		}
		if (TimGetTicks() >= deadline)
			return vmErrNoReply;
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), deadline - TimGetTicks(), &err);
	}
}

static Err WaitFor(UInt8 code, Int32 timeout)
{
	return WaitForEither(code, code, timeout);
}

/*
 * Discards anything already received before a new request, so a late reply to
 * an earlier request can't be taken as this one's. Pushes in it are still noted.
 */
static void DrainInput(void)
{
	Err err;

	for (;;) {
		while (gRxPos < gRxLen) {
			if (VmDecoderFeed(&gDecoder, gRx[gRxPos++]) && gDecoder.len &&
			    gDecoder.payload[0] == VM_PUSH_MSG_WAITING)
				gMsgWaiting = true;
		}
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), 0, &err);
		if (gRxLen == 0)
			break;
	}
	VmDecoderReset(&gDecoder);
}

/*
 * Sends a request frame and waits for the reply code. A try is repeated if
 * the frame couldn't be sent or no reply came, so only use tries > 1 for
 * commands that are safe to repeat.
 */
static Err Request(const UInt8 *frame, UInt16 len, UInt8 code, UInt16 tries)
{
	Err err = vmErrNoReply;
	UInt16 t;

	for (t = 0; t < tries; t++) {
		DrainInput();
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
	gRxPos = gRxLen = 0;
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

/*
 * Two tries: the Visor's USB link can drop while idle and re-attach on the
 * next send, and the settings commands are safe to repeat.
 */
Err SessionCommand(const UInt8 *frame, UInt16 len)
{
	if (!gConnected)
		return serErrNotOpen;
	return Request(frame, len, VM_RESP_OK, 2);
}

Err SessionGetContacts(UInt32 since, SessionContactFn fn, void *ctx, UInt32 *count, UInt32 *lastMod)
{
	UInt8 frame[VM_HEADER_LEN + 5];
	UInt16 len;
	VmContact c;
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	len = VmBuildGetContacts(frame, sizeof(frame), since);
	err = Request(frame, len, VM_RESP_CONTACTS_START, 2);
	if (err != errNone)
		return err;
	if (!VmParseContactsStart(gDecoder.payload, gDecoder.len, count))
		return vmErrBadReply;

	for (;;) {
		err = WaitForEither(VM_RESP_CONTACT, VM_RESP_END_OF_CONTACTS, VM_REPLY_TIMEOUT);
		if (err != errNone)
			return err;
		if (gDecoder.payload[0] == VM_RESP_END_OF_CONTACTS)
			return VmParseEndOfContacts(gDecoder.payload, gDecoder.len, lastMod) ? errNone : vmErrBadReply;
		if (!VmParseContact(gDecoder.payload, gDecoder.len, &c))
			return vmErrBadReply;
		err = fn(&c, ctx);
		if (err != errNone)
			return err;
	}
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
