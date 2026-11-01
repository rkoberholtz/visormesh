#include <PalmOS.h>
#include "protocol.h"
#include "ui/glyphs.h"
#include "channels.h"
#include "contacts.h"
#include "messages.h"
#include "convos.h"

static void HexPrefix(const UInt8 *p, Char *out)
{
	StrPrintF(out, "%02x%02x%02x%02x", p[0], p[1], p[2], p[3]);
}

void ContactNameForPrefix(const UInt8 *prefix, Char *out, UInt16 size)
{
	VmContact c;

	if (ContactsFindByPrefix(prefix, &c)) {
		StrNCopy(out, c.name, size - 1);
		out[size - 1] = 0;
	} else if (size > 8) {
		HexPrefix(prefix, out);
	} else {
		out[0] = 0;
	}
}

/* Channel names: "#foo" shows as marker '#' + "foo"; slot 0 / Public as '@'; others are private. */
static Char ChannelMarker(const VmChannel *c)
{
	if (c->name[0] == '#')
		return '#';
	if (c->idx == 0 || StrCaselessCompare(c->name, "Public") == 0)
		return '@';
	return GLYPH_LOCK;
}

static void ChannelDisplayName(const VmChannel *c, Char *out, UInt16 size)
{
	const Char *n = c->name[0] == '#' ? c->name + 1 : c->name;

	if (!*n)
		StrPrintF(out, "Channel %u", c->idx);
	else {
		StrNCopy(out, n, size - 1);
		out[size - 1] = 0;
	}
}

void ConvName(const VmConv *conv, Char *out, UInt16 size)
{
	VmChannel c;

	if (conv->type == VM_CONV_DIRECT) {
		ContactNameForPrefix(conv->key, out, size);
	} else if (ChannelsFindSlot(conv->channel, &c)) {
		ChannelDisplayName(&c, out, size);
	} else {
		StrPrintF(out, "Channel %u", conv->channel);
	}
}

static void AddSection(VmConvRow *rows, UInt16 *n, UInt16 max, const Char *label)
{
	if (*n >= max)
		return;
	MemSet(&rows[*n], sizeof(VmConvRow), 0);
	rows[*n].kind = VM_ROW_SECTION;
	StrCopy(rows[*n].name, label);
	(*n)++;
}

static VmConvRow *AddConv(VmConvRow *rows, UInt16 *n, UInt16 max, const VmConv *conv, Char marker,
                          const Char *name)
{
	VmConvRow *r;

	if (*n >= max)
		return NULL;
	r = &rows[(*n)++];
	MemSet(r, sizeof(VmConvRow), 0);
	r->kind = VM_ROW_CONV;
	r->conv = *conv;
	r->marker = marker;
	StrNCopy(r->name, name, VM_NAME_SIZE - 1);
	return r;
}

static VmConvRow *FindRow(VmConvRow *rows, UInt16 n, const VmConv *conv)
{
	UInt16 i;

	for (i = 0; i < n; i++)
		if (rows[i].kind == VM_ROW_CONV && ConvEqual(&rows[i].conv, conv))
			return &rows[i];
	return NULL;
}

static void SetPreview(VmConvRow *r, const VmMsgHeader *h)
{
	const Char *text = (const Char *)(h + 1);
	UInt16 len = 0;

	if (h->dir == VM_DIR_OUT) {
		StrCopy(r->preview, "you: ");
		len = 5;
	} else if (h->sender[0]) {
		StrNCopy(r->preview, h->sender, 12);
		r->preview[12] = 0;
		len = StrLen(r->preview);
		r->preview[len++] = ':';
		r->preview[len++] = ' ';
	}
	StrNCopy(r->preview + len, text, VM_PREVIEW_LEN - 1 - len);
	r->preview[VM_PREVIEW_LEN - 1] = 0;
}

UInt16 ConvosBuild(VmConvRow *rows, UInt16 max)
{
	VmChannel ch;
	VmContact ct;
	VmConv conv;
	VmConvRow *r, tmp;
	UInt16 n = 0, i, j, directStart, count;
	Char name[VM_NAME_SIZE];

	ChannelsOpen();
	ContactsOpen();
	MessagesOpen();

	AddSection(rows, &n, max, "Channels");
	count = ChannelsCount();
	for (i = 0; i < count; i++) {
		if (!ChannelsGet(i, &ch))
			continue;
		MemSet(&conv, sizeof(conv), 0);
		conv.type = VM_CONV_CHANNEL;
		conv.channel = ch.idx;
		ChannelDisplayName(&ch, name, sizeof(name));
		AddConv(rows, &n, max, &conv, ChannelMarker(&ch), name);
	}

	AddSection(rows, &n, max, "Direct");
	directStart = n;
	count = ContactsCount();
	for (i = 0; i < count; i++) {
		if (!ContactsGet(i, &ct) || ct.type != VM_ADV_TYPE_CHAT)
			continue;
		MemSet(&conv, sizeof(conv), 0);
		conv.type = VM_CONV_DIRECT;
		MemMove(conv.key, ct.pubKey, 6);
		AddConv(rows, &n, max, &conv, GLYPH_UNKNOWN, ct.name);
	}

	/* one pass over the messages: newest wins the preview */
	count = MessagesCount();
	for (i = 0; i < count; i++) {
		const VmMsgHeader *h = MessagesLock(i);

		if (!h)
			continue;
		r = FindRow(rows, n, &h->conv);
		if (!r && h->conv.type == VM_CONV_DIRECT) {      /* a DM from someone not in contacts */
			ContactNameForPrefix(h->conv.key, name, sizeof(name));
			r = AddConv(rows, &n, max, &h->conv, GLYPH_UNKNOWN, name);
		}
		if (r) {
			if (h->received >= r->last) {
				r->last = h->received;
				SetPreview(r, h);
			}
			if (h->unread)
				r->unread++;
			if (h->dir == VM_DIR_OUT && h->status == VM_ST_FAILED)
				r->failed = true;
			if (r->conv.type == VM_CONV_DIRECT)
				r->marker = GLYPH_COMPANION;     /* has history */
		}
		MessagesRelease(i);
	}

	/* direct: most recent first; those without messages keep their (alphabetical) order */
	for (i = directStart + 1; i < n; i++) {
		tmp = rows[i];
		for (j = i; j > directStart && rows[j - 1].last < tmp.last; j--)
			rows[j] = rows[j - 1];
		rows[j] = tmp;
	}
	return n;
}

UInt16 ConvosUnread(const VmConvRow *rows, UInt16 n)
{
	UInt16 i, total = 0;

	for (i = 0; i < n; i++)
		total += rows[i].unread;
	return total;
}
