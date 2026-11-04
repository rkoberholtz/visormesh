/*
 * The right end of a form's title bar: the connection state ("online",
 * "offline", "Connecting..." while it connects), drawn in the UI font. Tapping
 * it while offline is the same as Options > Connect. Call TitleStatusReset
 * when the form opens.
 */
#ifndef VM_TITLE_H
#define VM_TITLE_H

#include <PalmOS.h>
#include "../sections.h"

void    TitleStatusReset(void) VM_SECT3;
/* Draws text there, erasing back to where the previous text started. */
void    TitleStatus(const Char *text) VM_SECT3;
/* "online" or "offline", after prefix (may be empty). */
void    TitleConnState(const Char *prefix) VM_SECT3;
/* The left edge of what's drawn there now (160: nothing). */
Coord   TitleStatusLeft(void) VM_SECT3;
/* A tap on it while offline: queues Options > Connect. */
Boolean TitleStatusTapped(const EventType *event) VM_SECT3;
/* Cuts title with an ellipsis so its tab ends before "offline"; returns where the tab ends. */
Coord   TitleFit(Char *title) VM_SECT3;

#endif
