/*
 * Contacts > Remove Stale Contacts: frees the radio's contact slots from
 * nodes it hasn't heard an advert from in a while. Kept: contacts with
 * messages on the Visor, ones never heard (added by key) and ones we're
 * logged into.
 */
#ifndef VM_STALE_H
#define VM_STALE_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

/* Asks how old, then confirms with the count; errors show in an alert. True if any were removed. */
Boolean StaleRemove(void) VM_SECT5;

#endif
