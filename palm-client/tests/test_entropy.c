/*
 * Host tests for the key pool (src/entropy.c). Run with `make test`.
 * They can't measure randomness; they check the plumbing: no key before
 * enough samples, uncredited input not counting, keys never repeating, and
 * a saved pool carrying over.
 */
#include <stdio.h>
#include <string.h>
#include "../src/entropy.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* n pen moves, a tick apart, from a start time. */
static void Scribble(int n, UInt32 start)
{
	int i;

	for (i = 0; i < n; i++)
		EntropyEvent(start + (UInt32)i * 3, 2, (Int16)(i % 160), (Int16)(i * 7 % 160), 0, 1);
}

int main(void)
{
	UInt8 a[16], b[16], c[16], state[VM_ENTROPY_STATE], state2[VM_ENTROPY_STATE];
	UInt16 count, i, ones;
	int k;

	/* nothing yet */
	CHECK(!EntropyKey(a, 16));

	/* uncredited input (nil events, a reloaded pool) doesn't count */
	for (k = 0; k < 1000; k++)
		EntropyEvent((UInt32)k, 0, 0, 0, 0, 0);
	CHECK(EntropyCount() == 0 && !EntropyKey(a, 16));

	Scribble(VM_ENTROPY_MIN - 1, 1000);
	CHECK(!EntropyKey(a, 16));
	Scribble(1, 5000);
	CHECK(EntropyCount() == VM_ENTROPY_MIN);
	CHECK(EntropyKey(a, 16));

	/* the next key differs, with no new input */
	CHECK(EntropyKey(b, 16));
	CHECK(memcmp(a, b, 16) != 0);

	/* more than 32 bytes can't be had in one go */
	CHECK(!EntropyKey(c, 33));

	/* keys look like random bytes: about half their bits set over many */
	for (k = 0, ones = 0; k < 256; k++) {
		CHECK(EntropyKey(c, 16));
		for (i = 0; i < 16; i++) {
			UInt8 v = c[i];
			while (v) {
				ones += v & 1;
				v >>= 1;
			}
		}
	}
	CHECK(ones > 256 * 128 / 2 - 600 && ones < 256 * 128 / 2 + 600);

	/* save, then a fresh launch: the saved pool and count carry over, mixed in */
	EntropySave(state, &count);
	CHECK(count >= VM_ENTROPY_MIN);
	EntropySave(state2, &count);
	CHECK(memcmp(state, state2, VM_ENTROPY_STATE) != 0);   /* saving moves the pool on */
	EntropyLoad(state, count);
	CHECK(EntropyKey(a, 16));

	/* the count is capped, so an old count can't be inflated */
	EntropyLoad(state, 60000);
	CHECK(EntropyCount() <= 2 * VM_ENTROPY_MIN);

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("entropy: all tests passed\n");
	return 0;
}
