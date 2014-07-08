/** \file
 * \ingroup color
 *
 * $HopeName: SWv20!src:colorops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS color operators
 */

#include "core.h"

#include "pscontext.h"    /* ps_context_t */
#include "graphics.h"     /* GSTATE */
#include "gsccalib.h"     /* gsc_setcalibration */
#include "gsccrd.h"       /* gsc_setcolorrendering */
#include "gscdevci.h"     /* gsc_setoverprint */
#include "gschead.h"      /* gsc_setcolorspace */
#include "gschcms.h"      /* gsc_setinterceptcolorspace */
#include "gschtone.h"     /* gsc_setscreens */
#include "gscsmplk.h"     /* gsc_setblackgeneration */
#include "gscxfer.h"      /* gsc_settransfers */
#include "gstack.h"       /* gstateptr */
#include "objstack.h"     /* stack_push_real */
#include "params.h"       /* UserParams */
#include "routedev.h"     /* DEVICE_INVALID_CONTEXT */
#include "stacks.h"       /* operandstack */
#include "swerrors.h"     /* STACKUNDERFLOW */
#include "gsc_icc.h"      /* gsc_geticcbasedinfo */

/* ---------------------------------------------------------------------- */

Bool setcolorspace_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currentcolorspace_(ps_context_t *pscontext)
{
  OBJECT colorspace = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* See red book 2 page 383; note: the array returned must be that given,
   * unless it wasn't an array, in which case the one we put in
   * internaldict is used.
   */
  return gsc_currentcolorspace(gstateptr->colorInfo, GSC_FILL, &colorspace) &&
         push(&colorspace, &operandstack) ;
}

Bool setcolor_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setcolor(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currentcolor_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_currentcolor(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool setcmykcolor_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setcmykcolor(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currentcmykcolor_(ps_context_t *pscontext)
{
  USERVALUE cmyk[4];

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_invokeChainTransform(gstateptr->colorInfo, GSC_FILL,
                                  SPACE_DeviceCMYK,
                                  TRUE,
                                  cmyk) &&
         stack_push_real(cmyk[0], &operandstack) &&
         stack_push_real(cmyk[1], &operandstack) &&
         stack_push_real(cmyk[2], &operandstack) &&
         stack_push_real(cmyk[3], &operandstack);
}

Bool setrgbcolor_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setrgbcolor(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currentrgbcolor_(ps_context_t *pscontext)
{
  USERVALUE rgb[3];

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_invokeChainTransform(gstateptr->colorInfo, GSC_FILL,
                                  SPACE_DeviceRGB,
                                  TRUE,
                                  rgb) &&
         stack_push_real(rgb[0], &operandstack) &&
         stack_push_real(rgb[1], &operandstack) &&
         stack_push_real(rgb[2], &operandstack) ;
}

Bool setgray_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setgray(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currentgray_(ps_context_t *pscontext)
{
  USERVALUE gray[1];

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_invokeChainTransform(gstateptr->colorInfo, GSC_FILL,
                                  SPACE_DeviceGray,
                                  TRUE,
                                  gray) &&
         stack_push_real(gray[0], &operandstack);
}

Bool sethsbcolor_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_sethsbcolor(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

Bool currenthsbcolor_(ps_context_t *pscontext)
{
  USERVALUE rgb[3];
  USERVALUE hsv[3];

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_invokeChainTransform(gstateptr->colorInfo, GSC_FILL,
                                  SPACE_DeviceRGB,
                                  TRUE,
                                  rgb) &&
         gsc_rgb_to_hsv(rgb, hsv) &&
         stack_push_real(hsv[0], &operandstack) &&
         stack_push_real(hsv[1], &operandstack) &&
         stack_push_real(hsv[2], &operandstack);
}

Bool setpattern_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setpattern(gstateptr->colorInfo, &operandstack, GSC_FILL);
}

/* ---------------------------------------------------------------------- */

Bool settransfer_(ps_context_t *pscontext)
{
  /* Expect a single transfer function, which applies to all the color
   * components of the device color space.
   * A new transfer function effectively invalidates the color cache;
   * therefore we may want to go to some trouble to see whether the
   * function is the same.
   */
  if ( DEVICE_INVALID_CONTEXT())
    return error_handler(UNDEFINED) ;

  return gsc_settransfers(pscontext->corecontext,
                          gstateptr->colorInfo, &operandstack, 1);
}

Bool currenttransfer_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_currenttransfers(gstateptr->colorInfo, &operandstack, 3, 3);
}

Bool setcolortransfer_(ps_context_t *pscontext)
{
  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_settransfers(pscontext->corecontext,
                          gstateptr->colorInfo, &operandstack, 4);
}

Bool currentcolortransfer_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_currenttransfers(gstateptr->colorInfo, &operandstack, 0, 3);
}


/* ---------------------------------------------------------------------- */

Bool setcalibration_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW);

  theo = theTop(operandstack);

  if (!gsc_setcalibration(gstateptr->colorInfo, *theo))
    return FALSE ;

  pop(&operandstack);

  return TRUE ;
}

Bool currentcalibration_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_getcalibration(gstateptr->colorInfo, &operandstack);
}

