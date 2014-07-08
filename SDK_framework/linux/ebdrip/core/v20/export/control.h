/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:control.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rip control API
 */

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include "objects.h"    /* OBJECT */
#include "lowmem.h" /* memory_requirement_t */


/**
 * \defgroup ps PostScript interpretation.
 * \ingroup core
 * \{ */

/* flag set for handleLowMemory */
#define TRY_RIPTODISK       0x0008

#define TRY_MOST_METHODS      ( TRY_RIPTODISK )
#define TRY_NORMAL_METHODS    ( TRY_MOST_METHODS )
#define TRY_WITHOUT_RIPTODISK ( 0x0000 )


/* Flag to indicate whether the RIP is working. */
extern Bool rip_work_in_progress ;

/* Recursion level of the PS interpreter. */
extern int32 ps_interpreter_level ;

/* Is the RIP exiting in such a way that SwStart() will return. */
extern Bool exiting_rip_cleanly ;


void setup_error_name(uint8 *errname) ;

Bool interpreter(int32 n_objects, Bool *trap_exit) ;

/* Call to interpreter that cleans up the operand and dict stacks on failure
   (returns them to the level which they were before the call). This should
   only be used for internal calls to the interpreter within operators. The
   first arg is executed, all other args until a final NULL go on the operand
   stack (and are cleaned off in case of failure). */
Bool interpreter_clean(OBJECT *execme, ...) ;


/** Indicates the interpreter loop should take some action between
    operators (not incl. low-memory actions). */
extern Bool dosomeaction;


/** \brief Perform common actions between operators in interpreter loops.

  \return Success indication.

  Typical actions include checking for interrupts and timeouts, and
  doing low-memory handling to recover the reserves. N.B. This may
  include a partial paint.
 */
Bool handleNormalAction(void);

int32 handleLowMemory(int32 actionNumber, int32 tryflags, Bool *ripped_to_disk) ;

/** \brief Try to release enough free memory for the given requirements,
    using all low-memory handlers.

  \param[out] retry  Indicates if enough memory was freed.
  \param[in,out] tried_pp  Indicates if partial paint was tried; if already
                          TRUE on entry, partial paint will not be attempted.
  \param[in] context  The core context.
  \param[in] count  Number of requests.
  \param[in] requests  An array of requests.
  \retval FALSE Returned if an error was raised.
  \retval TRUE Returned if there were no errors (whether or not any memory was freed).

  This is rather like \c low_mem_handle(), only it enables the use of
  between-operators handlers, GC, and partial paint (if allowed by \a tried_pp).
 */
Bool low_mem_handle_between_ops(Bool *retry, Bool *tried_pp,
                                corecontext_t *context,
                                size_t count, memory_requirement_t* requests);


/** \brief Try to regain the reserves, using all low-memory handlers.

  \param[out] got_reserves  Indicates if all reserves were regained.
  \param[in] context  The core context.
  \retval FALSE Returned if an error was raised.
  \retval TRUE Returned if there were no errors.

  This is rather like \c mm_regain_reserves(), only it enables the use of
  between-operators handlers, GC, and partial paint.
 */
Bool low_mem_regain_reserves_with_pp(Bool *got_reserves,
                                     corecontext_t *context);


void setDynamicGlobalDefaults(void) ;
Bool setDistillEnable(Bool enable) ;

extern OBJECT *topDictStackObj;
extern Bool   execStackSizeNotChanged;

extern OBJECT errobject;

extern Bool   allow_interrupt;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
