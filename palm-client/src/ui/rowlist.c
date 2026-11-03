#include <PalmOS.h>
#include "rowlist.h"

#define RL_HOLD_TICKS (SysTicksPerSecond() / 2)   /* pen still this long: a hold */
#define RL_HOLD_SLOP  3                          /* pixels the pen may wander */

/* In the second code segment: see sections.h for the rules. */
static void GetBounds(const RowList *list, RectangleType *r) VM_SECT2;
static UInt16 MaxTop(const RowList *list) VM_SECT2;
static void UpdateScrollBar(const RowList *list) VM_SECT2;
static void Scroll(RowList *list, Int16 delta) VM_SECT2;

static void GetBounds(const RowList *list, RectangleType *r)
{
	FormType *form = FrmGetActiveForm();

	FrmGetObjectBounds(form, FrmGetObjectIndex(form, list->gadgetId), r);
}

static UInt16 MaxTop(const RowList *list)
{
	UInt16 visible = RowListVisibleRows(list);

	return list->count > visible ? list->count - visible : 0;
}

void RowListInit(RowList *list, UInt16 gadgetId, UInt16 scrollId, Coord rowH,
                 RowDrawFn draw, RowSelectableFn selectable, void *ctx)
{
	list->gadgetId = gadgetId;
	list->scrollId = scrollId;
	list->rowH = rowH;
	list->count = 0;
	list->top = 0;
	list->draw = draw;
	list->selectable = selectable;
	list->hold = NULL;
	list->ctx = ctx;
}

UInt16 RowListVisibleRows(const RowList *list)
{
	RectangleType r;

	GetBounds(list, &r);
	return (UInt16)(r.extent.y / list->rowH);
}

void RowListSetCount(RowList *list, UInt16 count)
{
	list->count = count;
	if (list->top > MaxTop(list))
		list->top = MaxTop(list);
}

void RowListScrollTo(RowList *list, UInt16 top)
{
	list->top = top > MaxTop(list) ? MaxTop(list) : top;
}

void RowListScrollToEnd(RowList *list)
{
	list->top = MaxTop(list);
}

static void UpdateScrollBar(const RowList *list)
{
	FormType *form = FrmGetActiveForm();
	ScrollBarType *bar = FrmGetObjectPtr(form, FrmGetObjectIndex(form, list->scrollId));
	UInt16 visible = RowListVisibleRows(list);

	SclSetScrollBar(bar, (Int16)list->top, 0, (Int16)MaxTop(list), (Int16)(visible ? visible - 1 : 1));
}

void RowListDraw(RowList *list)
{
	RectangleType area, row;
	UInt16 i, visible = RowListVisibleRows(list);

	GetBounds(list, &area);
	WinEraseRectangle(&area, 0);
	for (i = 0; i < visible && list->top + i < list->count; i++) {
		RctSetRectangle(&row, area.topLeft.x, area.topLeft.y + i * list->rowH, area.extent.x, list->rowH);
		list->draw(list->top + i, &row, list->ctx);
	}
	UpdateScrollBar(list);
}

static void Scroll(RowList *list, Int16 delta)
{
	Int16 top = (Int16)list->top + delta;

	if (top < 0)
		top = 0;
	RowListScrollTo(list, (UInt16)top);
	RowListDraw(list);
}

Boolean RowListHandleEvent(RowList *list, EventType *event, Int16 *tapped)
{
	RectangleType area, rowR;
	UInt16 row, visible;
	Boolean penDown, inRow, held = false;
	Coord x, y;
	UInt32 start;

	switch (event->eType) {
	case sclRepeatEvent:
		if (event->data.sclRepeat.scrollBarID != list->scrollId)
			return false;
		list->top = (UInt16)event->data.sclRepeat.newValue;
		RowListDraw(list);
		return false;   /* let the scroll bar keep tracking */

	case keyDownEvent:
		visible = RowListVisibleRows(list);
		if (event->data.keyDown.chr == pageUpChr) {
			Scroll(list, -(Int16)(visible > 1 ? visible - 1 : 1));
			return true;
		}
		if (event->data.keyDown.chr == pageDownChr) {
			Scroll(list, (Int16)(visible > 1 ? visible - 1 : 1));
			return true;
		}
		return false;

	case penDownEvent:
		GetBounds(list, &area);
		if (!RctPtInRectangle(event->screenX, event->screenY, &area))
			return false;
		row = list->top + (UInt16)((event->screenY - area.topLeft.y) / list->rowH);
		if (row >= list->count || (list->selectable && !list->selectable(row, list->ctx)))
			return true;
		/* highlight while the pen is down on the row, like a Palm list */
		RctSetRectangle(&rowR, area.topLeft.x, area.topLeft.y + (row - list->top) * list->rowH,
		                area.extent.x, list->rowH);
		WinInvertRectangle(&rowR, 0);
		inRow = true;
		start = TimGetTicks();
		do {
			EvtGetPen(&x, &y, &penDown);
			if (held)
				continue;    /* waiting for the pen to lift */
			if (list->hold && penDown &&
			    (x - event->screenX > RL_HOLD_SLOP || event->screenX - x > RL_HOLD_SLOP ||
			     y - event->screenY > RL_HOLD_SLOP || event->screenY - y > RL_HOLD_SLOP))
				start = 0;   /* moved: no longer a hold */
			if (list->hold && start && inRow && TimGetTicks() - start >= RL_HOLD_TICKS) {
				held = true;
				WinInvertRectangle(&rowR, 0);   /* unhighlight: the cue to lift */
				continue;
			}
			if (RctPtInRectangle(x, y, &rowR) != inRow) {
				inRow = !inRow;
				WinInvertRectangle(&rowR, 0);
			}
		} while (penDown);
		if (held) {
			if (!list->hold(row, &rowR, list->ctx))
				*tapped = (Int16)row;
		} else if (inRow) {
			WinInvertRectangle(&rowR, 0);
			*tapped = (Int16)row;
		}
		return true;

	default:
		return false;
	}
}
