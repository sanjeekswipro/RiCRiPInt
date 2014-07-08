/** \file
 * \ingroup errors
 *
 * $HopeName: COREerrors!export:swerrors.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1987-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of Core (RIP) errors and error handler functions; declarations
 * are found in implementation compounds. Definitions are in a separate
 * compound so that the SWv20 implementation does not need to be used in a
 * non-PostScript based product.
 *
 * The errors defined are very PostScript-centric.
 */

#ifndef __SWERRORS_H__
#define __SWERRORS_H__

#include <stdarg.h> /* va_list */

#ifdef __CC_ARM
#define NULL ((void*)0)
#endif

/**
 * \defgroup errors Error handling.
 * \ingroup core
 * \{
 */

/** \brief Enumeration error codes.
 *
 * The ordering of these error codes is important. They should be in the same
 * order as the error names in the names table.
 */
enum {
  NOT_AN_ERROR       = -1, /**< Use this to re-throw previous errors. */
  /* SUCCESS = 0, a.k.a. FALSE: DO NOT USE 0 AS AN ERROR. */
  DICTFULL           =  1,
  DICTSTACKOVERFLOW,
  DICTSTACKUNDERFLOW,
  EXECSTACKOVERFLOW,
  INTERRUPT,
  INVALIDACCESS,
  INVALIDEXIT,
  INVALIDFILEACCESS,
  INVALIDFONT,
  INVALIDRESTORE,
  IOERROR,
  LIMITCHECK,
  NOCURRENTPOINT,
  RANGECHECK,
  STACKOVERFLOW,
  STACKUNDERFLOW,
  SYNTAXERROR,
  TIMEOUT,
  TYPECHECK,
  UNDEFINED,
  UNDEFINEDFILENAME,
  UNDEFINEDRESULT,
  UNMATCHEDMARK,
  UNREGISTERED,
  VMERROR,
  DISKVMERROR,
  CONFIGURATIONERROR,
  UNDEFINEDRESOURCE
} ;

/* The error handlers below are provided by SWv20 in the Core RIP SWcore
   implementation. The error handler routine is expected to set newerror to
   one of the errors enumerated above, and to return FALSE. When the product
   has noticed and taken care of the error, it is expected to call
   error_clear(). */

/** The error context structure is used to capture error information to pass
    from error_handler() up the call chain to handleerror(). The core context
    reference to the error structure can be temporarily replaced in order
    to divert errors. */
typedef struct error_context_t {
  int32 new_error ;
  int32 old_error ;
  int32 orig_error ;
  Bool got_detail ;
  Bool interrupting ;
} error_context_t ;

/** Initialiser for error context variables. */
#define ERROR_CONTEXT_INIT { 0, 0, 0, FALSE, FALSE }

/** This pseudo-variable is set to one of the error codes when an error
    is signalled. Routines can reset it to FALSE in order to check for new
    errors being signalled, however the original error will still remain in
    effect. To clear the error indication completely, call \c error_clear(). */
int32 newerror_context(error_context_t *errcontext);
#define newerror newerror_context(get_core_context_interp()->error)

#if defined( ASSERT_BUILD )
/** \brief Catch a failure.
 *
 * This function is called by the FAILURE macro to aid debugging.
 */
void failure_handler(void) ;

/** \brief Return a value indicating a serious failure
 *
 * This macro should be used to return a value indicating that some serious
 * problem has been detected, where that return value WILL RESULT in
 * error_handler being called by a caller. In this way, the debugger can
 * break at the point of detection with the context still available rather
 * than much later when error_handler is called.
 *
 * For example: \code if (!ok) return FAILURE(FALSE) ; \endcode
 *          or: \code result = FAILURE(NULL) ; \endcode
 *
 * However, there is no need to use the macro if the failure is detected from
 * a function call which itself will have called error_handler or FAILURE.
 */
#define FAILURE(_val) (failure_handler(), (_val))

