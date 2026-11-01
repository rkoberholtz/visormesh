/*
 * A connection to the companion radio: request/reply on top of the transport
 * and protocol layers. Pushes that arrive while waiting for a reply are
 * skipped; PUSH_MSG_WAITING is remembered for SessionMessageWaiting().
 */
#ifndef VM_SESSION_H
#define VM_SESSION_H

#include <PalmOS.h>
#include "protocol.h"
#include "messages.h"

#define vmErrNoReply   (appErrorClass | 1)   /* radio didn't answer in time */
#define vmErrRadio     (appErrorClass | 2)   /* radio replied RESP_ERR */
#define vmErrBadReply  (appErrorClass | 3)   /* reply had the right code but didn't parse */
#define vmErrTooLong   (appErrorClass | 4)   /* message text over the byte limit */

/* What SessionPoll found changed, so the screen on show can redraw. */
#define VM_CHANGED_MESSAGES  0x0001
#define VM_CHANGED_CONTACTS  0x0002


/* Opens the link and runs DEVICE_QUERY + APP_START. On error the link is closed. */
Err     SessionConnect(void);
void    SessionDisconnect(void);
Boolean SessionIsConnected(void);
Boolean SessionMessageWaiting(void);

/* What the radio last reported (valid while connected). */
const VmDeviceInfo *SessionDevice(void);
const VmSelfInfo   *SessionSelf(void);

/* Sends a complete command frame and requires RESP_OK. */
Err     SessionCommand(const UInt8 *frame, UInt16 len);
/*
 * Asks the radio to reboot (it restarts without replying), drops the link,
 * waits for it to come back and reconnects. Used after radio-parameter
 * changes, which the receiver only picks up after a restart.
 */
Err     SessionRebootAndReconnect(void);
/* Re-reads DEVICE_INFO and SELF_INFO, e.g. after changing settings. */
Err     SessionRefresh(void);

/*
 * GET_CONTACTS: calls fn for each contact changed since 'since' (0 = all).
 * count is the radio's total (not the filtered number); lastMod is the newest
 * lastmod, to pass as 'since' next time. A non-zero return from fn stops the
 * download (the rest of the list is left in the radio's output).
 */
typedef Err (*SessionContactFn)(const VmContact *c, void *ctx);
Err     SessionGetContacts(UInt32 since, SessionContactFn fn, void *ctx, UInt32 *count, UInt32 *lastMod);

/* Our clock in Unix seconds (message timestamps). */
UInt32  SessionNow(void);

/*
 * Call regularly (the app does on nilEvent): handles pushes, pulls waiting
 * messages and times out unacked DMs. Returns VM_CHANGED_* flags.
 */
UInt16  SessionPoll(void);
/* Pulls queued messages into the message store (SYNC_NEXT_MESSAGE until none are left). */
Err     SessionSyncMessages(void);
Err     SessionGetChannel(UInt8 slot, VmChannel *out);

/*
 * Stores an outgoing message and sends it; *index is its record. Status goes
 * pending -> sent (channel: final) -> acked / failed. Never retried by itself.
 */
Err     SessionSend(const VmConv *conv, const Char *text, UInt16 *index);
/* Sends a failed message again (next attempt number). */
Err     SessionRetry(UInt16 index);
Err     SessionSendAdvert(Boolean flood);

#endif
