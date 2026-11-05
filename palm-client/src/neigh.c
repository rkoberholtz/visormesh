#include <PalmOS.h>
#include "session.h"
#include "protocol2.h"
#include "neigh.h"

static UInt8       gRepeater[6];
static VmNeighbour gList[VM_NEIGH_MAX];
static UInt16      gCount = 0;
static UInt32      gAt = 0;

Boolean NeighNote(const UInt8 *repeater6, const Char *text, UInt16 len)
{
	VmNeighbour list[VM_NEIGH_MAX];
	UInt16 start = 0, end, n = 0;
	UInt32 secs;

	while (start < len) {
		for (end = start; end < len && text[end] != '\n'; end++)
			;
		if (n < VM_NEIGH_MAX) {
			if (!VmParseNeighbour(text + start, end - start, list[n].prefix, &secs, &list[n].snrQ))
				return false;     /* any other line: not a neighbour list */
			n++;
		}
		start = end + 1;
	}
	if (n == 0)
		return false;
	MemMove(gRepeater, repeater6, 6);
	MemMove(gList, list, n * sizeof(VmNeighbour));
	gCount = n;
	gAt = SessionNow();
	return true;
}

UInt16 NeighGet(UInt8 *repeater6, VmNeighbour *out)
{
	if (!gCount || SessionNow() - gAt > VM_NEIGH_SECS)
		return 0;
	MemMove(repeater6, gRepeater, 6);
	MemMove(out, gList, gCount * sizeof(VmNeighbour));
	return gCount;
}
