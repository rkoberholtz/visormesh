/*
 * Host-side tests for the SB-LoRa bus SPI (src/sb_spi.c). Run with `make test`.
 * SimBusRead() models the carrier board: two 74LVC1G80 latches (Q-bar) on
 * CS0*, the inverter making SCLK from CS1*, and an SC16IS740 in SPI mode 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sb_spi.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

/* The board */
static int present;        /* module in the slot */
static int shift;          /* connector A0 = CPU A(shift) */
static int ncs = 1, mosi;  /* latch outputs */

/* The SC16IS740 */
static UInt8 regs[16], dll, dlh, efr, lcr;
static UInt8 rxq[SB_FIFO_SIZE], rxn;
static UInt8 txlog[256];
static int txn, txFree = SB_FIFO_SIZE;
static int bit, byteIndex, transactions;
static UInt8 cmd, in, out;

static void SimReset(void)
{
	memset(regs, 0, sizeof(regs));
	lcr = 0x1D;
	regs[SB_SPR] = 0xFF;
	dll = dlh = efr = 0;
}

static UInt8 RegRead(UInt8 reg)
{
	if (lcr == 0x80 && reg == SB_DLL) return dll;
	if (lcr == 0x80 && reg == SB_DLH) return dlh;
	if (lcr == 0xBF && reg == SB_EFR) return efr;
	switch (reg) {
	case SB_RHR: {
		UInt8 c = rxn ? rxq[0] : 0;
		if (rxn)
			memmove(rxq, rxq + 1, --rxn);
		return c;
	}
	case SB_LCR:   return lcr;
	case SB_TXLVL: return (UInt8)txFree;
	case SB_RXLVL: return rxn;
	default:       return regs[reg];
	}
}

static void RegWrite(UInt8 reg, UInt8 val)
{
	if (lcr == 0x80 && reg == SB_DLL) { dll = val; return; }
	if (lcr == 0x80 && reg == SB_DLH) { dlh = val; return; }
	if (lcr == 0xBF && reg == SB_EFR) { efr = val; return; }
	switch (reg) {
	case SB_THR:
		if (txn < (int)sizeof(txlog))
			txlog[txn++] = val;
		return;
	case SB_LCR:
		lcr = val;
		return;
	case SB_IER:
		regs[SB_IER] = (efr & 0x10) ? val : (UInt8)(val & 0x0F);
		return;
	case SB_IOCONTROL:
		if (val & 0x08)
			SimReset();
		return;
	default:
		regs[reg] = val;
	}
}

UInt16 SimBusRead(UInt32 addr)
{
	if (addr >= SB_CS0_BASE && addr < SB_CS0_BASE + SB_CS_SIZE) {
		UInt32 off = addr - SB_CS0_BASE;
		int newNcs = !((off >> (17 + shift)) & 1);

		mosi = !((off >> (8 + shift)) & 1);
		if (ncs && !newNcs) {
			bit = byteIndex = 0;
			out = 0;
			transactions++;
		}
		ncs = newNcs;
		return 0xFFFF;
	}
	if (addr >= SB_CS0_BASE + SB_CS_SIZE && addr < SB_CS0_BASE + 2 * SB_CS_SIZE) {
		int miso;

		if (!present || ncs)
			return (UInt16)rand();   /* MISO tri-stated: D0 floats */
		/* A read's data bytes are loaded as their first bit goes out (burst reads repeat). */
		if (bit == 0 && byteIndex > 0 && (cmd & 0x80))
			out = RegRead((cmd >> 3) & 0x0F);
		miso = (out >> 7) & 1;
		in = (UInt8)((in << 1) | mosi);   /* SCLK rising edge */
		out <<= 1;                        /* falling edge: next bit */
		if (++bit == 8) {
			bit = 0;
			if (byteIndex == 0)
				cmd = in;
			else if (!(cmd & 0x80))
				RegWrite((cmd >> 3) & 0x0F, in);
			byteIndex++;
		}
		return (UInt16)(0xFFFE | miso);
	}
	printf("FAIL: bus access outside the slot at 0x%08lx\n", (unsigned long)addr);
	failures++;
	return 0;
}

static void SimInsert(int mapping)
{
	present = 1;
	shift = mapping;
	SimReset();
	rxn = 0;
	txn = 0;
	txFree = SB_FIFO_SIZE;
}

static void test_probe(void)
{
	SimInsert(1);
	CHECK(SbProbe());
	CHECK(ncs);
	SimInsert(0);
	CHECK(SbProbe());
	present = 0;
	CHECK(!SbProbe());
}

static void test_uart_init(void)
{
	SimInsert(1);
	CHECK(SbProbe());
	regs[SB_SPR] = 0x12;
	SbUartInit();
	CHECK(dll == 8 && dlh == 0);
	CHECK(lcr == 0x03);
	CHECK(regs[SB_FCR] == 0x07);
	CHECK(regs[SB_IER] == 0);
	CHECK(regs[SB_SPR] == 0xFF);   /* the reset happened */
	CHECK(ncs);
}

static void test_registers(void)
{
	SimInsert(1);
	CHECK(SbProbe());
	SbWriteReg(SB_SPR, 0x3C);
	CHECK(regs[SB_SPR] == 0x3C);
	CHECK(SbReadReg(SB_SPR) == 0x3C);
	txFree = 17;
	CHECK(SbReadReg(SB_TXLVL) == 17);
}

static void test_fifos(void)
{
	static const UInt8 frame[] = { '<', 2, 0, 0x16, 0x03 };
	UInt8 buf[SB_FIFO_SIZE];
	int before, i;

	SimInsert(1);
	CHECK(SbProbe());
	SbUartInit();
	before = transactions;
	SbWriteFifo(frame, sizeof(frame));
	CHECK(transactions == before + 1);
	CHECK(txn == (int)sizeof(frame) && memcmp(txlog, frame, sizeof(frame)) == 0);

	for (i = 0; i < 40; i++)
		rxq[i] = (UInt8)(0xA0 + i);
	rxn = 40;
	CHECK(SbReadReg(SB_RXLVL) == 40);
	SbReadFifo(buf, 30);
	for (i = 0; i < 30; i++)
		CHECK(buf[i] == (UInt8)(0xA0 + i));
	CHECK(rxn == 10);              /* exactly 30 popped */
	SbReadFifo(buf, 10);
	CHECK(buf[0] == 0xA0 + 30 && buf[9] == 0xA0 + 39);
	CHECK(SbReadReg(SB_RXLVL) == 0);
	CHECK(ncs);
}

static void test_sleep(void)
{
	SimInsert(1);
	CHECK(SbProbe());
	SbUartInit();
	SbSleep(true);
	CHECK(efr & 0x10);
	CHECK(regs[SB_IER] == 0x10);
	CHECK(lcr == 0x03);
	SbSleep(false);
	CHECK(regs[SB_IER] == 0);
}

int main(void)
{
	test_probe();
	test_uart_init();
	test_registers();
	test_fifos();
	test_sleep();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("sblora: all tests passed\n");
	return 0;
}
