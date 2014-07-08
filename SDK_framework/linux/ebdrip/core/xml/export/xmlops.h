/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xmlops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xmlexec initialise/terminate interface.
 */

#ifndef __XMLOPS_H__
#define __XMLOPS_H__ (1)

struct core_init_fns ; /* from SWcore */

typedef struct XMLPARAMS {
  int32     spare;
} XMLPARAMS;

void xml_C_globals(struct core_init_fns *fns) ;

/* ============================================================================
* Log stripped */
#endif
