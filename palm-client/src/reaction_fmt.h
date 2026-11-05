/*
 * Emoji reactions (tapbacks) in MeshCore One's format, so MC1 and the apps
 * that follow it (MeshMonitor, ...) show ours and we show theirs. MeshCore
 * has no reaction packet: a reaction is an ordinary text message
 *
 *   channel  @[TargetSender]<emoji>\n<hash>   (before MC1 1.4.1: <emoji>@[TargetSender]\n<hash>)
 *   DM       <emoji>\n<hash>
 *
 * where <emoji> is one emoji (one grapheme) and <hash> names the message
 * reacted to: Crockford Base32 (lowercase) of the first 5 bytes of
 * SHA-256(UTF-8 text || sender's timestamp as a little-endian UInt32). A
 * channel message's text is its body after the "Name:" prefix, spaces and
 * tabs trimmed; a DM's is as sent. Spec: MeshCoreOne docs/Reactions.md.
 *
 * Works on the UTF-8 text as it is on the air: the Visor's character set
 * loses emoji detail, so hash and parse before converting.
 * Plain C with no Palm OS calls, so it also builds on the host for tests.
 */
#ifndef VM_REACTION_FMT_H
#define VM_REACTION_FMT_H

#include "protocol.h"   /* this module is in the fourth code segment */

#define VM_REACT_HASH_LEN   8
#define VM_REACT_HASH_BYTES 5
#define VM_REACT_EMOJI_MAX  32   /* UTF-8 bytes */

typedef struct {
	const UInt8 *target;          /* channel: the sender reacted to (not NUL-terminated) */
	UInt16       targetLen;
	const UInt8 *emoji;
	UInt16       emojiLen;
	UInt8        hash[VM_REACT_HASH_BYTES];
} VmReaction;

/* The message's hash, as the 5 bytes the Base32 text encodes. */
void    VmReactHash(const UInt8 *text, UInt16 len, UInt32 timestamp, UInt8 hash[VM_REACT_HASH_BYTES]) VM_SECT4;
/* The 8 lowercase characters for a hash, NUL-terminated. */
void    VmReactHashText(const UInt8 hash[VM_REACT_HASH_BYTES], Char out[VM_REACT_HASH_LEN + 1]) VM_SECT4;
/* A channel message's hashed body: after the first ':', spaces and tabs trimmed (all of it without a ':'). */
const UInt8 *VmReactChannelBody(const UInt8 *text, UInt16 len, UInt16 *bodyLen) VM_SECT4;
/* Builds a reaction's text (target is ignored for a DM). Returns its length, 0 if it doesn't fit. */
UInt16  VmReactBuild(Boolean isChannel, const UInt8 *target, UInt16 targetLen,
                     const UInt8 *emoji, UInt16 emojiLen, const UInt8 hash[VM_REACT_HASH_BYTES],
                     UInt8 *out, UInt16 outSize) VM_SECT4;
/*
 * Whether text is a reaction; a channel message's text is its body (after
 * "Name: "). Fills r with pointers into text.
 */
Boolean VmReactParse(Boolean isChannel, const UInt8 *text, UInt16 len, VmReaction *r) VM_SECT4;

#endif
