/*
 * Flood scopes on the Visor side (protocol in scope_proto.h): the list of
 * scope names to pick from, and each channel's choice of scope. The radio's
 * default scope is kept on the radio; per-channel scopes are kept here, by
 * channel key like mutes, and the session applies them before each send.
 */
#ifndef VM_SCOPES_H
#define VM_SCOPES_H

#include <PalmOS.h>
#include "sections.h"
#include "scope_proto.h"

/* A channel's choice */
#define VM_SCOPE_DEFAULT  0   /* whatever the radio's default is */
#define VM_SCOPE_NONE     1   /* unscoped (firmware v12+; older ones use the default) */
#define VM_SCOPE_NAMED    2

void    ScopesLoad(void) VM_SECT4;

/* The channel in slot's choice; for VM_SCOPE_NAMED also its key. */
UInt8   ScopesForChannel(UInt8 slot, UInt8 key[VM_SCOPE_KEY_SIZE]) VM_SECT4;

/* Options > Channel Scope: picks a scope for the channel on screen. */
void    ChannelScopeChooseCurrent(void) VM_SECT4;
/* The same for any channel (Home's tap-and-hold). */
void    ChannelScopeChoose(UInt8 slot) VM_SECT4;

/* Options > Flood Scopes: the list, and the radio's default. Done goes back to returnForm. */
void    ScopesFormOpen(UInt16 returnForm) VM_SECT4;
Boolean ScopesFormHandleEvent(EventType *event) VM_SECT4;

#endif
