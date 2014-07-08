/** \file
 * \ingroup funcs
 *
 * $HopeName: COREfunctions!export:pscalc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function API for PS-Calculator
 */

#ifndef __PSCALC_H__
#define __PSCALC_H__

#include "objecth.h"            /* OBJECT */

/**
 * List of PS-Calculator error values.
 */
enum {
  PSCALC_noerr,
  PSCALC_stackunderflow,
  PSCALC_stackoverflow,
  PSCALC_typecheck,
  PSCALC_rangecheck,
  PSCALC_undefinedresult
};

struct PSCALC_OBJ;

struct PSCALC_OBJ *pscalc_create(OBJECT *proc);
void pscalc_destroy(struct PSCALC_OBJ *func);
int32 pscalc_exec(struct PSCALC_OBJ *func, int32 n_in, int32 n_out,
                  USERVALUE *in, USERVALUE *out);

#endif /* __PSCALC_H__ */

/* Log stripped */
