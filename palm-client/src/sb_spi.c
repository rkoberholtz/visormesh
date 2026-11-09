#include "sb_spi.h"
#ifdef __palmos__
#include "hs_card.h"
#endif

/*
 * The core, SbCoreStart to SbCoreEnd, is in the first segment and uses only
 * the SbBus or SbRing it's given: the interrupt handler runs it, which can't
 * reach globals or call into vmcode3 (both need A5), and SbWakeArm copies it
 * as one block. Keep every function without VM_SECT3 between the two.
 */
static UInt8 Xfer(const SbBus *b, UInt8 out);
static void Select(const SbBus *b);
static void Deselect(const SbBus *b);
static UInt8 BusReadReg(const SbBus *b, UInt8 reg);
static void BusWriteReg(const SbBus *b, UInt8 reg, UInt8 val);
static void BusReadFifo(const SbBus *b, UInt8 *buf, UInt16 n);
static void Scan(SbRing *r, const UInt8 *p, UInt16 n);
static Boolean ScratchOk(UInt8 got[2]) VM_SECT3;

static SbBus gBus = { SB_CS0_BASE, SB_CS0_BASE + SB_CS_SIZE, 0, 0 };

void SbCoreStart(void)
{
}

/* nCS = NOT A17: an access with A17 high selects the chip. */
static void Select(const SbBus *b)
{
	(void)SB_BUS_READ(b->cs0 + b->offNcs);
}

static void Deselect(const SbBus *b)
{
	(void)SB_BUS_READ(b->cs0);
}

/*
 * One byte, MSB first. The CS0 read sets MOSI (= NOT A8) while keeping nCS
 * low; the CS1 read raises SCLK, where the chip samples MOSI, and returns
 * MISO, which only changes on the falling edge after it.
 */
static UInt8 Xfer(const SbBus *b, UInt8 out)
{
	UInt32 sel = b->cs0 + b->offNcs;
	UInt32 clk = b->cs1;
	UInt8 in = 0;
	UInt8 mask;

	for (mask = 0x80; mask; mask >>= 1) {
		(void)SB_BUS_READ((out & mask) ? sel : sel + b->offMosi);
		in = (UInt8)((in << 1) | (SB_BUS_READ(clk) & 1));
	}
	return in;
}

/* Register byte: bit 7 = read, bits 6:3 = register, bits 2:1 = channel 0. */
static UInt8 BusReadReg(const SbBus *b, UInt8 reg)
{
	UInt8 val;

	Select(b);
	Xfer(b, (UInt8)(0x80 | (reg << 3)));
	val = Xfer(b, 0);
	Deselect(b);
	return val;
}

static void BusWriteReg(const SbBus *b, UInt8 reg, UInt8 val)
{
	Select(b);
	Xfer(b, (UInt8)(reg << 3));
	Xfer(b, val);
	Deselect(b);
}

static void BusReadFifo(const SbBus *b, UInt8 *buf, UInt16 n)
{
	Select(b);
	Xfer(b, 0x80 | (SB_RHR << 3));
	while (n--)
		*buf++ = Xfer(b, 0);
	Deselect(b);
}

UInt8 SbReadReg(UInt8 reg)
{
	return BusReadReg(&gBus, reg);
}

void SbWriteReg(UInt8 reg, UInt8 val)
{
	BusWriteReg(&gBus, reg, val);
}

void SbWriteFifo(const UInt8 *buf, UInt16 n)
{
	Select(&gBus);
	Xfer(&gBus, SB_THR << 3);
	while (n--)
		Xfer(&gBus, *buf++);
	Deselect(&gBus);
}

void SbReadFifo(UInt8 *buf, UInt16 n)
{
	BusReadFifo(&gBus, buf, n);
}

const SbBus *SbCurrentBus(void)
{
	return &gBus;
}

/* Watches for '>' 01 00 83 across reads. A frame's payload could hold the same bytes: that only costs a check. */
static void Scan(SbRing *r, const UInt8 *p, UInt16 n)
{
	UInt8 m = r->match, c;

	while (n--) {
		c = *p++;
		if ((m == 0 && c == 0x3E) || (m == 1 && c == 0x01) || (m == 2 && c == 0x00)) {
			m++;
		} else if (m == 3 && c == 0x83) {
			r->waiting = true;
			m = 0;
		} else {
			m = c == 0x3E;
		}
	}
	r->match = m;
}

/*
 * Empties the RX FIFO into the ring, which also releases the chip's IRQ
 * (it's level-triggered). Bytes that don't fit are read and dropped.
 */
void SbDrainRx(SbRing *r)
{
	UInt8 drop[SB_FIFO_SIZE];
	UInt16 n, head, room, toEnd;

	while ((n = BusReadReg(&r->bus, SB_RXLVL)) != 0) {
		if (n > SB_FIFO_SIZE)
			n = SB_FIFO_SIZE;
		head = r->head;
		room = SB_RING_SIZE - (UInt16)(head - r->tail);
		if (room == 0) {
			BusReadFifo(&r->bus, drop, n);
			Scan(r, drop, n);
			r->overflow = true;
			continue;
		}
		toEnd = SB_RING_SIZE - (head & (SB_RING_SIZE - 1));
		if (n > room)
			n = room;
		if (n > toEnd)
			n = toEnd;
		BusReadFifo(&r->bus, r->buf + (head & (SB_RING_SIZE - 1)), n);
		Scan(r, r->buf + (head & (SB_RING_SIZE - 1)), n);
		r->head = head + n;
	}
}

