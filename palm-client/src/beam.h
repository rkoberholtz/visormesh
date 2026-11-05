/*
 * Beaming contacts and channels to another Visor (or any Palm running
 * VisorMesh) over IR, as meshcore:// links (protocol2.h), the same links
 * MeshCore One and the other apps share as QR codes. What arrives waits in
 * an inbox database until the app asks whether to add it.
 */
#ifndef VM_BEAM_H
#define VM_BEAM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment; BeamReceive in the first */
#include "protocol.h"

#define VM_BEAM_MAX     320            /* bytes: the longest link, with room */
#define VM_INBOX_NAME   "VisorMeshInbox"
#define VM_FTR_INBOX    2              /* feature: set when a beam arrives */
#define VM_BEAM_EXT     "vmsh"

/* sysAppLaunchCmdExgReceiveData, with or without globals (beam_rx.c, segment 1). */
Err     BeamReceive(ExgSocketPtr s);

void    BeamContact(const VmContact *c) VM_SECT5;
/* Our own contact, from the radio's SELF_INFO. */
void    BeamSelf(void) VM_SECT5;
void    BeamChannel(UInt8 slot) VM_SECT5;
/* At launch: notes an inbox left from before, so BeamInboxWaiting sees it. */
void    BeamInboxNote(void) VM_SECT5;
Boolean BeamInboxWaiting(void) VM_SECT5;
/* Asks about each beam received: add it, keep it for later, or discard it. */
void    BeamCheckInbox(void) VM_SECT5;

#endif
