/*
 * Blocked senders: their new messages and reactions are dropped as they
 * sync (what's already stored stays). A contact is blocked by its key and
 * its name, since channels name senders only by name; a channel sender with
 * no contact by name alone. Kept in a saved preference.
 */
#ifndef VM_BLOCKS_H
#define VM_BLOCKS_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */
#include "protocol.h"

#define VM_BLOCKS_MAX     32
#define VM_BLOCK_KEY_SIZE 6

/* True if a synced message is from someone blocked. */
Boolean BlocksMessage(const VmMessage *m) VM_SECT5;
Boolean BlocksIsContact(const VmContact *c) VM_SECT5;
Boolean BlocksIsName(const Char *name) VM_SECT5;
/* Asks, then blocks or unblocks c. */
void    BlocksToggleContact(const VmContact *c) VM_SECT5;
/* Asks, then blocks a channel sender by name; true if blocked. */
Boolean BlocksAskName(const Char *name) VM_SECT5;
/* Contacts > Blocked Senders: the list in a popup (listId on the active form); picking one unblocks it. */
void    BlocksManage(UInt16 listId) VM_SECT5;

#endif
