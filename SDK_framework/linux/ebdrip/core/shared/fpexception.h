/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:fpexception.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * FP exception control.
 */

#ifndef __FPEXCEPTION_H__
#define __FPEXCEPTION_H__ (1)

/* Note that floating point exceptions cannot be set when building a Java
 * Native Interface compatible library, since the Java VM requires a very
 * specific FP configuration; weird exceptions will be thrown by the VM if that
 * configuration is disturbed. */
#if defined(DEBUG_BUILD) && !defined(JNI_COMPATIBLE)
#define FPEXCEPTION (1)
#endif

/** \brief Enable fp exceptions in the current thread. */
void enable_fp_exceptions(void);

struct core_init_fns;
/** \brief Initialise fp exceptions global data. */
void fpe_C_globals(
  struct core_init_fns* fns);

/* Log stripped */
#endif /* !__FPEXCEPTION_H__ */
