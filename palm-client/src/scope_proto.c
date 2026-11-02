#include "scope_proto.h"
#include "sha256.h"

const Char *VmScopeBareName(const Char *name)
{
	return name[0] == '#' ? name + 1 : name;
}

void VmScopeKey(const Char *name, UInt8 key[VM_SCOPE_KEY_SIZE])
{
	UInt8 tag[VM_SCOPE_NAME_SIZE + 1];
	UInt8 digest[32];
	UInt16 n = 0;
	UInt16 i;

	name = VmScopeBareName(name);
	tag[n++] = '#';
	while (name[n - 1] && n < sizeof(tag)) {
		tag[n] = (UInt8)name[n - 1];
		n++;
	}
	VmSha256(tag, n, digest);
	for (i = 0; i < VM_SCOPE_KEY_SIZE; i++)
		key[i] = digest[i];
}

Boolean VmScopeNameValid(const Char *name)
{
	UInt16 n = 0;

	name = VmScopeBareName(name);
	if (name[0] == '$')
		return 0;
	for (; name[n]; n++)
		if ((UInt8)name[n] <= ' ' || (UInt8)name[n] > '~')
			return 0;
	return n > 0 && n < VM_SCOPE_NAME_SIZE;
}

UInt16 VmBuildSetScopeKey(UInt8 *out, UInt16 outSize, const UInt8 *key)
{
	UInt8 p[2 + VM_SCOPE_KEY_SIZE];
	UInt16 i;

	p[0] = VM_CMD_SET_FLOOD_SCOPE_KEY;
	p[1] = 0;
	if (!key)
		return VmEncodeFrame(out, outSize, p, 2);
	for (i = 0; i < VM_SCOPE_KEY_SIZE; i++)
		p[2 + i] = key[i];
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

UInt16 VmBuildSetUnscoped(UInt8 *out, UInt16 outSize)
{
	UInt8 p[2];

	p[0] = VM_CMD_SET_FLOOD_SCOPE_KEY;
	p[1] = 1;
	return VmEncodeFrame(out, outSize, p, 2);
}

UInt16 VmBuildSetDefaultScope(UInt8 *out, UInt16 outSize, const Char *name)
{
	UInt8 p[1 + VM_SCOPE_NAME_SIZE + VM_SCOPE_KEY_SIZE];
	UInt16 i;

	p[0] = VM_CMD_SET_DEFAULT_FLOOD_SCOPE;
	if (!name)
		return VmEncodeFrame(out, outSize, p, 1);
	if (!VmScopeNameValid(name))
		return 0;
	name = VmScopeBareName(name);
	for (i = 0; i < VM_SCOPE_NAME_SIZE; i++)
		p[1 + i] = 0;
	for (i = 0; name[i]; i++)
		p[1 + i] = (UInt8)name[i];
	VmScopeKey(name, p + 1 + VM_SCOPE_NAME_SIZE);
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

UInt16 VmBuildGetDefaultScope(UInt8 *out, UInt16 outSize)
{
	UInt8 p[1];

	p[0] = VM_CMD_GET_DEFAULT_FLOOD_SCOPE;
	return VmEncodeFrame(out, outSize, p, 1);
}

Boolean VmParseDefaultScope(const UInt8 *p, UInt16 len, Char *name, UInt8 key[VM_SCOPE_KEY_SIZE])
{
	UInt16 i;

	if (len < 1 || p[0] != VM_RESP_DEFAULT_FLOOD_SCOPE)
		return 0;
	name[0] = 0;
	for (i = 0; i < VM_SCOPE_KEY_SIZE; i++)
		key[i] = 0;
	if (len == 1)
		return 1;      /* no default */
	if (len < 1 + VM_SCOPE_NAME_SIZE + VM_SCOPE_KEY_SIZE)
		return 0;
	for (i = 0; i < VM_SCOPE_NAME_SIZE - 1 && p[1 + i]; i++)
		name[i] = (Char)p[1 + i];
	name[i] = 0;
	for (i = 0; i < VM_SCOPE_KEY_SIZE; i++)
		key[i] = p[1 + VM_SCOPE_NAME_SIZE + i];
	return 1;
}

UInt16 VmBuildRegionsReq(UInt8 *out, UInt16 outSize, const UInt8 *pubKey)
{
	UInt8 p[1 + VM_PUB_KEY_SIZE + 2];
	UInt16 i;

	p[0] = VM_CMD_SEND_ANON_REQ;
	for (i = 0; i < VM_PUB_KEY_SIZE; i++)
		p[1 + i] = pubKey[i];
	p[1 + VM_PUB_KEY_SIZE] = VM_ANON_REQ_REGIONS;
	p[2 + VM_PUB_KEY_SIZE] = 0;         /* reply path: zero-hop */
	return VmEncodeFrame(out, outSize, p, sizeof(p));
}

Boolean VmParseRegionsReply(const UInt8 *p, UInt16 len, UInt32 *tag,
                            const UInt8 **names, UInt16 *namesLen)
{
	UInt16 n;

	if (len < 10 || p[0] != VM_PUSH_BINARY_RESPONSE)
		return 0;
	*tag = (UInt32)p[2] | ((UInt32)p[3] << 8) | ((UInt32)p[4] << 16) | ((UInt32)p[5] << 24);
	*names = p + 10;                    /* after the repeater's clock */
	for (n = 0; 10 + n < len && p[10 + n]; n++)
		;
	*namesLen = n;
	return 1;
}

Boolean VmNextRegionName(const UInt8 *names, UInt16 len, UInt16 *pos, Char *out)
{
	UInt16 i = *pos, n = 0;

	while (i < len && names[i] == ' ')
		i++;
	if (i >= len)
		return 0;
	while (i < len && names[i] != ',') {
		if (n < VM_SCOPE_NAME_SIZE - 1)
			out[n++] = (Char)names[i];
		i++;
	}
	while (n && out[n - 1] == ' ')
		n--;
	out[n] = 0;
	*pos = i < len ? i + 1 : i;
	return 1;
}
