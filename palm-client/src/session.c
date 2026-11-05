/*
 * In the first code segment, and it must stay there: the reply-code tables
 * are static const inside functions, which read as garbage from an extra
 * segment (sections.h). It's also on the closed-app check's path.
 */
#include <PalmOS.h>
#include "protocol.h"
#include "transport.h"
#include "messages.h"
#include "channels.h"
#include "contacts.h"
#include "ui/text.h"
#include "session.h"
#include "heard.h"
#include "remote.h"
#include "sounds.h"
#include "mutes.h"
#include "scopes.h"
#include "rxpath.h"
#include "reactions.h"
#include "reaction_fmt.h"
#include "protocol2.h"
#include "packets_form.h"

#define VM_APP_NAME        "VisorMesh"
#define VM_REPLY_TIMEOUT   (2 * SysTicksPerSecond())
#define VM_CONNECT_TRIES   3     /* over USB the host bridge needs a moment to attach */
#define VM_SYNC_MAX        50    /* messages pulled per sync, so the UI isn't blocked forever */
#define VM_ACK_MIN_SECS    10    /* shortest wait for a DM ack, whatever the radio estimates */
#define VM_CLOCK_SLACK     60    /* radio clock behind ours by more than this gets set forward */
#define VM_CLOCK_MAX_STEP  (7UL * 86400UL)  /* a bigger jump means one clock is wrong: don't guess */
#define VM_CLOCK_VALID     1735689600UL     /* 2025-01-01: a radio clock before this was never set */
#define PALM_EPOCH_OFFSET  2082844800UL   /* 1904-01-01 (Palm) to 1970-01-01 (Unix) */

#define VM_YIELD_SLICE     (SysTicksPerSecond() / 10)   /* how often a connect looks for the user */

static VmFrameDecoder gDecoder;
static Boolean gConnected = false;
static Boolean gLost = false;         /* the radio stopped answering: see SessionTakeLost */
static UInt32  gHeardAt = 0;          /* ticks when the radio last sent anything */
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

static void NoteSound(UInt8 kind, const VmMessage *m);
static void SetConnected(void);

/* How a connect gives way to the user (SessionConnect). */
#define VM_YIELD_NEVER    0
#define VM_YIELD_BETWEEN  1    /* before each request: replies to a request are never abandoned */
#define VM_YIELD_ANYTIME  2    /* also while waiting: the handshake, whose link is closed on cancel */
static UInt8 gYield = VM_YIELD_NEVER;

/* True if the user tapped or pressed a key; the event stays queued for the app. */
static Boolean UserWaiting(void)
{
	return gYield != VM_YIELD_NEVER && EvtSysEventAvail(true);
}

/*
 * What the radio's flood-scope override is set to on this connection. It
 * outlasts connections (until the radio reboots), so it starts unknown and
 * the first flood send sets it.
 */
#define VM_SCOPE_UNKNOWN 0xFF
static UInt8 gScopeMode = VM_SCOPE_UNKNOWN;
static UInt8 gScopeKey[VM_SCOPE_KEY_SIZE];

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
	} else if (code == VM_PUSH_STATUS_RESPONSE) {
		if (RemoteOnStatus(gDecoder.payload, gDecoder.len))
			gChanged |= VM_CHANGED_REMOTE;
	} else if (code == VM_PUSH_TELEMETRY_RESPONSE) {
		if (RemoteOnTelemetry(gDecoder.payload, gDecoder.len))
			gChanged |= VM_CHANGED_REMOTE;
	} else if (code == VM_PUSH_LOG_RX) {
		VmRxLog rx;

		if (VmParseRxLog(gDecoder.payload, gDecoder.len, &rx)) {
			HeardRecord(&rx, SessionNow());
			RxPathNote(&rx, SessionNow());
			PacketsNote(&rx, SessionNow());
		}
	} else if (code == VM_PUSH_ADVERT || code == VM_PUSH_NEW_ADVERT || code == VM_PUSH_PATH_UPDATED) {
		/* NEW_ADVERT is the radio's first sight of a node (MyMesh::onDiscoveredContact is_new) */
		if (code == VM_PUSH_NEW_ADVERT)
			NoteSound(VM_SND_CONTACT, NULL);
		gChanged |= VM_CHANGED_CONTACTS;
	}
}

