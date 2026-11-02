/*
 * SB-LoRa Springboard module: bus SPI over the slot chip selects, and the
 * SC16IS740 SPI-UART behind it (hardware/sb-lora/README.md, "Bus SPI").
 *
 *   CS0* access   latches nCS = NOT A17 and MOSI = NOT A8 on its rising edge
 *   CS1* read     one SCLK pulse; MISO comes back on D0
 *
 * So one SPI bit is a CS0 read at an address that encodes nCS and MOSI,
 * then a CS1 read that clocks it and returns MISO. The SC16IS740 is mode 0.
 *
 * Plain C with no Palm OS calls, so it also builds on the host, where
 * tests/test_sblora.c supplies SimBusRead() and a simulated module.
 */
#ifndef VM_SB_SPI_H
#define VM_SB_SPI_H

#include "protocol.h"   /* UInt8 etc. on the host */

/*
 * csSlot0 base and the default 16 MB chip-select size (Springboard Dev Guide
 * §2.1). Used until SbSetBase(); on the Visor, transport_sb.c passes what
 * HsCardAttrGet reports.
 */
#define SB_CS0_BASE  0x28000000UL
#define SB_CS_SIZE   0x01000000UL

/* SC16IS740 registers (general set; DLL/DLH need LCR = 0x80, EFR LCR = 0xBF). */
#define SB_RHR       0x00   /* read */
#define SB_THR       0x00   /* write */
#define SB_IER       0x01
#define SB_FCR       0x02   /* write */
#define SB_LCR       0x03
#define SB_MCR       0x04
#define SB_LSR       0x05
#define SB_SPR       0x07
#define SB_TXLVL     0x08
#define SB_RXLVL     0x09
#define SB_IOCONTROL 0x0E
#define SB_DLL       0x00
#define SB_DLH       0x01
#define SB_EFR       0x02

#define SB_FIFO_SIZE 64

/*
 * Finds the module by writing and reading back the scratch register. The
 * connector's address lines may map to CPU A1 and up (word addressing) or
 * to A0 and up; both are tried, and the one that works is kept.
 */
Boolean SbProbe(void) VM_SECT3;

/* csSlot0's address and the size of each chip select; csSlot1 follows csSlot0. */
void    SbSetBase(UInt32 cs0, UInt32 csSize) VM_SECT3;

/* 115200 8N1 (14.7456 MHz crystal, DLL = 8), FIFOs on, interrupts off. */
void    SbUartInit(void) VM_SECT3;

/* Low-power sleep (IER bit 4) while the link is closed, or wake. */
void    SbSleep(Boolean sleep) VM_SECT3;

UInt8   SbReadReg(UInt8 reg) VM_SECT3;
void    SbWriteReg(UInt8 reg, UInt8 val) VM_SECT3;

/* Burst FIFO access: n bytes to THR, or n bytes from RHR, in one transaction. */
void    SbWriteFifo(const UInt8 *buf, UInt16 n) VM_SECT3;
void    SbReadFifo(UInt8 *buf, UInt16 n) VM_SECT3;

#ifdef __palmos__
#define SB_BUS_READ(addr) (*(volatile UInt16 *)(addr))
#else
UInt16 SimBusRead(UInt32 addr);
#define SB_BUS_READ(addr) SimBusRead(addr)
#ifndef true
#define true  1
#define false 0
#endif
#endif

#endif
