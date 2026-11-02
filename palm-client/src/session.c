#include <PalmOS.h>
#include "protocol.h"
#include "transport.h"
#include "messages.h"
#include "channels.h"
#include "contacts.h"
#include "ui/text.h"
#include "session.h"
#include "heard.h"

#define VM_APP_NAME        "VisorMesh"
#define VM_REPLY_TIMEOUT   (2 * SysTicksPerSecond())
#define VM_CONNECT_TRIES   3     /* over USB the host bridge needs a moment to attach */
#define VM_SYNC_MAX        50    /* messages pulled per sync, so the UI isn't blocked forever */
#define VM_ACK_MIN_SECS    10    /* shortest wait for a DM ack, whatever the radio estimates */
#define VM_CLOCK_SLACK     60    /* radio clock behind ours by more than this gets set forward */
#define VM_CLOCK_MAX_STEP  (7UL * 86400UL)  /* a bigger jump means one clock is wrong: don't guess */
#define VM_CLOCK_VALID     1735689600UL     /* 2025-01-01: a radio clock before this was never set */
#define PALM_EPOCH_OFFSET  2082844800UL   /* 1904-01-01 (Palm) to 1970-01-01 (Unix) */

static VmFrameDecoder gDecoder;
static Boolean gConnected = false;
static Boolean gMsgWaiting = false;
static UInt16  gChanged = 0;
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
 * The Visor's clock is local time with no time zone (Palm OS 3.5), so the
 * offset to UTC is an app preference. Everything stored and sent is UTC.
 */
static Int32 gUtcOffsetSecs = 0;      /* local minus UTC */

void SessionSetUtcOffset(Int16 minutes)
{
	gUtcOffsetSecs = (Int32)minutes * 60;
}

Int16 SessionUtcOffset(void)
{
	return (Int16)(gUtcOffsetSecs / 60);
}

UInt32 SessionNow(void)
{
	return TimGetSeconds() - PALM_EPOCH_OFFSET - gUtcOffsetSecs;
}

UInt32 SessionLocalPalm(UInt32 utc)
{
	return utc + PALM_EPOCH_OFFSET + gUtcOffsetSecs;
}

/* Acts on a frame nobody asked for: pushes from the radio. */
static void HandlePush(void)
{
	UInt8 code = gDecoder.payload[0];
	UInt32 ack, trip;

	if (code == VM_PUSH_MSG_WAITING) {
		gMsgWaiting = true;
	} else if (code == VM_PUSH_SEND_CONFIRMED) {
		if (VmParseSendConfirmed(gDecoder.payload, gDecoder.len, &ack, &trip) && MessagesAcked(ack))
			gChanged |= VM_CHANGED_MESSAGES;
	} else if (code == VM_PUSH_LOG_RX) {
		VmRxLog rx;

		if (VmParseRxLog(gDecoder.payload, gDecoder.len, &rx))
			HeardRecord(&rx, SessionNow());
	} else if (code == VM_PUSH_ADVERT || code == VM_PUSH_NEW_ADVERT || code == VM_PUSH_PATH_UPDATED) {
		gChanged |= VM_CHANGED_CONTACTS;
	}
}

/*
 * Reads frames until one whose code is in codes[] arrives; pushes on the way
 * are acted on, RESP_ERR ends the wait. The reply is left in gDecoder.payload.
 */
static Err WaitForAny(const UInt8 *codes, UInt16 n, Int32 timeout)
{
	UInt32 deadline = TimGetTicks() + timeout;
	UInt16 i;
	Err err;

	for (;;) {
		while (gRxPos < gRxLen) {
			UInt8 got;

			if (!VmDecoderFeed(&gDecoder, gRx[gRxPos++]) || gDecoder.len == 0)
				continue;
			got = gDecoder.payload[0];
			for (i = 0; i < n; i++)
				if (got == codes[i])
					return errNone;
			if (got == VM_RESP_ERR)
				return vmErrRadio;
			if (VM_IS_PUSH(got))
				HandlePush();
			/* stray replies are ignored */
		}
		if (TimGetTicks() >= deadline)
			return vmErrNoReply;
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), deadline - TimGetTicks(), &err);
	}
}

/*
 * Handles everything already received without waiting: pushes are acted on,
 * anything else is a late reply to an earlier request and is dropped, so it
 * can't be taken as the next request's.
 */
static void DrainInput(void)
{
	Err err;

	for (;;) {
		while (gRxPos < gRxLen) {
			if (VmDecoderFeed(&gDecoder, gRx[gRxPos++]) && gDecoder.len && VM_IS_PUSH(gDecoder.payload[0]))
				HandlePush();
		}
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), 0, &err);
		if (gRxLen == 0)
			break;
	}
	VmDecoderReset(&gDecoder);
}

