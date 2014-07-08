/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:forms.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Forms API
 */

#ifndef __FORMS_H__
#define __FORMS_H__

#include "stacks.h"

Bool gs_execform( corecontext_t *context, STACK *stack ) ;

Bool in_execform(void);

void preserve_execform(
  DL_STATE* page);

#endif /* protection for multiple inclusion */

/* Log stripped */