/*
 * Reads frames until one whose code is in codes[] arrives; pushes on the way
 * are acted on, RESP_ERR ends the wait. The reply is left in gDecoder.payload.
 */
static Err WaitForAny(const UInt8 *codes, UInt16 n, Int32 timeout)
{
	UInt32 deadline = TimGetTicks() + (timeout > 0 ? timeout : 0);
	Int32 left;
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
		/* the time left, read once: a negative timeout would mean wait forever */
		left = (Int32)(deadline - TimGetTicks());
		if (left <= 0)
			return vmErrNoReply;
		if (gYield == VM_YIELD_ANYTIME) {
			if (UserWaiting())
				return vmErrCancelled;
			if (left > VM_YIELD_SLICE)
				left = VM_YIELD_SLICE;
		}
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), left, &err);
		if (gRxLen)
			gHeardAt = TimGetTicks();
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
		gHeardAt = TimGetTicks();
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
		if (UserWaiting())
			return vmErrCancelled;
		DrainInput();
		err = TransportSend(frame, len);
		if (err == serErrTimeOut)
			continue;
		if (err != errNone)
			break;
		err = WaitForAny(codes, n, VM_REPLY_TIMEOUT);
		if (err != vmErrNoReply)
			return err;
	}
	if (err == serErrTimeOut)
		err = vmErrNoReply;
	/*
	 * The radio itself always answers at once, so the link is gone (the bridge
	 * stopped, a cable pulled): offline, rather than "online" with every
	 * command failing. gConnected first, so the disconnect sends nothing.
	 */
	if (gConnected) {
		gConnected = false;
		gLost = true;
		SessionDisconnect();
	}
	return err;
}

static Err Request(const UInt8 *frame, UInt16 len, UInt8 code, UInt16 tries)
{
	return RequestAny(frame, len, &code, 1, tries);
}

/* APP_START into gSelf. */
static Err StartApp(void)
{
	UInt8 frame[VM_HEADER_LEN + 8 + VM_NAME_SIZE];
	UInt16 len;
	Err err;

	len = VmBuildAppStart(frame, sizeof(frame), VM_APP_NAME);
	err = Request(frame, len, VM_RESP_SELF_INFO, 1);
	if (err == errNone && !VmParseSelfInfo(gDecoder.payload, gDecoder.len, &gSelf))
		err = vmErrBadReply;
	if (err == errNone)
		TxtUtf8NameToPalm(gSelf.name, sizeof(gSelf.name));
	return err;
}

/* DEVICE_QUERY + APP_START into gDevice/gSelf. The first request gets extra tries (USB attach). */
static Err QueryRadio(UInt16 firstTries)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt16 len;
	Err err;

	len = VmBuildDeviceQuery(frame, sizeof(frame));
	err = Request(frame, len, VM_RESP_DEVICE_INFO, firstTries);
	if (err == errNone && !VmParseDeviceInfo(gDecoder.payload, gDecoder.len, &gDevice))
		err = vmErrBadReply;
	return err == errNone ? StartApp() : err;
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

/* Opens the link and runs DEVICE_QUERY + APP_START; on error the link is closed. */
static Err OpenAndQuery(void)
{
	UInt16 t;
	Err err;

	SessionDisconnect();
	err = TransportOpen();
	if (err != errNone)
		return err;
	VmDecoderReset(&gDecoder);
	gRxPos = gRxLen = 0;
	gMsgWaiting = false;

	/* Over USB the Visor only appears to the host after SrmOpen; give the bridge time to attach. */
	gYield = VM_YIELD_ANYTIME;
	if (TransportGetPort() == VM_PORT_USB)
		for (t = 0; t < 10 && !UserWaiting(); t++)
			SysTaskDelay(VM_YIELD_SLICE);

	err = UserWaiting() ? vmErrCancelled : QueryRadio(VM_CONNECT_TRIES);
	gYield = VM_YIELD_BETWEEN;
	if (err != errNone) {
		TransportClose();
		return err;
	}
	SetConnected();
	return errNone;
}

