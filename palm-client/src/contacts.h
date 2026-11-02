/*
 * The contact list, kept in a Palm database so it survives between launches
 * (and is backed up by HotSync). Records are VmContact structs, sorted by name.
 */
#ifndef VM_CONTACTS_H
#define VM_CONTACTS_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the second code segment */
#include "protocol.h"

Err     ContactsOpen(void) VM_SECT2;
void    ContactsClose(void) VM_SECT2;
UInt16  ContactsCount(void) VM_SECT2;
Boolean ContactsGet(UInt16 index, VmContact *out) VM_SECT2;
/* Finds a contact by the first 6 bytes of its key (how DMs name their sender). */
Boolean ContactsFindByPrefix(const UInt8 *prefix6, VmContact *out) VM_SECT2;
/* Same with the first n bytes (room posts name their author by 4). */
Boolean ContactsFindByPrefixN(const UInt8 *prefix, UInt16 n, VmContact *out) VM_SECT2;

/*
 * Downloads the radio's whole contact list and replaces the stored one. The
 * old records are only removed once the download succeeds, so a failed
 * refresh leaves the previous list intact. count (may be NULL) gets the radio's total.
 */
Err     ContactsRefresh(UInt32 *count) VM_SECT2;

#endif
