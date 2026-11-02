/*
 * The few Handspring Springboard card calls VisorMesh uses, declared from the
 * Handspring SDK's HsExt.h (not vendored: it's under Handspring's EULA).
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

#define hsSelCardAttrGet       0x6

/* Two levels so table/vector/selector are macro-expanded before _Str. */
#define VM_HS_CALL(table, vector, selector)                                 \
	__attribute__ ((__callseq__ (                                           \
		"move.w #" _Str(selector) ",-(%%sp); "                              \
		"trap #" _Str(table) "; dc.w " _Str(vector))))
#define VM_HS_SEL_TRAP(trapNum, selector) VM_HS_CALL(sysDispatchTrapNum, trapNum, selector)

Err HsCardAttrGet(UInt16 cardNo, UInt16 attr, void *valueP)
	VM_HS_SEL_TRAP(sysTrapOEMDispatch, hsSelCardAttrGet);

#endif
