/*
 * Host tests for the request retry rules (src/request.c), with a scripted
 * link: what each send and each wait returns, and how many frames went out.
 * The point: a frame that went out is only sent again when tries allows it,
 * so messages, adverts and syncs (tries 1) are never sent twice.
 * Run with `make test`.
 */
#include <stdio.h>
#include "../src/request.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

#define vmErrRadio (appErrorClass | 2)

typedef struct {
	Err sends[8], waits[8];
	int nSends, nWaits;     /* script positions */
	int delivered;          /* sends that went out whole */
	int attempts;           /* every call to send */
	int cancelAt;           /* cancelled from this attempt on; 0 = never */
} Script;

static Boolean Cancelled(void *ctx)
{
	Script *s = (Script *)ctx;

	return s->cancelAt && s->attempts + 1 >= s->cancelAt;
}

static void Drain(void *ctx)
{
	(void)ctx;
}

static Err Send(void *ctx)
{
	Script *s = (Script *)ctx;
	Err e = s->sends[s->nSends++];

	s->attempts++;
	if (e == errNone)
		s->delivered++;
	return e;
}

static Err Wait(void *ctx)
{
	Script *s = (Script *)ctx;

	return s->waits[s->nWaits++];
}

static Err Run(Script *s, UInt16 tries, Boolean *gone)
{
	VmRequestLink l;

	l.cancelled = Cancelled;
	l.drain = Drain;
	l.send = Send;
	l.wait = Wait;
	l.ctx = s;
	return VmRequestRun(&l, tries, gone);
}

int main(void)
{
	Boolean gone;
	Err e;

	/* answered first time */
	{ Script s = { { errNone }, { errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 1, &gone);
	  CHECK(e == errNone && !gone && s.delivered == 1); }

	/* tries 1, sent but no answer: never sent again, and the link is reported gone */
	{ Script s = { { errNone, errNone }, { vmErrNoReply, errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 1, &gone);
	  CHECK(e == vmErrNoReply && gone && s.delivered == 1); }

	/* tries 2, safe to repeat: the second try answers */
	{ Script s = { { errNone, errNone }, { vmErrNoReply, errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 2, &gone);
	  CHECK(e == errNone && !gone && s.delivered == 2); }

	/* tries 2, no answer either time */
	{ Script s = { { errNone, errNone }, { vmErrNoReply, vmErrNoReply }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 2, &gone);
	  CHECK(e == vmErrNoReply && gone && s.delivered == 2); }

	/* tries 1, the frame didn't go twice (USB re-attaching), then did: delivered once */
	{ Script s = { { serErrTimeOut, serErrTimeOut, errNone }, { errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 1, &gone);
	  CHECK(e == errNone && !gone && s.attempts == 3 && s.delivered == 1); }

	/* frames that don't go stop after VM_UNSENT_TRIES, nothing delivered */
	{ Script s = { { serErrTimeOut, serErrTimeOut, serErrTimeOut, serErrTimeOut, errNone }, { errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 1, &gone);
	  CHECK(e == vmErrNoReply && gone && s.attempts == VM_UNSENT_TRIES && s.delivered == 0); }

	/* unsent sends don't use up tries: one didn't go, one went unanswered, tries 1 stops there */
	{ Script s = { { serErrTimeOut, errNone, errNone }, { vmErrNoReply, errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 1, &gone);
	  CHECK(e == vmErrNoReply && s.attempts == 2 && s.delivered == 1); }

	/* cut off part way: never resent, even with tries 2 */
	{ Script s = { { vmErrPartSent, errNone }, { errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 2, &gone);
	  CHECK(e == vmErrNoReply && gone && s.attempts == 1); }

	/* the radio said no: that's an answer, not a lost link, and not retried */
	{ Script s = { { errNone, errNone }, { vmErrRadio, errNone }, 0, 0, 0, 0, 0 };
	  e = Run(&s, 2, &gone);
	  CHECK(e == vmErrRadio && !gone && s.delivered == 1); }

	/* the user taps first: nothing sent */
	{ Script s = { { errNone }, { errNone }, 0, 0, 0, 0, 1 };
	  e = Run(&s, 2, &gone);
	  CHECK(e == vmErrCancelled && !gone && s.attempts == 0); }

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("request: all tests passed\n");
	return 0;
}