/*
 * Sends a request frame and waits for one of the reply codes. A try is
 * repeated if the frame couldn't be sent or no reply came, so only use
 * tries > 1 for commands that are safe to repeat.
 */
static Err RequestAny(const UInt8 *frame, UInt16 len, const UInt8 *codes, UInt16 n, UInt16 tries)
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
		err = WaitForAny(codes, n, VM_REPLY_TIMEOUT);
		if (err != vmErrNoReply)
			return err;
	}
	return err == serErrTimeOut ? vmErrNoReply : err;
}

static Err Request(const UInt8 *frame, UInt16 len, UInt8 code, UInt16 tries)
{
	return RequestAny(frame, len, &code, 1, tries);
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
	if (err == errNone)
		TxtUtf8NameToPalm(gSelf.name, sizeof(gSelf.name));
	return err;
}

/* Sets the radio's clock from ours if it's behind (the firmware only lets it move forward). */
static UInt32 gRadioTime = 0;       /* radio clock at gRadioTimeAt (our local seconds) */
static Boolean gClockMismatch = false;
static UInt32 gRadioTimeAt = 0;

void SessionSyncClock(void)
{
	UInt8 frame[VM_HEADER_LEN + 5];
	UInt32 radio, now = SessionNow();
	UInt16 len;

	len = VmBuildGetTime(frame, sizeof(frame));
	if (Request(frame, len, VM_RESP_CURR_TIME, 1) != errNone ||
	    !VmParseCurrTime(gDecoder.payload, gDecoder.len, &radio))
		return;
	gRadioTime = radio;
	gRadioTimeAt = TimGetSeconds();
	/*
	 * The radio's clock can only move forward, so a wrong Visor date (a slip in
	 * the date picker set it 11 months ahead once) can't be undone short of a
	 * radio reboot. If the radio's clock looks set, only correct small drift.
	 */
	gClockMismatch = radio >= VM_CLOCK_VALID && (now > radio + VM_CLOCK_MAX_STEP || radio > now + VM_CLOCK_MAX_STEP);
	if (gClockMismatch)
		return;
	if (radio + VM_CLOCK_SLACK < now) {
		len = VmBuildSetTime(frame, sizeof(frame), now);
		if (Request(frame, len, VM_RESP_OK, 1) == errNone)
			gRadioTime = now;
	}
}

Boolean SessionClockMismatch(void)
{
	return gClockMismatch;
}

UInt32 SessionRadioTime(void)
{
	return gRadioTime ? gRadioTime + (TimGetSeconds() - gRadioTimeAt) : 0;
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

	/* Best effort: the connection stands even if one of these fails; the stored copies stay. */
	SessionSyncClock();
	if (ChannelsRefresh(gDevice.maxChannels) == errNone)
		gChanged |= VM_CHANGED_MESSAGES;
	if (ContactsRefresh(NULL) == errNone)
		gChanged |= VM_CHANGED_CONTACTS;
	SessionSyncMessages();
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
	static const UInt8 codes[] = { VM_RESP_CONTACT, VM_RESP_END_OF_CONTACTS };
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
		err = WaitForAny(codes, 2, VM_REPLY_TIMEOUT);
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

Err SessionGetChannel(UInt8 slot, VmChannel *out)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt16 len = VmBuildGetChannel(frame, sizeof(frame), slot);
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	err = Request(frame, len, VM_RESP_CHANNEL_INFO, 2);
	if (err == errNone && !VmParseChannelInfo(gDecoder.payload, gDecoder.len, out))
		err = vmErrBadReply;
	return err;
}

/* The newest message that should alert (a DM, or a mention of us in a channel). */
static VmConv  gAlertConv;

static void NoteAlert(const VmMessage *m)
{
	MemSet(&gAlertConv, sizeof(gAlertConv), 0);
	if (m->isChannel) {
		gAlertConv.type = VM_CONV_CHANNEL;
		gAlertConv.channel = m->channelIdx;
	} else {
		gAlertConv.type = VM_CONV_DIRECT;
		MemMove(gAlertConv.key, m->senderPrefix, 6);
	}
	gChanged |= VM_CHANGED_ALERT;
}

/* True if the (UTF-8) text contains "@[our name]". */
static Boolean MentionsUs(const VmMessage *m)
{
	UInt8 tag[VM_NAME_SIZE + 4];
	UInt16 n, i;

	if (!gSelf.name[0])
		return false;
	tag[0] = '@';
	tag[1] = '[';
	n = TxtPalmToUtf8(gSelf.name, StrLen(gSelf.name), tag + 2, VM_NAME_SIZE);
	if (n == 0)
		return false;
	tag[2 + n] = ']';
	n += 3;
	for (i = 0; i + n <= m->textLen; i++)
		if (MemCmp(m->text + i, tag, n) == 0)
			return true;
	return false;
}

