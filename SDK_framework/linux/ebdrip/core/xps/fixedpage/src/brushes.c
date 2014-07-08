/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:brushes.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:brushes.c,v 1.54.2.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload callbacks for brushes.
 *
 * See localFunctions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "gschead.h"
#include "graphics.h"
#include "gsc_icc.h"            /* gsc_get_iccbased_intent */
#include "gschcms.h"            /* gsc_setrenderingintent */
#include "gstate.h"
#include "tranState.h"
#include "miscops.h"
#include "namedef_.h"
#include "xpspriv.h"
#include "xpsiccbased.h"


Bool xps_setcolor(xmlDocumentContext *xps_ctxt, int32 colortype, xps_color_designator *color_designator)
{
  OBJECT   *colorspace;
  NAMECACHE *renderingIntentName;

  HQASSERT(color_designator != NULL, "color_designator is NULL") ;
  HQASSERT(colortype == GSC_FILL ||colortype == GSC_STROKE,
           "Expect colortype to be defined at this point");

  HQASSERT(color_designator->color_set == TRUE, "color is not set") ;
  HQASSERT(color_designator->n_colorants > 0, "colorant count is not set") ;
  HQASSERT(color_designator->alpha >= 0.0f,"alpha is not set") ;

  HQASSERT(color_designator->colorspace > not_set &&
           color_designator->colorspace < dummy_max_colorspace,
           "Unexpected xps colorspace") ;

  HQASSERT(color_designator->color_profile_partname != NULL ||
           ((color_designator->colorspace == sRGB ||
             color_designator->colorspace == scRGB ) &&
            color_designator->n_colorants == 3),
           "wrong number of colorants") ;

  if ( color_designator->colorspace == sRGB || color_designator->colorspace == scRGB ) {

    if( color_designator->colorspace == sRGB ) {
      /* Set up the sRGB colorSpace */
      if( !set_xps_sRGB( colortype ))
        return FALSE;
    } else {
      /* Set up the scRGB colorSpace */
      if( !set_xps_scRGB( colortype ))
        return FALSE;
    }

    /* And the default rendering intent */
    oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
  }
  else {
    /* Create an ICCBased colorspace from the profile provided for
       values specified as CMYK, N-Color or Named Color. */

    if( !xps_icc_cache_define(color_designator->color_profile_partname,
                              color_designator->n_colorants,
                              &colorspace) ||
        !push(colorspace, &operandstack) ||
        !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, colortype) )
      return FALSE;

    /* Set the rendering intent from the profile */
    if (!gsc_get_iccbased_intent(gstateptr->colorInfo,
                                 colorspace, &renderingIntentName))
      return FALSE;
    oName(nnewobj) = renderingIntentName;
  }

  if ( !gsc_setrenderingintent(gstateptr->colorInfo, &nnewobj) )
    return FALSE;

  if ( !gsc_setcolordirect(gstateptr->colorInfo, colortype, &(color_designator->color[0])) )
    return FALSE;

  /* An optional alpha component can be specified with the rgb or scRGB values;
     it is compulsory with CMYK, N-Channel, and Named Color values;
     the alpha is multiplicative with the opacity alpha attribute. */
  if ( (color_designator->alpha) < 1.0 ) {
    if ( xps_ctxt->capture_opacity ) {
      if ( colortype == GSC_STROKE ) {
        xps_ctxt->stroke_brush_opacity *= color_designator->alpha;
      } else {
        xps_ctxt->fill_brush_opacity *= color_designator->alpha;
      }
    } else {
      USERVALUE alpha = color_designator->alpha * tsConstantAlpha(gsTranState(gstateptr), colortype == GSC_STROKE);
      tsSetConstantAlpha(gsTranState(gstateptr), colortype == GSC_STROKE,
                         alpha, gstateptr->colorInfo);
    }
  }

  return TRUE;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static int32 xps_SolidColorBrush_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  Bool success = FALSE;

  static USERVALUE opacity;
  static Bool opacity_set;
  static xps_color_designator color_designator = { XML_INTERN(SolidColorBrush), FALSE };

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity},
    { XML_INTERN(Color), NULL, NULL, xps_convert_ST_Color, &color_designator},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  HQASSERT(xps_ctxt->colortype == GSC_FILL ||
           xps_ctxt->colortype == GSC_STROKE,
           "Expect colortype to be defined at this point");


  color_designator.color_set = FALSE;
  color_designator.color_profile_partname = NULL;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
  {
    success = error_handler(UNDEFINED);
    goto cleanup;
  }

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
  {
    success = error_handler(UNDEFINED);
    goto cleanup;
  }

  /* It MUST be set because its not optional in the match. */
  HQASSERT(color_designator.color_set == TRUE,
           "color is not set");

  if ( !xps_setcolor(xps_ctxt, xps_ctxt->colortype, &color_designator) )
    goto cleanup;

  if ( opacity_set && opacity < 1.0 ) {
    if ( xps_ctxt->capture_opacity ) {
      if ( xps_ctxt->colortype == GSC_STROKE ) {
        xps_ctxt->stroke_brush_opacity *= opacity;
      } else {
        xps_ctxt->fill_brush_opacity *= opacity;
      }
    } else {
      USERVALUE alpha = opacity * tsConstantAlpha(gsTranState(gstateptr), xps_ctxt->colortype == GSC_STROKE);
      tsSetConstantAlpha(gsTranState(gstateptr), xps_ctxt->colortype == GSC_STROKE,
                         alpha, gstateptr->colorInfo);
    }
  }

  success = TRUE; /* keep on parsing */

cleanup:
  if (color_designator.color_profile_partname != NULL)
    xps_partname_free(&color_designator.color_profile_partname);

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts solidbrush_functions[] =
{
  { XML_INTERN(SolidColorBrush),
    xps_SolidColorBrush_Start,
    NULL,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
