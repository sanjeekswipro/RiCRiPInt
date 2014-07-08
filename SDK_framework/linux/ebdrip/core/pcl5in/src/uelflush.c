/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:uelflush.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#include "core.h"

#include "stacks.h"
#include "namedef_.h"
#include "uelflush.h"

#define UEL_STRING  "\033%-12345X"

/* Flush file stream up to EOF or the next UEL */
Bool uelflush(OBJECT *ofile)
{
  ps_context_t *pscontext = get_core_context_interp()->pscontext ;
  OBJECT oint = OBJECT_NOTVM_INTEGER(0);
  OBJECT uelstring = OBJECT_NOTVM_STRING(UEL_STRING);
  OBJECT subfiledecode = OBJECT_NOTVM_NAME(NAME_SubFileDecode, LITERAL);

  /* Set up an SFD looking for the UEL and then flush it */
  if ( !push4(ofile, &oint, &uelstring, &subfiledecode, &operandstack) ) {
    return(FALSE);
  }
  if ( !filter_(pscontext) ) {
    npop(3, &operandstack);
    return(FALSE);
  }
  if ( !flushfile_(pscontext) ) {
    npop(1, &operandstack);
    return(FALSE);
  }

  return(TRUE);

} /* uelflush */

/*
* Log stripped */
/* EOF uelflush.c */
