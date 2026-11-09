/*
 * The few Handspring Springboard card calls VisorMesh uses, declared from the
 * Handspring SDK's HsExt.h (docs/reference/Handspring_SDK.zip).
 * Calls go through sysTrapOEMDispatch with a 16-bit selector pushed on the
 * stack, which the dispatcher pops ("unpopped" in HsExt.h).
 */
#ifndef VM_HS_CARD_H
#define VM_HS_CARD_H

#include <PalmOS.h>

#define hsFtrCreator           'hsEx'   /* present on Handspring devices */
#define hsFtrIDVersion         0

#define hsCardSlot             1        /* the Springboard slot; 0 is built-in */

/* HsCardAttrEnum values */
#define hsCardAttrHwInstalled  1        /* UInt8: module present and out of reset */
#define hsCardAttrCsBase       3        /* UInt32: csSlot0 address */
#define hsCardAttrCsSize       4        /* UInt32: size of each chip select */
#define hsCardAttrIntEnable    7        /* UInt8: slot IRQ* enabled (off by default) */
#define hsCardAttrCardParam    8        /* UInt32: passed to the handlers */
#define hsCardAttrIntHandler   9        /* HsCardIntHandlerPtr */

/* Called on IRQ* (level, active low); *sysAwakeP false = only memory accesses allowed. */
typedef void (*HsCardIntHandlerPtr)(UInt32 cardParam, Boolean *sysAwakeP);

#define hsCardAttrEvtHandler   11       /* HsCardEvtHandlerPtr */

/* Called from the event loop after HsCardEventPost; any system call is allowed. */
typedef Boolean (*HsCardEvtHandlerPtr)(UInt32 cardParam, UInt16 evtNum, UInt16 evtParam);

#define hsSelCardAttrGet       0x6
#define hsSelCardAttrSet       0x7
#define hsSelCardEventPost     0x8
#define hsSelEvtResetAutoOffTimer 0x14

/* Two levels so table/vector/selector are macro-expanded before _Str. */
#define VM_HS_CALL(table, vector, selector)                                 \
	__attribute__ ((__callseq__ (                                           \
		"move.w #" _Str(selector) ",-(%%sp); "                              \
		"trap #" _Str(table) "; dc.w " _Str(vector))))
#define VM_HS_SEL_TRAP(trapNum, selector) VM_HS_CALL(sysDispatchTrapNum, trapNum, selector)

Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
	VM_HS_SEL_TRAP(sysTrapOEMDispatch, hsSelCardAttrGet);
Err HsCardAttrSet(UInt16 cardNo, UInt16 attr, void *valueP)
	VM_HS_SEL_TRAP(sysTrapOEMDispatch, hsSelCardAttrSet);
/* From an interrupt handler only with *sysAwakeP true. */
Err HsCardEventPost(UInt16 cardNo, UInt16 evtNum, UInt16 evtParam)
	VM_HS_SEL_TRAP(sysTrapOEMDispatch, hsSelCardEventPost);
/* How long to stay awake after an interrupt (-1: the auto-off time), and whether to turn the screen on. */
Err HsEvtResetAutoOffTimer(Int32 stayAwakeTicks, Boolean userOn)
	VM_HS_SEL_TRAP(sysTrapOEMDispatch, hsSelEvtResetAutoOffTimer);

#endif
