/*
 * The remote admin console for one repeater or room: CLI commands and their
 * replies, status and telemetry reports, as wrapped lines for the screen.
 * One request is outstanding at a time; "Read settings" queues several gets
 * and sends each when the previous one is answered or times out.
 */
#ifndef VM_REMOTE_H
#define VM_REMOTE_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the third code segment */
#include "protocol.h"
#include "remote_fmt.h"

#define VM_RLINE_CHARS 48

#define VM_RL_CMD    0   /* a command we sent */
#define VM_RL_REPLY  1   /* the node's answer */
#define VM_RL_INFO   2   /* a status / telemetry line */
#define VM_RL_NOTE   3   /* ours: "no answer", errors */

typedef struct {
	UInt8 kind;
	Char  text[VM_RLINE_CHARS];
} VmRLine;

/* Opens the console screen for a node (clears it if it was another node's). */
void          RemoteOpen(const VmContact *c) VM_SECT3;
const VmContact *RemoteTarget(void) VM_SECT3;

UInt16        RemoteLineCount(void) VM_SECT3;
const VmRLine *RemoteLine(UInt16 i) VM_SECT3;
Boolean       RemoteBusy(void) VM_SECT3;

/* Sends a command (Palm text) now, or queues it behind the outstanding request. */
void          RemoteCommand(const Char *text) VM_SECT3;
/* Queues a '|'-separated list of commands (the string must stay valid: a literal). */
void          RemoteBatch(const Char *cmds) VM_SECT3;
void          RemoteStatus(void) VM_SECT3;
void          RemoteTelemetry(void) VM_SECT3;

/* From the session, for frames addressed to us. True if the console took it. */
Boolean       RemoteOnCli(const UInt8 *prefix6, const UInt8 *utf8, UInt16 len) VM_SECT3;
Boolean       RemoteOnStatus(const UInt8 *frame, UInt16 len) VM_SECT3;
Boolean       RemoteOnTelemetry(const UInt8 *frame, UInt16 len) VM_SECT3;
/* Timeouts and the next queued command; true if lines changed. Called from the poll loop. */
Boolean       RemoteTick(void) VM_SECT3;

#endif
