/*
 * MeshCore companion protocol: framing, command builders and reply parsers.
 *
 * App -> radio:  '<' len_lo len_hi payload[len]
 * Radio -> app:  '>' len_lo len_hi payload[len]
 *
 * Layouts follow examples/companion_radio/MyMesh.cpp in MeshCore (the commit
 * pinned in firmware/Makefile). Multi-byte fields are little-endian; the
 * Visor's 68k is big-endian, so fields are assembled byte by byte, never
 * copied with memcpy.
 *
 * Plain C with no Palm OS calls, so it also builds on the host for tests.
 */
#ifndef VM_PROTOCOL_H
#define VM_PROTOCOL_H

#ifdef __palmos__
#include <PalmTypes.h>
#else
#include <stdint.h>
typedef uint8_t  UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef int8_t   Int8;
typedef int16_t  Int16;
typedef int32_t  Int32;
typedef uint8_t  Boolean;
typedef char     Char;
#endif

#define VM_FRAME_TO_RADIO   0x3C    /* '<' */
#define VM_FRAME_FROM_RADIO 0x3E    /* '>' */
#define VM_MAX_FRAME        172     /* MeshCore MAX_FRAME_SIZE (payload bytes) */
#define VM_HEADER_LEN       3

#define VM_APP_TARGET_VER   0x03    /* we understand the v3 message frames */
#define VM_PUB_KEY_SIZE     32
#define VM_NAME_SIZE        32      /* contact names: 31 chars + NUL */
#define VM_MAX_PATH_SIZE    64

/* Commands (app -> radio) */
#define VM_CMD_APP_START          1
#define VM_CMD_GET_CONTACTS       4
#define VM_CMD_SYNC_NEXT_MESSAGE  10
#define VM_CMD_DEVICE_QUERY       22

/* Replies (radio -> app) */
#define VM_RESP_OK                0
#define VM_RESP_ERR               1
#define VM_RESP_CONTACTS_START    2
#define VM_RESP_CONTACT           3
#define VM_RESP_END_OF_CONTACTS   4
#define VM_RESP_SELF_INFO         5
#define VM_RESP_NO_MORE_MESSAGES  10
#define VM_RESP_DEVICE_INFO       13
#define VM_RESP_CONTACT_MSG_V3    16
#define VM_RESP_CHANNEL_MSG_V3    17

/* Pushes (radio -> app, unsolicited) */
#define VM_PUSH_ADVERT            0x80
#define VM_PUSH_SEND_CONFIRMED    0x82
#define VM_PUSH_MSG_WAITING       0x83
#define VM_IS_PUSH(code)          ((code) >= 0x80)

#define VM_TXT_TYPE_PLAIN         0
#define VM_TXT_TYPE_CLI_DATA      1
#define VM_TXT_TYPE_SIGNED_PLAIN  2

typedef struct {
	UInt8  state;
	UInt16 len;
	UInt16 pos;
	UInt8  payload[VM_MAX_FRAME];
} VmFrameDecoder;

typedef struct {
	UInt8  advType;
	UInt8  txPowerDbm;
	UInt8  maxTxPowerDbm;
	UInt8  pubKey[VM_PUB_KEY_SIZE];
	Int32  latE6;            /* degrees * 1e6 */
	Int32  lonE6;
	UInt32 freqKHz;
	UInt32 bwHz;
	UInt8  sf;
	UInt8  cr;
	Char   name[VM_NAME_SIZE];
} VmSelfInfo;

typedef struct {
	UInt8  pubKey[VM_PUB_KEY_SIZE];
	UInt8  type;
	UInt8  flags;
	UInt8  outPathLen;       /* 0xFF = no known path (flood) */
	Char   name[VM_NAME_SIZE];
	UInt32 lastAdvert;
	Int32  latE6;
	Int32  lonE6;
	UInt32 lastMod;
} VmContact;

typedef struct {
	Boolean      isChannel;
	Int8         snrQuarterDb;   /* SNR * 4 */
	UInt8        channelIdx;     /* channel messages */
	UInt8        senderPrefix[6];/* contact messages: first 6 bytes of the sender's key */
	UInt8        pathLen;        /* 0xFF = direct */
	UInt8        txtType;
	UInt32       timestamp;      /* sender's clock, seconds since 1970 */
	const UInt8 *text;           /* points into the payload; not NUL-terminated */
	UInt16       textLen;
} VmMessage;

/* Framing */
UInt16  VmEncodeFrame(UInt8 *out, UInt16 outSize, const UInt8 *payload, UInt16 len);
void    VmDecoderReset(VmFrameDecoder *d);
Boolean VmDecoderFeed(VmFrameDecoder *d, UInt8 byte);

/* Command builders: each writes a complete '<' frame and returns its length, or 0 if it won't fit. */
UInt16 VmBuildDeviceQuery(UInt8 *out, UInt16 outSize);
UInt16 VmBuildAppStart(UInt8 *out, UInt16 outSize, const Char *appName);
UInt16 VmBuildGetContacts(UInt8 *out, UInt16 outSize, UInt32 since);
UInt16 VmBuildSyncNextMessage(UInt8 *out, UInt16 outSize);

/* Reply parsers: take a decoded payload (code byte first). Return true if it has the expected code and length. */
Boolean VmParseSelfInfo(const UInt8 *p, UInt16 len, VmSelfInfo *out);
Boolean VmParseContactsStart(const UInt8 *p, UInt16 len, UInt32 *count);
Boolean VmParseContact(const UInt8 *p, UInt16 len, VmContact *out);
Boolean VmParseEndOfContacts(const UInt8 *p, UInt16 len, UInt32 *lastMod);
Boolean VmParseMessage(const UInt8 *p, UInt16 len, VmMessage *out);

#endif
