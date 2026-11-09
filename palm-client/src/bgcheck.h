/*
 * Checking for messages while the app is closed (Preferences > Check when
 * closed). The Alarm Manager launches the app every few minutes. That launch
 * has no globals (the app's other code segments aren't loaded either), so
 * BgAlarmTriggered sublaunches the app again with new globals
 * (VM_LAUNCH_BGCHECK). That launch connects, pulls what the radio queued,
 * and plays the notification sound. If anything new arrived, the alarm stays
 * for display, and BgDisplayAlarm shows an alert with the count per
 * conversation and an Open button.
 *
 * While the app is open it checks for itself: the alarm keeps coming and is
 * skipped.
 */
#ifndef VM_BGCHECK_H
#define VM_BGCHECK_H

#include <PalmOS.h>
#include "sections.h"

#define VM_LAUNCH_BGCHECK  sysAppLaunchCmdCustomBase   /* cmdPBP: UInt16 *, gets the sound kinds */

/* Minutes between checks; 0 = off. */
UInt16 BgInterval(void);
void   BgSetInterval(UInt16 minutes);

/* Sets the next alarm from the interval (clears it when off), spaced out after checks without a radio. */
void   BgSchedule(void);
/* After the app reached the radio itself: ends the spacing-out after checks that found none. */
void   BgRadioSeen(void);
/* BgSchedule unless an alarm is already pending (at exit: keeps the time Preferences showed). */
void   BgScheduleIfIdle(void);

/* Set while the app runs normally, so alarms that come meanwhile are skipped. */
void   BgSetAppOpen(Boolean open);

/* Launch codes; no globals needed. */
void   BgAlarmTriggered(SysAlarmTriggeredParamType *p);
void   BgDisplayAlarm(void);

/* "Last 18:05 ok, next 18:10" for Preferences: out holds 40. */
void   BgStatusText(Char *out) VM_SECT4;

/*
 * With checks on and the Slot link, leaves the module waking the Visor for
 * waiting messages after the app closes (SbWakeArm); otherwise removes that.
 */
void   BgArmWake(void) VM_SECT4;

/* The VM_LAUNCH_BGCHECK work, with globals: returns the enabled sound kinds (1 << VM_SND_*) found. */
UInt16 BgCheckRun(void) VM_SECT4;

#endif
