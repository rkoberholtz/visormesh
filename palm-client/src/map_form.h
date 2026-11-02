/*
 * Map screen: the basemap (the VisorMeshMap database from tools/make_map.py)
 * with the contacts that advertise a position plotted on it. Nodes without a
 * position aren't shown.
 */
#ifndef VM_MAP_FORM_H
#define VM_MAP_FORM_H

#include <PalmOS.h>
#include "sections.h"

Boolean MapFormHandleEvent(EventType *event) VM_SECT3;

#endif
