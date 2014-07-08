/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swoften.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains definitions necessary to incorporate SwOften macros in
 * OEM code.
 *
 * SwOften must be called frequently in routines which are likely to take some
 * time to complete: for example, if a large write is being done, it should be
 * broken down into smaller chunks, with a call to SwOften each time round the
 * loop.
 *
 * There are two versions of which only one will be compiled: for systems with
 * and without a real time clock interrupt of some sort.
 *
 * Note that SwOften may be called recursively, and therefore the device
 * tickle_functions which it activates may also be called recursively. Tickle
 * functions are responsible for resetting their own timer. A function that
 * does not do so will be called on every clock tick thereafter.
 *
 * Note also that if SwOften returns a negative number, the caller must tidy up
 * and return immediately since an interrupt has been requested.
 *
 * PC users note: when using appropriate tools, particularly in respect of
 * DOS extenders, it will be necessary for SwTimer to be a FAR pointer, since
 * the decrement will need to be done in an interrupt for which access via
 * a FAR pointer is required.
 *
 * NOTE: this file is now set up by default to use prototyped functions. If you
 * require not to use prototypes you should arrange for the macro
 * SW_NO_PROTOTYPES to be defined when compiling.
 */

#ifndef __SWOFTEN_H__
#define __SWOFTEN_H__

#include "ripcall.h" /* RIPCALL default definition. */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FAR
#define FAR
#define NULL_FAR_DEFINITION
#endif

/**
 * To import a variable from the corerip dll product into its skin, the
 * PC requires that this variable is marked with a declspec. Imported functions don't require this
 * because the information is given on the command line of the linker. Variable imports cause
 * the compiler to generate different code, and hence this blech is needed here.
 */
#ifdef _MSC_VER
#ifdef IMPORT_CORERIP_DLL
#define IMPORT_SPEC __declspec(dllimport)
#endif
#endif

#ifndef IMPORT_SPEC
#define IMPORT_SPEC
#endif

/**
 * \brief Defines a callback that the skin can implement to signal expiry
 * of the \c SwTimer.
 *
 * This callback can be used if the host platform does not provide any
 * means for the skin to decrement \c SwTimer independently. See
 * \c SetSwTimer() for more information.
 *
 * \return \c TRUE if the timer has expired, otherwise \c FALSE.
 */
typedef int32 (RIPCALL *SkinTimerExpiredFn)();

/**
 * \brief Mask out the declaration of variables in contexts when using the
 * Microsoft Linker DELAYLOAD feature for the core RIP DLL on the PC.
 *
 * This is because imports of variables are not supported by this feature,
 * and we want to avoid any possibility of an unintended reference to a symbol
 * that is not fixed up. When using DELAYLOAD, SwOften() is also redefined as a
 * function exported from the core library, again avoiding the variable
 * references resulting from macro-expansion. Use
 * \c SetSwTimer() to initialize the timer, and/or \c SetSkinTimerExpiredFn()
 * to initialize the expiry function.
 */
#ifndef MS_LINKER_DELAYLOAD
extern IMPORT_SPEC int32 FAR * SwTimer;
extern IMPORT_SPEC SkinTimerExpiredFn pfSkinTimerExpired;
#endif

#define SwTimerExpired() (FALSE)

/* N.B.: Never use this function, unless you understand the restrictions
   on asynchronous PostScript! */
extern int32 RIPCALL SwOftenActivateSafe RIPPROTO((void));
typedef int32 (RIPCALL *SwOftenActivateSafe_fp_t) RIPPROTO((void));

extern int32 RIPCALL SwOftenActivateUnsafe RIPPROTO((void));
typedef int32 (RIPCALL *SwOftenActivateUnsafe_fp_t) RIPPROTO((void));

/**
 * Identical to SwOften(), but a pure function rather than a macro, exported
 * from the core library.
 */
extern int32 RIPCALL SwOftenCore RIPPROTO((void));
typedef int32 (RIPCALL *SwOftenCore_fp_t) RIPPROTO((void));

/**
 * Assigns the given integer pointer to SwTimer. Normally, you would just
 * assign the pointer directly to SwTimer. This function is mainly intended
 * to assist when using the Microsoft Linker DELAYLOAD option on the PC, since
 * SwTimer itself is made unavailable in this case.
 */
extern void RIPCALL SetSwTimer RIPPROTO(( int32 FAR * ));
typedef void (RIPCALL *SetSwTimer_fp_t) RIPPROTO(( int32 FAR * ));

/**
 * \brief Sets the given function as the timer expiry callback.
 *
 * The RIP skin should always call \c SetSwTimer() (or assign
 * to the \c SwTimer variable), but calling \c SetSkinTimerExpiredFn()
 * is optional.
 *
 * The skin can implement a timer expiry callback in situations where
 * it cannot decrement the \c SwTimer variable directly. For example,
 * on platforms that do not support multi-threading, it is not possible
 * to spawn a separate thread to decrement the timer. A hardware interrupt
 * mechanism may exist as an alternative, but, if it does not, the RIP
 * will poll this callback at regular intervals. The callback takes no
 * arguments. It should return \c TRUE when the timer has expired.
 * The skin can, for example, check a system clock from within its
 * implementation of the callback.
 *
 * If the skin is able to use a threaded or interrupt-driven mechanism
 * to manage the timer, it should not call this function. Registering
 * an expiry callback in this situation is harmless, but unnecessary,
 * and may incur a performance penalty.
 */
extern void RIPCALL SetSkinTimerExpiredFn RIPPROTO(( SkinTimerExpiredFn ));
typedef void (RIPCALL *SetSkinTimerExpiredFn_fp_t) RIPPROTO(( SkinTimerExpiredFn ));

#ifdef MS_LINKER_DELAYLOAD
#define SwOften() SwOftenCore()
#else
/**
 * SwOften is calling the unsafe version, as we are not supposed to
 * process async requests such as async interrupt when a ptask is
 * running. See task 30320.
 */
#define SwOften() SkinOftenUnsafe()
#endif

/* N.B.: Never use this macro, unless you understand the restrictions on
   asynchronous PostScript! */
#define SkinOftenSafe() (SwOftenActivateSafe())

#define SkinOftenUnsafe() (SwOftenActivateUnsafe())

#ifdef NULL_FAR_DEFINITION
#undef FAR
#undef NULL_FAR_DEFINITION
#endif

/**
 * SwTimer is a pointer to an integer; earlier versions had SwTickle, an
 * integer referred to directly. For upward compatibility therefore, SwTickle
 * is a macro which redefines SwTickle in terms of SwTimer.
 */

#define SwTickle (* SwTimer)

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
