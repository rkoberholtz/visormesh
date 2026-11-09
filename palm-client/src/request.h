/*
 * The retry rules for a request to the radio, apart from the transport so
 * the host tests (tests/test_request.c) can drive them with a fake link:
 *
 *   - a frame that didn't go at all (serErrTimeOut from the transport: the
 *     USB host re-attaching) is sent again, up to VM_UNSENT_TRIES times,
 *     whatever tries says;
 *   - a frame that went out but got no reply is sent again only while tries
 *     lasts, so tries > 1 only for commands that are safe to repeat;
 *   - a frame cut off part way (vmErrPartSent) is never sent again.
 *
 * Pure C: session.c supplies the link.
 */
#ifndef VM_REQUEST_H
#define VM_REQUEST_H

#include "protocol.h"

#ifdef __palmos__
#include <PalmOS.h>
#else
typedef UInt16 Err;
#define errNone        0
#define serErrTimeOut  0x0305
#define appErrorClass  0x8000
#endif

/* session.h and transport.h define these on the Palm; the host tests get them here */
#ifndef vmErrNoReply
#define vmErrNoReply   (appErrorClass | 1)
#endif
#ifndef vmErrCancelled
#define vmErrCancelled (appErrorClass | 7)
#endif
#ifndef vmErrPartSent
#define vmErrPartSent  (appErrorClass | 9)
#endif

#define VM_UNSENT_TRIES 4    /* sends that didn't go, about a second each (transport_srm.c) */

typedef struct {
	Boolean (*cancelled)(void *ctx);   /* the user wants out */
	void    (*drain)(void *ctx);       /* drops stale input before a send */
	Err     (*send)(void *ctx);        /* the frame: errNone, serErrTimeOut (nothing went) or another error */
	Err     (*wait)(void *ctx);        /* the reply: errNone, vmErrNoReply or another error */
	void     *ctx;
} VmRequestLink;

/*
 * Runs one request. *linkGone is set when it ended without an answer from
 * the radio (which always answers at once), so the caller can go offline.
 * serErrTimeOut and vmErrPartSent come back as vmErrNoReply.
 */
Err VmRequestRun(const VmRequestLink *l, UInt16 tries, Boolean *linkGone);

#endif
