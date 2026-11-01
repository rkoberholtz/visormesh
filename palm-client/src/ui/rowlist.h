/*
 * A scrolling list of fixed-height rows drawn by a callback, in a form's
 * gadget area with a scroll bar beside it. Used by every list screen: rows
 * can be section headers (not selectable) or items. The hard up/down buttons
 * page it. The form forwards its events with RowListHandleEvent.
 */
#ifndef VM_ROWLIST_H
#define VM_ROWLIST_H

#include <PalmOS.h>
#include "../sections.h"   /* this module is in the second code segment */

typedef void    (*RowDrawFn)(UInt16 row, const RectangleType *bounds, void *ctx);
typedef Boolean (*RowSelectableFn)(UInt16 row, void *ctx);

typedef struct {
	UInt16          gadgetId;
	UInt16          scrollId;
	Coord           rowH;
	UInt16          count;
	UInt16          top;          /* first visible row */
	RowDrawFn       draw;
	RowSelectableFn selectable;   /* NULL = every row can be tapped */
	void           *ctx;
} RowList;

void   RowListInit(RowList *list, UInt16 gadgetId, UInt16 scrollId, Coord rowH,
                   RowDrawFn draw, RowSelectableFn selectable, void *ctx) VM_SECT2;
/* Changes the row count, keeping the scroll position where it can. */
void   RowListSetCount(RowList *list, UInt16 count) VM_SECT2;
UInt16 RowListVisibleRows(const RowList *list) VM_SECT2;
void   RowListScrollTo(RowList *list, UInt16 top) VM_SECT2;
void   RowListScrollToEnd(RowList *list) VM_SECT2;
/* Redraws the visible rows and updates the scroll bar. */
void   RowListDraw(RowList *list) VM_SECT2;

/*
 * Handles pen taps in the list, scroll-bar moves and the page buttons. Returns
 * true if it used the event; a tap on a selectable row also sets *tapped to the
 * row (and returns true), otherwise *tapped is left alone.
 */
Boolean RowListHandleEvent(RowList *list, EventType *event, Int16 *tapped) VM_SECT2;

#endif
