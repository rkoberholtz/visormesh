#include <PalmOS.h>
#include "resources.h"
#include "session.h"
#include "ui/text.h"
#include "ui/fontmetrics.h"
#include "app.h"
#include "remote.h"

/* In an extra code segment (VM_SECT3): see sections.h for the rules. */
static void AddLine(UInt8 kind, const Char *text, UInt16 len) VM_SECT3;
static void AddText(UInt8 kind, const Char *text, UInt16 len) VM_SECT3;
static void Note(const Char *text) VM_SECT3;
static Boolean Mine(const UInt8 *prefix6) VM_SECT3;
static void Start(UInt8 kind, const Char *cmd, UInt16 cmdLen) VM_SECT3;
static void Next(void) VM_SECT3;
static void Done(void) VM_SECT3;
static void Enqueue(UInt8 kind, const Char *text, UInt16 len) VM_SECT3;

#define MAX_LINES   120
#define WRAP_W      148
#define SLACK_TICKS (8 * SysTicksPerSecond())    /* past the radio's estimate, before "no answer" */
#define MAX_QUEUE   12

#define REQ_NONE    0
#define REQ_CLI     1
#define REQ_STATUS  2
#define REQ_TELEM   3

static VmContact gTarget;
static VmRLine  *gLines = NULL;
static UInt16    gCount = 0;
static UInt8     gPending = REQ_NONE;
static UInt32    gDeadline;
/* queued commands: pointers into strings that outlive the queue, with lengths */
static const Char *gQueue[MAX_QUEUE];
static UInt16    gQueueLen[MAX_QUEUE];
static UInt8     gQueueKind[MAX_QUEUE];
static UInt16    gQHead = 0, gQCount = 0;
static Char      gTyped[MAX_QUEUE][VM_RLINE_CHARS * 3];   /* copies of typed commands */

void RemoteOpen(const VmContact *c)
{
	if (MemCmp(gTarget.pubKey, c->pubKey, VM_PUB_KEY_SIZE) != 0) {
		gCount = 0;
		gPending = REQ_NONE;
		gQCount = 0;
	}
	gTarget = *c;
	if (!gLines)
		gLines = MemPtrNew(sizeof(VmRLine) * MAX_LINES);
	FrmGotoForm(RemoteForm);
}

const VmContact *RemoteTarget(void)
{
	return &gTarget;
}

UInt16 RemoteLineCount(void)
{
	return gLines ? gCount : 0;
}

const VmRLine *RemoteLine(UInt16 i)
{
	return &gLines[i];
}

Boolean RemoteBusy(void)
{
	return gPending != REQ_NONE;
}

/* One display line; the oldest scrolls away when full. */
static void AddLine(UInt8 kind, const Char *text, UInt16 len)
{
	VmRLine *l;

	if (!gLines)
		return;
	if (gCount == MAX_LINES) {
		MemMove(gLines, gLines + 1, sizeof(VmRLine) * (MAX_LINES - 1));
		gCount--;
	}
	l = &gLines[gCount++];
	l->kind = kind;
	if (len > VM_RLINE_CHARS - 1)
		len = VM_RLINE_CHARS - 1;
	MemMove(l->text, text, len);
	l->text[len] = 0;
}

/* Wrapped to the console's width, breaking at line breaks too. */
static void AddText(UInt8 kind, const Char *text, UInt16 len)
{
	UInt16 off = 0, n, used;

	if (len == 0) {
		AddLine(kind, "", 0);
		return;
	}
	while (off < len) {
		n = TxtWrap(text + off, (UInt16)(len - off), WRAP_W, TxtSmallWidth, &used);
		if (used == 0)
			break;
		AddLine(kind, text + off, n);
		off += used;
	}
}

static void Note(const Char *text)
{
	AddText(VM_RL_NOTE, text, StrLen(text));
}

static Boolean Mine(const UInt8 *prefix6)
{
	return gLines && MemCmp(gTarget.pubKey, prefix6, 6) == 0;
}

