/*
 * Companion commands added after protocol.c filled its segment: trace paths,
 * the radio's stats and battery, a contact's route, plus the meshcore://
 * share links (as MeshCore One and the other apps write them) and finding a
 * #hashtag in text. Same rules as protocol.h: plain C, little-endian fields
 * byte by byte, builders return a '<' frame's length or 0.
 */
#ifndef VM_PROTOCOL2_H
#define VM_PROTOCOL2_H

#include "protocol.h"   /* this module is in the fifth code segment */

#define VM_CMD_GET_BATT_AND_STORAGE 20
#define VM_CMD_GET_CONTACT_BY_KEY   30
#define VM_CMD_SEND_TRACE_PATH      36
#define VM_CMD_GET_STATS            56
#define VM_RESP_BATT_AND_STORAGE    12
#define VM_RESP_STATS               24
#define VM_PUSH_TRACE_DATA          0x89

#define VM_STATS_CORE    0
#define VM_STATS_RADIO   1
#define VM_STATS_PACKETS 2

#define VM_CONTACT_FRAME_LEN 148   /* RESP_CONTACT: code, key, type, flags, path len, path[64], name[32], 4 x 4 */
#define VM_TRACE_MAX     32        /* hash bytes in a trace path we send */

/* A command that's only its code (GET_BATT_AND_STORAGE). */
UInt16 VmBuildCode(UInt8 *out, UInt16 outSize, UInt8 code) VM_SECT5;
UInt16 VmBuildGetStats(UInt8 *out, UInt16 outSize, UInt8 type) VM_SECT5;
UInt16 VmBuildGetContactByKey(UInt8 *out, UInt16 outSize, const UInt8 *pubKey) VM_SECT5;
/*
 * SEND_TRACE_PATH: hashSize bytes (1, 2 or 4) per hop. The trace is sent
 * direct along the path, each hop adding the SNR it heard it at; whoever
 * hears it after the last hop reports it (PUSH_TRACE_DATA). For an answer,
 * the path must lead back to us: A B A.
 */
UInt16 VmBuildTrace(UInt8 *out, UInt16 outSize, UInt32 tag, UInt8 hashSize, const UInt8 *path, UInt16 pathBytes) VM_SECT5;

typedef struct {
	UInt32 tag;
	UInt8  hashSize;
	UInt8  hops;
	UInt8  hashes[64];
	Int8   snrQ[65];        /* per hop: the SNR it was heard at, quarter dB; snrQ[hops]: ours */
} VmTrace;
Boolean VmParseTrace(const UInt8 *p, UInt16 len, VmTrace *out) VM_SECT5;

typedef struct {
	UInt16  battMv;
	Boolean hasStorage;
	UInt32  usedKb, totalKb;
} VmBattery;
Boolean VmParseBattery(const UInt8 *p, UInt16 len, VmBattery *out) VM_SECT5;

/* GET_STATS replies: each fills its own part and sets its flag. */
typedef struct {
	Boolean hasCore, hasRadio, hasPackets;
	UInt16  battMv;
	UInt32  uptimeSecs;
	UInt16  errFlags;
	UInt8   queueLen;
	Int16   noiseFloor;     /* dBm */
	Int8    lastRssi;
	Int8    lastSnrQ;
	UInt32  txAirSecs, rxAirSecs;
	UInt32  recv, sent, sentFlood, sentDirect, recvFlood, recvDirect;
} VmStats;
Boolean VmParseStats(const UInt8 *p, UInt16 len, VmStats *out) VM_SECT5;

/* A RESP_CONTACT's route: the path length byte (VM_PATH_*) and its bytes (up to 64). */
Boolean VmContactPath(const UInt8 *p, UInt16 len, UInt8 *pathLen, UInt8 *path) VM_SECT5;
/*
 * ADD_UPDATE_CONTACT from a RESP_CONTACT with a new route: hops of hashSize
 * bytes (0 hops: a neighbour, sent direct). Everything else is kept as the
 * radio had it.
 */
UInt16 VmBuildContactRoute(UInt8 *out, UInt16 outSize, const UInt8 *contact, UInt16 len,
                           UInt8 hashSize, const UInt8 *path, UInt8 hops) VM_SECT5;

/*
 * Hop codes typed as hex, separated by spaces or commas ("3f2a, 91c0"): all
 * the same size (1, 2 or 4 bytes). Fills out (up to max bytes) and returns
 * how many hops, with the size in *size; -1 if they don't parse.
 */
Int16  VmParseHopCodes(const Char *s, UInt8 *out, UInt16 max, UInt8 *size) VM_SECT5;

#define VM_SHARE_NONE    0
#define VM_SHARE_CONTACT 1
#define VM_SHARE_CHANNEL 2
typedef struct {
	UInt8  kind;
	UInt8  type;                          /* contact: VM_ADV_TYPE_* */
	UInt8  key[VM_PUB_KEY_SIZE];
	UInt8  secret[VM_CHANNEL_SECRET_SIZE];
	UInt8  name[VM_NAME_SIZE];            /* UTF-8, not NUL-terminated */
	UInt16 nameLen;
} VmShare;
/* meshcore://contact/add?name=..&public_key=<64 hex>&type=N; returns the length (no NUL), 0 if it won't fit. */
UInt16 VmShareContact(Char *out, UInt16 size, const UInt8 *name, UInt16 nameLen, const UInt8 *key, UInt8 type) VM_SECT5;
/* meshcore://channel/add?name=..&secret=<32 hex> */
UInt16 VmShareChannel(Char *out, UInt16 size, const UInt8 *name, UInt16 nameLen, const UInt8 *secret) VM_SECT5;
Boolean VmShareParse(const Char *s, UInt16 len, VmShare *out) VM_SECT5;

/*
 * The #hashtag (MeshCore One's pattern: '#', a letter or digit, then letters,
 * digits and '-') that character k of text is in, lowercased with its '#',
 * into out (NUL-terminated); false if k isn't in one or it won't fit.
 */
Boolean VmHashtagAt(const Char *text, UInt16 len, UInt16 k, Char *out, UInt16 size) VM_SECT5;

/*
 * One line of a repeater's "neighbors" reply, "a1b2c3d4:120:-14": a
 * neighbour's first 4 key bytes, seconds since heard, SNR in quarter dB.
 * len stops at the line's end; returns false if it isn't one.
 */
Boolean VmParseNeighbour(const Char *s, UInt16 len, UInt8 *prefix4, UInt32 *secsAgo, Int16 *snrQ) VM_SECT5;

#endif
