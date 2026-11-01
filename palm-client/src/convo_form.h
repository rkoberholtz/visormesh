#ifndef VM_CONVO_FORM_H
#define VM_CONVO_FORM_H

#include <PalmOS.h>

Boolean ConvoFormHandleEvent(EventType *event);
/* Called on idle while the form is up (keeps the byte counter current). */
void    ConvoFormIdle(void);

#endif