/* Sends one request now. */
static void Start(UInt8 kind, const Char *cmd, UInt16 cmdLen)
{
	UInt8 frame[VM_HEADER_LEN + 13 + VM_MAX_DM_TEXT], utf8[VM_MAX_DM_TEXT];
	Char msg[48];
	UInt16 len = 0, n;
	VmSent sent;
	Err err;

	if (kind == REQ_CLI) {
		AddText(VM_RL_CMD, cmd, cmdLen);
		n = TxtPalmToUtf8(cmd, cmdLen, utf8, sizeof(utf8));
		if (n == 0) {
			Note("Too long to send.");
			return;
		}
		len = VmBuildSendCli(frame, sizeof(frame), gTarget.pubKey, 0, utf8, n);
	} else if (kind == REQ_STATUS) {
		Note("Asking for status...");
		len = VmBuildStatusReq(frame, sizeof(frame), gTarget.pubKey);
	} else {
		Note("Asking for telemetry...");
		len = VmBuildTelemetryReq(frame, sizeof(frame), gTarget.pubKey);
	}
	err = SessionRequestSent(frame, len, &sent);
	if (err != errNone) {
		AppErrorText(err, msg);
		Note(msg);
		return;
	}
	gPending = kind;
	gDeadline = TimGetTicks() + (sent.timeoutMs / 1000UL + 2) * SysTicksPerSecond() + SLACK_TICKS;
}

/* Starts queued requests until one is outstanding. */
static void Next(void)
{
	while (gPending == REQ_NONE && gQCount > 0) {
		UInt16 i = gQHead;

		gQHead = (gQHead + 1) % MAX_QUEUE;
		gQCount--;
		Start(gQueueKind[i], gQueue[i], gQueueLen[i]);
	}
}

static void Done(void)
{
	gPending = REQ_NONE;
	Next();
}

static void Enqueue(UInt8 kind, const Char *text, UInt16 len)
{
	UInt16 i;

	if (!SessionIsConnected()) {
		Note("Not connected to the radio.");
		return;
	}
	if (gQCount == MAX_QUEUE) {
		Note("Busy: wait for the answers.");
		return;
	}
	i = (gQHead + gQCount) % MAX_QUEUE;
	gQueueKind[i] = kind;
	gQueue[i] = text;
	gQueueLen[i] = len;
	gQCount++;
	Next();
}

void RemoteCommand(const Char *text)
{
	UInt16 i = (gQHead + gQCount) % MAX_QUEUE, len = StrLen(text);

	/* typed text goes away with the field: keep a copy in the queue slot */
	if (len > sizeof(gTyped[0]) - 1)
		len = sizeof(gTyped[0]) - 1;
	MemMove(gTyped[i], text, len);
	gTyped[i][len] = 0;
	Enqueue(REQ_CLI, gTyped[i], len);
}

void RemoteBatch(const Char *cmds)
{
	const Char *p = cmds;

	while (*p) {
		const Char *e = p;

		while (*e && *e != '|')
			e++;
		Enqueue(REQ_CLI, p, (UInt16)(e - p));
		p = *e ? e + 1 : e;
	}
}

void RemoteStatus(void)
{
	Enqueue(REQ_STATUS, "", 0);
}

void RemoteTelemetry(void)
{
	Enqueue(REQ_TELEM, "", 0);
}

Boolean RemoteOnCli(const UInt8 *prefix6, const UInt8 *utf8, UInt16 len)
{
	Char text[VM_MAX_FRAME];
	UInt16 n;

	if (!Mine(prefix6))
		return false;
	n = TxtUtf8ToPalm(utf8, len, text, sizeof(text));
	AddText(VM_RL_REPLY, text, n);
	if (gPending == REQ_CLI)
		Done();
	return true;
}

Boolean RemoteOnStatus(const UInt8 *frame, UInt16 len)
{
	VmRepeaterStats s;
	UInt8 prefix[6];
	Char line[64];
	UInt16 i;

	if (!VmParseStatus(frame, len, prefix, &s) || !Mine(prefix))
		return false;
	for (i = 0; i < VM_STATUS_LINES; i++) {
		VmFormatStatusLine(&s, i, line);
		AddText(VM_RL_INFO, line, StrLen(line));
	}
	if (gPending == REQ_STATUS)
		Done();
	return true;
}

Boolean RemoteOnTelemetry(const UInt8 *frame, UInt16 len)
{
	const UInt8 *data;
	UInt8 prefix[6];
	UInt16 dlen, pos = 0, shown = 0;
	Char line[64];

	if (!VmParseTelemetry(frame, len, prefix, &data, &dlen) || !Mine(prefix))
		return false;
	while (VmLppNext(data, dlen, &pos, line)) {
		AddText(VM_RL_INFO, line, StrLen(line));
		shown++;
	}
	if (!shown)
		Note("No telemetry readings (or not permitted).");
	if (gPending == REQ_TELEM)
		Done();
	return true;
}

Boolean RemoteTick(void)
{
	if (gPending == REQ_NONE || TimGetTicks() < gDeadline)
		return false;
	Note(gPending == REQ_CLI ? "(no answer: not admin, or out of range?)" : "(no answer)");
	Done();
	return true;
}
