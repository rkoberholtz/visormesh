/*
 * MeshCore companion protocol framing.
 *
 * App -> radio:  '<' len_lo len_hi payload[len]
 * Radio -> app:  '>' len_lo len_hi payload[len]
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
typedef int16_t  Int16;
typedef uint8_t  Boolean;
#endif

#define VM_FRAME_TO_RADIO   0x3C    /* '<' */
#define VM_FRAME_FROM_RADIO 0x3E    /* '>' */
#define VM_MAX_FRAME        172     /* MeshCore MAX_FRAME_SIZE (payload bytes) */
#define VM_HEADER_LEN       3

/* Commands (app -> radio). Add others as they're verified against MeshCore. */
#define VM_CMD_DEVICE_QUERY 0x16
#define VM_APP_TARGET_VER   0x03

typedef struct {
	UInt8  state;
	UInt16 len;
	UInt16 pos;
	UInt8  payload[VM_MAX_FRAME];
} VmFrameDecoder;

/* Writes a '<' frame into out. Returns total bytes written, or 0 if it won't fit. */
UInt16 VmEncodeFrame(UInt8 *out, UInt16 outSize, const UInt8 *payload, UInt16 len);

void VmDecoderReset(VmFrameDecoder *d);

/*
 * Feeds one byte from the radio. Returns 1 when a complete '>' frame is in
 * d->payload (length d->len); the next call starts a new frame. Bytes outside
 * a frame and frames longer than VM_MAX_FRAME are dropped.
 */
Boolean VmDecoderFeed(VmFrameDecoder *d, UInt8 byte);

#endif
