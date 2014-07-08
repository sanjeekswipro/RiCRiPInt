/** \file
 * \ingroup psloop
 *
 * $HopeName: SWv20!export:execops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS execution operators
 */

#ifndef __EXECOPS_H__
#define __EXECOPS_H__

/** \defgroup psloop PostScript interpreter loop
    \ingroup ps */
/** \{ */

#include "matrix.h" /* OMATRIX */
#include "paths.h"  /* PATHLIST */

struct STACK ; /* from COREobjects */

typedef struct {
  OMATRIX pathctm ;
  PATHLIST *apath ;
} PATHFORALL ;

/* Memory allocation for pathforall structures. */
#define get_forallpath()  ((PATHFORALL *) mm_alloc(mm_pool_temp, sizeof(PATHFORALL),    \
                                                   MM_ALLOC_CLASS_PATHFORALL))
#define free_forallpath(ptr) (mm_free(mm_pool_temp,(mm_addr_t)(ptr), sizeof(PATHFORALL)))

Bool setup_pending_exec(OBJECT *theo, Bool exec_immediately);
Bool xpathforall(PATHFORALL *thepath);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
