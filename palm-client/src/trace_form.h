/*
 * Trace Path (Go menu, or a contact's More > Trace / Route) and Ping (a
 * repeater's More). Both send a trace, so both transmit.
 */
#ifndef VM_TRACE_FORM_H
#define VM_TRACE_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */
#include "protocol.h"

/* c NULL: any path; a contact: start from its route, and Set Route changes it. */
void    TraceFormOpen(const VmContact *c) VM_SECT5;
Boolean TraceFormHandleEvent(EventType *event) VM_SECT5;
/* A zero-hop trace to a repeater and back: the time and SNR both ways, in an alert. */
void    TracePing(const VmContact *c) VM_SECT5;

#endif
