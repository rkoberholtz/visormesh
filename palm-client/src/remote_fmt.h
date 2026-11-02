/*
 * Remote administration of repeaters and rooms: request builders, the status
 * and telemetry replies, and their text. Pure C (no Palm calls) so the host
 * tests cover it.
 */
#ifndef VM_REMOTE_FMT_H
#define VM_REMOTE_FMT_H

#include "sections.h"   /* this module is in the third code segment */
#include "protocol.h"

#define VM_CMD_SEND_STATUS_REQ     27
#define VM_CMD_SEND_TELEMETRY_REQ  39
#define VM_PUSH_STATUS_RESPONSE    0x87
#define VM_PUSH_TELEMETRY_RESPONSE 0x8B

/* A repeater's RepeaterStats (simple_repeater/MyMesh.h), as REQ_TYPE_GET_STATUS returns it. */
typedef struct {
	UInt16 battMv;
	UInt16 txQueue;
	Int16  noiseFloor;
	Int16  lastRssi;
	UInt32 recv, sent, txAirSecs, upSecs;
	UInt32 sentFlood, sentDirect, recvFlood, recvDirect;
	UInt16 errEvents;
	Int16  lastSnrQ;
	UInt16 directDups, floodDups;
	UInt32 rxAirSecs;
	UInt32 recvErrors;
} VmRepeaterStats;
#define VM_STATUS_LINES 6

/* SEND_TXT_MSG with txt_type CLI: a command for a node we're logged into as admin. */
UInt16  VmBuildSendCli(UInt8 *out, UInt16 outSize, const UInt8 *prefix6, UInt32 timestamp,
                       const UInt8 *text, UInt16 textLen) VM_SECT3;
UInt16  VmBuildStatusReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey) VM_SECT3;
UInt16  VmBuildTelemetryReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey) VM_SECT3;

/* PUSH_STATUS_RESPONSE: code, 0, prefix[6], stats. */
Boolean VmParseStatus(const UInt8 *p, UInt16 len, UInt8 *prefix6, VmRepeaterStats *out) VM_SECT3;
/* PUSH_TELEMETRY_RESPONSE: code, 0, prefix[6], Cayenne LPP data. */
Boolean VmParseTelemetry(const UInt8 *p, UInt16 len, UInt8 *prefix6, const UInt8 **data, UInt16 *dataLen) VM_SECT3;

/* Line n (0 .. VM_STATUS_LINES-1) of a status report. */
void    VmFormatStatusLine(const VmRepeaterStats *s, UInt16 n, Char *out) VM_SECT3;
/*
 * The next Cayenne LPP reading at *pos as text ("1 Voltage 4.12V"); advances
 * *pos. False at the end, or for a type it doesn't know (whose size is
 * unknown, so nothing after it can be read).
 */
Boolean VmLppNext(const UInt8 *d, UInt16 len, UInt16 *pos, Char *out) VM_SECT3;

#endif
