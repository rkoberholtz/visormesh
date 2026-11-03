/*
 * Screenshots (Options > Screenshot): copies the screen into a record of the
 * VisorMeshShots database. `make shots` fetches it over HotSync and
 * tools/shots.py turns the records into PNGs. A click means it was saved.
 *
 * All of this is in the first segment and uses no globals: the delayed
 * capture runs from a procedure alarm, inside whatever event loop is running
 * then (an open menu, a popup list), which is the point of it.
 */
#ifndef VM_SHOTS_H
#define VM_SHOTS_H

#include <PalmOS.h>

/* Record layout: this header (68k byte order), then rowBytes * height bytes of screen bits. */
typedef struct {
	UInt16 width;
	UInt16 height;
	UInt16 rowBytes;
	UInt8  pixelSize;    /* bits per pixel: 4 on the Neo (0 = white, 15 = black) */
	UInt8  reserved;
	UInt32 time;         /* TimGetSeconds: local time, seconds since 1904 */
} ShotHeader;

void ShotCapture(void);
/* Captures in about 'seconds' (the Alarm Manager counts whole seconds). */
void ShotLater(UInt16 seconds);
/* Cancels a pending ShotLater: the alarm calls into the app's code, so before the app exits. */
void ShotCancel(void);

#endif
