/*
 * Which repeaters a received message came through. The radio reports only a
 * hop count with a message; the path itself is in the RX log push it sends
 * as the packet arrives, before the app syncs the message. So the session
 * notes the last few text packets heard, and a message stored soon after
 * takes the oldest one that fits: same packet type and hop count, and for a
 * DM the sender's hash byte. Taking one also uses up the other copies of the
 * same packet (flood duplicates), which carry the same payload.
 * A guess when two arrive close together; no match means "not recorded".
 * Plain C with no Palm OS calls, so it also builds on the host for tests.
 */
#ifndef VM_RXPATH_H
#define VM_RXPATH_H

#include "protocol.h"   /* this module is in the third code segment */

#define VM_RXPATH_SLOTS 8
#define VM_RXPATH_SECS  120   /* how long a heard packet can wait for its message */

/* A text packet heard (others are ignored). */
void    RxPathNote(const VmRxLog *rx, UInt32 now) VM_SECT3;
/*
 * The packet for a message just synced: pathLen as the radio gave it (0xFF
 * direct), and for a DM the sender's key prefix (NULL for a channel).
 */
Boolean RxPathTake(Boolean isChannel, UInt8 pathLen, const UInt8 *senderPrefix, UInt32 now, VmRxLog *out) VM_SECT3;
void    RxPathClear(void) VM_SECT3;

#endif
