/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:imageops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * All PS image operators
 */

#include "core.h"
#include "swerrors.h"

#include "graphics.h"
#include "images.h"
#include "objstack.h"
#include "routedev.h"
#include "stacks.h"
#include "vndetect.h"
#include "surfacet.h"

#include "namedef_.h"

/* ----------------------------------------------------------------------------
   function:            image_()           author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 170.

---------------------------------------------------------------------------- */
Bool image_(ps_context_t *pscontext)
{
  return gs_image(ps_core_context(pscontext), &operandstack);
}

/* ----------------------------------------------------------------------------
   function:            colorimage_()      author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   11-Nov-1989
   arguments:           none .
   description:

   See Adobe Color Operator Definitions Manual, page 13.

---------------------------------------------------------------------------- */
Bool colorimage_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  IMAGEARGS imageargs ;
  Bool result ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if (DEVICE_INVALID_CONTEXT ())
    return error_handler (UNDEFINED);

  init_image_args(&imageargs, GSC_IMAGE) ;
  result = get_image_args(context, &operandstack, &imageargs, NAME_colorimage) ;
  if ( result ) {
    result = DEVICE_IMAGE(context->page, &operandstack, &imageargs) ;
  }
  free_image_args( & imageargs ) ;

  return result ;
}

/* ----------------------------------------------------------------------------
   function:            imagemask_()       author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 171.

---------------------------------------------------------------------------- */
Bool imagemask_(ps_context_t *pscontext)
{
  return gs_imagemask(ps_core_context(pscontext), &operandstack);
}


/* Log stripped */
