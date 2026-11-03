/*
 * Muted channels: no sound for their messages (mentions of us still sound),
 * and their unread messages are left out of the Home title's total. Kept by
 * channel key rather than slot, so a mute survives the cache refresh and
 * follows the channel if it moves to another slot.
 */
#ifndef VM_MUTES_H
#define VM_MUTES_H

#include <PalmOS.h>
#include "sections.h"
#include "protocol.h"

void    MutesLoad(void) VM_SECT3;
Boolean ChannelMuted(const VmChannel *c) VM_SECT3;
Boolean ChannelSlotMuted(UInt8 slot) VM_SECT3;

/* Options > Mute Channel: toggles the channel on screen and says which way it went. */
void    ChannelMuteToggleCurrent(void) VM_SECT3;
/* The same for any channel (Home's tap-and-hold). */
void    ChannelMuteToggle(UInt8 slot) VM_SECT3;

#endif
