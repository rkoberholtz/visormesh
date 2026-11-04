/*
 * App-wide state shared by the screens: which conversation is open, the
 * saved link choice, and connecting with progress on screen.
 */
#ifndef VM_APP_H
#define VM_APP_H

#include <PalmOS.h>
#include "messages.h"

#define VM_CREATOR       'VMsh'
#define VM_PREFS_ID      0
#define VM_PREFS_VERSION 2

/* The conversation ConvoForm shows. */
extern VmConv gAppConv;

void AppOpenConversation(const VmConv *conv);

/*
 * Loads/saves the link choice (USB or serial, so the app reconnects the same way)
 * and the UTC offset (the Visor's clock has no time zone). AppLoadPrefs also
 * loads every module's own prefs (sounds, mutes, scopes, pins): the
 * closed-app check (BgCheckRun) relies on it, so add new ones there.
 */
void AppLoadPrefs(void);
void AppSavePrefs(void);

/*
 * Connects (if not already), drawing progress with the given status callback,
 * and reports a failure in an alert unless quiet. Returns the error.
 */
typedef void (*AppStatusFn)(const Char *text);
Err  AppConnect(AppStatusFn status, Boolean quiet);
/* True once the launch-time automatic connect has been tried. */
extern Boolean gAppAutoConnectTried;

/* Options > About: the app's version and, if connected, the radio's firmware. */
void AppAbout(void);

/* Short human text for a connect/send error. */
void AppErrorText(Err err, Char *out);

#endif
