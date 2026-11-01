/*
 * Message store: one record per message in a Palm database (backed up by
 * HotSync), in the order they arrived. Text is stored in the Visor's
 * character set. Keeps the newest VM_MAX_MESSAGES.
 */
#ifndef VM_MESSAGES_H
#define VM_MESSAGES_H

#include <PalmOS.h>
#include "protocol.h"

#define VM_MAX_MESSAGES 500

/* A conversation: a channel slot, or a contact (by key prefix). */
#define VM_CONV_CHANNEL 0
#define VM_CONV_DIRECT  1

typedef struct {
	UInt8 type;          /* VM_CONV_CHANNEL / VM_CONV_DIRECT */
	UInt8 channel;       /* channel slot */
	UInt8 key[6];        /* contact key prefix */
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

Err     MessagesOpen(void);
void    MessagesClose(void);
UInt16  MessagesCount(void);
/* Locks record i; returns its header (text right after it). Call MessagesRelease(i) when done. */
const VmMsgHeader *MessagesLock(UInt16 i);
void    MessagesRelease(UInt16 i);

Boolean ConvEqual(const VmConv *a, const VmConv *b);

/* Stores a received message (converting its UTF-8 text). Returns its index. */
Err     MessagesAddIncoming(const VmMessage *m, UInt32 now);
/* Stores an outgoing message as pending; *index is its record. */
Err     MessagesAddOutgoing(const VmConv *conv, const Char *text, UInt32 now, UInt16 *index);
void    MessagesSetStatus(UInt16 index, UInt8 status, UInt32 ack, UInt32 ackDeadline);
/* Before a retry: the radio wants a higher attempt number for the same message. */
void    MessagesBumpAttempt(UInt16 index);
/* Marks the DM with this ack acked. Returns true if one matched. */
Boolean MessagesAcked(UInt32 ack);
/* Fails DMs whose ack deadline has passed. Returns true if any changed. */
Boolean MessagesExpireAcks(UInt32 now);
/* Clears the unread flag on a conversation's messages. */
void    MessagesMarkRead(const VmConv *conv);

#endif
