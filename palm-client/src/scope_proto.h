/*
 * Flood scopes (MeshCore "regions"): commands and key derivation.
 *
 * A flood packet can carry a transport code derived from a scope key, and
 * repeaters set up for that region forward it (region allowf / denyf). A
 * public scope's key is the first 16 bytes of SHA-256 of "#" + its name,
 * the same scheme as hashtag channels (TransportKeyStore::getAutoKeyFor).
 *
 *   SET_FLOOD_SCOPE_KEY (54)    0, key[16]: override the scope for sends from now on
 *                               0:          clear the override (back to the default)
 *                               1:          send unscoped (firmware v12+)
 *   SET_DEFAULT_FLOOD_SCOPE (63) name[31], key[16]: the radio's saved default; just
 *                               the code clears it
 *   GET_DEFAULT_FLOOD_SCOPE (64) -> DEFAULT_FLOOD_SCOPE (28) name[31], key[16], or
 *                               just the code for none
 *
 * Discovering a repeater's regions (simple_repeater handleAnonRegionsReq):
 *   SEND_ANON_REQ (57) pub_key[32], 0x01, reply_path_len, reply_path
 *     -> SENT (tag), later PUSH_BINARY_RESPONSE (0x8C) 0, tag, its clock[4],
 *        names: comma-separated, '#' dropped, "*" for the wildcard
 *   The repeater ignores it unless it arrives direct, so it only works for
 *   repeaters we have a direct path to; with reply_path_len 0 the answer comes
 *   back zero-hop, so only neighbours (firmware v13+ for non-contacts).
 *
 * The override lasts until the radio reboots, across app connections, so the
 * session sets it before every flood send (session.c, ApplyScope).
 * Plain C with no Palm OS calls, so it also builds on the host for tests.
 */
#ifndef VM_SCOPE_PROTO_H
#define VM_SCOPE_PROTO_H

#include "protocol.h"

#define VM_CMD_SET_FLOOD_SCOPE_KEY     54
#define VM_CMD_SET_DEFAULT_FLOOD_SCOPE 63
#define VM_CMD_GET_DEFAULT_FLOOD_SCOPE 64
#define VM_RESP_DEFAULT_FLOOD_SCOPE    28

#define VM_CMD_SEND_ANON_REQ           57
#define VM_PUSH_BINARY_RESPONSE        0x8C
#define VM_ANON_REQ_REGIONS            0x01

#define VM_SCOPE_KEY_SIZE   16
#define VM_SCOPE_NAME_SIZE  31     /* including the NUL, as the firmware stores it */

#define VM_FW_SCOPES        8      /* SET_FLOOD_SCOPE_KEY */
#define VM_FW_UNSCOPED      12     /* SET_FLOOD_SCOPE_KEY 1 */

/* Name without a leading '#', as stored; key from "#" + that. */
const Char *VmScopeBareName(const Char *name) VM_SECT4;
void   VmScopeKey(const Char *name, UInt8 key[VM_SCOPE_KEY_SIZE]) VM_SECT4;

/*
 * A name we can derive a key for: 1-30 printable ASCII characters after any
 * '#', not starting with '$' (private regions have keys only the repeaters
 * that set them up know).
 */
Boolean VmScopeNameValid(const Char *name) VM_SECT4;

/* key NULL: clear the override, so sends use the radio's default. */
UInt16 VmBuildSetScopeKey(UInt8 *out, UInt16 outSize, const UInt8 *key) VM_SECT4;
UInt16 VmBuildSetUnscoped(UInt8 *out, UInt16 outSize) VM_SECT4;
/* name NULL: no default. */
UInt16 VmBuildSetDefaultScope(UInt8 *out, UInt16 outSize, const Char *name) VM_SECT4;
UInt16 VmBuildGetDefaultScope(UInt8 *out, UInt16 outSize) VM_SECT4;

/* A zero-hop regions request to the repeater with this key. */
UInt16 VmBuildRegionsReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey) VM_SECT4;

/* PUSH_BINARY_RESPONSE: the request's tag, and the names (not NUL-terminated). */
Boolean VmParseRegionsReply(const UInt8 *p, UInt16 len, UInt32 *tag,
                            const UInt8 **names, UInt16 *namesLen) VM_SECT4;

/*
 * The next name in a comma list from *pos (advanced past it), trimmed, into
 * out[VM_SCOPE_NAME_SIZE] (cut if longer). Returns false at the end.
 */
Boolean VmNextRegionName(const UInt8 *names, UInt16 len, UInt16 *pos, Char *out) VM_SECT4;

/* DEFAULT_FLOOD_SCOPE: name[VM_SCOPE_NAME_SIZE] is "" when the radio has none. */
Boolean VmParseDefaultScope(const UInt8 *p, UInt16 len, Char *name, UInt8 key[VM_SCOPE_KEY_SIZE]) VM_SECT4;

#endif
