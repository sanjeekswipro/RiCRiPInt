/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!export:interrupts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interrupts API.
 */

#ifndef __INTERRUPTS_H__
#define __INTERRUPTS_H__ (1)

struct core_init_fns ;

/* Clear user interrupt. */
void clear_interrupts(void);

/* Clear timeout interrupt. */
void clear_timeout(void);

/* Check if no interrupts pending.
 * allow_interrupt masks user interrupts only.
 */
Bool interrupts_clear(Bool allow_interrupt);

/* Check if user interrupt has been raised. */
Bool user_interrupt(void);

/* Raise a user interrupt. */
void raise_interrupt(void);

/* Raise a timeout interrupt. */
void raise_timeout(void);

/* Report pending interrupt.
 * allow_interrupt masks user interrupts only.
 */
Bool report_interrupt(Bool allow_interrupt);

void interrupts_C_globals(struct core_init_fns *fns);

#endif /* !__INTERRUPTS_H__ */

/* Log stripped */
