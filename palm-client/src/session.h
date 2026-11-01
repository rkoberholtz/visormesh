/*
 * A connection to the companion radio: request/reply on top of the transport
 * and protocol layers. Pushes that arrive while waiting for a reply are
 * skipped; PUSH_MSG_WAITING is remembered for SessionMessageWaiting().
 */
#ifndef VM_SESSION_H
#define VM_SESSION_H

#include <PalmOS.h>
#include "protocol.h"

#define vmErrNoReply   (appErrorClass | 1)   /* radio didn't answer in time */
#define vmErrRadio     (appErrorClass | 2)   /* radio replied RESP_ERR */
#define vmErrBadReply  (appErrorClass | 3)   /* reply had the right code but didn't parse */

/* Opens the link and runs DEVICE_QUERY + APP_START. On error the link is closed. */
Err     SessionConnect(VmDeviceInfo *device, VmSelfInfo *self);
void    SessionDisconnect(void);
Boolean SessionIsConnected(void);
Boolean SessionMessageWaiting(void);

#endif
