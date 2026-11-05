/*
 * Conversation extras, kept out of the second segment: a tap on a #hashtag
 * joins (or opens) that channel, and the @mention picker ("@" in the compose
 * field, or Options > Insert Mention) lists recent senders and contacts.
 */
#ifndef VM_CONVO_EXTRA_H
#define VM_CONVO_EXTRA_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

/* tag is a #hashtag (protocol2.h VmHashtagAt): opens it if joined, else asks to join. */
void    ConvoOpenHashtag(const Char *tag) VM_SECT5;
/*
 * The picker (listId on the active form) for the field: picking inserts
 * "@[Name] ". typedAt: the user typed '@', which goes in if they cancel.
 * Returns true if the field changed.
 */
Boolean ConvoMentionPick(UInt16 listId, UInt16 fieldId, Boolean typedAt) VM_SECT5;

#endif
