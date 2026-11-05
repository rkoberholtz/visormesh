/*
 * Message store: one record per message in a Palm database (backed up by
 * HotSync), in the order they arrived. Text is stored in the Visor's
 * character set. Keeps the newest VM_MAX_MESSAGES.
 */
#ifndef VM_MESSAGES_H
#define VM_MESSAGES_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the second code segment */
#include "protocol.h"

#define VM_MAX_MESSAGES 500

/*
 * A conversation: a channel slot, or a contact (by key prefix). Stored
 * channel messages also carry the channel's secret prefix in key, so their
 * history follows the channel when its slot changes (channels.c).
 */
#define VM_CONV_CHANNEL 0
#define VM_CONV_DIRECT  1
#define VM_SLOT_GONE    0xFF

typedef struct {
	UInt8 type;          /* VM_CONV_CHANNEL / VM_CONV_DIRECT */
	UInt8 channel;       /* channel slot; VM_SLOT_GONE: a channel that was left */
	UInt8 key[6];        /* contact key prefix; a stored channel message: the secret's */
} VmConv;

#define VM_DIR_IN   0
#define VM_DIR_OUT  1

#define VM_ST_NONE     0   /* received */
#define VM_ST_PENDING  1   /* outgoing, not yet accepted by the radio */
#define VM_ST_SENT     2   /* radio sent it; a DM waits for its ack, a channel message is done */
#define VM_ST_ACKED    3
#define VM_ST_FAILED   4

typedef struct {
	VmConv conv;
	UInt8  dir;
	UInt8  status;
	UInt8  unread;
	Int8   snrQuarterDb;
	UInt8  pathLen;
	UInt8  attempt;
	UInt32 timestamp;    /* sender's clock (Unix seconds) */
	UInt32 received;     /* our clock when stored (Unix seconds) */
	UInt32 ack;          /* outgoing DM: expected ack */
	UInt32 ackDeadline;  /* outgoing DM: Unix seconds after which it counts as failed */
	Char   sender[VM_NAME_SIZE];   /* channel messages: who sent it */
	UInt16 textLen;
	/* text follows, textLen bytes plus a NUL */
} VmMsgHeader;

/*
 * Kept after a received message's text (records from before it have none):
 * its reaction hash, worked out from the UTF-8 as received, and the
 * repeaters it came through when the radio's RX log showed them (rxpath.h).
 */
typedef struct {
	UInt8  hasHash;
	UInt8  hash[5];      /* VM_REACT_HASH_BYTES */
	UInt8  hasPath;
	UInt8  hashSize;     /* bytes per repeater hash */
	UInt8  pathBytes;    /* in path: the first VM_RX_PATH_MAX */
	UInt8  hops;
	UInt8  path[VM_RX_PATH_MAX];
} VmMsgExtra;

Err     MessagesOpen(void) VM_SECT2;
void    MessagesClose(void) VM_SECT2;
UInt16  MessagesCount(void) VM_SECT2;
/* Locks record i; returns its header (text right after it). Call MessagesRelease(i) when done. */
const VmMsgHeader *MessagesLock(UInt16 i) VM_SECT2;
void    MessagesRelease(UInt16 i) VM_SECT2;
/* Record i's trailer; false if it has none. */
Boolean MessagesExtra(UInt16 i, VmMsgExtra *ex) VM_SECT2;
/* Record i's unique ID (0 if none): unlike the index, it stays put when old messages drop off. */
UInt32  MessagesUniqueId(UInt16 i) VM_SECT2;

Boolean ConvEqual(const VmConv *a, const VmConv *b) VM_SECT2;

/* Stores a received message (converting its UTF-8 text), with ex after it if given. */
Err     MessagesAddIncoming(const VmMessage *m, const VmMsgExtra *ex, UInt32 now) VM_SECT2;
/* Stores an outgoing message as pending; *index is its record. */
Err     MessagesAddOutgoing(const VmConv *conv, const Char *text, UInt32 now, UInt16 *index) VM_SECT2;
void    MessagesSetStatus(UInt16 index, UInt8 status, UInt32 ack, UInt32 ackDeadline) VM_SECT2;
/* Before a retry: the radio wants a higher attempt number for the same message. */
void    MessagesBumpAttempt(UInt16 index) VM_SECT2;
/* Marks the DM with this ack acked. Returns true if one matched. */
Boolean MessagesAcked(UInt32 ack) VM_SECT2;
/* Fails DMs whose ack deadline has passed. Returns true if any changed. */
Boolean MessagesExpireAcks(UInt32 now) VM_SECT2;
/* Clears the unread flag on a conversation's messages. */
void    MessagesMarkRead(const VmConv *conv) VM_SECT2;
/*
 * Calls fn for each channel message's conversation; fn may change it (its
 * slot, its key) and the record is rewritten if it did.
 */
typedef void (*MessagesConvFn)(VmConv *conv, void *ctx);
void    MessagesRefileChannels(MessagesConvFn fn, void *ctx) VM_SECT2;

#endif
