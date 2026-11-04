/*
 * The app's record databases (messages, contacts, channels): open one,
 * creating it with the backup bit (HotSync keeps a copy) the first time.
 */
#ifndef VM_DB_H
#define VM_DB_H

#include <PalmOS.h>
#include "sections.h"

/* Opens name read/write into *db (0 on error), creating it with VM_CREATOR and type if missing. */
Err DbOpen(const Char *name, UInt32 type, DmOpenRef *db) VM_SECT4;

#endif
