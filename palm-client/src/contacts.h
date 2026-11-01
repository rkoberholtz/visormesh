/*
 * The contact list, kept in a Palm database so it survives between launches
 * (and is backed up by HotSync). Records are VmContact structs, sorted by name.
 */
#ifndef VM_CONTACTS_H
#define VM_CONTACTS_H

#include <PalmOS.h>
#include "protocol.h"

Err     ContactsOpen(void);
void    ContactsClose(void);
UInt16  ContactsCount(void);
Boolean ContactsGet(UInt16 index, VmContact *out);
/* Finds a contact by the first 6 bytes of its key (how DMs name their sender). */
Boolean ContactsFindByPrefix(const UInt8 *prefix6, VmContact *out);

/*
 * Downloads the radio's whole contact list and replaces the stored one. The
 * old records are only removed once the download succeeds, so a failed
 * refresh leaves the previous list intact. count (may be NULL) gets the radio's total.
 */
Err     ContactsRefresh(UInt32 *count);

#endif
