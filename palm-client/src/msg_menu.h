/*
 * What can be done with one message in a conversation (tap and hold it, or
 * Options > Message Actions): react with an emoji, reply quoting it, send
 * its sender a DM, see the path it came by. Also the reactions shown under
 * messages: load a conversation's once per rebuild (MsgReactLoad), then ask
 * for each message's summary.
 */
#ifndef VM_MSG_MENU_H
#define VM_MSG_MENU_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the third code segment */

#define MSG_MENU_NONE    0
#define MSG_MENU_REFRESH 1   /* the transcript changed (a reaction, a retry): rebuild it */
#define MSG_MENU_TEXT    2   /* the compose field changed */

/*
 * The actions popup for message msg, beside its row b (b NULL: over the
 * transcript's bottom); list and area are the form's popup list and transcript gadget.
 */
UInt16 MsgMenuOpen(UInt16 msg, const RectangleType *b, UInt16 listId, UInt16 areaId, UInt16 fieldId) VM_SECT3;
/* The newest message in the conversation from someone else, for the menu command; false if none. */
Boolean MsgMenuNewest(UInt16 *msg) VM_SECT3;

void   MsgReactLoad(void) VM_SECT3;
void   MsgReactFree(void) VM_SECT3;
/*
 * Message msg's reactions as glyphs and counts ("<thumbs>2 <heart>"), kept
 * until the next MsgReactLoad: returns where it is for MsgReactText, -1 if none.
 */
Int16  MsgReactSummary(UInt16 msg, UInt16 *len) VM_SECT3;
const Char *MsgReactText(UInt16 off) VM_SECT3;
/* Who reacted to msg, in an alert. */
void   MsgReactWho(UInt16 msg) VM_SECT3;

#endif
