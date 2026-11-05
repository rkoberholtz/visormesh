/* Heard Packets (Go menu): the last packets the radio received, from its RX log pushes. */
#ifndef VM_PACKETS_FORM_H
#define VM_PACKETS_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */
#include "protocol.h"

/* Every RX log push (session.c), whatever screen is open. */
void    PacketsNote(const VmRxLog *rx, UInt32 now) VM_SECT5;
Boolean PacketsFormHandleEvent(EventType *event) VM_SECT5;
/* On idle events while the screen is open: shows new packets when it's at the top. */
void    PacketsFormIdle(void) VM_SECT5;

#endif
