/*
 * SHA-256 (FIPS 180-4), for deriving hashtag channel keys: a "#name"
 * channel's secret is the first 16 bytes of SHA-256("#name").
 * Plain C, tested on the host. In the second code segment (sections.h).
 */
#ifndef VM_SHA256_H
#define VM_SHA256_H

#include "protocol.h"

void VmSha256(const UInt8 *data, UInt32 len, UInt8 out[32]) VM_SECT2;

#endif
