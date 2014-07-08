/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:noxml.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for Core XML functions when compiled out.
 */

#include "core.h"
#include "coreinit.h"
#include "mmcompat.h"
#include "xmlops.h"
#include "hqmemset.h"
#include "swstart.h"

static Bool xml_swstart(SWSTART *params)
{
  XMLPARAMS *xmlparams ;

  UNUSED_PARAM(SWSTART*, params) ;

  /* Initialise empty XML params, it is used in the save structure */
  CoreContext.xmlparams = xmlparams = mm_alloc_static(sizeof(XMLPARAMS)) ;
  if ( xmlparams == NULL )
    return FALSE ;

  HqMemZero(xmlparams, sizeof(XMLPARAMS)) ;

  return TRUE ;
}

static void xml_finish(void)
{
  CoreContext.xmlparams = NULL ;
}

void xml_C_globals(core_init_fns *fns)
{
  fns->swstart = xml_swstart ;
  fns->finish = xml_finish ;
}

/* Log stripped */
