/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:dev_if.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Device interface API
 */

#ifndef __DEV_IF_H__
#define __DEV_IF_H__ 1

#include "objectt.h"            /* OBJECT */

struct core_init_fns ; /* from SWcore */

void ps_boot_C_globals(struct core_init_fns *fns) ;

Bool device_set_params(OBJECT *thekey,
                       OBJECT *theval ,
                       void *argBlockPtr) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
