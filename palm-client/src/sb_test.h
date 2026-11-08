/*
 * SB-LoRa Test (Options > SB-LoRa Test): checks a module in the Springboard
 * slot step by step for board bring-up: the slot's chip selects, which
 * address mapping reaches the SC16IS740, its registers, an internal UART
 * loopback, the slot interrupt and a DEVICE_QUERY to the radio. Nothing is
 * transmitted over the air.
 */
#ifndef VM_SB_TEST_H
#define VM_SB_TEST_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

Boolean SbTestFormHandleEvent(EventType *event) VM_SECT5;

#endif
