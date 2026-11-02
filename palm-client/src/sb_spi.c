#include "sb_spi.h"

static UInt8 Xfer(UInt8 out) VM_SECT3;
static void Select(void) VM_SECT3;
static void Deselect(void) VM_SECT3;
static Boolean ScratchOk(void) VM_SECT3;

static UInt32 gCs0 = SB_CS0_BASE;
static UInt32 gCs1 = SB_CS0_BASE + SB_CS_SIZE;
static UInt32 gOffNcs;    /* byte offset that drives connector A17 */
static UInt32 gOffMosi;   /* ... and A8 */

/* nCS = NOT A17: an access with A17 high selects the chip. */
static void Select(void)
{
	(void)SB_BUS_READ(gCs0 + gOffNcs);
}

static void Deselect(void)
{
	(void)SB_BUS_READ(gCs0);
}

/*
 * One byte, MSB first. The CS0 read sets MOSI (= NOT A8) while keeping nCS
 * low; the CS1 read raises SCLK, where the chip samples MOSI, and returns
 * MISO, which only changes on the falling edge after it.
 */
static UInt8 Xfer(UInt8 out)
{
	UInt32 sel = gCs0 + gOffNcs;
	UInt8 in = 0;
	UInt8 mask;

	for (mask = 0x80; mask; mask >>= 1) {
		(void)SB_BUS_READ((out & mask) ? sel : sel + gOffMosi);
		in = (UInt8)((in << 1) | (SB_BUS_READ(gCs1) & 1));
	}
	return in;
}

/* Register byte: bit 7 = read, bits 6:3 = register, bits 2:1 = channel 0. */
UInt8 SbReadReg(UInt8 reg)
{
	UInt8 val;

	Select();
	Xfer((UInt8)(0x80 | (reg << 3)));
	val = Xfer(0);
	Deselect();
	return val;
}

void SbWriteReg(UInt8 reg, UInt8 val)
{
	Select();
	Xfer((UInt8)(reg << 3));
	Xfer(val);
	Deselect();
}

void SbWriteFifo(const UInt8 *buf, UInt16 n)
{
	Select();
	Xfer(SB_THR << 3);
	while (n--)
		Xfer(*buf++);
	Deselect();
}

void SbReadFifo(UInt8 *buf, UInt16 n)
{
	Select();
	Xfer(0x80 | (SB_RHR << 3));
	while (n--)
		*buf++ = Xfer(0);
	Deselect();
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
	gCs0 = cs0;
	gCs1 = cs0 + csSize;
}

Boolean SbProbe(void)
{
	UInt8 shift;

	/* Connector A0 = CPU A1 first: the slot only does 16-bit, even-address cycles. */
	for (shift = 1; ; shift--) {
		gOffNcs = 1UL << (17 + shift);
		gOffMosi = 1UL << (8 + shift);
		Deselect();
		if (ScratchOk())
			return true;
		if (shift == 0)
			break;
	}
	Deselect();
	return false;
}

void SbUartInit(void)
{
	SbWriteReg(SB_IOCONTROL, 0x08);   /* software reset; self-clearing */
	SbWriteReg(SB_LCR, 0x80);         /* divisor latch */
	SbWriteReg(SB_DLL, 8);            /* 14.7456 MHz / (16 * 8) = 115200 */
	SbWriteReg(SB_DLH, 0);
	SbWriteReg(SB_LCR, 0x03);         /* 8N1 */
	SbWriteReg(SB_FCR, 0x07);         /* FIFOs on, both reset */
	SbWriteReg(SB_IER, 0x00);         /* polled */
}

/* Sleep mode is an enhanced function: EFR bit 4 must be set to change IER bit 4. */
void SbSleep(Boolean sleep)
{
	SbWriteReg(SB_LCR, 0xBF);
	SbWriteReg(SB_EFR, 0x10);
	SbWriteReg(SB_LCR, 0x03);
	SbWriteReg(SB_IER, sleep ? 0x10 : 0x00);
}
