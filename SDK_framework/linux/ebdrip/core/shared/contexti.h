/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:contexti.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Context initialisation.
 */

#ifndef __CONTEXTI_H__
#define __CONTEXTI_H__

struct SWSTART ; /* from COREinterface */

Bool context_swinit(struct SWSTART *params) ;

Bool context_swstart(struct SWSTART *params) ;

void context_finish(void) ;

/* Log stripped */
#endif /* Protection from multiple inclusion */
