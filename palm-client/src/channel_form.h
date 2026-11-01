#ifndef VM_CHANNEL_FORM_H
#define VM_CHANNEL_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the second code segment */

Boolean ChannelFormHandleEvent(EventType *event) VM_SECT2;
/* Options > Leave Channel: asks, then empties the open channel's slot. */
void    ChannelLeaveCurrent(void) VM_SECT2;

#endif
