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
#ifndef VM_HOST_TYPES
#define VM_HOST_TYPES
typedef uint8_t  UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef int8_t   Int8;
typedef int16_t  Int16;
typedef int32_t  Int32;
typedef uint8_t  Boolean;
typedef char     Char;
#endif
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
#define VM_CMD_SET_ADVERT_NAME    8
#define VM_CMD_SYNC_NEXT_MESSAGE  10
#define VM_CMD_SET_RADIO_PARAMS   11
#define VM_CMD_SET_RADIO_TX_POWER 12
#define VM_CMD_REBOOT             19
#define VM_CMD_DEVICE_QUERY       22
#define VM_CMD_SET_PATH_HASH_MODE 61

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

/* Advert types (contact.type) */
#define VM_ADV_TYPE_CHAT          1
#define VM_ADV_TYPE_REPEATER      2
#define VM_ADV_TYPE_ROOM          3
#define VM_ADV_TYPE_SENSOR        4

/*
 * Path length bytes (contact out_path_len, message path_len): bits 7-6 are
 * the path hash size - 1, bits 5-0 the hop count; 0xFF = no known path.
 */
#define VM_PATH_UNKNOWN           0xFF
#define VM_PATH_HOPS(len)         ((UInt8)((len) & 0x3F))
#define VM_PATH_HASH_BYTES(len)   ((UInt8)(((len) >> 6) + 1))

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
	UInt8  fwVerCode;        /* companion protocol version the firmware speaks */
	UInt16 maxContacts;
	UInt8  maxChannels;
	Char   buildDate[13];
	Char   manufacturer[41];
	Char   version[21];
	Boolean hasRepeat;       /* firmware v9+ reports client repeat */
	Boolean repeatEnabled;
	Boolean hasPathHashMode; /* firmware v10+ reports the path hash mode */
	UInt8  pathHashMode;     /* path hash size in bytes = mode + 1 */
} VmDeviceInfo;

/* Limits the firmware enforces (MyMesh.cpp); checked before sending. */
#define VM_FREQ_MIN_KHZ   150000UL
#define VM_FREQ_MAX_KHZ   2500000UL
#define VM_BW_MIN_HZ      7000UL
#define VM_BW_MAX_HZ      500000UL
#define VM_SF_MIN         5
#define VM_SF_MAX         12
#define VM_CR_MIN         5
#define VM_CR_MAX         8
#define VM_TX_POWER_MIN   (-9)
#define VM_NAME_MAX       31
#define VM_PATH_HASH_MODE_MAX 2

typedef struct {
	UInt8  advType;
	Int8   txPowerDbm;      /* can be negative (down to -9) */
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

/*
 * Fixed-point decimals without floating point (the 68k has no FPU):
 * VmParseDecimal("910.525", 3) = 910525, ("62.5", 3) = 62500, ("921", 3) = 921000.
 * Returns false on anything but digits with at most one '.' and 'scale' decimals.
 * VmFormatDecimal(910525, 3) = "910.525", (62500, 3) = "62.5", (921000, 3) = "921".
 */
Boolean VmParseDecimal(const Char *s, UInt16 scale, UInt32 *out);
void    VmFormatDecimal(Char *out, UInt32 value, UInt16 scale);
/* Optional leading '-', then digits. */
Boolean VmParseInt(const Char *s, Int32 *out);

/* Framing */
UInt16  VmEncodeFrame(UInt8 *out, UInt16 outSize, const UInt8 *payload, UInt16 len);
void    VmDecoderReset(VmFrameDecoder *d);
Boolean VmDecoderFeed(VmFrameDecoder *d, UInt8 byte);

/* Command builders: each writes a complete '<' frame and returns its length, or 0 if it won't fit. */
UInt16 VmBuildDeviceQuery(UInt8 *out, UInt16 outSize);
UInt16 VmBuildAppStart(UInt8 *out, UInt16 outSize, const Char *appName);
UInt16 VmBuildGetContacts(UInt8 *out, UInt16 outSize, UInt32 since);
UInt16 VmBuildSyncNextMessage(UInt8 *out, UInt16 outSize);
UInt16 VmBuildSetAdvertName(UInt8 *out, UInt16 outSize, const Char *name);
UInt16 VmBuildSetRadioParams(UInt8 *out, UInt16 outSize, UInt32 freqKHz, UInt32 bwHz, UInt8 sf, UInt8 cr,
                             Boolean includeRepeat, Boolean repeat);
UInt16 VmBuildSetTxPower(UInt8 *out, UInt16 outSize, Int8 dbm);
UInt16 VmBuildSetPathHashMode(UInt8 *out, UInt16 outSize, UInt8 mode);
/* CMD_REBOOT: the radio restarts without replying. */
UInt16 VmBuildReboot(UInt8 *out, UInt16 outSize);

/* Reply parsers: take a decoded payload (code byte first). Return true if it has the expected code and length. */
Boolean VmParseDeviceInfo(const UInt8 *p, UInt16 len, VmDeviceInfo *out);
Boolean VmParseSelfInfo(const UInt8 *p, UInt16 len, VmSelfInfo *out);
Boolean VmParseContactsStart(const UInt8 *p, UInt16 len, UInt32 *count);
Boolean VmParseContact(const UInt8 *p, UInt16 len, VmContact *out);
Boolean VmParseEndOfContacts(const UInt8 *p, UInt16 len, UInt32 *lastMod);
Boolean VmParseMessage(const UInt8 *p, UInt16 len, VmMessage *out);

#endif
