/*
 * The conversation list: channels (slot order) then direct conversations
 * (most recent first), with unread counts, last activity and a preview,
 * built from the channel cache, the contacts and one pass over the messages.
 */
#ifndef VM_CONVOS_H
#define VM_CONVOS_H

#include <PalmOS.h>
#include "messages.h"

#define VM_ROW_SECTION 0
#define VM_ROW_CONV    1
#define VM_PREVIEW_LEN 40

typedef struct {
	UInt8   kind;                 /* VM_ROW_SECTION (name is the label) or VM_ROW_CONV */
	Char    marker;               /* glyph before the name */
	VmConv  conv;
	Char    name[VM_NAME_SIZE];
	UInt16  unread;
	UInt32  last;                 /* Unix seconds of the newest message, 0 = none */
	Boolean failed;               /* an outgoing message here wasn't delivered */
	Char    preview[VM_PREVIEW_LEN];
} VmConvRow;

/* Fills rows (sections included); returns how many. */
UInt16 ConvosBuild(VmConvRow *rows, UInt16 max);
/* Total unread messages across conversations. */
UInt16 ConvosUnread(const VmConvRow *rows, UInt16 n);

/* Display name for a conversation (channel name, contact name, or key prefix in hex). */
void   ConvName(const VmConv *conv, Char *out, UInt16 size);
/* Display name for a DM sender / contact key prefix. */
void   ContactNameForPrefix(const UInt8 *prefix, Char *out, UInt16 size);

#endif