static void SetConnected(void)
{
	gConnected = true;
	gHeardAt = TimGetTicks();
	gScopeMode = VM_SCOPE_UNKNOWN;
}

/* Best effort: the connection stands even if one of these fails; the stored copies stay. */
static void SyncAll(void)
{
	SessionSyncClock();
	if (ChannelsRefresh(gDevice.maxChannels) == errNone)
		gChanged |= VM_CHANGED_MESSAGES;
	if (ContactsUpdate() == errNone)
		gChanged |= VM_CHANGED_CONTACTS;
	if (SessionSyncMessages() == vmErrCancelled)
		gMsgWaiting = true;     /* the next poll carries on */
}

Err SessionConnect(void)
{
	Err err = OpenAndQuery();

	if (err == errNone)
		SyncAll();
	gYield = VM_YIELD_NEVER;
	return err;
}

/*
 * The stepped connect. Only finding the radio is in steps: with no bridge or
 * radio that's where the time goes (each send waits, each reply times out).
 * Once DEVICE_INFO arrives the rest takes a moment and gives way between
 * requests, as SessionConnect does.
 */
#define STEP_NONE    0
#define STEP_ATTACH  1    /* link open, giving the USB host time to attach */
#define STEP_QUERY   2    /* DEVICE_QUERY sent */
static UInt8  gStep = STEP_NONE;
static UInt16 gStepTries;
static UInt32 gStepAt;            /* ticks: the attach wait or the reply wait ends */

/* Whether a reply with this code came in, without waiting; pushes are handled, other frames dropped. */
static Boolean ReplyArrived(UInt8 code)
{
	Err err;

	for (;;) {
		while (gRxPos < gRxLen) {
			if (!VmDecoderFeed(&gDecoder, gRx[gRxPos++]) || gDecoder.len == 0)
				continue;
			if (gDecoder.payload[0] == code)
				return true;
			if (VM_IS_PUSH(gDecoder.payload[0]))
				HandlePush();
		}
		gRxPos = 0;
		gRxLen = TransportRecv(gRx, sizeof(gRx), 0, &err);
		if (gRxLen == 0)
			return false;
		gHeardAt = TimGetTicks();
	}
}

Err SessionConnectStart(void)
{
	Err err;

	SessionDisconnect();
	err = TransportOpen();
	if (err != errNone)
		return err;
	VmDecoderReset(&gDecoder);
	gRxPos = gRxLen = 0;
	gMsgWaiting = false;
	gStep = STEP_ATTACH;
	gStepTries = 0;
	gStepAt = TimGetTicks() + (TransportGetPort() == VM_PORT_USB ? SysTicksPerSecond() : 0);
	return vmErrPending;
}

Err SessionConnectStep(void)
{
	UInt8 frame[VM_HEADER_LEN + 2];
	UInt16 len;
	Err err;

	if (gStep == STEP_NONE)
		return serErrNotOpen;
	if (gStep == STEP_QUERY && ReplyArrived(VM_RESP_DEVICE_INFO)) {
		gStep = STEP_NONE;
		err = VmParseDeviceInfo(gDecoder.payload, gDecoder.len, &gDevice) ? errNone : vmErrBadReply;
		gYield = VM_YIELD_BETWEEN;
		if (err == errNone)
			err = StartApp();
		if (err == errNone) {
			SetConnected();
			SyncAll();
		} else {
			TransportClose();
		}
		gYield = VM_YIELD_NEVER;
		return err;
	}
	if ((Int32)(TimGetTicks() - gStepAt) < 0)
		return vmErrPending;
	if (gStepTries >= VM_CONNECT_TRIES) {
		gStep = STEP_NONE;
		TransportClose();
		return vmErrNoReply;
	}
	/* a send that fails counts as a try: with no host it fails until the bridge is back */
	len = VmBuildDeviceQuery(frame, sizeof(frame));
	TransportSendOnce(frame, len);
	gStepTries++;
	gStep = STEP_QUERY;
	gStepAt = TimGetTicks() + VM_REPLY_TIMEOUT;
	return vmErrPending;
}