/* ---------------------------------------------------------------------- */

Bool setscreen_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED );
  return gsc_setscreens(gstateptr->colorInfo, &operandstack, ST_SETSCREEN);
}

Bool currentscreen_(ps_context_t *pscontext)
{
  return gsc_currentscreens(ps_core_context(pscontext), gstateptr->colorInfo, &operandstack, 3, 3);
}

Bool setcolorscreen_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED );
  return gsc_setscreens(gstateptr->colorInfo, &operandstack, ST_SETCOLORSCREEN);
}

Bool currentcolorscreen_(ps_context_t *pscontext)
{
  return gsc_currentscreens(ps_core_context(pscontext), gstateptr->colorInfo, &operandstack, 0, 3);
}

Bool sethalftone_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED );
  return gsc_setscreens(gstateptr->colorInfo, &operandstack, ST_SETHALFTONE);
}

Bool currenthalftone_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_currenthalftones(gstateptr->colorInfo, &operandstack);
}

/* ---------------------------------------------------------------------- */
Bool setoverprint_(ps_context_t *pscontext)
{
  OBJECT *theo;
  int32 overprint;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW);

  theo = theTop(operandstack);
  if ( oType(*theo) != OBOOLEAN)
    return error_handler(TYPECHECK);

  overprint = oBool(*theo);
  pop(&operandstack);

  return gsc_setoverprint(gstateptr->colorInfo, GSC_FILL, overprint) &&
         gsc_setoverprint(gstateptr->colorInfo, GSC_STROKE, overprint);
}

Bool currentoverprint_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(gsc_getoverprint(gstateptr->colorInfo, GSC_FILL) ?
              &tnewobj : &fnewobj,
              &operandstack);
}

Bool setoverprintmode_(ps_context_t *pscontext)
{
  OBJECT *theo;
  int32 overprintMode;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW);

  theo = theTop(operandstack);
  if (oType(*theo) != OBOOLEAN)
    return error_handler(TYPECHECK);

  overprintMode = oBool(*theo);
  pop(&operandstack);

  /* Set the value to be applied in overprint calcualations */
  if (!gsc_setoverprintmode(gstateptr->colorInfo, overprintMode))
    return FALSE;

  /* Set the value to be returned from currentoverprintmode */
  if (!gsc_setcurrentoverprintmode(gstateptr->colorInfo, overprintMode))
    return FALSE;

  return TRUE;
}

Bool currentoverprintmode_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(gsc_getcurrentoverprintmode(gstateptr->colorInfo) ? &tnewobj : &fnewobj,
              &operandstack);
}

/* ---------------------------------------------------------------------- */

Bool setblackgeneration_(ps_context_t *pscontext)
{
  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setblackgeneration(pscontext->corecontext,
                                gstateptr->colorInfo, &operandstack);
}

