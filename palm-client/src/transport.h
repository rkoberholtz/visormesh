/*
 * Byte-stream link to the companion radio. One implementation per transport:
 *   transport_srm.c  Serial Manager (cradle serial port, or an emulator's
 *                    serial port bridged to a radio on the host)
 *   (later)          SB-LoRa Springboard module via the SC16IS740
 */
#ifndef VM_TRANSPORT_H
#define VM_TRANSPORT_H

#include <PalmOS.h>

Err    TransportOpen(void);
void   TransportClose(void);
Err    TransportSend(const UInt8 *buf, UInt16 len);

/*
 * Waits up to timeoutTicks for data, then returns whatever is available,
 * up to max bytes. Returns 0 on timeout.
 */
UInt16 TransportRecv(UInt8 *buf, UInt16 max, Int32 timeoutTicks, Err *errP);

#endif
