#include "sb_spi.h"

/*
 * The bus core (Xfer to BusReadFifo) and SbDrainRx stay in the first segment
 * and use only the SbBus they're given: the interrupt handler runs them, and
 * it can't reach globals or call into vmcode3 (both need A5).
 */
static UInt8 Xfer(const SbBus *b, UInt8 out);
static void Select(const SbBus *b);
static void Deselect(const SbBus *b);
static UInt8 BusReadReg(const SbBus *b, UInt8 reg);
static void BusReadFifo(const SbBus *b, UInt8 *buf, UInt16 n);
static Boolean ScratchOk(void) VM_SECT3;

static SbBus gBus = { SB_CS0_BASE, SB_CS0_BASE + SB_CS_SIZE, 0, 0 };

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
	Select(&gBus);
	Xfer(&gBus, (UInt8)(reg << 3));
	Xfer(&gBus, val);
	Deselect(&gBus);
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
			r->overflow = true;
			continue;
		}
		toEnd = SB_RING_SIZE - (head & (SB_RING_SIZE - 1));
		if (n > room)
			n = room;
		if (n > toEnd)
			n = toEnd;
		BusReadFifo(&r->bus, r->buf + (head & (SB_RING_SIZE - 1)), n);
		r->head = head + n;
	}
}

static Boolean ScratchOk(void)
{
	SbWriteReg(SB_SPR, 0x55);
	if (SbReadReg(SB_SPR) != 0x55)
		return false;
	SbWriteReg(SB_SPR, 0xAA);
	return SbReadReg(SB_SPR) == 0xAA;
}

void SbSetBase(UInt32 cs0, UInt32 csSize)
{
	gBus.cs0 = cs0;
	gBus.cs1 = cs0 + csSize;
}

Boolean SbProbe(void)
{
	UInt8 shift;

	/* Connector A0 = CPU A1 first: the slot only does 16-bit, even-address cycles. */
	for (shift = 1; ; shift--) {
		gBus.offNcs = 1UL << (17 + shift);
		gBus.offMosi = 1UL << (8 + shift);
		Deselect(&gBus);
		if (ScratchOk())
			return true;
		if (shift == 0)
			break;
	}
	Deselect(&gBus);
	return false;
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