Boolean SessionConnecting(void)
{
	return gStep != STEP_NONE;
}

/* Gives way like SessionConnect: a tap in the app in front ends the check early. */
Err SessionConnectLight(void)
{
	Err err = OpenAndQuery();

	if (err == errNone && SessionSyncMessages() == vmErrCancelled)
		gMsgWaiting = true;
	gYield = VM_YIELD_NEVER;
	return err;
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
		if (UserWaiting())
			return vmErrCancelled;    /* the rest of the list arrives and is drained */
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

/* Sound kinds noted since SessionTakeSounds, and the conversation of the newest of each. */
static UInt16  gSounds;
static VmConv  gSoundConv[VM_SND_COUNT];

/* m is the message, or NULL for an event without a conversation (a new contact). */
static void NoteSound(UInt8 kind, const VmMessage *m)
{
	VmConv *c = &gSoundConv[kind];

	MemSet(c, sizeof(*c), 0);
	if (m && m->isChannel) {
		c->type = VM_CONV_CHANNEL;
		c->channel = m->channelIdx;
	} else if (m) {
		c->type = VM_CONV_DIRECT;
		MemMove(c->key, m->senderPrefix, 6);
	}
	gSounds |= 1 << kind;
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

/* Room posts arrive signed, from the room's key; a room contact covers unsigned ones. */
static UInt8 MessageSound(const VmMessage *m)
{
	VmChannel ch;
	VmContact ct;

	if (MentionsUs(m))
		return VM_SND_MENTION;   /* even in a muted channel */
	if (m->isChannel) {
		if (!ChannelsFindSlot(m->channelIdx, &ch))
			return VM_SND_PRIVATE;
		if (ChannelMuted(&ch))
			return VM_SND_NONE;
		switch (ChannelKind(&ch)) {
		case VM_CHAN_PUBLIC:  return VM_SND_PUBLIC;
		case VM_CHAN_HASHTAG: return VM_SND_HASHTAG;
		default:              return VM_SND_PRIVATE;
		}
	}
	if (m->hasSigner || (ContactsFindByPrefix(m->senderPrefix, &ct) && ct.type == VM_ADV_TYPE_ROOM))
		return VM_SND_ROOM;
	return VM_SND_DIRECT;
}

UInt16 SessionTakeSounds(void)
{
	UInt16 kinds = gSounds;

	gSounds = 0;
	return kinds;
}

const VmConv *SessionSoundConv(UInt8 kind)
{
	return &gSoundConv[kind];
}

Err SessionSyncMessages(void)
{
	static const UInt8 codes[] = { VM_RESP_CONTACT_MSG_V3, VM_RESP_CHANNEL_MSG_V3, VM_RESP_NO_MORE_MESSAGES };
	UInt8 frame[VM_HEADER_LEN + 5];
	UInt16 len = VmBuildSyncNextMessage(frame, sizeof(frame));
	UInt16 n;
	VmMessage m;
	VmMsgExtra ex;
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
		if (!VmParseMessage(gDecoder.payload, gDecoder.len, &m))
			continue;
		if (!m.isChannel && m.txtType == VM_TXT_TYPE_CLI_DATA) {
			/* a CLI reply: for the admin console, not the conversation */
			RemoteOnCli(m.senderPrefix, m.text, m.textLen);
			gChanged |= VM_CHANGED_REMOTE;
			continue;
		}
		if (ReactionsTake(&m, SessionNow())) {
			gChanged |= VM_CHANGED_MESSAGES;
			continue;
		}
		MessageExtraFor(&m, SessionNow(), &ex);
		if (MessagesAddIncoming(&m, &ex, SessionNow()) == errNone) {
			UInt8 kind = MessageSound(&m);

			gChanged |= VM_CHANGED_MESSAGES;
			if (kind != VM_SND_NONE)
				NoteSound(kind, &m);
		}
	}
	gMsgWaiting = true;     /* more left: carry on at the next poll */
	return errNone;
}

/*
 * Sets the scope for the next flood send: the channel's own (scopes.h), or the
 * radio's default for everything else. Skipped when it's already set, and on
 * firmware without scopes.
 */
static Err ApplyScope(const VmConv *conv)
{
	UInt8 frame[VM_HEADER_LEN + 2 + VM_SCOPE_KEY_SIZE];
	UInt8 key[VM_SCOPE_KEY_SIZE];
	UInt8 mode = VM_SCOPE_DEFAULT;
	UInt16 len;
	Err err;

	if (gDevice.fwVerCode < VM_FW_SCOPES)
		return errNone;
	if (conv && conv->type == VM_CONV_CHANNEL)
		mode = ScopesForChannel(conv->channel, key);
	if (mode == VM_SCOPE_NONE && gDevice.fwVerCode < VM_FW_UNSCOPED)
		mode = VM_SCOPE_DEFAULT;
	if (mode == gScopeMode && (mode != VM_SCOPE_NAMED || MemCmp(key, gScopeKey, VM_SCOPE_KEY_SIZE) == 0))
		return errNone;
	if (mode == VM_SCOPE_NAMED)
		len = VmBuildSetScopeKey(frame, sizeof(frame), key);
	else if (mode == VM_SCOPE_NONE)
		len = VmBuildSetUnscoped(frame, sizeof(frame));
	else
		len = VmBuildSetScopeKey(frame, sizeof(frame), NULL);
	err = Request(frame, len, VM_RESP_OK, 1);
	if (err != errNone) {
		gScopeMode = VM_SCOPE_UNKNOWN;
		return err;
	}
	gScopeMode = mode;
	if (mode == VM_SCOPE_NAMED)
		MemMove(gScopeKey, key, VM_SCOPE_KEY_SIZE);
	return errNone;
}

Err SessionGetDefaultScope(Char *name)
{
	UInt8 frame[VM_HEADER_LEN + 5];
	UInt8 key[VM_SCOPE_KEY_SIZE];
	Err err;

	name[0] = 0;
	if (!gConnected)
		return serErrNotOpen;
	if (gDevice.fwVerCode < VM_FW_SCOPES)
		return errNone;
	err = Request(frame, VmBuildGetDefaultScope(frame, sizeof(frame)), VM_RESP_DEFAULT_FLOOD_SCOPE, 1);
	if (err == errNone && !VmParseDefaultScope(gDecoder.payload, gDecoder.len, name, key))
		err = vmErrBadReply;
	return err;
}

Err SessionSetDefaultScope(const Char *name)
{
	UInt8 frame[VM_HEADER_LEN + 1 + VM_SCOPE_NAME_SIZE + VM_SCOPE_KEY_SIZE];
	UInt16 len;

	if (!gConnected)
		return serErrNotOpen;
	if (gDevice.fwVerCode < VM_FW_SCOPES)
		return vmErrRadio;
	len = VmBuildSetDefaultScope(frame, sizeof(frame), name);
	if (len == 0)
		return vmErrBadReply;
	return Request(frame, len, VM_RESP_OK, 1);
}

Err SessionAskRegions(const UInt8 *pubKey, Char *names, UInt16 size)
{
	static const UInt8 sentCode[] = { VM_RESP_SENT };
	static const UInt8 replyCode[] = { VM_PUSH_BINARY_RESPONSE };
	UInt8 frame[VM_HEADER_LEN + 1 + VM_PUB_KEY_SIZE + 2];
	const UInt8 *list;
	UInt32 deadline, tag;
	UInt16 len, n;
	VmSent sent;
	Err err;

	names[0] = 0;
	if (!gConnected)
		return serErrNotOpen;
	len = VmBuildRegionsReq(frame, sizeof(frame), pubKey);
	err = RequestAny(frame, len, sentCode, 1, 1);
	if (err == errNone && !VmParseSent(gDecoder.payload, gDecoder.len, &sent))
		err = vmErrBadReply;
	if (err != errNone)
		return err;
	/* the answer comes back over the mesh: as long as the radio estimates, plus slack */
	deadline = TimGetTicks() + (sent.timeoutMs + 3000UL) / 1000UL * SysTicksPerSecond();
	for (;;) {
		err = WaitForAny(replyCode, 1, (Int32)(deadline - TimGetTicks()));
		if (err != errNone)
			return err;
		if (VmParseRegionsReply(gDecoder.payload, gDecoder.len, &tag, &list, &len) && tag == sent.expectedAck)
			break;
		if (TimGetTicks() >= deadline)
			return vmErrNoReply;
	}
	n = len < size - 1 ? len : size - 1;
	MemMove(names, list, n);
	names[n] = 0;
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
	err = ApplyScope(&h.conv);
	if (err != errNone) {
		MessagesSetStatus(index, VM_ST_FAILED, 0, 0);
		return err;
	}
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

Err SessionSendReaction(const VmConv *conv, const UInt8 *hash, const Char *target, Char glyph)
{
	static const UInt8 dmCodes[] = { VM_RESP_SENT };
	UInt8 frame[VM_HEADER_LEN + 13 + VM_MAX_DM_TEXT];
	UInt8 text[VM_MAX_DM_TEXT], name[VM_NAME_SIZE * 2], emoji[8];
	UInt16 len, n, nameLen = 0, emojiLen;
	Boolean channel = conv->type == VM_CONV_CHANNEL;
	UInt32 now = SessionNow();
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	emojiLen = TxtPalmToUtf8(&glyph, 1, emoji, sizeof(emoji));
	if (channel)
		nameLen = TxtPalmToUtf8(target, StrLen(target), name, sizeof(name));
	n = VmReactBuild(channel, name, nameLen, emoji, emojiLen, hash, text,
	                 channel ? VM_MAX_CHANNEL_TEXT : VM_MAX_DM_TEXT);
	if (n == 0 || emojiLen == 0 || (channel && nameLen == 0))
		return vmErrTooLong;
	err = ApplyScope(conv);
	if (err != errNone)
		return err;
	/* one try: a repeated send would go out twice; no ack is waited for */
	if (channel) {
		len = VmBuildSendChannelTxt(frame, sizeof(frame), conv->channel, now, text, n);
		err = Request(frame, len, VM_RESP_OK, 1);
	} else {
		len = VmBuildSendTxt(frame, sizeof(frame), conv->key, now, 0, text, n);
		err = RequestAny(frame, len, dmCodes, 1, 1);
	}
	if (err == errNone) {
		ReactionsAddMine(conv, hash, target, glyph, now);
		gChanged |= VM_CHANGED_MESSAGES;
	}
	return err;
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

Err SessionQuery(const UInt8 *frame, UInt16 len, UInt8 code, UInt8 *reply, UInt16 size, UInt16 *got)
{
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	if (len == 0)
		return vmErrBadReply;
	err = Request(frame, len, code, 2);
	if (err != errNone)
		return err;
	*got = gDecoder.len < size ? gDecoder.len : size;
	MemMove(reply, gDecoder.payload, *got);
	return errNone;
}

Err SessionTrace(const UInt8 *path, UInt16 pathBytes, UInt8 hashSize, VmTrace *out, UInt32 *tripMs)
{
	static const UInt8 sentCode[] = { VM_RESP_SENT };
	static const UInt8 traceCode[] = { VM_PUSH_TRACE_DATA };
	UInt8 frame[VM_HEADER_LEN + 10 + VM_TRACE_MAX];
	UInt32 tag = TimGetTicks() ^ ((UInt32)SysRandom(0) << 16) ^ TimGetSeconds(), start, deadline;
	UInt16 len = VmBuildTrace(frame, sizeof(frame), tag, hashSize, path, pathBytes);
	VmSent sent;
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	if (len == 0)
		return vmErrTooLong;
	start = TimGetTicks();
	/* one try: a repeated trace would go out twice */
	err = RequestAny(frame, len, sentCode, 1, 1);
	if (err == errNone && !VmParseSent(gDecoder.payload, gDecoder.len, &sent))
		err = vmErrBadReply;
	if (err != errNone)
		return err;
	/* the radio's estimate for the whole path, plus slack */
	deadline = start + (sent.timeoutMs + 2000UL) / 1000UL * SysTicksPerSecond();
	for (;;) {
		err = WaitForAny(traceCode, 1, (Int32)(deadline - TimGetTicks()));
		if (err != errNone)
			return err;
		if (VmParseTrace(gDecoder.payload, gDecoder.len, out) && out->tag == tag)
			break;
		if ((Int32)(TimGetTicks() - deadline) >= 0)
			return vmErrNoReply;
	}
	*tripMs = (TimGetTicks() - start) * 1000UL / SysTicksPerSecond();
	return errNone;
}

/* After a quiet minute, asks the radio the time, so a lost link shows (RequestAny) while idle. */
#define VM_QUIET_TICKS (60 * SysTicksPerSecond())

static void CheckAlive(void)
{
	UInt8 frame[VM_HEADER_LEN + 5];
	UInt16 len;

	if (TimGetTicks() - gHeardAt < VM_QUIET_TICKS)
		return;
	len = VmBuildGetTime(frame, sizeof(frame));
	Request(frame, len, VM_RESP_CURR_TIME, 1);
}

UInt16 SessionPoll(void)
{
	UInt16 changed;

	if (gConnected) {
		DrainInput();
		CheckAlive();
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
static Boolean gLoginAdmin[VM_MAX_LOGINS];
static UInt16 gLoginCount = 0;

Boolean SessionIsAdmin(const UInt8 *prefix6)
{
	UInt16 i;

	for (i = 0; i < gLoginCount; i++)
		if (MemCmp(gLogins[i], prefix6, 6) == 0)
			return gLoginAdmin[i];
	return false;
}

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
	err = ApplyScope(NULL);
	if (err != errNone)
		return err;
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
	for (n = 0; n < gLoginCount && MemCmp(gLogins[n], prefix, 6) != 0; n++)
		;
	if (n == gLoginCount && gLoginCount < VM_MAX_LOGINS)
		MemMove(gLogins[gLoginCount++], prefix, 6);
	if (n < gLoginCount)
		gLoginAdmin[n] = *isAdmin;
	return errNone;
}

Err SessionRequestSent(const UInt8 *frame, UInt16 len, VmSent *sent)
{
	static const UInt8 sentCode[] = { VM_RESP_SENT };
	Err err;

	if (!gConnected)
		return serErrNotOpen;
	if (len == 0)
		return vmErrTooLong;
	err = ApplyScope(NULL);
	if (err != errNone)
		return err;
	err = RequestAny(frame, len, sentCode, 1, 1);
	if (err == errNone && !VmParseSent(gDecoder.payload, gDecoder.len, sent))
		err = vmErrBadReply;
	return err;
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
	/* leave the radio on its default scope, for other apps and the next connection */
	if (gConnected && gScopeMode != VM_SCOPE_DEFAULT && gScopeMode != VM_SCOPE_UNKNOWN)
		ApplyScope(NULL);
	TransportClose();
	gConnected = false;
	gStep = STEP_NONE;
	gLoginCount = 0;          /* the radio keeps room sessions alive, but re-check after a reconnect */
}

Boolean SessionIsConnected(void)
{
	return gConnected;
}

Boolean SessionTakeLost(void)
{
	Boolean lost = gLost;

	gLost = false;
	return lost;
}

Boolean SessionMessageWaiting(void)
{
	return gMsgWaiting;
}
