#include "reaction_fmt.h"
#include "sha256.h"

static Char    B32Char(UInt8 v) VM_SECT4;
static Int16   B32Value(Char c) VM_SECT4;
static UInt16  Utf8Next(const UInt8 *s, UInt16 len, UInt16 i, UInt32 *cp) VM_SECT4;
static Boolean EmojiBase(UInt32 cp) VM_SECT4;
static Boolean OneEmoji(const UInt8 *s, UInt16 len) VM_SECT4;
static Boolean HasMention(const UInt8 *s, UInt16 len) VM_SECT4;

/* Crockford's alphabet, 0-9 then a-z without i, l, o, u: worked out, not a table (sections.h). */
static Char B32Char(UInt8 v)
{
	Char c;

	if (v < 10)
		return (Char)('0' + v);
	for (c = 'a', v -= 10; ; c++) {
		if (c == 'i' || c == 'l' || c == 'o' || c == 'u')
			continue;
		if (v-- == 0)
			return c;
	}
}

/* Either case; o reads as 0 and i, l as 1. -1 if it isn't one. */
static Int16 B32Value(Char c)
{
	Int16 v = 10;
	Char k;

	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'Z')
		c = (Char)(c - 'A' + 'a');
	if (c == 'o')
		return 0;
	if (c == 'i' || c == 'l')
		return 1;
	if (c < 'a' || c > 'z' || c == 'u')
		return -1;
	for (k = 'a'; k < c; k++)
		if (k != 'i' && k != 'l' && k != 'o' && k != 'u')
			v++;
	return v;
}

void VmReactHash(const UInt8 *text, UInt16 len, UInt32 timestamp, UInt8 hash[VM_REACT_HASH_BYTES])
{
	UInt8 buf[VM_MAX_FRAME + 4], digest[32];
	UInt16 i;

	if (len > VM_MAX_FRAME)
		len = VM_MAX_FRAME;
	for (i = 0; i < len; i++)
		buf[i] = text[i];
	buf[len] = (UInt8)timestamp;
	buf[len + 1] = (UInt8)(timestamp >> 8);
	buf[len + 2] = (UInt8)(timestamp >> 16);
	buf[len + 3] = (UInt8)(timestamp >> 24);
	VmSha256(buf, (UInt32)len + 4, digest);
	for (i = 0; i < VM_REACT_HASH_BYTES; i++)
		hash[i] = digest[i];
}

/* 40 bits, 5 at a time from the top. */
void VmReactHashText(const UInt8 hash[VM_REACT_HASH_BYTES], Char out[VM_REACT_HASH_LEN + 1])
{
	UInt16 i, bit, v, b;

	for (i = 0; i < VM_REACT_HASH_LEN; i++) {
		v = 0;
		for (b = 0; b < 5; b++) {
			bit = i * 5 + b;
			v = (UInt16)((v << 1) | ((hash[bit / 8] >> (7 - bit % 8)) & 1));
		}
		out[i] = B32Char((UInt8)v);
	}
	out[VM_REACT_HASH_LEN] = 0;
}

const UInt8 *VmReactChannelBody(const UInt8 *text, UInt16 len, UInt16 *bodyLen)
{
	UInt16 i, start = 0, end = len;

	for (i = 0; i < len; i++) {
		if (text[i] == ':') {
			start = i + 1;
			break;
		}
	}
	while (start < end && (text[start] == ' ' || text[start] == '\t'))
		start++;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
		end--;
	*bodyLen = end - start;
	return text + start;
}

UInt16 VmReactBuild(Boolean isChannel, const UInt8 *target, UInt16 targetLen,
                    const UInt8 *emoji, UInt16 emojiLen, const UInt8 hash[VM_REACT_HASH_BYTES],
                    UInt8 *out, UInt16 outSize)
{
	Char h[VM_REACT_HASH_LEN + 1];
	UInt16 n = 0, i;

	if ((isChannel ? targetLen + 3 : 0) + emojiLen + 1 + VM_REACT_HASH_LEN > outSize)
		return 0;
	if (isChannel) {
		out[n++] = '@';
		out[n++] = '[';
		for (i = 0; i < targetLen; i++)
			out[n++] = target[i];
		out[n++] = ']';
	}
	for (i = 0; i < emojiLen; i++)
		out[n++] = emoji[i];
	out[n++] = '\n';
	VmReactHashText(hash, h);
	for (i = 0; i < VM_REACT_HASH_LEN; i++)
		out[n++] = (UInt8)h[i];
	return n;
}

/* Decodes the code point at i; returns the index after it, or 0 if it's malformed. */
static UInt16 Utf8Next(const UInt8 *s, UInt16 len, UInt16 i, UInt32 *cp)
{
	UInt8 c = s[i];
	UInt16 n, k;

	if (c < 0x80) {
		*cp = c;
		return i + 1;
	}
	if ((c & 0xE0) == 0xC0) {
		n = 1;
		*cp = c & 0x1F;
	} else if ((c & 0xF0) == 0xE0) {
		n = 2;
		*cp = c & 0x0F;
	} else if ((c & 0xF8) == 0xF0) {
		n = 3;
		*cp = c & 0x07;
	} else {
		return 0;
	}
	for (k = 1; k <= n; k++) {
		if (i + k >= len || (s[i + k] & 0xC0) != 0x80)
			return 0;
		*cp = (*cp << 6) | (s[i + k] & 0x3F);
	}
	return i + n + 1;
}

