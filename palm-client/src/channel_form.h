#ifndef VM_CHANNEL_FORM_H
#define VM_CHANNEL_FORM_H

#include <PalmOS.h>

Boolean ChannelFormHandleEvent(EventType *event);
/* Options > Leave Channel: asks, then empties the open channel's slot. */
void    ChannelLeaveCurrent(void);

#endif
