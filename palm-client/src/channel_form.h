#ifndef VM_CHANNEL_FORM_H
#define VM_CHANNEL_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fourth code segment */

Boolean ChannelFormHandleEvent(EventType *event) VM_SECT4;
/*
 * Joins a channel and opens it: secret NULL for a #hashtag (the key comes
 * from the name). One already joined with that key just opens. Errors show
 * in an alert; false if it wasn't joined.
 */
Boolean ChannelJoin(const Char *name, const UInt8 *secret) VM_SECT4;
/* Options > Leave Channel: asks, then empties the open channel's slot. */
void    ChannelLeaveCurrent(void) VM_SECT4;
/* The same for any channel (Home's tap-and-hold); goes back to Home. */
void    ChannelLeave(UInt8 slot) VM_SECT4;

#endif