/** \brief A serious failure means a jump to a "cleanup" label
 *
 * This macro should be used to jump to an exception "catch" style
 * cleanup section of code, where after tidying up an error will be
 * propagated to the caller. Like \c FAILURE, its usefulness is in
 * breaking into the debugger just at the point that the error has
 * first arisen, will all relevant state intact.
 */
#define FAILURE_GOTO(_label) MACRO_START \
  failure_handler(); goto _label; \
MACRO_END

#else
#define FAILURE(_val) (_val)
#define FAILURE_GOTO(_label) goto _label
#endif

/** \brief Signal an error.
 *
 * \param[in] errorno Error number; one of the enumeration error codes.
 * \return    The return value is always FALSE.
 */
Bool error_handler(int32 errorno) ;

/** \brief Predicate for error signalled.
 *
 * \return This function returns TRUE if an error has been signalled, but not
 *         handled, FALSE if no error has been signalled.
 */
Bool error_signalled_context(error_context_t *errcontext) ;
#define error_signalled() error_signalled_context(CoreContext.error)

/** \brief Save the signalled error.
 *
 * This function should be paired with \c error_restore_context(). Together,
 * they delineate blocks of code in which \c error_clear() or \c interpreter()
 * can be called without affecting the currently signalled error. They should be
 * used when these functions are called during cleanup of a previously-thrown
 * error. This also clears the current state, allowing that error handling or
 * interpretation to start on a clear slate.
 */
void error_save_context(error_context_t *errcontext, int32 *savederror);

/** \brief Restore the signalled error.
 *
 * This function should be paired with \c error_save(). Together, they
 * delineate blocks of code in which \c error_clear() or \c interpreter() can
 * be called without affecting the currently signalled error. They should be
 * used when these functions are called during cleanup of a previously-thrown
 * error.
 */
void error_restore_context(error_context_t *errcontext, int32 savederror);

/** \brief Clear error indications.
 *
 * This function should be called if an error is signalled, but the caller
 * decides it can ignore it. The caller should return TRUE up the call stack
 * in this case.
 */
void error_clear_context(error_context_t *errcontext) ;
#define error_clear() error_clear_context(get_core_context_interp()->error)

/** \brief Clear error indication locally.
 *
 * This function is used when the caller needs to check for errors
 * signalled locally (newerror), but earlier errors are not overridden.
 */
void error_clear_newerror_context(error_context_t *errcontext);
#define error_clear_newerror() \
  error_clear_newerror_context(get_core_context_interp()->error)


/** \brief Signal an error, adding detail information.
 *
 * Error handler which puts more information about the error in /errorinfo,
 * which is a key in $error, so errors can be more informative.
 *
 * \param[in] errorno Error number; one of the enumeration error codes.
 * \param[in] detail  A string with detailed information on the error.
 * \return    The return value is always FALSE.
 *
 * \note The detail strings passed to this function are USER VISIBLE.
 * This means they should start with a capital letter and end with a full stop,
 * and avoid information meant only for developers such as function names.
 */
Bool detail_error_handler(int32 errorno, const char *detail) ;

/** \brief Signal an error, adding detail information using a swcopyf-format
 * string and arguments.
 *
 * \param[in] errorno Error number; one of the enumeration error codes.
 * \param[in] format  A swcopyf-style format string with detailed error
 *                    information.
 * \param[in] ...     Additional parameters as required by the format string.
 * \return    The return value is always FALSE.
 */
Bool detailf_error_handler(int32 errorno, const char *format, ...) ;

/** \brief Signal an error, adding detail information using a varargs list.
 *
 * \param[in] errorno Error number; one of the enumeration error codes.
 * \param[in] format  A vswcopyf-style format string with detailed error
 *                    information.
 * \param[in] vlist   A variable arguments list containing parameters as
 *                    required by the format string.
 * \return            The return value is always FALSE.
 */
Bool vdetailf_error_handler(int32 errorno, const char *format, va_list vlist) ;

/** \brief Return the latest error signalled, even if already handled.
 */
int32 error_latest_context(error_context_t *errcontext);
#define error_latest() error_latest_context(CoreContext.error)

/** \} */

/*
Log stripped */

#endif /* protection for multiple inclusion */
