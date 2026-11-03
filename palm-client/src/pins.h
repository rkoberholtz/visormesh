/*
 * Pinned direct conversations: they stay at the top of Home's Direct list
 * (most recent first among themselves), above the unpinned ones. Kept by the
 * contact's key prefix, so a pin survives the contact list refresh and works
 * for senders who aren't contacts.
 */
#ifndef VM_PINS_H
#define VM_PINS_H

#include <PalmOS.h>
#include "sections.h"

void    PinsLoad(void) VM_SECT4;
Boolean DirectPinned(const UInt8 *key6) VM_SECT4;
/* Pins or unpins; false (with an alert) if the list of pins is full. */
Boolean DirectPinToggle(const UInt8 *key6) VM_SECT4;

#endif
