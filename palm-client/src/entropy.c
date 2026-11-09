#include "entropy.h"
#include "sha256.h"

static void Stir(void) VM_SECT5;
static void Next(UInt8 tag) VM_SECT5;

#define BUF 64

static UInt8  gPool[VM_ENTROPY_STATE];
static UInt8  gBuf[BUF];
static UInt16 gPos = 0, gCount = 0;

/* pool = SHA-256(pool || buffer) */
static void Stir(void)
{
	UInt8 in[VM_ENTROPY_STATE + BUF];
	UInt16 i;

	for (i = 0; i < VM_ENTROPY_STATE; i++)
		in[i] = gPool[i];
	for (i = 0; i < BUF; i++)
		in[VM_ENTROPY_STATE + i] = gBuf[i];
	VmSha256(in, sizeof(in), gPool);
	gPos = 0;
}

/* pool = SHA-256(pool || tag): a one-way step, so an output never shows the pool */
static void Next(UInt8 tag)
{
	UInt8 in[VM_ENTROPY_STATE + 1];
	UInt16 i;

	for (i = 0; i < VM_ENTROPY_STATE; i++)
		in[i] = gPool[i];
	in[VM_ENTROPY_STATE] = tag;
	VmSha256(in, sizeof(in), gPool);
}

void EntropyAdd(const UInt8 *p, UInt16 n, Boolean credit)
{
	while (n--) {
		gBuf[gPos] ^= *p++;
		if (++gPos == BUF)
			Stir();
	}
	if (credit && gCount < 2 * VM_ENTROPY_MIN)
		gCount++;
}

void EntropyEvent(UInt32 ticks, UInt16 type, Int16 x, Int16 y, UInt16 chr, Boolean credit)
{
	UInt8 s[12];

	s[0] = (UInt8)ticks;          /* the low bits jitter most: first */
	s[1] = (UInt8)(ticks >> 8);
	s[2] = (UInt8)(ticks >> 16);
	s[3] = (UInt8)(ticks >> 24);
	s[4] = (UInt8)type;
	s[5] = (UInt8)(type >> 8);
	s[6] = (UInt8)x;
	s[7] = (UInt8)((UInt16)x >> 8);
	s[8] = (UInt8)y;
	s[9] = (UInt8)((UInt16)y >> 8);
	s[10] = (UInt8)chr;
	s[11] = (UInt8)(chr >> 8);
	EntropyAdd(s, sizeof(s), credit);
}

UInt16 EntropyCount(void)
{
	return gCount;
}

Boolean EntropyKey(UInt8 *out, UInt16 n)
{
	UInt8 in[VM_ENTROPY_STATE + 1], h[32];
	UInt16 i;

	if (gCount < VM_ENTROPY_MIN || n > sizeof(h))
		return 0;
	Stir();
	for (i = 0; i < VM_ENTROPY_STATE; i++)
		in[i] = gPool[i];
	in[VM_ENTROPY_STATE] = 'K';
	VmSha256(in, sizeof(in), h);
	for (i = 0; i < n; i++)
		out[i] = h[i];
	Next('N');
	return 1;
}

void EntropySave(UInt8 state[VM_ENTROPY_STATE], UInt16 *count)
{
	UInt16 i;

	Stir();
	Next('S');                 /* what's saved isn't the pool a key came from */
	for (i = 0; i < VM_ENTROPY_STATE; i++)
		state[i] = gPool[i];
	*count = gCount;
}

/* Mixed in, not copied over: samples from this launch so far stay. */
void EntropyLoad(const UInt8 state[VM_ENTROPY_STATE], UInt16 count)
{
	EntropyAdd(state, VM_ENTROPY_STATE, 0);
	Stir();
	if (count > gCount)
		gCount = count > 2 * VM_ENTROPY_MIN ? 2 * VM_ENTROPY_MIN : count;
}
