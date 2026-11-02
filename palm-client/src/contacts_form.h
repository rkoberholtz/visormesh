#ifndef VM_CONTACTS_FORM_H
#define VM_CONTACTS_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the second code segment */

Boolean ContactsFormHandleEvent(EventType *event) VM_SECT2;

/* The type mark and name, and "direct" / "2 hops" / "flood" for a path length. */
Char        ContactTypeGlyph(UInt8 type) VM_SECT2;
const Char *ContactTypeName(UInt8 type) VM_SECT2;
void        ContactFormatRoute(Char *out, UInt8 pathLen) VM_SECT2;

#endif
