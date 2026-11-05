/*
 * Emoji reactions received and sent (reaction_fmt.h has the format), in
 * their own database so they never show as chat or count as unread. Each
 * names its message by hash, plus the sender's name in a channel; the
 * message may arrive after its reaction, so matching happens when a
 * conversation is drawn. Keeps the newest VM_MAX_REACTIONS.
 */
#ifndef VM_REACTIONS_H
#define VM_REACTIONS_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fourth code segment */
#include "messages.h"

#define VM_MAX_REACTIONS 200

typedef struct {
	VmConv conv;                    /* a channel's: its slot and secret prefix, like stored messages */
	UInt8  hash[5];                 /* the message reacted to (VM_REACT_HASH_BYTES) */
	UInt8  mine;
	Char   glyph;                   /* the emoji in the Visor's set (GLYPH_EMOJI if it has none) */
	Char   target[VM_NAME_SIZE];    /* channel: whose message it is */
	Char   from[VM_NAME_SIZE];      /* channel: who reacted; a DM's is the contact (empty) */
	UInt32 time;
} VmReact;

void    ReactionsClose(void) VM_SECT4;
/* If a synced message is a reaction, stores it (once) and returns true: it isn't chat. */
Boolean ReactionsTake(const VmMessage *m, UInt32 now) VM_SECT4;
/* Stores a reaction of ours that the radio took. */
void    ReactionsAddMine(const VmConv *conv, const UInt8 *hash, const Char *target, Char glyph, UInt32 now) VM_SECT4;
/* A conversation's reactions, oldest first: fills up to max, returns how many. */
UInt16  ReactionsFor(const VmConv *conv, VmReact *out, UInt16 max) VM_SECT4;

/* What's kept after a synced message's text: its hash and, if the RX log showed it, its path. */
void    MessageExtraFor(const VmMessage *m, UInt32 now, VmMsgExtra *ex) VM_SECT4;
/* A message's hash: from its trailer when received, else from its text (ours: what was sent). */
Boolean MessageHash(UInt16 index, UInt8 *hash) VM_SECT4;

#endif
