/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:nopdf.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for PDF base functions when compiled out.
 */

#include "core.h"
#include "swpdf.h"
#include "objecth.h"
#include "swstart.h"

Bool pdf_register_execution_context_base(PDFXCONTEXT **base)
{
  UNUSED_PARAM(PDFXCONTEXT **, base) ;
  return TRUE ;
}

Bool pdf_purge_execution_contexts(int32 savelevel)
{
  UNUSED_PARAM(int32, savelevel) ;
  return TRUE ;
}

Bool pdf_purge_caches(void)
{
  return(FALSE);
}

OBJECT *streamLookupDict(OBJECT *theo)
{
  UNUSED_PARAM(OBJECT *, theo) ;
  return NULL ;
}

int32 pdf_find_execution_context(int32 id, PDFXCONTEXT *base,
                                 PDFXCONTEXT **pdfxc)
{
  UNUSED_PARAM(int32, id) ;
  UNUSED_PARAM(PDFXCONTEXT *, base) ;
  UNUSED_PARAM(PDFXCONTEXT **, pdfxc) ;
  return FALSE ;
}

void pdf_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

/* Log stripped */
