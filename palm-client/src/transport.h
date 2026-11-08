/*
 * Byte-stream link to the companion radio. One implementation per transport:
 *   transport_srm.c  Serial Manager: the cradle serial port, or the Visor's
 *                    USB data port bridged to a radio by tools/visor_bridge.py
 *   transport_sb.c   SB-LoRa Springboard module via the SC16IS740; picked
 *                    with VM_PORT_SBLORA, which transport_srm.c hands over
 */
#ifndef VM_TRANSPORT_H
#define VM_TRANSPORT_H

#include <PalmOS.h>

/* Serial Manager ports (see the Ports screen for what a device has). */
#define VM_PORT_SERIAL  serPortCradlePort
#define VM_PORT_USB     'HsUd'      /* Handspring "USB" driver: the Visor's GENERIC USB port */
#define VM_PORT_SBLORA  'SbLo'      /* not a Serial Manager port: transport_sb.c */

#define vmErrNoModule   (appErrorClass | 6)   /* no SB-LoRa module answered (session.h has 1-5, 7 and 8) */
#define vmErrPartSent   (appErrorClass | 9)   /* the link stopped part way through a frame */

void   TransportSetPort(UInt32 port);   /* takes effect on the next TransportOpen */
UInt32 TransportGetPort(void);

Err    TransportOpen(void);
void   TransportClose(void);
/*
 * serErrTimeOut means nothing was sent (USB host away, TX FIFO stuck), so
 * the frame can be sent again; vmErrPartSent means some of it went.
 */
Err    TransportSend(const UInt8 *buf, UInt16 len);
/* One attempt, and none over USB while the host isn't reading: for a reconnect that mustn't hold up the app. */
Err    TransportSendOnce(const UInt8 *buf, UInt16 len);

/*
 * Waits up to timeoutTicks for data, then returns whatever is available,
 * up to max bytes. Returns 0 on timeout.
 */
UInt16 TransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP);

Err    SbTransportOpen(void);
void   SbTransportClose(void);
Err    SbTransportSend(const UInt8 *buf, UInt16 len);
UInt16 SbTransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP);
/* Whether receiving is interrupt-driven (the slot handler installed) rather than polled. */
Boolean SbTransportIrq(void);
/* The module self-test: loops the UART's TX back to its RX inside the chip (nothing reaches the radio). */
void   SbTransportLoopback(Boolean on);

/*
 * What's in the Springboard slot, without touching the bus. With
 * SB_SLOT_READY, base and size are the module's chip selects, and the SPI
 * driver uses them.
 */
#define SB_SLOT_NONE   0   /* not a Handspring device */
#define SB_SLOT_EMPTY  1
#define SB_SLOT_NO_CS  2   /* a module, but the OS gave no chip-select window */
#define SB_SLOT_READY  3
UInt8  SbSlotState(UInt32 *base, UInt32 *size);

#endif
