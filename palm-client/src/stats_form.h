/* Radio Stats (Go menu, or Stats on Node): the companion radio's own report and its noise floor. */
#ifndef VM_STATS_FORM_H
#define VM_STATS_FORM_H

#include <PalmOS.h>
#include "sections.h"   /* this module is in the fifth code segment */

Boolean StatsFormHandleEvent(EventType *event) VM_SECT5;
/* On idle events while the screen is open: reads the radio again every few seconds. */
void    StatsFormIdle(void) VM_SECT5;

#endif