/* A code point that's an emoji on its own: pictographs, symbols, dingbats, arrows. */
static Boolean EmojiBase(UInt32 cp)
{
	return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2190 && cp <= 0x2BFF) ||
	       cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299;
}

/*
 * One emoji grapheme, near enough: an emoji code point, then only variation
 * selectors, skin tones, a keycap mark, tags, a second flag letter, or a
 * joiner and another emoji. A keycap (1, #, *) or (c) / (r) only counts with
 * the selector or keycap mark after it. Text that merely starts with an emoji
 * isn't one.
 */
static Boolean OneEmoji(const UInt8 *s, UInt16 len)
{
	UInt32 cp, first;
	UInt16 i, next, n = 0;
	Boolean flag, joined = 0, plainBase;

	if (len == 0 || len > VM_REACT_EMOJI_MAX)
		return 0;
	i = Utf8Next(s, len, 0, &first);
	if (i == 0)
		return 0;
	flag = first >= 0x1F1E6 && first <= 0x1F1FF;
	plainBase = (first >= '0' && first <= '9') || first == '#' || first == '*' ||
	            first == 0xA9 || first == 0xAE;
	if (!EmojiBase(first) && !plainBase)
		return 0;
	while (i < len) {
		next = Utf8Next(s, len, i, &cp);
		if (next == 0)
			return 0;
		n++;
		if (joined) {
			if (!EmojiBase(cp))
				return 0;
			joined = 0;
		} else if (cp == 0x200D) {
			joined = 1;
		} else if (flag && n == 1 && cp >= 0x1F1E6 && cp <= 0x1F1FF) {
			/* the flag's second letter */
		} else if (!(cp == 0xFE0F || cp == 0xFE0E || cp == 0x20E3 ||
		             (cp >= 0x1F3FB && cp <= 0x1F3FF) || (cp >= 0xE0020 && cp <= 0xE007F))) {
			return 0;
		}
		i = next;
	}
	if (joined)
		return 0;
	return !plainBase || n > 0;
}

static Boolean HasMention(const UInt8 *s, UInt16 len)
{
	UInt16 i;

	for (i = 0; i + 1 < len; i++)
		if (s[i] == '@' && s[i + 1] == '[')
			return 1;
	return 0;
}

Boolean VmReactParse(Boolean isChannel, const UInt8 *text, UInt16 len, VmReaction *r)
{
	UInt16 nl, i, head;
	UInt32 bits;
	Int16 v;
	UInt16 bit;

	/* the hash after the last line break: exactly 8 characters */
	for (nl = len; nl > 0 && text[nl - 1] != '\n'; nl--)
		;
	if (nl == 0 || len - nl != VM_REACT_HASH_LEN)
		return 0;
	head = nl - 1;
	for (i = 0; i < VM_REACT_HASH_BYTES; i++)
		r->hash[i] = 0;
	for (i = 0; i < VM_REACT_HASH_LEN; i++) {
		v = B32Value((Char)text[nl + i]);
		if (v < 0)
			return 0;
		for (bits = 0; bits < 5; bits++) {
			bit = (UInt16)(i * 5 + bits);
			if (v & (0x10 >> bits))
				r->hash[bit / 8] |= (UInt8)(0x80 >> (bit % 8));
		}
	}
	r->target = 0;
	r->targetLen = 0;
	if (!isChannel) {
		if (HasMention(text, head))
			return 0;
		r->emoji = text;
		r->emojiLen = head;
		return OneEmoji(r->emoji, r->emojiLen);
	}
	if (head >= 2 && text[0] == '@' && text[1] == '[') {
		for (i = 2; i < head && text[i] != ']'; i++)
			;
		if (i >= head)
			return 0;
		r->target = text + 2;
		r->targetLen = i - 2;
		r->emoji = text + i + 1;
		r->emojiLen = head - (i + 1);
	} else {
		/* before MC1 1.4.1: the emoji, then the mention ending the line */
		for (i = 0; i + 1 < head && !(text[i] == '@' && text[i + 1] == '['); i++)
			;
		if (i + 1 >= head || text[head - 1] != ']')
			return 0;
		r->emoji = text;
		r->emojiLen = i;
		r->target = text + i + 2;
		r->targetLen = head - 1 - (i + 2);
	}
	if (r->targetLen == 0)
		return 0;
	for (i = 0; i < r->targetLen; i++)
		if (r->target[i] == ']' || r->target[i] == '\n')
			return 0;
	return OneEmoji(r->emoji, r->emojiLen);
}