Bool currentblackgeneration_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(gsc_getblackgenerationobject(gstateptr->colorInfo),
              &operandstack);
}

Bool setundercolorremoval_(ps_context_t *pscontext)
{
  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  return gsc_setundercolorremoval(pscontext->corecontext,
                                  gstateptr->colorInfo, &operandstack);
}

Bool currentundercolorremoval_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(gsc_getundercolorremovalobject(gstateptr->colorInfo),
              &operandstack);
}

/* ---------------------------------------------------------------------- */

Bool setcolorrendering_(ps_context_t *pscontext)
{
  /* See red book 2 page 497:
   * sets the CIE color rendering dictionary in the graphics state -
   * applied when converting from CIE to device color.
   */

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (DEVICE_INVALID_CONTEXT())
    return error_handler(UNDEFINED);

  if (!gsc_setcolorrendering(gstateptr->colorInfo, &operandstack))
    return FALSE;

  return TRUE;
}

Bool currentcolorrendering_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(gsc_getcolorrendering(gstateptr->colorInfo),
              &operandstack);
}

/* ---------------------------------------------------------------------- */
Bool setinterceptcolorspace_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW);

  theo = theTop(operandstack);

  if (!gsc_setinterceptcolorspace(gstateptr->colorInfo, theo))
    return FALSE ;

  pop(&operandstack);

  return TRUE ;
}

Bool currentinterceptcolorspace_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_getinterceptcolorspace(gstateptr->colorInfo, &operandstack);
}

Bool setrenderingintent_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  theo = theTop (operandstack);

  if (!gsc_setrenderingintent(gstateptr->colorInfo, theo))
    return FALSE;

  pop(&operandstack);

  return TRUE;
}

Bool currentrenderingintent_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_getrenderingintent(gstateptr->colorInfo, &operandstack);
}

Bool setreproduction_(ps_context_t *pscontext)
{
  SYSTEMPARAMS *systemparams = ps_core_context(pscontext)->systemparams;
  OBJECT *theo;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  theo = theTop (operandstack);

  /* Check that this operator is allowed on this instance of the rip */
  if ( !(systemparams->HCMS || systemparams->HCEP ||
         systemparams->ICC || systemparams->HCMSLite) ) {
    /* Check if the dictionary is empty. If there is any key present then
     * we have to check security. A trivial dictionary is allowed because it
     * is convenient to allow postscript to set default values with no colour
     * management.
     */
    if (oType(*theo) == ODICTIONARY) {
      int length;
      getDictLength(length, theo);
      if (length != 0) {
        /* This HQTRACE is to nudge an internal user to fix their passwords, not
         * for us to fix the code */
        HQTRACE(TRUE, ("ColorPro password failure"));
        return detail_error_handler(CONFIGURATIONERROR, "ColorPro password incorrect");
      }
    }
  }

  if (!gsc_setreproduction(gstateptr->colorInfo, theo))
    return FALSE;

  pop(&operandstack);

  return TRUE;
}

Bool currentreproduction_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_getreproduction(gstateptr->colorInfo, &operandstack);
}

Bool setmiscobjectmappings_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  theo = theTop (operandstack);

  if (!gsc_setmiscobjectmappings(gstateptr->colorInfo, theo))
    return FALSE;

  pop(&operandstack);

  return TRUE;
}

Bool currentmiscobjectmappings_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gsc_getmiscobjectmappings(gstateptr->colorInfo, &operandstack);
}

Bool geticcbasedinfo_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  return gsc_geticcbasedinfo(gstateptr->colorInfo, &operandstack);
}

Bool geticcbasedintent_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  return gsc_geticcbasedintent(gstateptr->colorInfo, &operandstack);
}

Bool geticcbasedisscrgb_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  return gsc_geticcbased_is_scRGB(gstateptr->colorInfo, &operandstack);
}

Bool getcurrentcolorspacerange_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty(operandstack))
    return error_handler (STACKUNDERFLOW);

  return gsc_getcurrentcolorspacerange(gstateptr->colorInfo, &operandstack);
}


/* Log stripped */

/* eof */
