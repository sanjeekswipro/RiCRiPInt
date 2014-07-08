/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:often.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Software implementation of interrupt/multi-processing functionality
 * for PS interpreter.
 */

#ifndef __OFTEN_H__
#define __OFTEN_H__


/* This is the internal part of swoften.h - see comments in that file for
   more detail from an OEM viewpoint.

   Here is the explanation of SwOftenSafe vs SwOftenUnsafe:

   "Safe" means "safe to call some asynchronous PostScript"...
   tickles may ask for some PostScript to be run, and it is not
   always appropriate to do this.

   Whether you call SwOftenSafe or SwOftenUnsafe, the C part of the
   asynchronous action still gets called, and the execution of the
   routine incorporating the SwOften[Un]safe still carries on just the
   same. */

#include "swoften.h"
#include "asyncps.h"
#include "progupdt.h"


int32 SwOftenActivateSafeInternal(void);


/**
 * \brief Macro called from within the core at regular intervals, but only
 * when the interpreter is in a safe state to execute an asynchronous
 * PostScript action.
 *
 * \c SwOftenUnsafe() is called much more frequently, but does not
 * permit any assumptions about the state of the interpreter.
 *
 * If there is a async_action on the waiting list, we will process it
 * immediately no matter what SwTimer says. This is because now that
 * we have redefine SwOften() to be the unsafe version outside the
 * RIP, the chance of hitting a safe version with negative SwTimer is
 * rare.  It causes the async action to be delayed to a (sometimes
 * very) late stage--undesirable especially in the case of an async
 * interrupt. (See Task 30320).
 *
 * In addition to checking the timer and the \c async_action_pending flag,
 * this macro will also poll the optional \c pfSkinTimerExpired function.
 * This allows for the skin to trigger a safe activation in cases where
 * it cannot independently decrement the timer. (Typically, the skin
 * would use a thread-driven or interrupt-driven mechanism to decrement
 * the \c SwTimer variable, but some platforms - such as embedded platforms -
 * may not provide these facilities. The callback mechanism serves as
 * an alternative.
 */
#define SwOftenSafe() \
  MACRO_START \
  if ( async_ps_pending() || \
       do_progress_updates ) { \
    ( void )SwOftenActivateSafeInternal() ; \
  } \
  MACRO_END

/**
 * \brief Macro called from within the core at regular and highly frequent
 * intervals, allowing for system-level tasks to be performed, but
 * not for asynchronous PostScript actions. NOW OBSOLETE!
 */
#define SwOftenUnsafe() MACRO_START MACRO_END

#endif /* protection for multiple inclusion */

/* Log stripped */