const VmConv *SessionAlertConv(void)
{
	return &gAlertConv;
}

Err SessionSyncMessages(void)
{
	static const UInt8 codes[] = { VM_RESP_CONTACT_MSG_V3, VM_RESP_CHANNEL_MSG_V3, VM_RESP_NO_MORE_MESSAGES };
	UInt8 frame[VM_HEADER_LEN + 1];
	UInt16 len = VmBuildSyncNextMessage(frame, sizeof(frame));
	UInt16 n;
	VmMessage m;
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	gMsgWaiting = false;
	for (n = 0; n < VM_SYNC_MAX; n++) {
		/* one try: a repeated SYNC_NEXT_MESSAGE would skip a message */
		err = RequestAny(frame, len, codes, 3, 1);
		if (err != errNone)
			return err;
		if (gDecoder.payload[0] == VM_RESP_NO_MORE_MESSAGES)
			return errNone;
		if (VmParseMessage(gDecoder.payload, gDecoder.len, &m) && MessagesAddIncoming(&m, SessionNow()) == errNone) {
			gChanged |= VM_CHANGED_MESSAGES;
			if (!m.isChannel || MentionsUs(&m))
				NoteAlert(&m);
		}
	}
	gMsgWaiting = true;     /* more left: carry on at the next poll */
	return errNone;
}

/* Sends message record 'index' (new or a retry) and records the outcome on it. */
static Err SendRecord(UInt16 index)
{
	static const UInt8 dmCodes[] = { VM_RESP_SENT };
	UInt8 frame[VM_HEADER_LEN + 13 + VM_MAX_DM_TEXT];
	UInt8 utf8[VM_MAX_DM_TEXT];
	const VmMsgHeader *hdr;
	VmMsgHeader h;
	UInt16 len = 0, n;
	VmSent sent;
	UInt32 now = SessionNow(), wait;
	Err err;

	hdr = MessagesLock(index);
	if (!hdr)
		return dmErrIndexOutOfRange;
	h = *hdr;
	n = TxtPalmToUtf8((const Char *)(hdr + 1), hdr->textLen, utf8,
	                  h.conv.type == VM_CONV_CHANNEL ? VM_MAX_CHANNEL_TEXT : VM_MAX_DM_TEXT);
	MessagesRelease(index);
	if (n == 0) {
		MessagesSetStatus(index, VM_ST_FAILED, 0, 0);
		return vmErrTooLong;
	}
	if (h.conv.type == VM_CONV_CHANNEL)
		len = VmBuildSendChannelTxt(frame, sizeof(frame), h.conv.channel, h.timestamp, utf8, n);
	else
		len = VmBuildSendTxt(frame, sizeof(frame), h.conv.key, h.timestamp, h.attempt, utf8, n);

	MessagesSetStatus(index, VM_ST_PENDING, 0, 0);
	gChanged |= VM_CHANGED_MESSAGES;
	/* one try: a repeated send would go out twice */
	if (h.conv.type == VM_CONV_CHANNEL) {
		err = Request(frame, len, VM_RESP_OK, 1);
		MessagesSetStatus(index, err == errNone ? VM_ST_SENT : VM_ST_FAILED, 0, 0);
		return err;
	}
	err = RequestAny(frame, len, dmCodes, 1, 1);
	if (err == errNone && !VmParseSent(gDecoder.payload, gDecoder.len, &sent))
		err = vmErrBadReply;
	if (err != errNone) {
		MessagesSetStatus(index, VM_ST_FAILED, 0, 0);
		return err;
	}
	wait = sent.timeoutMs / 1000 * 3 / 2;
	if (wait < VM_ACK_MIN_SECS)
		wait = VM_ACK_MIN_SECS;
	MessagesSetStatus(index, sent.expectedAck ? VM_ST_SENT : VM_ST_ACKED, sent.expectedAck, now + wait);
	return errNone;
}

Err SessionSend(const VmConv *conv, const Char *text, UInt16 *index)
{
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	err = MessagesAddOutgoing(conv, text, SessionNow(), index);
	if (err != errNone)
		return err;
	return SendRecord(*index);
}

Err SessionRetry(UInt16 index)
{
	VmMsgHeader h;
	const VmMsgHeader *hdr;

	if (!gConnected)
		return serErrNotOpen;
	hdr = MessagesLock(index);
	if (!hdr)
		return dmErrIndexOutOfRange;
	h = *hdr;
	MessagesRelease(index);
	if (h.dir != VM_DIR_OUT || h.status != VM_ST_FAILED)
		return errNone;
	MessagesBumpAttempt(index);
	return SendRecord(index);
}

