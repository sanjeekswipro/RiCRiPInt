/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!src:fpexception.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * FP exception control.
 */

#include "core.h"

#include "coreinit.h"
#include "fpexception.h"


#if defined(linux) && defined(__GNUC__)
#if !defined(LIBuClibc)
/* gcc extends C99 float environment so exceptions can be enabled if __USE_GNU
 * is defined. (C99 requires a no-exception environment and does not provide
 * a means of enabling exceptions.) */
#define __USE_GNU
#include <fenv.h>

/* Turn on fp exceptions to catch invalid fp behaviour.
 */
void enable_fp_exceptions(void)
{
  /* Purge any pending exceptions and turn on fp exceptions except inexact */
  feclearexcept(FE_ALL_EXCEPT);
  feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);

} /* enable_fp_exceptions */

/*#elif defined(linux) && defined(LIBuClibc)*/
#else           /* LIBuClibc */

void enable_fp_exceptions(void)
{
   /* not supported for arm/uclibc */
   /* uclibc support this only for i386 */
}
#endif  /* LIBuClibc */

#elif defined(_MSC_VER)

#include <float.h>

/* Turn on fp exceptions to catch invalid fp behaviour.
 */
void enable_fp_exceptions(void)
{
  uint32 control_word;

  /* Purge any pending exceptions and turn on fp exceptions except inexact */
  _clearfp();
  _controlfp_s(&control_word, _EM_INEXACT, _MCW_EM);
  HQASSERT((control_word&EM_AMBIGUOUS) == 0, "Ambiguous fp control words.");

} /* enable_fp_exceptions */

#elif defined(MINGW_GCC)

/* MinGW provides acces to the Windows API for accessing the FP exeception
 * handling. Use these to trap FP errors on MinGW platform.
 * Use of the -ansi flag in the compilation of this source code
 * will exclude the declarations of _controlfp etc from <float.h>.
 * so they are duplicated below. */

unsigned int _controlfp(unsigned int, unsigned int);
unsigned int _clearfp(void);
#define _EM_INEXACT     0x00000001
#define _MCW_EM         0x0008001F
void enable_fp_exceptions(void)
{
  /* Purge any pending exceptions and turn on fp exceptions except inexact */
  _clearfp();
  _controlfp(_EM_INEXACT, _MCW_EM);

} /* enable_fp_exceptions */

#elif defined(MACOSX)

void enable_fp_exceptions(void)
{

} /* enable_fp_exceptions */

#elif defined(Solaris)

void enable_fp_exceptions(void)
{

} /* enable_fp_exceptions */

#elif defined(VXWORKS)

void enable_fp_exceptions(void)
{

} /* enable_fp_exceptions */

#elif defined(THREADX)

void enable_fp_exceptions(void)
{

} /* enable_fp_exceptions */

#elif defined(__NetBSD__)

void enable_fp_exceptions(void)
{

} /* enable_fp_exceptions */

#else /* All other platforms */

void enable_fp_exceptions(void)
{
  /* This message should only appear on new platforms/compilers.  When doing a
   * port add a new version of the function for the platform/compiler to enable
   * fp exceptions, or add a stub if this is not possible. */
  HQTRACE(TRUE, ("Unrecognised environment - fp exceptions not enabled."));

} /* enable_fp_exceptions */

#endif /* All other platforms */

/* Thread specialiser to enable fp exceptions. */
static void fpe_specialise(
  corecontext_t*  context,
  context_specialise_private* data)
{
  UNUSED_PARAM(corecontext_t*, context);

  enable_fp_exceptions();

  context_specialise_next(context, data);
}

static context_specialiser_t fpe_specialiser = {
  fpe_specialise, NULL
};

/* RIP startup function to add a thread specialiser to enable fp exceptions in
 * the thread.  Should be first specialiser so that other specialiser are run
 * with the exceptions exposed.
 */
static
Bool add_fpe(struct SWSTART* params)
{
  UNUSED_PARAM(struct SWSTART*, params);

  context_specialise_add(&fpe_specialiser);

  return (TRUE);
}

static
void init_C_globals_fpe(void)
{
  fpe_specialiser.next = NULL;
}

void fpe_C_globals(
  struct core_init_fns* fns)
{
  init_C_globals_fpe();

  fns->swstart = add_fpe;
}

/* Log stripped */
