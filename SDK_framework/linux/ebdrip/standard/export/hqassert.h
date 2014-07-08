/** \file
 * \ingroup assertions
 *
 * $HopeName: HQNc-standard!export:hqassert.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Harlequin standard "assert" and "trace" macros.
 */

#ifndef __HQASSERT_H__ /* { */
#define __HQASSERT_H__

/**
 * \defgroup assertions Harlequin standard "assert" and "trace" macros.
 * \ingroup cstandard
 * \{
 */

#ifdef __cplusplus /* { */
extern "C" {
#endif /* } */

#ifndef ASSERT_BUILD /* { */

#define HQASSERT(fCondition, pszMessage) EMPTY_STATEMENT()
#define HQASSERTV(fCondition, printf_style_args) EMPTY_STATEMENT()
#define HQFAIL(pszMessage) EMPTY_STATEMENT()
#define HQFAILV(printf_style_args) EMPTY_STATEMENT()
#define HQASSERT_EXPR(fCondition, pszMessage, value)  ( value )
#define HQASSERTV_EXPR(fCondition, printf_style_args, value)  ( value )
#define HQTRACE_ALWAYS(printf_style_args) EMPTY_STATEMENT()
#define HQTRACE(fCondition, printf_style_args) EMPTY_STATEMENT()
#define HQASSERT_DEPTH 0
#define HQASSERT_FPEXCEP(pszMessage) EMPTY_STATEMENT()
#define HQASSERT_PTR(pPtr) EMPTY_STATEMENT()
#define HQASSERT_WPTR(pPtr) EMPTY_STATEMENT()
#define HQASSERT_LPTR(pPtr) EMPTY_STATEMENT()
#define HQASSERT_DPTR(pPtr) EMPTY_STATEMENT()

#else /* } { ASSERT_BUILD */

/* Platform independent Assert and Trace functions.

   HqAssertPhonyExit is Splint annotated as if it never returns,
   i.e. the assumption is made that the user of a debug executable
   always chooses 'Abort' from the alert. Actually it does nothing, so
   that if the user chooses another option, they can ignore the
   assertion failure as before. It's done this way rather than just
   annotating HqAssert itself as non-returning because we want to be
   able to use HQFAIL without getting unreachable code warnings for
   everything after it.

   NOTE that the fact you don't get Splint warnings for the subject of
   the assertion does not mean an assert will do in the place of a
   test!  Only use an assertion where the interface to the function
   containing it defines that the condition you are asserting cannot
   happen. If there is a genuine chance, for instance, that a pointer
   will be null, then use a test rather than an assertion, and tidy up
   gracefully rather than crashing. You can add an HQFAIL in the
   tidy-up code too if you like. People often hide behind the
   efficiency argument when they add assertions (or nothing!)  when
   they should be adding defensive tests. I say show me a profile that
   proves that a few tests make it go significantly slower, and then
   you can think about _hoisting_ the test higher into the call
   stack. Even then, it should be softened to an assertion rather than
   removed altogether. Bottom line: the people paying for our software
   don't want a product that crashes quickly, their primary concern is
   for a product that works. Here endeth the religious diatribe for
   today.
*/

#include "hqncall.h"

/** This function always returns FALSE. It is used to prevent warnings about
    always true or false conditions in asserts. */
extern int HQNCALL HqAssertFalse(void) ;

extern void HQNCALL HqAssert(const char *pszFormat, ...) ;

extern void HQNCALL /*@noreturn@*/ HqAssertPhonyExit( void ) ;

extern void HQNCALL HqTraceSetFileAndLine(/*@observer@*/ const char *pszFilename,
                                          int nLine) ;

extern void HQNCALL HqTrace(const char *pszMessage, ...);

/* If HQASSERT_LOCAL_FILE is defined, then the HQASSERT_FILE() macro should
 * be "called" to define a static local variable containing the name of the
 * current file. This allows space used for static strings to be minimised
 * for compilers that don't support string pooling.
 */
#ifdef HQASSERT_LOCAL_FILE /* { */

#define HQASSERT_FILENAME hqassert_you_havent_called_HQASSERT_FILE
#define HQASSERT_FILE() static char HQASSERT_FILENAME[] = __FILE__

#else  /* } { !HQASSERT_LOCAL_FILE */

#define HQASSERT_FILENAME __FILE__

#endif /* } !HQASSERT_LOCAL_FILE */


/* HQFAIL, HQASSERT
 * ================
 *
 * HQFAIL calls the HqAssert function
 *
 * Typically this will interrupt the execution of the program and
 * display a message to the user.
 *
 * HQASSERT evaluates its first argument and calls HQFAIL if it is FALSE.
 *
 */

#define HQFAILV(printf_style_args)  \
  (HqTraceSetFileAndLine(HQASSERT_FILENAME, __LINE__), \
   HqAssert printf_style_args)

#define HQFAIL(pszMessage)  \
  HQFAILV(("%s", (pszMessage)))

#define HQASSERTV(fCondition, printf_style_args) MACRO_START            \
  if ( !(fCondition) || HqAssertFalse() ) {                             \
    HQFAILV(printf_style_args) ;                                        \
    HqAssertPhonyExit() ;                                               \
  }                                                                     \
MACRO_END

#define HQASSERT(fCondition, pszMessage) \
  HQASSERTV((fCondition), ("%s", (pszMessage)))


/* HQASSERT_EXPR
 * =============
 *
 * An assert mechanism that can be used in an expression context
 *
 * The first test gives a run-time warning if fCondition is FALSE.
 * The second line may give a compile time error or warning or nothing depending
 * on the compiler if fCondition is FALSE.
 *
 */

#define HQASSERTV_EXPR(fCondition, printf_style_args, value) \
( \
  (void)( ( fCondition ) ? 0 : ( HQFAILV(printf_style_args), 0 ) ) \
  , (void)( 0 / ( ( fCondition ) ? 1 : 0 ) ) \
  , ( value ) \
)

#define HQASSERT_EXPR(fCondition, pszMessage, value) \
  HQASSERTV_EXPR((fCondition), ("%s", (pszMessage)), (value))

/* HQTRACE
 * =======
 *
 * Evaluates its first argument - if it is TRUE (non-zero), calls the
 * HqTrace function. The second argument should be a printf-style
 * parameter list (including brackets), e.g.
 *
 *    HQTRACE(TRUE, ("Something went wrong - code %d", 42));
 *
 * Typically this will output the message to the user, but will not
 * interrupt the execution of the program (i.e. it should be used for
 * informational messages).
 */

#define HQTRACE_ALWAYS(printf_style_args) MACRO_START                   \
  HqTraceSetFileAndLine(HQASSERT_FILENAME, __LINE__);                   \
  HqTrace printf_style_args;                                            \
MACRO_END


#define HQTRACE(fCondition, printf_style_args) MACRO_START              \
  if ( (fCondition) || HqAssertFalse() ) {                              \
    HQTRACE_ALWAYS(printf_style_args) ;                                 \
  }                                                                     \
MACRO_END


/* HQFAIL_AT_COMPILE_TIME
 * ======================
 *
 * Compile time versions of HQFAIL and HQASSERT. We need these because some
 * compilers get all upset about #error.
 *
 * This is designed to look like a variable declaration of an unknown type.
 * Hopefully this way we can get a sensible error message as well as an
 * error.
 */

#define HQFAIL_AT_COMPILE_TIME() \
    compile_time_assert this_should_give_an_error_message


/* HQASSERT_DEPTH
 * ==============
 *
 * This is the recursion level of HqAssert, and hence is zero if not within
 * an assert.
 */

#define HQASSERT_DEPTH HqAssertDepth()

extern int HQNCALL HqAssertDepth( void );


/* HQASSERT_FPEXCEP
 * ================
 *
 * Macro to prod any pending fp exceptions on Win32 Intel platforms.
 */
#define HQASSERT_FPEXCEP(pszMessage)  EMPTY_STATEMENT()

#ifdef WIN32 /* { */
#ifndef ALPHA /* { */

#undef HQASSERT_FPEXCEP
#define HQASSERT_FPEXCEP(pszMessage) \
MACRO_START \
  double  xyzzy = 0.0; \
  double  plover = xyzzy + 1.0; \
  xyzzy = plover; \
MACRO_END

#endif /* } !ALPHA */
#endif /* } WIN32 */


/* HQASSERT_[L|W]PTR
 * =================
 *
 * Asserts the validity of a pointer - both against NULL and an obviously
 * bad address. Won't catch everything, but it's a start.
 * I KNOW I'm casting a pointer to intptr_t, but all I want is the bottom
 * few bits to test for the appropriate word alignment. WPTR is for word
 * alignment, LPTR for longword and DPTR for double...
 */

#define HQASSERT_PTR( pPtr ) MACRO_START \
  HQASSERT(( pPtr ) != NULL , #pPtr " NULL" ) ; \
MACRO_END

#define HQASSERT_WPTR( pPtr ) MACRO_START \
  HQASSERT((( pPtr ) != NULL ) && ((((intptr_t)( pPtr )) & 0x01  ) == 0 ) , \
              #pPtr " NULL or invalid" ) ; \
MACRO_END

#define HQASSERT_LPTR( pPtr ) MACRO_START \
  HQASSERT((( pPtr ) != NULL ) && ((((intptr_t)( pPtr )) & 0x03 ) == 0 ) , \
              #pPtr " NULL or invalid" ) ; \
MACRO_END

#define HQASSERT_DPTR( pPtr ) MACRO_START \
  HQASSERT((( pPtr ) != NULL ) && ((((intptr_t)( pPtr )) & 0x07 ) == 0 ) , \
              #pPtr " NULL or invalid" ) ; \
MACRO_END


#endif /* } ! ASSERT_BUILD */

#ifndef HQASSERT_FILE /* { */
/* We must have something in this declaration since there will */
/* be a semi-colon after the "call" in the source */
#define HQASSERT_FILE() extern void HQASSERT_FILE_dummy(void)
#endif /* } !HQASSERT_FILE */

#ifdef __cplusplus /* { */
}
#endif /* } */

/** \} */

#endif /* } __HQASSERT_H__ */