UInt16 SessionPoll(void)
{
	UInt16 changed;

	if (gConnected) {
		DrainInput();
		if (gMsgWaiting)
			SessionSyncMessages();
		if (MessagesExpireAcks(SessionNow()))
			gChanged |= VM_CHANGED_MESSAGES;
	}
	changed = gChanged;
	gChanged = 0;
	return changed;
}

/* Rooms logged into this session (key prefixes); the firmware keeps them alive. */
#define VM_MAX_LOGINS 4
static UInt8  gLogins[VM_MAX_LOGINS][6];
static UInt16 gLoginCount = 0;

Boolean SessionIsLoggedIn(const UInt8 *prefix6)
{
	UInt16 i;

	for (i = 0; i < gLoginCount; i++)
		if (MemCmp(gLogins[i], prefix6, 6) == 0)
			return true;
	return false;
}

Err SessionLogin(const UInt8 *pubKey, const Char *password, Boolean *isAdmin)
{
	static const UInt8 sentCode[] = { VM_RESP_SENT };
	static const UInt8 resultCodes[] = { VM_PUSH_LOGIN_SUCCESS, VM_PUSH_LOGIN_FAIL };
	UInt8 frame[VM_HEADER_LEN + 1 + VM_PUB_KEY_SIZE + VM_NAME_SIZE];
	UInt8 pw[VM_NAME_SIZE], prefix[6];
	UInt16 len, n;
	VmSent sent;
	Boolean ok;
	Int32 wait;
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	n = TxtPalmToUtf8(password, StrLen(password), pw, sizeof(pw));
	if (*password && n == 0)
		return vmErrTooLong;
	len = VmBuildLogin(frame, sizeof(frame), pubKey, pw, n);
	err = RequestAny(frame, len, sentCode, 1, 1);
	if (err == errNone && !VmParseSent(gDecoder.payload, gDecoder.len, &sent))
		err = vmErrBadReply;
	if (err != errNone)
		return err;
	/* the answer comes back over the mesh: wait as long as the radio estimates, plus slack */
	wait = (Int32)((sent.timeoutMs + 5000UL) / 1000UL) * SysTicksPerSecond();
	err = WaitForAny(resultCodes, 2, wait);
	if (err != errNone)
		return err;
	if (!VmParseLoginResult(gDecoder.payload, gDecoder.len, &ok, isAdmin, prefix))
		return vmErrBadReply;
	if (!ok)
		return vmErrLoginFailed;
	if (!SessionIsLoggedIn(prefix) && gLoginCount < VM_MAX_LOGINS)
		MemMove(gLogins[gLoginCount++], prefix, 6);
	return errNone;
}

Err SessionKeyCommand(UInt8 code, const UInt8 *pubKey)
{
	UInt8 frame[VM_HEADER_LEN + 1 + VM_PUB_KEY_SIZE];
	UInt16 len = VmBuildKeyCommand(frame, sizeof(frame), code, pubKey);

	if (!gConnected)
		return serErrNotOpen;
	return Request(frame, len, VM_RESP_OK, 1);
}

Err SessionAddContact(const UInt8 *pubKey, UInt8 type, const Char *name)
{
	UInt8 frame[VM_HEADER_LEN + 1 + VM_PUB_KEY_SIZE + 3 + VM_MAX_PATH_SIZE + VM_NAME_SIZE + 4];
	UInt8 utf8[VM_NAME_SIZE];
	UInt16 n = TxtPalmToUtf8(name, StrLen(name), utf8, VM_NAME_SIZE - 1), len;

	if (!gConnected)
		return serErrNotOpen;
	if (n == 0)
		return vmErrTooLong;
	len = VmBuildAddContact(frame, sizeof(frame), pubKey, type, utf8, n);
	return Request(frame, len, VM_RESP_OK, 1);
}

Err SessionSendAdvert(Boolean flood)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt16 len = VmBuildSendAdvert(frame, sizeof(frame), flood);

	if (!gConnected)
		return serErrNotOpen;
	return Request(frame, len, VM_RESP_OK, 1);
}

#define VM_REBOOT_WAIT  (4 * SysTicksPerSecond())   /* XIAO restart until it answers again */

Err SessionRebootAndReconnect(void)
{
	UInt8 frame[VM_HEADER_LEN + 7];
	UInt16 len = VmBuildReboot(frame, sizeof(frame));
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	DrainInput();
	err = TransportSend(frame, len);
	SessionDisconnect();
	if (err != errNone)
		return err;
	SysTaskDelay(VM_REBOOT_WAIT);
	return SessionConnect();
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
	gLoginCount = 0;          /* the radio keeps room sessions alive, but re-check after a reconnect */
}

Boolean SessionIsConnected(void)
{
	return gConnected;
}

Boolean SessionMessageWaiting(void)
{
	return gMsgWaiting;
}
