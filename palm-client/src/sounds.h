/*
 * Notification sounds: a short tune for each kind of event, each of which can
 * be turned off on the Preferences screen. Played at the system's alarm volume
 * (Prefs > General > Alarm Sound), so muting taps doesn't mute messages.
 * The same screen has the Text size option (the larger UI font).
 */
#ifndef VM_SOUNDS_H
#define VM_SOUNDS_H

#include <PalmOS.h>
#include "sections.h"
#include "messages.h"

/* In priority order: one sound per poll, for the first kind that applies. */
#define VM_SND_DIRECT   0   /* a direct message */
#define VM_SND_MENTION  1   /* @[our name] anywhere */
#define VM_SND_ROOM     2   /* a room server post */
#define VM_SND_PRIVATE  3   /* a private (secret-key) channel */
#define VM_SND_HASHTAG  4   /* a #hashtag channel */
#define VM_SND_PUBLIC   5   /* the Public channel */
#define VM_SND_CONTACT  6   /* a node we haven't seen advertised itself */
#define VM_SND_COUNT    7
#define VM_SND_NONE     0xFF   /* nothing to sound for (a muted channel) */

void    SoundsLoad(void) VM_SECT3;
void    SoundsPlay(UInt8 kind) VM_SECT3;

/*
 * Plays the first enabled kind in 'kinds' (a bitmask of 1 << VM_SND_*), skipping
 * messages in the conversation on screen (onScreen; NULL if none is).
 */
void    SoundsNotify(UInt16 kinds, const VmConv *onScreen) VM_SECT3;

/* The kinds turned on (1 << VM_SND_*). */
UInt16  SoundsEnabledKinds(void) VM_SECT3;

/* Text size: the larger UI font (ui/draw.h). */
Boolean SoundsLargeText(void) VM_SECT3;

/* The Preferences screen (SoundsForm); Done goes back to returnForm. */
void    SoundsFormOpen(UInt16 returnForm) VM_SECT3;
Boolean SoundsFormHandleEvent(EventType *event) VM_SECT3;

#endif
