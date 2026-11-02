#ifndef VM_CONTACT_FORM_H
#define VM_CONTACT_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the third code segment */
#include "protocol.h"

/* Shows a contact's detail screen. */
void    ContactFormOpen(const VmContact *c) VM_SECT3;
Boolean ContactFormHandleEvent(EventType *event) VM_SECT3;
Boolean ContactAddFormHandleEvent(EventType *event) VM_SECT3;

#endif