void SbIrq(SbParam cardParam, Boolean *sysAwakeP)
{
	SbRing *r = (SbRing *)cardParam;

	SbDrainRx(r);
	if (!r->waiting)
		return;
	if (!*sysAwakeP) {
		/* no system calls yet: hold IRQ* with the chip kept awake, and have the rest of the system woken */
		BusWriteReg(&r->bus, SB_IER, (UInt8)((r->ier & ~SB_IER_SLEEP) | SB_IER_THR));
		r->woke = true;
		*sysAwakeP = true;
		return;
	}
	BusWriteReg(&r->bus, SB_IER, r->ier);
	r->waiting = false;
	r->wakes++;
	if (r->resident)
		HsCardEventPost(hsCardSlot, 0, 0);
	if (r->woke)
		HsEvtResetAutoOffTimer(SB_AWAKE_TICKS, false);
	r->woke = false;
}

Boolean SbWakeEvt(SbParam cardParam, UInt16 evtNum, UInt16 evtParam)
{
#ifdef __palmos__
	SbRing *r = (SbRing *)cardParam;
	LocalID id = DmFindDatabase(r->appCard, r->appName);

	if (id)
		AlmSetAlarm(r->appCard, id, 0, TimGetSeconds(), true);
#else
	(void)cardParam;
#endif
	(void)evtNum;
	(void)evtParam;
	return true;
}

static Boolean ScratchOk(UInt8 got[2])
{
	SbWriteReg(SB_SPR, 0x55);
	got[0] = SbReadReg(SB_SPR);
	SbWriteReg(SB_SPR, 0xAA);
	got[1] = SbReadReg(SB_SPR);
	return got[0] == 0x55 && got[1] == 0xAA;
}

void SbSetBase(UInt32 cs0, UInt32 csSize)
{
	gBus.cs0 = cs0;
	gBus.cs1 = cs0 + csSize;
}

Boolean SbTryMapping(UInt8 shift, UInt8 got[2])
{
	Boolean ok;

	gBus.offNcs = 1UL << (17 + shift);
	gBus.offMosi = 1UL << (8 + shift);
	Deselect(&gBus);
	ok = ScratchOk(got);
	Deselect(&gBus);
	return ok;
}

UInt8 SbMappingShift(void)
{
	return gBus.offMosi == (1UL << 9) ? 1 : 0;
}

Boolean SbProbe(void)
{
	UInt8 got[2];

	/* Connector A0 = CPU A1 first: the slot only does 16-bit, even-address cycles. */
	return SbTryMapping(1, got) || SbTryMapping(0, got);
}

/*
 * 64 bytes at 115200 take 5.6 ms; a register read is a few hundred bus
 * cycles, so this many reads is well past that without needing a timer.
 */
#define SB_LOOP_POLLS 4000

UInt16 SbLoopback(const UInt8 *out, UInt16 n, UInt8 *in)
{
	UInt16 got = 0, polls, k;

	if (n > SB_FIFO_SIZE)
		n = SB_FIFO_SIZE;
	SbWriteReg(SB_FCR, 0x07);   /* empty both FIFOs */
	SbWriteReg(SB_MCR, SB_MCR_LOOPBACK);
	SbWriteFifo(out, n);
	for (polls = 0; got < n && polls < SB_LOOP_POLLS; polls++) {
		k = SbReadReg(SB_RXLVL);
		if (k > n - got)
			k = n - got;
		if (k) {
			SbReadFifo(in + got, k);
			got += k;
		}
	}
	SbWriteReg(SB_MCR, 0);
	return got;
}

void SbUartInit(void)
{
	SbWriteReg(SB_IOCONTROL, 0x08);   /* software reset; self-clearing */
	SbWriteReg(SB_LCR, 0x80);         /* divisor latch */
	SbWriteReg(SB_DLL, 8);            /* 14.7456 MHz / (16 * 8) = 115200 */
	SbWriteReg(SB_DLH, 0);
	SbWriteReg(SB_LCR, 0x03);         /* 8N1 */
	SbWriteReg(SB_FCR, 0x07);         /* FIFOs on, both reset; RX trigger at 8 */
	SbWriteReg(SB_IER, 0x00);         /* interrupts off until a handler is in */
}

/* Sleep mode is an enhanced function: EFR bit 4 must be set to change IER bit 4. */
void SbSleep(Boolean sleep)
{
	SbWriteReg(SB_LCR, 0xBF);
	SbWriteReg(SB_EFR, 0x10);
	SbWriteReg(SB_LCR, 0x03);
	SbWriteReg(SB_IER, sleep ? 0x10 : 0x00);
}

/* Last in the file: everything in the first segment comes before it. */
void SbCoreEnd(void)
{
}
