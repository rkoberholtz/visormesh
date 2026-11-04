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
#define vmErrLoginFailed (appErrorClass | 5) /* a room/repeater refused the password */
#define vmErrCancelled (appErrorClass | 7)   /* the user tapped or pressed a key during a connect */
#define vmErrPending   (appErrorClass | 8)   /* SessionConnectStep: still connecting */

/* What SessionPoll found changed, so the screen on show can redraw. */
#define VM_CHANGED_MESSAGES  0x0001
#define VM_CHANGED_CONTACTS  0x0002
#define VM_CHANGED_ALERT     0x0004   /* something to sound for: see SessionTakeSounds */
#define VM_CHANGED_REMOTE    0x0008   /* the admin console has new lines */


/*
 * Opens the link and runs DEVICE_QUERY + APP_START, then refreshes the clock,
 * channels and contacts and pulls queued messages. On error the link is closed.
 *
 * A connect gives way to the user: a pen tap or key press (left in the queue
 * for the app) stops it. Before the radio has answered, that's
 * vmErrCancelled and the link is closed; after, the connection stands and
 * only the refresh steps left are skipped, between requests so no reply is
 * lost (the stored copies are kept, and the next poll pulls the messages).
 */
Err     SessionConnect(void);
/*
 * Like SessionConnect without refreshing the channels and contacts (the stored
 * copies are used): for the check while the app is closed.
 */
Err     SessionConnectLight(void);
void    SessionDisconnect(void);
/*
 * A connect in steps, so the app keeps running while it finds the radio
 * (reconnects after a wake or a lost link). Start opens the link; then call
 * Step a few times a second until it returns something other than
 * vmErrPending: errNone once connected and synced, or the error (link
 * closed). SessionConnect or SessionDisconnect abandons it.
 */
Err     SessionConnectStart(void);
Err     SessionConnectStep(void);
Boolean SessionConnecting(void);
Boolean SessionIsConnected(void);
/* True once after the radio stopped answering a command and the session closed itself; clears it. */
Boolean SessionTakeLost(void);
Boolean SessionMessageWaiting(void);

/* What the radio last reported (valid while connected). */
const VmDeviceInfo *SessionDevice(void);

/* The radio's default flood scope (scope_proto.h): name[VM_SCOPE_NAME_SIZE], "" for none. */
Err     SessionGetDefaultScope(Char *name);
/* name NULL: no default. */
Err     SessionSetDefaultScope(const Char *name);
/*
 * Asks a repeater for its regions (scope_proto.h): names gets them comma-separated
 * ("" if it has none). Only reaches neighbours; vmErrNoReply if it doesn't answer.
 */
Err     SessionAskRegions(const UInt8 *pubKey, Char *names, UInt16 size);
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

/* Now as Unix seconds (UTC), from the Visor's local clock and the UTC offset. */
UInt32  SessionNow(void);
/* A UTC Unix time as the Visor's local clock reads it (for TimSecondsToDateTime). */
UInt32  SessionLocalPalm(UInt32 utc);
/* Local time minus UTC, in minutes (e.g. -300 for EST). */
void    SessionSetUtcOffset(Int16 minutes);
Int16   SessionUtcOffset(void);
/* Sets the radio's clock to ours (UTC) if it's behind; it can only move forward. */
void    SessionSyncClock(void);
/* True if the radio's and the Visor's clocks differ by more than a week (one is wrong; not synced). */
Boolean SessionClockMismatch(void);
/* The radio's clock (UTC) as last read, advanced by the time since; 0 if never read. */
UInt32  SessionRadioTime(void);

/*
 * Call regularly (the app does on nilEvent): handles pushes, pulls waiting
 * messages and times out unacked DMs. Returns VM_CHANGED_* flags.
 */
UInt16  SessionPoll(void);
/* The sound kinds (1 << VM_SND_*, sounds.h) noted since the last call; clears them. */
UInt16  SessionTakeSounds(void);
/* The conversation of the newest message of that kind. */
const VmConv *SessionSoundConv(UInt8 kind);
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

/*
 * Logs into a room (or repeater) by its full key: waits for the answer over
 * the mesh (vmErrLoginFailed for a wrong password, vmErrNoReply if none).
 */
Err     SessionLogin(const UInt8 *pubKey, const Char *password, Boolean *isAdmin);
Boolean SessionIsLoggedIn(const UInt8 *prefix6);
Boolean SessionIsAdmin(const UInt8 *prefix6);
/* Sends a request whose first reply is RESP_SENT (CLI, status, telemetry); the answer comes later as a push. */
Err     SessionRequestSent(const UInt8 *frame, UInt16 len, VmSent *sent);
/* VM_CMD_RESET_PATH, VM_CMD_REMOVE_CONTACT, VM_CMD_LOGOUT for a full key; needs RESP_OK. */
Err     SessionKeyCommand(UInt8 code, const UInt8 *pubKey);
/* Adds a contact the radio hasn't heard yet, by its full key (unknown path). */
Err     SessionAddContact(const UInt8 *pubKey, UInt8 type, const Char *name);

#endif
