/*
 * Lines of text the screens build at run time, kept out of the forms so the
 * host tests (tests/test_screens.c) can feed them their longest values and
 * measure the result in the large font. Pure C, no Palm OS calls.
 * Each out[] needs SCR_LINE bytes.
 */
#ifndef VM_SCREEN_FMT_H
#define VM_SCREEN_FMT_H

#include "protocol2.h"

#define SCR_LINE  48

/* A local or UTC time as the screens show it. */
typedef struct {
	UInt8 month, day, hour, minute;
} ScrTime;

/* "45s", "12m", "3h 20m", "2d 4h" */
void   ScrSecs(Char *out, UInt32 secs) VM_SECT5;

/* Radio Stats: the report's lines into lines[], at most SCR_STATS_LINES; returns how many. */
#define SCR_STATS_LINES 9
UInt16 ScrStatsLines(const VmStats *st, const VmBattery *batt, Boolean hasBatt, Char lines[][SCR_LINE]) VM_SECT5;

/* Node: "Visor 11/8 14:02 (UTC-5:00)" */
void   ScrVisorClock(Char *out, const ScrTime *local, Int16 offsetMin) VM_SECT5;
/* Node: "Radio 11/8 19:02 UTC, sys 300", or "... UTC: MISMATCH"; radio NULL while it's unknown. */
void   ScrRadioClock(Char *out, const ScrTime *radio, Boolean mismatch, Int32 sysMinutesWest) VM_SECT5;

/* Contact: "<glyph> Room server, 3 hops, logged in" */
void   ScrContactSummary(Char *out, Char glyph, const Char *type, const Char *route, Boolean loggedIn) VM_SECT5;
/* Contact: "Advert 11-08 14:02 (3h ago)" */
void   ScrContactAdvert(Char *out, const ScrTime *when, const Char *ago) VM_SECT5;

/* Trace: "Back in 1.2 s, 3 hops" */
void   ScrTraceBack(Char *out, UInt32 ms, UInt16 hops) VM_SECT5;

#endif
