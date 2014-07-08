/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:imskgen4.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * MaskedImage Type 4 API
 */

#ifndef __IMSKGEN4_H__
#define __IMSKGEN4_H__

#include "objecth.h"

typedef struct maskgendata MASKGENDATA ;

MASKGENDATA *im_genmaskopen(int32 ncomps, int32 nconv, OBJECT *maskcolor) ;
void im_genmaskfree(MASKGENDATA *mg) ;
void im_genmask(MASKGENDATA *mg, int32 *iubuf, uint8 *mbuf) ;
void im_genmask_data(MASKGENDATA *mg, int32 *nvalues, int32 **colorinfo) ;

#endif  /* !__IMSKGEN4_H__ */

/* Log stripped */
