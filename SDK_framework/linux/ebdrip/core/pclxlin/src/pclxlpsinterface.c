/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpsinterface.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains various wrappers around Postscript APIs
 * provided by the Core RIP including:
 *
 *  save_(pscontext)
 *  restore_(pscontext)
 *  run_ps_string()
 *  thegsPageCTM()
 *
 * \todo ajcd 2009-01-15: Most of this file needs re-written. Gstate
 * operations should be phrased in terms of the underlying graphics model,
 * not by calling PostScript operators.
 */

#include <math.h>       /* fabs() */

#include "core.h"
#include "routedev.h"
#include "swctype.h"    /* isprint() etc. */
#include "dictscan.h"   /* NAMETYPEMATCH */
#include "namedef_.h"   /* NAME_PageSize, NAME_Orientation etc.*/
#include "swcopyf.h"
#include "display.h"
#include "miscops.h"
#include "matrix.h"
#include "paths.h"
#include "gstack.h"
#include "gschead.h"
#include "gu_ctm.h"
#include "gu_path.h"    /* path_moveto() */
#include "graphics.h"
#include "pathcons.h"
#include "stacks.h"
#include "fcache.h"     /* plotchar() */
#include "dicthash.h"
#include "showops.h"    /* init_charpath() and end_charpath() */
#include "pclGstate.h"  /* pclGstateInit(), setPcl5Rop() et al. */
#include "clipops.h"    /* gs_addclip */
#include "gu_fills.h"
#include "pathops.h"
#include "system.h"
#include "gstate.h"
#include "gu_cons.h"
#include "utils.h"
#include "hqmemset.h"
#include "hqmemcpy.h"
#include "constant.h"
#include "plotops.h"
#include "gu_chan.h"

#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlparsercontext.h"
#include "pclxlerrors.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlpattern.h"
#include "pclAttrib.h"

/**
 * The global cached palette (defined in pcl5 printmodel.c), used in conjunction with setPclXLPattern to
 * communicate the palette in the form required by the core.
 */
extern PclXLCachedPalette cached_palette;


static Bool pclxl_ps_set_colorspace_internal(PCLXL_ColorSpace color_space,
                                             int32 color_type);
static void clear_last_font();


Bool
pclxl_ps_save(PCLXL_CONTEXT pclxl_context, OBJECT* save_location)
{
  ps_context_t *pscontext ;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  if ( !save_(pscontext) )
  {
    /*
     * We have failed to save the current Postscript state
     * So we need to log this
     * And then return FALSE to abort further processing
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to \"save\" Postscript state"));

    return FALSE;
  }
  else
  {
    Copy(save_location, theTop(operandstack)) ;

    pop(&operandstack) ;

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: save"));

    return TRUE;
  }
}

Bool
pclxl_ps_restore(PCLXL_CONTEXT pclxl_context, OBJECT* saved_state)
{
  ps_context_t *pscontext ;
  PCLXL_NON_GS_STATE non_gs_state ;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  non_gs_state = &pclxl_context->non_gs_state ;
  non_gs_state->setg_required = TRUE ;

  if ( !push(saved_state, &operandstack) ||
       !restore_(pscontext) )
  {
    /*
     * We have failed to restore the current Postscript state
     * So we need to log this
     * And then return FALSE to abort further processing
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to \"restore\" Postscript state"));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: restore"));

    return TRUE;
  }
}

Bool
pclxl_ps_run_ps_string(PCLXL_CONTEXT pclxl_context,
                       uint8* postscript_string,
                       size_t postscript_string_len)
{
  PCLXL_NON_GS_STATE non_gs_state;
  UNUSED_PARAM(size_t, postscript_string_len);

  if (! finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return FALSE;
  }
  non_gs_state = &pclxl_context->non_gs_state;
  non_gs_state->setg_required = TRUE;

  if ( ! run_ps_string(postscript_string) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to run_ps_string(\"%s\")",
                                postscript_string));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: \"%s\"",
                 postscript_string));

    return TRUE;
  }

  /*NOTREACHED*/
}

/**
 * \brief pclxl_ps_units_per_pclxl_uom() is passed a PCLXL "Measure"
 * which is actually an enumeration eInch == 0, eMillimeter == 1
 * and eTenthsOfAMillimeter == 2 or a private value <1/7200th of an inch> == 4
 *
 * pclxl_ps_uints_per_pclxl_uom() returns a value that represents
 * the corresponding number of Postscript units
 *
 * It is basically used for 3 purposes:
 *
 * 1) Converting PCLXL page sizes (expressed in 1/7200ths of an inch)
 *    into Postscript Units
 *
 * 2) Converting PCLXL "user units" into a base Postscript CTM
 *    that means that PCLXL "user units" are directly usable
 *    for Postscript moveto/lineto/curveto etc. operations
 *
 * 3) Doing a reverse calculation from a number of real-world units
 *    (i.e. inches, millimeters etc.) back into current PCLXL "user units"
 *    such that when the resultant values are passed back through the
 *    current CTM (in (2) above) they are placed on the page
 *    at this real-world-unit placement.
 *
 *    This is principally used as part of some resolution and "measure"
 *    *independent* tests in pclxltests.{c,h}
 */

PCLXL_SysVal
pclxl_ps_units_per_pclxl_uom(uint8 pclxl_uom)
{
  static PCLXL_SysVal ps_units_per_pclxl_uom[4] =
  {
    72.0,           /* 1 inch = 72[.0] Postscript units */
    72.0/25.4,      /* 1 millimeter = 2.8346... Postscript units */
    72.0/254.0,     /* 0.1 millimeters = 0.28346.. Postscript units */
    72.0/7200.0     /* 1/7200th of an inch = 0.01 Postscript units */
  };

  HQASSERT((pclxl_uom < sizeof(ps_units_per_pclxl_uom)), "PCLXL UnitsOfMeasure must be one of eInch = 0, eMillimeter = 1, eTenthsOfAMillimeter = 2 or the private \"1/7200th of an inch\" = 4");

  return ps_units_per_pclxl_uom[pclxl_uom];
}

/* See header for doc. */
Bool pclxl_ps_init_core(PCLXL_CONTEXT pclxl_context)
{
  uint8* ps_string;
  GUCR_RASTERSTYLE *rs = gsc_getRS(gstateptr->colorInfo);
  DEVICESPACEID dspace ;
  int32 bits = pclxl_context->config_params.vds_select ;

  guc_deviceColorSpace(rs, &dspace, NULL) ;
  switch ( dspace ) {
  case DEVICESPACE_Gray:
    bits >>= PCL_VDS_GRAY_SHIFT ;
    break ;
  case DEVICESPACE_CMYK:
    bits >>= PCL_VDS_CMYK_SHIFT ;
    break ;
  case DEVICESPACE_RGB:
    bits >>= PCL_VDS_RGB_SHIFT ;
    break ;
  default:
    bits >>= PCL_VDS_OTHER_SHIFT ;
    break ;
  }
  bits &= PCL_VDS_MASK ;

  /* Enable PCL gstate application and initialise extended gstate. */
  pclGstateEnable(pclxl_context->corecontext, TRUE, FALSE);

  /* Appendix A of the PCL XL spec refers to the pixel placement rules.  It's
   * tesselating scan conversion for vector objects. */
  /* Enable OverprintPreview to match behaviour when BackdropRender was required. */
#define SET_VIRTUAL_SPACE(b_, r_, x_)                                 \
  (uint8 *)("<</OverprintPreview " #b_ " >> setinterceptcolorspace "  \
            "<<"                                                      \
            "  /VirtualDeviceSpace /" #x_                             \
            "  /LateColorManagement true "                            \
            "  /ScanConversion /RenderTesselating "                   \
            "  /DeviceROP " #r_                                       \
            ">> setpagedevice")
  ps_string = (bits == PCL_VDS_CMYK_F_T
               ? SET_VIRTUAL_SPACE(false, true, DeviceCMYK)
               : bits == PCL_VDS_GRAY_F_T
               ? SET_VIRTUAL_SPACE(false, true, DeviceGray)
               : bits == PCL_VDS_RGB_T_T
               ? SET_VIRTUAL_SPACE(true, true, DeviceRGB)
               : SET_VIRTUAL_SPACE(true, false, DeviceRGB)) ;
  return pclxl_ps_run_ps_string(pclxl_context, ps_string,
                                strlen((char*) ps_string));
}

/* See header for doc. */
void pclxl_ps_finish_core(PCLXL_CONTEXT pclxl_context)
{
  /* Enable PCL gstate application. */
  pclGstateEnable(pclxl_context->corecontext, FALSE, FALSE);
}

#define PTS_TO_CENTIPTS(v)  ((int32)((v)*100 + 0.5))
/*
 * pclxl_ps_currentpagedevice() calls Postscript's "currentpagedevice"
 * and then extracts various settings from the resultant
 * "page device dictionary" that is left on the Postscript operand stack
 *
 * These settings are then written into the supplied non-gs-state structure
 */

Bool
pclxl_ps_currentpagedevice(PCLXL_CONTEXT pclxl_context,
                           PCLXL_MEDIA_DETAILS  current_media_details)
{
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

#ifdef DEBUG_BUILD

  static char* orientations[] =
  {
    "Portrait",
    "Landscape",
    "ReversePortrait",
    "ReverseLandscape",
    "Default"
  };

  static char* page_side[] =
  {
    "Front (Duplex)",
    "Back (Duplex)",
    "Front (Simplex)"
  };

#endif

  enum
  {
    PCLXL_CPD_PCLXLDEFAULTPAGESIZE = 0,
    PCLXL_CPD_PCLXLDEFAULTMEDIASOURCE,
    PCLXL_CPD_PCLXLDEFAULTMEDIADESTINATION,
    PCLXL_CPD_PCLXLDEFAULTMEDIATYPE,

    PCLXL_CPD_PCLXLDEFAULTPRINTABLEAREAMARGINS,

    PCLXL_CPD_PCLDEFAULTORIENTATION,
    PCLXL_CPD_PCLDEFAULTDUPLEX,
    PCLXL_CPD_PCLDEFAULTTUMBLE,

    PCLXL_CPD_PAGESIZE,
    PCLXL_CPD_MEDIATYPE,
    PCLXL_CPD_DUPLEX,
    PCLXL_CPD_TUMBLE,
    PCLXL_CPD_PCLORIENTATION,

#if 0
    PCLXL_CPD_LEADINGEDGE,
#endif

    PCLXL_CPD_END_INDEX
  };

  static NAMETYPEMATCH match[PCLXL_CPD_END_INDEX + 1] =
  {
    {NAME_PCLXLDefaultPageSize | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCLXLDefaultMediaSource | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCLXLDefaultMediaDestination | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PCLXLDefaultMediaType | OOPTIONAL, 1, {OSTRING}},

    {NAME_PCLXLDefaultPrintableAreaMargins | OOPTIONAL, 1, {OARRAY}},

    {NAME_PCLDefaultOrientation | OOPTIONAL, 2, {ONAME, OINTEGER}},
    {NAME_PCLDefaultDuplex | OOPTIONAL, 2, {OINTEGER, OBOOLEAN}},
    {NAME_PCLDefaultTumble | OOPTIONAL, 2, {OINTEGER, OBOOLEAN}},

    {NAME_PageSize | OOPTIONAL, 1, {OARRAY}},
    {NAME_MediaType | OOPTIONAL, 2, {OSTRING, ONULL}},
    {NAME_Duplex | OOPTIONAL, 2, {OINTEGER, OBOOLEAN}},
    {NAME_Tumble | OOPTIONAL, 2, {OINTEGER, OBOOLEAN}},
    {NAME_PCLOrientation | OOPTIONAL, 1, {OINTEGER}},

#if 0
    {NAME_LeadingEdge | OOPTIONAL, 1, {OINTEGER}},
#endif

    DUMMY_END_MATCH
  };

  OBJECT* currentpagedevice_dict = NULL;

  OBJECT* o_pclxl_default_page_size = NULL;
  OBJECT* o_pclxl_default_media_source = NULL;
  OBJECT* o_pclxl_default_media_destination = NULL;
  OBJECT* o_pclxl_default_media_type = NULL;

  OBJECT* o_pclxl_default_printable_area_margins = NULL;

  OBJECT* o_pcl_default_orientation = NULL;
  OBJECT* o_pcl_default_duplex = NULL;
  OBJECT* o_pcl_default_tumble = NULL;

  OBJECT* o_media_type = NULL;

  OBJECT* o_duplex = NULL;
  OBJECT* o_tumble = NULL;
  OBJECT* o_orientation = NULL;

  SYSTEMVALUE values[4] ;

#if 0
  OBJECT* o_leading_edge = NULL;
#endif

  Bool tumble = FALSE;

  HQASSERT((dev_is_bandtype(CURRENT_DEVICE())),
           "Unexpected non-band device");
  currentpagedevice_dict = &thegsDevicePageDict(*gstateptr);

  if ( !dictmatch(currentpagedevice_dict, match) )
  {
    /*
     * We were expecting certain specific entries in the
     * page device dictionary
     * But it appears that there is something wrong with our expectation
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("current page device dictionary content does not match the expected minimumum content and/or type mismatch"));

    return FALSE;
  }

  /*
   * We can now begin handling each of the individual dictionary keys
   */

  if ( (o_pclxl_default_page_size = match[PCLXL_CPD_PCLXLDEFAULTPAGESIZE].result) != NULL )
  {
    config_params->default_media_size = (PCLXL_MediaSize) oInteger(*o_pclxl_default_page_size);

    /*
     * Ok, we have found the *default* PCLXL media size
     *
     * But we only use this default if the requested media size
     * has not been specified
     */

    if ( !current_media_details->media_size_value_type )
    {
      if ( !(current_media_details->media_size_value_type & PCLXL_MEDIA_SIZE_ENUM_VALUE) )
      {
        current_media_details->media_size = config_params->default_media_size;

        current_media_details->media_size_value_type |= PCLXL_MEDIA_SIZE_ENUM_VALUE;
      }

      /*
       * I know that there is little point in setting both media size enum
       * and media size name values because we will only use one of them
       * (see preferred order below)
       * But it makes it clearer what the origin of the enum value was
       * if we also set the name.
       */

      if ( !(current_media_details->media_size_value_type & PCLXL_MEDIA_SIZE_NAME_VALUE) )
      {
        current_media_details->media_size_name = (uint8*) "Default";

        current_media_details->media_size_value_type |= PCLXL_MEDIA_SIZE_NAME_VALUE;
      }
    }
  }

  if ( (o_pclxl_default_media_source = match[PCLXL_CPD_PCLXLDEFAULTMEDIASOURCE].result) != NULL )
  {
    config_params->default_media_source = (PCLXL_MediaSize) oInteger(*o_pclxl_default_media_source);
  }

  if ( (o_pclxl_default_media_destination = match[PCLXL_CPD_PCLXLDEFAULTMEDIADESTINATION].result) != NULL )
  {
    config_params->default_media_destination = (PCLXL_MediaSize) oInteger(*o_pclxl_default_media_destination);
  }

  if ( (o_pclxl_default_media_type = match[PCLXL_CPD_PCLXLDEFAULTMEDIATYPE].result) != NULL )
  {
    uint8* default_media_type = oString(*o_pclxl_default_media_type);
    uint32 default_media_type_len = theLen(*o_pclxl_default_media_type);
    uint32 default_media_type_max_len = sizeof(config_params->default_media_type);

    if ( default_media_type_len > default_media_type_max_len )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("PCLXL default media type too long"));

      return FALSE;
    }
    else
    {
      HqMemCpy(config_params->default_media_type,
               default_media_type,
               config_params->default_media_type_len = default_media_type_len);

      if (default_media_type_max_len > default_media_type_len)
      {
        HqMemZero(&config_params->default_media_type[default_media_type_len],
                  (default_media_type_max_len - default_media_type_len));
      }
    }
  }

  if ( (o_pclxl_default_printable_area_margins =
          match[PCLXL_CPD_PCLXLDEFAULTPRINTABLEAREAMARGINS].result) != NULL )
  {
     /*
      * Well we do have a value, and it is an array.
      * The question is: is it an array of 4 integers?
      */

      if (object_get_numeric_array(o_pclxl_default_printable_area_margins, values, 4) )
      {
        int i;

        for (i = 0 ; i < NUM_ARRAY_ITEMS(current_media_details->printable_area_margins) ; i++)
        {
          current_media_details->printable_area_margins[i] = PTS_TO_CENTIPTS(values[i]);
        }
      }
      else
        /*
         * The "/PCLXLDefaultPrintableAreaMargins" dictionary entry
         * is missing or it is not an array or it
         * is not length 4
         */
        PCLXL_ERROR_HANDLER(pclxl_context,
                            PCLXL_SS_KERNEL,
                            PCLXL_INTERNAL_ERROR,
                            ("Problem with current page device /PCLXLDefaultPrintableAreaMargins dictionary entry value"));
        return FALSE;
  }
  else
  {
    /*
     * If we have not been supplied with a set of default margins
     * then we use 1/6th == 12/72nds == 1200/7200ths of an inch
     * (we always store the value in centi-points
     */

    current_media_details->printable_area_margins[0] =
      current_media_details->printable_area_margins[1] =
        current_media_details->printable_area_margins[2] =
          current_media_details->printable_area_margins[3] =
            1200;
  }

  if ( (o_pcl_default_orientation = match[PCLXL_CPD_PCLDEFAULTORIENTATION].result) != NULL )
  {
    config_params->default_orientation = (PCLXL_Orientation) oInteger(*o_pcl_default_orientation);
  }

  if ( (o_pcl_default_duplex = match[PCLXL_CPD_PCLDEFAULTDUPLEX].result) != NULL )
  {
    if ( oType(*o_pcl_default_duplex) == OINTEGER )
    {
      config_params->default_duplex = oInteger(*o_pcl_default_duplex);
    }
    else
    {
      config_params->default_duplex = oBool(*o_pcl_default_duplex);
    }
  }

  if ( (o_pcl_default_tumble = match[PCLXL_CPD_PCLDEFAULTTUMBLE].result) != NULL )
  {
    if ( oType(*o_pcl_default_tumble) == OINTEGER )
    {
      config_params->default_duplex_binding = (oInteger(*o_pcl_default_tumble) ?
                                               PCLXL_eDuplexHorizontalBinding :
                                               PCLXL_eDuplexVerticalBinding);
    }
    else
    {
      config_params->default_duplex_binding = (oBool(*o_pcl_default_tumble) ?
                                               PCLXL_eDuplexHorizontalBinding :
                                               PCLXL_eDuplexVerticalBinding);
    }
  }


  if ( match[PCLXL_CPD_PAGESIZE].result &&
       object_get_numeric_array(match[PCLXL_CPD_PAGESIZE].result, values, 2) ) {
    /*
     * Yep we've got a "PageSize" dictionary entry and it is an array containing
     * 2 entries
     *
     * So we can go ahead and extract the two point values and record them as
     * the current (real) "media size XY" in centipoints.
     */
    current_media_details->media_size_xy.x = PTS_TO_CENTIPTS(values[0]);
    current_media_details->media_size_xy.y = PTS_TO_CENTIPTS(values[1]);
    current_media_details->media_size_value_type |= PCLXL_MEDIA_SIZE_XY_VALUE;
  } else {
    /*
     * The "/PageSize" dictionary entry is missing or it is not an array or it
     * is not length 2
     */
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                        ("Problem with current page device /PageSize dictionary entry"));
    return FALSE;
  }



  if ( ((o_media_type = match[PCLXL_CPD_MEDIATYPE].result) != NULL) &&
       (oType(*o_media_type) == OSTRING) )
  {
    uint8* media_type = oString(*o_media_type);
    uint32 media_type_len = theLen(*o_media_type);
    uint32 media_type_max_len = sizeof(current_media_details->media_type);

    if ( media_type_len > media_type_max_len )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("media type too long"));

      return FALSE;
    }
    else
    {
      HqMemCpy(current_media_details->media_type,
               media_type,
               current_media_details->media_type_len = media_type_len);

      if (media_type_max_len > media_type_len)
      {
        HqMemZero(&current_media_details->media_type[media_type_len],
                  (media_type_max_len - media_type_len));
      }
    }
  }
  else
  {
    uint32 media_type_max_len = sizeof(current_media_details->media_type);

    HQASSERT(media_type_max_len == sizeof(config_params->default_media_type),
             "Default media type max len is not the same as current media type max len");

    HqMemCpy(current_media_details->media_type,
             config_params->default_media_type,
             current_media_details->media_type_len = config_params->default_media_type_len);

    if ( current_media_details->media_type_len < media_type_max_len )
    {
      HqMemZero(&current_media_details->media_type[current_media_details->media_type_len],
                (media_type_max_len - current_media_details->media_type_len));
    }
  }

  if ( (o_duplex = match[PCLXL_CPD_DUPLEX].result) != NULL )
  {
    current_media_details->duplex = oInteger(*o_duplex);

    if ( !current_media_details->duplex )
      current_media_details->simplex_page_side = PCLXL_eSimplexFrontSide;
  }
  else
  {
    current_media_details->duplex = config_params->default_duplex;
  }

  if ( (o_tumble = match[PCLXL_CPD_TUMBLE].result) != NULL )
  {
    tumble = oInteger(*o_tumble);

    current_media_details->duplex_binding = (tumble ?
                                             PCLXL_eDuplexHorizontalBinding :
                                             PCLXL_eDuplexVerticalBinding);
  }
  else
  {
    tumble = (current_media_details->duplex &&
              (config_params->default_duplex_binding == PCLXL_eDuplexHorizontalBinding));

    current_media_details->duplex_binding = (tumble ?
                                             PCLXL_eDuplexHorizontalBinding :
                                             PCLXL_eDuplexVerticalBinding);
  }

  if ( (o_orientation = match[PCLXL_CPD_PCLORIENTATION].result) != NULL )
  {
    current_media_details->orientation = (PCLXL_Orientation) oInteger(*o_orientation);
  }
  else
  {
    current_media_details->orientation = config_params->default_orientation;
  }

#if 0
  if ( (o_leading_edge = match[PCLXL_CPD_LEADINGEDGE].result) != NULL )
  {
    current_media_details->leading_edge = oInteger(*o_leading_edge);

    HQASSERT(((current_media_details->leading_edge >= 0) &&
              (current_media_details->leading_edge <= 3)),
             "LeadingEdge value must be 0, 1, 2 or 3 (as it is used as an index into an array)");
  }
  else
#endif
  {
    SYSTEMVALUE dx;
    SYSTEMVALUE dy;

    MATRIX_TRANSFORM_DXY(1.0, 1.0, dx, dy, &thegsDeviceCTM(*gstateptr));

    if ( (dx < 0.0) && (dy < 0.0) ) current_media_details->leading_edge = 2;
    else if ( dy < 0.0 ) current_media_details->leading_edge = 3;
    else if ( dx < 0.0 ) current_media_details->leading_edge = 1;
    else current_media_details->leading_edge = 0;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
              ("PS: currentpagedevice => PageSize = [ %f , %f ], Orientation = %s, Printable Area Clip Margins = [ %d, %d, %d, %d ], PageSide = %s, Duplex = %s, Tumble = %s, LeadingEdge = %d, MediaType = \"%s\", MediaSize = %d, MediaSizeName = \"%s\"",
               current_media_details->media_size_xy.x,
               current_media_details->media_size_xy.y,
               orientations[current_media_details->orientation],
               current_media_details->printable_area_margins[0],
               current_media_details->printable_area_margins[1],
               current_media_details->printable_area_margins[2],
               current_media_details->printable_area_margins[3],
               page_side[(current_media_details->duplex ? current_media_details->duplex_page_side : (current_media_details->simplex_page_side + 2))],
               (current_media_details->duplex ? "true" : "false"),
               (tumble ? "true" : "false"),
               current_media_details->leading_edge,
               current_media_details->media_type,
               current_media_details->media_size,
               (current_media_details->media_size_name ? current_media_details->media_size_name : (uint8*) "<Null>")));

  return TRUE;
}

/**
 * Calculate the DPI of the output device.
 */
static uint32 pclxl_calculate_device_dpi(void)
{
  double x = 7200, y = 0;

  /* Transform one inch to device space. */
  MATRIX_TRANSFORM_XY(x, y, x, y, &thegsDeviceCTM(*gstateptr));

  /* Round the result. */
  return (uint32)(sqrt((x * x) + (y * y)) + 0.5);
}

/**
 * pclxl_ctm_sanitize_default_matrix is a workaround to remove inaccuracies in the
 * device CTM which build up as a result of the way the CTM is constructed in
 * pagedev.pss and also the /Scaling [ 0.01 0.01 ] entry in oil_psconfig.c.  The
 * construction of the CTM involves casts from doubles to floats for the
 * arguments and also with the use of currentmatrix in pagedev.pss.  This
 * routine recognises that most of the time the matrix values will be resolution
 * / 7200 and therefore the values in the matrix can be snapped accordingly,
 * back to double accuracy.  Without this objects may misalign leading to output
 * problems.
 */
static Bool pclxl_ctm_sanitize_default_matrix(void)
{
  Bool reset = FALSE;
  uint32 device_dpi = pclxl_calculate_device_dpi();
  OMATRIX matrix;
  int32 i, j;

  MATRIX_COPY(&matrix, &thegsPageCTM(*gstateptr));

  for ( i = 0; i < 2; ++i ) {
    for ( j = 0; j < 2; ++j ) {
      SYSTEMVALUE tmp = matrix.matrix[i][j];

      if ( fabs(tmp) > EPSILON ) {
        SYSTEMVALUE dpi = fabs(tmp) * 7200;

        if ( fabs(device_dpi - dpi) < 0.0001 ) {
          matrix.matrix[i][j] = device_dpi / 7200.0;

          if ( tmp < 0 )
            matrix.matrix[i][j] = -matrix.matrix[i][j];

          reset = TRUE;
        }
      }
    }
  }

  if ( reset ) {
    if ( !gs_setctm(&matrix, FALSE) )
      return FALSE;
  }

  return TRUE;
}

/**
 * pclxl_ps_setpagedevice() is passed an existing PCLXL_MEDIA_DETAILS
 * which contains the current page device settings
 * and a "requested page device" which is what is now requested/required
 * for the next page.
 *
 * \todo The existing settings are supplied so that
 * (in the future) we can minimize the changes required.
 *
 * Right now we simply construct a page device dictionary
 * containing a /ExtraPageDeviceKeys sub-dictionary
 * containing /PCLXLMediaDestination, /PCLXLMediaSource, /PCLXLMediaType
 * /PCLOrientation and /PCLXLPageSize
 *
 * Note that /PCLXLPageSize can take an integer, a string
 * or even a [ <Width>, <Height> ] array depending upon
 * exactly how
 */

Bool
pclxl_ps_setpagedevice(PCLXL_CONTEXT pclxl_context,
                       PCLXL_MEDIA_DETAILS  previous_page_device,
                       PCLXL_MEDIA_DETAILS  requested_page_device)
{
  uint8 ps_string[512];
  size_t ps_string_len = 0;
  uint8 page_dev_changes = 0;

  ps_string_len = swncopyf(&ps_string[ps_string_len],
                           CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                           (uint8*)"<<");

  if ( requested_page_device->media_destination != previous_page_device->media_destination )
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCLXLMediaDestination %d",
                              requested_page_device->media_destination);
    page_dev_changes++;
  }

  if ( requested_page_device->media_source != previous_page_device->media_source )
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCLXLMediaSource %d",
                              requested_page_device->media_source);
    page_dev_changes++;
  }

  if ( strcmp((char*) requested_page_device->media_type,
              (char*) previous_page_device->media_type) )
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCLXLMediaType(%s)",
                              requested_page_device->media_type);
    page_dev_changes++;
  }

  if ( (requested_page_device->orientation != previous_page_device->orientation) ||
       (requested_page_device->orientation != 0) ||
       (page_dev_changes) )
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCLOrientation %d",
                              requested_page_device->orientation);
    page_dev_changes++;
  }

  if ( requested_page_device->duplex != previous_page_device->duplex )
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/Duplex %s",
                              (requested_page_device->duplex ? "true" : "false"));
    page_dev_changes++;
  }

  if ( requested_page_device->duplex &&
       ((requested_page_device->duplex_binding != previous_page_device->duplex_binding) ||
        (requested_page_device->orientation != previous_page_device->orientation)) ) {
    Bool tumble = (requested_page_device->duplex_binding == PCLXL_eDuplexVerticalBinding)
      ? (requested_page_device->orientation == PCLXL_eLandscapeOrientation ||
         requested_page_device->orientation == PCLXL_eReverseLandscape)
      : (requested_page_device->orientation == PCLXL_ePortraitOrientation ||
         requested_page_device->orientation == PCLXL_eReversePortrait);
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/Tumble %s", (tumble ? "true" : "false"));
    page_dev_changes++;
  }

  /*
   * Now we come to the media size (a.k.a. page size) setting We reset this
   * whenever there is a requested media *size* change *or* when any other media
   * change (like media type, duplex, tumble etc.) because the presence of the
   * PCLXLPageSize key in the setpagedevice dictionary triggers all sorts of
   * other current page device changes
   *
   * We have 3 possible sources of media size change: The (custome) media size
   * "XY" value, the media size enum and the media size name.
   *
   * We always favour the media size XY value, if any, because this is the route
   * that a custom media size is requested.  Then we favour using the media size
   * enumeration, if any, because this is the route that the printer default
   * media size will be specified.  Finally we use any media size name string
   * as, in the absence of the other two, this is likely to be the way that a
   * new media size can be specified in a job and then used, if available, on a
   * printer.
   */

  if ((requested_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_XY_VALUE) &&
      ((!(previous_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_XY_VALUE)) ||
       (page_dev_changes) ||
       (requested_page_device->media_size_xy.x != previous_page_device->media_size_xy.x) ||
       (requested_page_device->media_size_xy.y != previous_page_device->media_size_xy.y)))
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCL5PageSize -1/PCL5PageLength -1/PCLXLPageSize[%f %f]",
                              requested_page_device->media_size_xy.x,
                              requested_page_device->media_size_xy.y);
    page_dev_changes++;
  }
  else if ((requested_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_ENUM_VALUE) &&
           ((!(previous_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_ENUM_VALUE)) ||
            (page_dev_changes) ||
            (requested_page_device->media_size != previous_page_device->media_size)))
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCL5PageSize -1/PCL5PageLength -1/PCLXLPageSize %d",
                              requested_page_device->media_size);
    page_dev_changes++;
  }
  else if ((requested_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_NAME_VALUE) &&
           ((!(previous_page_device->media_size_value_type & PCLXL_MEDIA_SIZE_NAME_VALUE)) ||
            (page_dev_changes) ||
            (strcmp((char*) previous_page_device->media_size_name,
                    (char*) requested_page_device->media_size_name))))
  {
    ps_string_len += swncopyf(&ps_string[ps_string_len],
                              CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                              (uint8*)"/PCL5PageSize -1/PCL5PageLength -1/PCLXLPageSize(%s)",
                              requested_page_device->media_size_name);
    page_dev_changes++;
  }
  else if ( !requested_page_device->media_size_value_type )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("No valid media size requested (media_size_value_type = 0x%02x)",
                                requested_page_device->media_size_value_type));

    return FALSE;
  }

#ifdef DEBUG_BUILD

  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: Using default media size or same media size as previous page"));
  }

#endif

  if ( page_dev_changes > 0 )
  {
#define PCLXL_SPD_TRAILER ">>setpagedevice"
    int32 written;
    written = swncopyf(&ps_string[ps_string_len],
                       CAST_SIZET_TO_INT32(sizeof(ps_string) - ps_string_len),
                       (uint8*)PCLXL_SPD_TRAILER);
    HQASSERT(written == CAST_SIZET_TO_INT32(strlen(PCLXL_SPD_TRAILER)),
             "setpagedevice string buffer overflow");
    ps_string_len += written;

    /* Sanitize CTM after setpagedevice to ensure sufficient accuracy in CTMs. */
    return (pclxl_ps_run_ps_string(pclxl_context, ps_string, ps_string_len) &&
            pclxl_ctm_sanitize_default_matrix());
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: There were no Postscript page device changes required because this PCLXL page's media details are exactly the same as the last page's media details"));

  return TRUE;
}

Bool
pclxl_ps_set_page_scaling(PCLXL_CONTEXT pclxl_context,
                          PCLXL_SysVal scale_x,
                          PCLXL_SysVal scale_y)
{
  uint8 ps_string[64];
  int32 ps_string_len;

  ps_string_len = swncopyf(ps_string,
                           CAST_SIZET_TO_INT32(sizeof(ps_string)),
                           (uint8*)"<</Scaling[%f -%f]>>setpagedevice",
                           scale_x, scale_y);

  return pclxl_ps_run_ps_string(pclxl_context, ps_string, ps_string_len);
}


Bool
pclxl_ps_showpage(PCLXL_CONTEXT pclxl_context,
                  uint32 page_copies)
{
  ps_context_t* pscontext;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context");
  pscontext = pclxl_context->corecontext->pscontext;
  HQASSERT(pscontext != NULL, "No PostScript context");

  clear_last_font();

  oInteger(inewobj) = page_copies;
  return(fast_insert_hash_name(&userdict, NAME_copies, &inewobj) && showpage_(pscontext));
}


static Bool
pclxl_ps_push_integer(PCLXL_CONTEXT pclxl_context,
                      int32 integer)
{
  OBJECT o_integer = OBJECT_NOTVM_NULL;

  object_store_integer(&o_integer, integer);

  if ( !push(&o_integer, &operandstack) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to push %d onto Postscript operand stack",
                                integer));

    return FALSE;
  }
  else
  {
    return TRUE;
  }
}

static Bool
pclxl_ps_push_sysval(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal sysval)
{
  OBJECT o_sysval = OBJECT_NOTVM_NULL;

  object_store_real(&o_sysval, (USERVALUE) sysval);

  if ( !push(&o_sysval, &operandstack) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to push %f onto Postscript operand stack",
                                sysval));

    return FALSE;
  }
  else
  {
    return TRUE;
  }
}

Bool
pclxl_ps_rotate(PCLXL_CONTEXT pclxl_context,
                PCLXL_SysVal rotation_degrees)
{
  UNUSED_PARAM(PCLXL_CONTEXT, pclxl_context) ;

  if ( rotation_degrees != 0.0 ) {
    OMATRIX matrix ;
    matrix_set_rotation(&matrix, rotation_degrees) ;
    gs_modifyctm(&matrix) ;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: rotate(degrees = %f)", rotation_degrees));

  return TRUE;
}

Bool
pclxl_ps_scale(PCLXL_CONTEXT pclxl_context,
               PCLXL_SysVal scale_x,
               PCLXL_SysVal scale_y)
{
  UNUSED_PARAM(PCLXL_CONTEXT, pclxl_context) ;

  if ( scale_x != 1.0 || scale_y != 1.0 ) {
    OMATRIX matrix ;
    matrix_set_scale(&matrix, scale_x, scale_y) ;
    gs_modifyctm(&matrix) ;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: scale(scale_x = %f, scale_y = %f)", scale_x, scale_y));

  return TRUE;
}

Bool
pclxl_ps_translate(PCLXL_CONTEXT pclxl_context,
                   PCLXL_SysVal translate_x,
                   PCLXL_SysVal translate_y)
{
  UNUSED_PARAM(PCLXL_CONTEXT, pclxl_context);

  gs_translatectm(translate_x, translate_y);

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: translate(translate_x = %f, translate_y = %f)", translate_x, translate_y));

  return TRUE;
}

/*
 * pclxl_ps_is_matrix_invertible() takes a matrix and
 * finds out if it is capable of being inverted
 */

Bool
pclxl_ps_is_matrix_invertible(OMATRIX* matrix)
{
  PCLXL_SysVal matrix_determinant = ((MATRIX_00(matrix) * MATRIX_11(matrix)) - (MATRIX_10(matrix) * MATRIX_01(matrix)));
  Bool is_invertible = (matrix_determinant != 0.0);

#ifdef DEBUG_BUILD

  if ( !is_invertible )
  {
    PCLXL_DEBUG((PCLXL_DEBUG_PAGE_CTM | PCLXL_DEBUG_CURSOR_POSITION),
                ("CTM ((%f, %f)(%f, %f)(%f, %f)) is NOT invertible",
                 MATRIX_00(matrix), MATRIX_01(matrix),
                 MATRIX_10(matrix), MATRIX_11(matrix),
                 MATRIX_20(matrix), MATRIX_21(matrix)));
  }

#endif

  return is_invertible;
}

Bool
pclxl_ps_get_current_point(PCLXL_SysVal_XY* p_xy)
{
  HQASSERT((p_xy != NULL), "Cannot get current (Postscript) position into a NULL XY location");

  return ((CurrentPoint != NULL) &&
          pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) &&
          gs_currentpoint(&thePathInfo(*gstateptr), &p_xy->x, &p_xy->y));
}

Bool
pclxl_ps_transform_point(PCLXL_SysVal_XY* existing_point,
                         OMATRIX*         existing_ctm,
                         OMATRIX*         new_ctm,
                         PCLXL_SysVal_XY* new_point)
{
  PCLXL_SysVal new_x;
  PCLXL_SysVal new_y;


  if ( pclxl_ps_is_matrix_invertible(new_ctm) )
  {
    OMATRIX inverse_new_ctm;
    OMATRIX transform_matrix;

    matrix_inverse(new_ctm, &inverse_new_ctm);

    matrix_mult(existing_ctm, &inverse_new_ctm, &transform_matrix);

    MATRIX_TRANSFORM_XY(existing_point->x,
                        existing_point->y,
                        new_x,
                        new_y,
                        &transform_matrix);

    if ( new_point )
    {
      new_point->x = new_x;
      new_point->y = new_y;
    }

    return TRUE;
  }
  else
  {
    /*
     * The new transformation matrix
     * is "singular" (a.k.a. "degenerate")
     * and so does not have an inverse
     *
     * This is probably because we have a page scale (factor)
     * of zero in either or both of the X and Y directions
     *
     * Since we cannot transform this point
     * we will arbitarily set the new point to be (0,0)
     * and return TRUE here.
     */

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("Transform point (%f,%f) using new CTM ((%f, %f)(%f, %f)(%f, %f)) is not possible because the new CTM is not invertible",
                 existing_point->x, existing_point->y,
                 MATRIX_00(new_ctm), MATRIX_01(new_ctm),
                 MATRIX_10(new_ctm), MATRIX_11(new_ctm),
                 MATRIX_20(new_ctm), MATRIX_21(new_ctm)));

    if ( new_point )
    {
      new_point->x = 0.0;
      new_point->y = 0.0;
    }

    return TRUE;
  }
}

Bool
pclxl_ps_set_current_ctm(OMATRIX* p_ctm)
{
  HQASSERT((p_ctm != NULL), "Cannot set Current (Postscript) Transformation Matrix (CTM) into a NULL OMATRIX location");

  if ( gs_setctm(p_ctm, FALSE) )
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("Set CTM ((%f, %f)(%f, %f)(%f, %f))%s",
                 MATRIX_00(p_ctm), MATRIX_01(p_ctm),
                 MATRIX_10(p_ctm), MATRIX_11(p_ctm),
                 MATRIX_20(p_ctm), MATRIX_21(p_ctm),
                 (pclxl_ps_is_matrix_invertible(p_ctm) ? "" : " which is NOT invertible")));

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

Bool
pclxl_ps_moveto(PCLXL_CONTEXT pclxl_context,
                PCLXL_SysVal x,
                PCLXL_SysVal y)
{
  SYSTEMVALUE args[2] ;

  args[0] = x ;
  args[1] = y ;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !gs_moveto(TRUE, args, &thePathInfo(*gstateptr)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to moveto(x = %f, y = %f)",
                                x,
                                y));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: moveto(x = %f, y = %f)", x, y));

    return TRUE;
  }
}

Bool
pclxl_ps_moveif(PCLXL_CONTEXT pclxl_context,
                PCLXL_SysVal x,
                PCLXL_SysVal y)
{
  PCLXL_SysVal_XY current_point_xy;

#ifndef FEPSILON
#define PCLXL_POINT_EPSILON (0.01)
#else
#define PCLXL_POINT_EPSILON FEPSILON
#endif

  if ( (!pclxl_ps_get_current_point(&current_point_xy)) ||
       (fabs(current_point_xy.x - x) > PCLXL_POINT_EPSILON) ||
       (fabs(current_point_xy.y - y) > PCLXL_POINT_EPSILON) )
  {
    /*
     * There is either no current point
     * OR the requested position is not the same as the current position
     *
     * In both cases we *need* to do a moveto this new requested position
     *
     * This is used when drawing arc (quadrants)
     * and when plotting characters
     */

    return pclxl_ps_moveto(pclxl_context, x, y);
  }
  else
  {
    /*
     * We're already at the requested point
     * So we can return TRUE without further ado
     */

    return TRUE;
  }
}

Bool
pclxl_ps_lineto(PCLXL_CONTEXT pclxl_context,
                PCLXL_SysVal x,
                PCLXL_SysVal y)
{
  SYSTEMVALUE args[2] ;

  args[0] = x ;
  args[1] = y ;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !gs_lineto(TRUE, TRUE, args, &thePathInfo(*gstateptr)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to lineto(x = %f, y = %f)",
                                x,
                                y));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: lineto(x = %f, y = %f)", x, y));

    return TRUE;
  }
}

Bool
pclxl_ps_rlineto(PCLXL_CONTEXT pclxl_context,
                 PCLXL_SysVal x,
                 PCLXL_SysVal y)
{
  SYSTEMVALUE args[2] ;

  args[0] = x ;
  args[1] = y ;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !gs_lineto(FALSE, TRUE, args, &thePathInfo(*gstateptr)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to rlineto(x = %f, y = %f)",
                                x,
                                y));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: rlineto(x = %f, y = %f)", x, y));

    return TRUE;
  }
}

Bool
pclxl_ps_curveto(PCLXL_CONTEXT pclxl_context,
                 PCLXL_SysVal  control_point_1_x,
                 PCLXL_SysVal  control_point_1_y,
                 PCLXL_SysVal  control_point_2_x,
                 PCLXL_SysVal  control_point_2_y,
                 PCLXL_SysVal  curveto_x,
                 PCLXL_SysVal  curveto_y)
{
  SYSTEMVALUE args[6] ;

  args[0] = control_point_1_x ;
  args[1] = control_point_1_y ;
  args[2] = control_point_2_x ;
  args[3] = control_point_2_y ;
  args[4] = curveto_x ;
  args[5] = curveto_y ;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !gs_curveto(TRUE, TRUE, args, &thePathInfo(*gstateptr)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to curveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                                control_point_1_x, control_point_1_y,
                                control_point_2_x, control_point_2_y,
                                curveto_x, curveto_y));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: curveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                 control_point_1_x, control_point_1_y,
                 control_point_2_x, control_point_2_y,
                 curveto_x, curveto_y));

    return TRUE;
  }
}

Bool
pclxl_ps_rcurveto(PCLXL_CONTEXT pclxl_context,
                  PCLXL_SysVal  rcontrol_point_1_x,
                  PCLXL_SysVal  rcontrol_point_1_y,
                  PCLXL_SysVal  rcontrol_point_2_x,
                  PCLXL_SysVal  rcontrol_point_2_y,
                  PCLXL_SysVal  rcurveto_x,
                  PCLXL_SysVal  rcurveto_y)
{
  SYSTEMVALUE args[6];

  args[0] = rcontrol_point_1_x;
  args[1] = rcontrol_point_1_y;
  args[2] = rcontrol_point_2_x;
  args[3] = rcontrol_point_2_y;
  args[4] = rcurveto_x;
  args[5] = rcurveto_y;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !gs_curveto(FALSE, TRUE, args, &thePathInfo(*gstateptr)) )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to rcurveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                                rcontrol_point_1_x, rcontrol_point_1_y,
                                rcontrol_point_2_x, rcontrol_point_2_y,
                                rcurveto_x, rcurveto_y));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: rcurveto(cp1_x = %f, cp1_y = %f, cp2_x = %f, cp2_y = %f, curveto_x = %f, curveto_y = %f)",
                 rcontrol_point_1_x, rcontrol_point_1_y,
                 rcontrol_point_2_x, rcontrol_point_2_y,
                 rcurveto_x, rcurveto_y));

    return TRUE;
  }
}

static Bool
pclxl_ps_plot_notdef(char_selector_t* char_selector,
                     int32            char_type,
                     int32            char_count,
                     FVECTOR*         advance,
                     void*            char_data)
{
  char_selector_t selector_copy;

  UNUSED_PARAM(void*, char_data);

  HQASSERT(char_selector, "No char selector for PCLXL notdef character");
  HQASSERT((char_selector->cid != 0), "PCLXL notdef character itself not defined");

  selector_copy = *char_selector;
  selector_copy.cid = 0;

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  return plotchar(&selector_copy, char_type, char_count, NULL, NULL, advance, CHAR_NORMAL);
}

Bool
pclxl_ps_plot_char(PCLXL_CONTEXT pclxl_context,
                   uint16 character_code,
                   Bool outline_char_path)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  FVECTOR advance;
  char_selector_t char_selector;
  Bool result = TRUE;

  if ( outline_char_path && !init_charpath(TRUE) ) {
    /* We are attempting to draw an outline character path but we have failed to
     * initialize the char path
     */
    (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                               ("Failed to init_charpath() when trying to draw an outline character path"));
    return FALSE;
  }

  char_selector.name = NULL;
  char_selector.cid = character_code;

  if (!outline_char_path) {
    non_gs_state->setg_required = 0 ;
  }

  HQASSERT(pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)),
           "CTM is non-invertable. This should have been \"guarded against\" in the higher level PCLXL functions to inhibit this lower level function even being called");

  if ( /* pclxl_ps_is_matrix_invertible(&thegsPageCTM(*gstateptr)) && */
       !plotchar(&char_selector,
                 (outline_char_path ? DOCHARPATH : DOSHOW),
                 1, /* char count always 1, because we always plot 1 character at a time */
                 pclxl_ps_plot_notdef, NULL, &advance, CHAR_NORMAL) ) {
    result = FALSE;
    (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                               (((character_code > 0xff)
                                 ? "Failed to plotchar(cid = 0x%04x%s)"
                                 : (isprint(character_code)
                                    ? "Failed to plotchar(cid = '%c'%s)"
                                    : "Failed to plotchar(cid = 0x%02x%s)")),
                                char_selector.cid, (outline_char_path ? " (Outline)" : "")));
  }

  if ( outline_char_path ) {
    /* Always end the path regardless of earlier failures. */
    if (! end_charpath(TRUE) ) {
      result = FALSE;
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to end_charpath() when trying to draw an outline character path"));
    }
  }

  PCLXL_DEBUG(PCLXL_DEBUG_PLOTCHAR,
              (((character_code > 0xff)
                ? "PS: plotchar(cid = 0x%04x%s)"
                : (isprint(char_selector.cid)
                   ? "PS: plotchar(cid = '%c'%s)"
                   : "PS: plotchar(cid = 0x%02x%s)")),
               char_selector.cid, (outline_char_path ? " (Outline)" : "")));

  return result;
}

/* This function is only ever called for the error page, so it will only ever be
 * used with the lineprinter font with fixed metrics of 16.67cpi.  XL default
 * UoM is 600.
 */
Bool
pclxl_ps_show_string(PCLXL_CONTEXT pclxl_context,
                     PCLXL_SysVal x,
                     PCLXL_SysVal y,
                     uint8* string,
                     PCLXL_SysVal* x_advance)
{
  /* Flush chars here as we are about to do a setg. */
  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return(FALSE);
  }

  if ( !pclxl_ps_moveif(pclxl_context, x, y) ) {
    return FALSE;
  }
  textContextEnter();
  if ( !DEVICE_SETG(pclxl_context->corecontext->page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
    textContextExit();
    return(FALSE);
  }
  while ( *string ) {
    if ( !pclxl_ps_plot_char(pclxl_context, *string++, FALSE) ) {
      textContextExit();
      return FALSE;
    }
    x += 600.0/(16.0 + 2.0/3.0);
    if ( !pclxl_ps_moveto(pclxl_context, x, y) ) {
      textContextExit();
      return FALSE;
    }
  }

  if ( x_advance != NULL ) {
    *x_advance = x;
  }
  textContextExit();
  return TRUE;
}

void
pclxl_free_clip_record(PCLXL_GRAPHICS_STATE graphics_state)
{
  gs_freecliprec(&graphics_state->clip_record);
}

void
pclxl_save_clip_record(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;

  pclxl_free_clip_record(graphics_state);

  if ( (graphics_state->clip_record = theClipRecord(thegsPageClip(*gstateptr))) != NULL )
  {
    gs_reservecliprec(graphics_state->clip_record);

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: Saved current Postscript clip path into PCLXL graphics state"));
  }
}

Bool
pclxl_restore_clip_record(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  ps_context_t* pscontext;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext;
  HQASSERT(pscontext != NULL, "No PostScript context");

  if ( !graphics_state->clip_record )
  {
    return TRUE;
  }
  else if ( !cliprestore_(pscontext) ||
            !clipsave_(pscontext) )
  {
    return FALSE;
  }
  else if ( !gs_addclip(graphics_state->clip_record->cliptype,
                        &theClipPath(*graphics_state->clip_record),
                        TRUE) )
  {
   (void) PCLXL_ERROR_HANDLER(pclxl_context,
                              PCLXL_SS_KERNEL,
                              PCLXL_INTERNAL_ERROR,
                              ("Failed to restore clip path to gstate"));

    return FALSE;
  }
  else
  {
    pclxl_free_clip_record(graphics_state);

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: Restored PCLXL clip path into Postscript gstate"));

    return TRUE;
  }
}

/**
 * pclxl_ps_set_page_clip() releases any existing clip path
 * and then takes the current media dimensions
 * and the supplied PCL5 CTM and PCL5 clip offsets from the edge of the physical page (i.e. borders)
 * to calculate a clip rectangle (defined by a corner coordinate and a width and height)
 * which is then installed as the new clip path
 */

Bool
pclxl_ps_set_page_clip(PCLXL_CONTEXT pclxl_context,
                       OMATRIX* physical_page_ctm,
                       uint32 page_clip_offsets[4])
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  ps_context_t* pscontext = pclxl_context->corecontext->pscontext;

  /*
   * We now need a coordinate and a width and height based in our "physical_page_ctm"
   * (i.e. a portrait page described using a top-left origin and centipoints increasing rightwards and downwards
   * Remembering that the PCL5 page_clip_offsets are in the order:
   * from the left edge of the page, from the right edge of the page,
   * from the top edge of the page and from the bottom edge of the page
   */

  PCLXL_SysVal x = page_clip_offsets[0];
  PCLXL_SysVal y = page_clip_offsets[2];
  PCLXL_SysVal width = (current_media_details->media_size_xy.x - x - page_clip_offsets[1]);
  PCLXL_SysVal height = (current_media_details->media_size_xy.y - y - page_clip_offsets[3]);

  /*
   * We temporarily switch to using the physical_page_ctm,
   * apply this clip rectangle and then switch back to the current CTM
   */

  if ( !gs_ctop() ||
       !gs_cpush()||
       !gs_setctm(physical_page_ctm, FALSE) ||
       !pclxl_ps_push_sysval(pclxl_context, x) ||
       !pclxl_ps_push_sysval(pclxl_context, y) ||
       !pclxl_ps_push_sysval(pclxl_context, width) ||
       !pclxl_ps_push_sysval(pclxl_context, height) ||
       !rectclip_(pscontext) ||
       !gs_setctm(&graphics_state->current_ctm, FALSE) )
  {
    /*
     * Oops, we seem to have failed to install a rectangular clip region
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to replace the current clip path with a PCL5 page clip"));

    return FALSE;
  }

#ifdef DEBUG_BUILD
  {
    OMATRIX* ctm = &graphics_state->current_ctm;

    PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                ("PS: rectclip(x = %f. y = %f, width = %f, height = %f) page_width = %f, page_height = %f, Physical Page CTM = ((%f, %f)(%f, %f)(%f, %f)), Current CTM = ((%f, %f)(%f, %f)(%f, %f))",
                 x, y, width, height,
                 current_media_details->media_size_xy.x, current_media_details->media_size_xy.y,
                 MATRIX_00(physical_page_ctm), MATRIX_01(physical_page_ctm),
                 MATRIX_10(physical_page_ctm), MATRIX_11(physical_page_ctm),
                 MATRIX_20(physical_page_ctm), MATRIX_21(physical_page_ctm),
                 MATRIX_00(ctm), MATRIX_01(ctm),
                 MATRIX_10(ctm), MATRIX_11(ctm),
                 MATRIX_20(ctm), MATRIX_21(ctm)));
  }
#endif

  return TRUE;
}

static Bool
pclxl_ps_is_path_closed(PATHINFO* path)
{
  Bool is_closed;

  HQASSERT(path, "Not expecting a NULL path when testing for it being closed");

  HQASSERT(path->lastline, "A non-NULL path is expected to *always* have a non-NULL lastline if we get to here");

  is_closed = (theLineType(*path->lastline) == CLOSEPATH);

  return is_closed;
}

Bool
pclxl_ps_fill(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PATHINFO* path = &gstateptr->thepath;

  /*
   * PCLXL is defined to *preserve* its path across a PaintPath operation
   * Unfortunately a fill *needs* a closed path.
   * Therefore if the path is *not* closed, we must take a copy of it
   * and the pass this copy to the fill
   */

  PATHINFO copy_of_path;
  int32 ps_fill_mode;
  mm_pool_t memory_pool = mm_pool_temp;

  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to finish the text entry"));
    return FALSE;
  }

  if ( !pclxl_ps_is_path_closed(path) )
  {
    /*
     * Use temp pool rather than a PCLXL pool for two reasons:
     * 1) The PCLXL pool doesn't have the "sac" option.
     * 2) Some of the path code uses mm_pool_temp
     *    rather than a caller-supplied pool
     */

    path = &copy_of_path;

    path_init(path);

    if ( !path_copy(path, &gstateptr->thepath, memory_pool) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to copy the path for filling"));
      return FALSE;
    }
  }

  /*
   * We now use whatever path is referenced by the local pointer "path"
   * But IFF this local pointer points at a local path_copy
   * then we must remember to free it before we return from this function
   */

  switch ( graphics_state->fill_details.fill_mode ) {
  default:
    HQFAIL("Unexpected fill mode");
    /* fall thru */
  case PCLXL_eNonZeroWinding:
    ps_fill_mode = NZFILL_TYPE;
    break;
  case PCLXL_eEvenOdd:
    ps_fill_mode = EOFILL_TYPE;
    break;
  }

  if ( !dofill(path, ps_fill_mode, GSC_FILL, FILL_NORMAL|FILL_POLYCACHE) )
  {
    if ( path == &copy_of_path )
      path_free_list(path->firstpath, memory_pool) ;

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to fill the path"));
    return FALSE;
  }

  if ( path == &copy_of_path )
    path_free_list(path->firstpath, memory_pool);

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: fill"));

  return TRUE;
}

Bool
pclxl_ps_stroke(PCLXL_CONTEXT pclxl_context)
{
  STROKE_PARAMS sparams;

  if (!finishaddchardisplay(pclxl_context->corecontext->page, 1)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to flush chars to DL"));
    return FALSE;
  }

  /*
   * Don't draw hairlines for zero-length dashes
   * or spuriously short line segments drawn at the end
   * of a dash-pattern repeat that ends in a gap
   */

  gstateptr->thepath.flags |= (PATHINFO_PCLXL | PATHINFO_IGNORE_ZERO_LEN_DASH);

  set_gstate_stroke(&sparams, &gstateptr->thepath, NULL, FALSE);

  if ( !dostroke(&sparams, GSC_FILL, STROKE_NORMAL) ) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed stroke the path"));
    return FALSE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: stroke"));

  return TRUE;
}


Bool pclxl_ps_set_colorspace_explicit(PCLXL_CONTEXT pclxl_context,
                                      PCLXL_COLOR_SPACE_DETAILS color_space_details,
                                      int32 color_type)
{
  PCLXL_ColorSpace base_color_space = color_space_details->color_space;

  if ( color_space_details->color_palette_len != 0 ) {
    /* This is an indexed color space. Create an array containing 4 values and
     * push it onto the stack (and then call "setcolorspace_"). */
    OBJECT indexed_color_space_params = OBJECT_NOTVM_NOTHING;
    uint8 bytes_per_color = 1;
    int32 max_color_index = 0;

    if ( !ps_array(&indexed_color_space_params, 4) ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to create a Postscript array (containing 4 entries) to hold the indexed color space parameters"));

      return FALSE;
    }

    /* Fill in the indexed colorspace array */
    object_store_name(&oArray(indexed_color_space_params)[0], NAME_Indexed, LITERAL);

    switch ( base_color_space ) {
    case PCLXL_eGray:
      object_store_name(&oArray(indexed_color_space_params)[1], NAME_DeviceGray, LITERAL);
      bytes_per_color = 1;
      break;

    case PCLXL_eRGB:
    case PCLXL_eSRGB:
      object_store_name(&oArray(indexed_color_space_params)[1], NAME_DeviceRGB, LITERAL);
      bytes_per_color = 3;
      break;

#ifdef DEBUG_BUILD
    case PCLXL_eCMYK:
      object_store_name(&oArray(indexed_color_space_params)[1], NAME_DeviceCMYK, LITERAL);
      bytes_per_color = 4;
      break;
#endif
    }

    max_color_index = ((color_space_details->color_palette_len / bytes_per_color) - 1);

    HQASSERT(max_color_index >= 0, "max_color_index for an indexed color space must be greater than or equal to zero");
    HQASSERT(max_color_index <= 255, "max_color_index for an indexed color space must be less than or equal to 255");

    object_store_integer(&oArray(indexed_color_space_params)[2], max_color_index);

    if ( !ps_string(&oArray(indexed_color_space_params)[3],
                    color_space_details->color_palette,
                    color_space_details->color_palette_len) ) {
      (void)PCLXL_ERROR_HANDLER(pclxl_context,
                                PCLXL_SS_KERNEL,
                                PCLXL_INTERNAL_ERROR,
                                ("Failed to set Postscript array entry 3 to point at the color palette data"));
      return FALSE;
    }

    /* Now set the indexed colorspace */
    if ( !gsc_setcustomcolorspacedirect(gstateptr->colorInfo, color_type,
                                        &indexed_color_space_params, FALSE) ) {
      (void)PCLXL_ERROR_HANDLER(pclxl_context,
                                PCLXL_SS_KERNEL,
                                PCLXL_INTERNAL_ERROR,
                                ("Failed to set indexed (/Device%s) colorspace",
                                 pclxl_color_space_name(base_color_space)));
      return FALSE;
    }
  } else {
    if (!pclxl_ps_set_colorspace_internal(base_color_space, color_type)) {
      (void)PCLXL_ERROR_HANDLER(pclxl_context,
                                PCLXL_SS_KERNEL,
                                PCLXL_INTERNAL_ERROR,
                                ("Failed to setcolorspace(/Device%s)",
                                 pclxl_color_space_name(base_color_space)));
      return FALSE;
    }
  }

  PCLXL_DEBUG((PCLXL_DEBUG_POSTSCRIPT_OPS | PCLXL_DEBUG_COLOR),
              ((color_space_details->color_palette_len ?
              "PS: setcolorspace(/Indexed /Device%s %d <...>)" :
              "PS: setcolorspace(/Device%s)"),
              pclxl_color_space_name(base_color_space),
              color_space_details->color_palette_len));

  return TRUE;
}


Bool
pclxl_ps_set_black_preservation(PCLXL_CONTEXT             pclxl_context,
                                PCLXL_COLOR_SPACE_DETAILS color_space_details,
                                Bool                      override_vector_objects_setting)
{
  /*
   * PCLXL's "SetNeutralAxis" eTonerBlack versus eProcessBlack settings
   * are mapped (where possible) into a "setinterceptcolorspace" call
   * supplying a dictionary containing a /BlackTint dictionary key/value
   * whose value it itself a dictionary containing /Picture, /Text and /Other keys
   * whose values are "false" for eTonerBlack and "true" for eProcessBlack respectively.
   *
   * We then call setinterceptcolorspace with this dictionary
   */

  uint8 ps_string[128];

  (void) swncopyf(ps_string,
                  (int32) sizeof(ps_string),
                  (uint8*) " << "
                           " /BlackTint "
                           "   << "
                           "     /Picture %s "
                           "     /Text %s "
                           "     /Other %s "
                           "   >> "
                           " >> setinterceptcolorspace ",
                  (color_space_details->raster_black_type ? "true" : "false"),
                  (color_space_details->text_black_type ? "true" : "false"),
                  ((color_space_details->vector_black_type ||
                    override_vector_objects_setting) ? "true" : "false"));

  return pclxl_ps_run_ps_string(pclxl_context, ps_string,
                                strlen((char*) ps_string));
}

/* See header for doc. */
Bool pclxl_ps_set_colorspace(PCLXL_CONTEXT context,
                             PCLXL_COLOR_SPACE_DETAILS color_space_details)
{
  return pclxl_ps_set_colorspace_explicit(context, color_space_details,
                                          GSC_FILL);
}

static Bool pclxl_pcl_update_color_source(PCLXL_CONTEXT context)
{
  PCLXL_NON_GS_STATE non_gs_state;

  if (PCL_DL_COLOR_IS_FOREGROUND != getPclForegroundSource()) {
    non_gs_state = &context->non_gs_state;
    non_gs_state->setg_required = TRUE;
  }

  if (! setPclForegroundSource(context->corecontext->page,
                               PCL_DL_COLOR_IS_FOREGROUND)) {
    (void) PCLXL_ERROR_HANDLER(context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to update PCL color source"));
    return FALSE;
  }
  return TRUE;
}

/**
 * \brief the pclxl_ps_set_color() function
 * is passed a set of PCLXL_COLOR_DETAILS
 * (either the line_style.pen_color or fill_details.brush_color)
 *
 * It takes the supplied color and sets this color
 * as the current Postscript color
 *
 * In doing so it also sets the Postscript color *space*
 * because the PCLXL colors can actually be set in two different
 * PCLXL color spaces (eGray or eRGB/eSRGB)
 */
Bool
pclxl_ps_set_color(PCLXL_CONTEXT pclxl_context,
                   PCLXL_COLOR_DETAILS  color_details,
                   Bool for_an_image)
{
  ps_context_t *pscontext ;
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  /* This must be called first as it may change the core colorspace or color */
  pclxl_ps_set_pattern(pclxl_context, color_details);

  if ( color_details->color_array_len == 0 )
  {
    PCLXL_SysVal white = 1.0;

    if ( !pclxl_ps_set_colorspace(pclxl_context,
                                  color_details->color_space_details) )
    {
      return FALSE;
    }
    else if ( !pclxl_ps_push_sysval(pclxl_context, white) )
    {
      return FALSE;
    }
    else if ( !setgray_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to setgray(gray_level = %f)",
                                  white));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setgray(%f)", white));

      return TRUE;
    }
  }
  else if ( (color_details->color_array_len == 1) &&
            (color_details->color_space_details->color_palette_len > 0) &&
            (color_details->color_space_details->color_palette != NULL) &&
            for_an_image )
  {
    /* We have a special case of an "indexed" color. Indexed palettes
     * are only installed for images.
     *
     * In this case we must *not* assume that it is a shade of gray
     * and set the color space to /DeviceGray (and call setgray).
     *
     * In this case we must call setcolor with the single color value
     */

    int32 color_index = (int) (color_details->color_array[0] * 255);

    if ( !pclxl_ps_set_colorspace(pclxl_context,
                                  color_details->color_space_details) )
    {
      return FALSE;
    }
    else if ( !pclxl_ps_push_integer(pclxl_context, color_index) )
    {
      return FALSE;
    }
    else if ( !setcolor_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to set[indexed]color(color_index = %d)",
                                  color_details->color_array_len));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG((PCLXL_DEBUG_POSTSCRIPT_OPS | PCLXL_DEBUG_COLOR),
                  ("PS: set[indexed]color(color_index = %d)", color_index));

      return TRUE;
    }
  }
  else if ( ((config_params->strict_pclxl_protocol_class) &&
             (color_details->color_space_details->color_space == PCLXL_eGray)) ||
            (color_details->color_array_len == 1) )
  {
    /* We have an interesting issue here:
     *
     * According to the PCLXL Protocol Class Specification the current
     * "color_array_len" should match the current color space.
     *
     * However it appears that both QL PCLXL FTS test "t305.bin" *and*
     * at least the hp4700n printer are pretty lax about checking this.
     */

    PCLXL_SysVal gray_level = color_details->color_array[PCLXL_GRAY_CHANNEL];

    HQASSERT((color_details->color_array_len == 1),
             "Color details primary array length should be 1 for gray colour");

    if ( !pclxl_ps_set_colorspace(pclxl_context,
                                  color_details->color_space_details) )
    {
      return FALSE;
    }
    else if ( !pclxl_ps_push_sysval(pclxl_context, gray_level) )
    {
      return FALSE;
    }
    else if ( !setgray_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to setgray(gray_level = %f)",
                                  gray_level));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("PS: setgray(gray_level = %f)", gray_level));

      return TRUE;
    }
  }
  else if ( (config_params->strict_pclxl_protocol_class &&
             ((color_details->color_space_details->color_space == PCLXL_eRGB) ||
              (color_details->color_space_details->color_space == PCLXL_eSRGB))) ||
            (color_details->color_array_len == 3) )
  {
    PCLXL_SysVal red_level = color_details->color_array[PCLXL_RED_CHANNEL];
    PCLXL_SysVal green_level = color_details->color_array[PCLXL_GREEN_CHANNEL];
    PCLXL_SysVal blue_level = color_details->color_array[PCLXL_BLUE_CHANNEL];

    HQASSERT((color_details->color_array_len == 3),
             "Color details primary array length should be 3 for [s]RGB colour");

    if ( !pclxl_ps_set_colorspace(pclxl_context,
                                  color_details->color_space_details) )
    {
      return FALSE;
    }
    else if ( (!pclxl_ps_push_sysval(pclxl_context, red_level)) ||
              (!pclxl_ps_push_sysval(pclxl_context, green_level)) ||
              (!pclxl_ps_push_sysval(pclxl_context, blue_level)) )
    {
      return FALSE;
    }
    else if ( !setrgbcolor_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to setrgbcolor(red = %f, green = %f, blue = %f)",
                                  red_level, green_level, blue_level));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("PS: setrgbcolor(red = %f, green = %f, blue = %f)",
                   red_level, green_level, blue_level));

      return TRUE;
    }
  }
#ifdef DEBUG_BUILD
  else if ( (config_params->strict_pclxl_protocol_class &&
             (color_details->color_space_details->color_space == PCLXL_eCMYK)) ||
            (color_details->color_array_len == 4) )
  {
    PCLXL_SysVal cyan_level = color_details->color_array[PCLXL_CYAN_CHANNEL];
    PCLXL_SysVal magenta_level = color_details->color_array[PCLXL_MAGENTA_CHANNEL];
    PCLXL_SysVal yellow_level = color_details->color_array[PCLXL_YELLOW_CHANNEL];
    PCLXL_SysVal black_level = color_details->color_array[PCLXL_BLACK_CHANNEL];

    HQASSERT((color_details->color_array_len == 4),
             "Color details primary array length should be 4 for CMYK colour");

    if ( !pclxl_ps_set_colorspace(pclxl_context,
                                  color_details->color_space_details) )
    {
      return FALSE;
    }
    else if ( (!pclxl_ps_push_sysval(pclxl_context, cyan_level)) ||
              (!pclxl_ps_push_sysval(pclxl_context, magenta_level)) ||
              (!pclxl_ps_push_sysval(pclxl_context, yellow_level)) ||
              (!pclxl_ps_push_sysval(pclxl_context, black_level)) )
    {
      return FALSE;
    }
    else if ( !setcmykcolor_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to setcmykcolor(cyan = %f, magenta = %f, yellow = %f, black = %f)",
                                  cyan_level, magenta_level, yellow_level, black_level));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("PS: setcmykcolor(cyan = %f, magenta = %f, yellow = %f, blue = %f)",
                   cyan_level, magenta_level, yellow_level, black_level));

      return TRUE;
    }
  }
#endif
  else
  {
    uint32 i;
    /* Note that we are explicitly not going to "mess with" the
     * current color *space* settings because we have no clue what to
     * do for the best here because the number of color channel levels
     * does not match any recognized color space
     */

    for ( i = 0 ;
          i < color_details->color_array_len ;
          i++ )
    {
      PCLXL_SysVal color_level = color_details->color_array[i];

      if ( !pclxl_ps_push_sysval(pclxl_context, color_level) )
      {
        return FALSE;
      }
    }

    if ( !setcolor_(pscontext) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to setcolor(%d color levels)",
                                  color_details->color_array_len));

      return FALSE;
    }
    else
    {
      if (! pclxl_pcl_update_color_source(pclxl_context))
        return FALSE;

      PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS,
                  ("PS: setcolor(%d color levels)",
                   color_details->color_array_len));

      return TRUE;
    }
  }

  /*NOTREACHED*/
}

/* See header for doc. */
Bool pclxl_ps_set_shade(USERVALUE shade)
{
  if (! pclxl_ps_set_colorspace_internal(PCLXL_eGray, GSC_FILL))
    return FALSE ;
  return gsc_setcolordirect(gstateptr->colorInfo, GSC_FILL, &shade);
}

/** Set the passed color in the core.
 */
static Bool pclxl_ps_set_color_internal(USERVALUE *color)
{
  return gsc_setcolordirect(gstateptr->colorInfo, GSC_FILL, color);
}

/** Set the passed colorspace in the core.
 */
static Bool pclxl_ps_set_colorspace_internal(PCLXL_ColorSpace color_space,
                                             int32 color_type)
{
  COLORSPACE_ID colorspace_id = SPACE_notset;

  switch ( color_space )
  {
    case PCLXL_eGray:
      colorspace_id = SPACE_DeviceGray;
      break;

    case PCLXL_eRGB:
    case PCLXL_eSRGB:
      colorspace_id = SPACE_DeviceRGB;
      break;

#ifdef DEBUG_BUILD
    case PCLXL_eCMYK:
      colorspace_id = SPACE_DeviceCMYK;
      break;
#endif

    default:
      HQFAIL("Unexpected colorspace");
      break;
  }

  if (gsc_getcolorspace(gstateptr->colorInfo, color_type) == colorspace_id )
    return TRUE;
  return gsc_setcolorspacedirect(gstateptr->colorInfo, color_type, colorspace_id);
}

/**
 * Update the cached palette to match the current PCLXL palette.
 * N.B. This may change the core colorspace and/or color.
 *      It is up to clients to restore this if required.
 */
static
Bool pclxl_update_cached_palette(PCLXL_CONTEXT pclxl_context,
                                 PCLXL_COLOR_DETAILS color_details)
{
  PCLXL_ColorSpace color_space = color_details->color_space_details->color_space ;
  uint8* color_palette = color_details->color_space_details->color_palette;
  ps_context_t *pscontext ;
  DL_STATE *page;
  uint32 index;
  int32 j, num_components;
  USERVALUE color[4];

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  page = pclxl_context->corecontext->page;

  HQASSERT(color_palette != NULL, "Color palette is Null");
  num_components = pclxl_color_space_num_components(color_space);

  cached_palette.uid++;
  cached_palette.size = color_details->color_space_details->color_palette_len / num_components;

  /* N.B. It has not been investigated what should happen if the palette does
   *      not have the expected number of entries for the colorspace.
   *      It is likely that the reference printer uses some kind of modulo
   *      calculation, so this may be too strict.
   */
  if (! (cached_palette.size > 0)) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_IMAGE,
                               PCLXL_PALETTE_UNDEFINED,
                               ("Palette does not meet minimum size requirements"));
    return FALSE ;
  }

  HQASSERT(cached_palette.size <= 256, "Exceeded max size of cached palette");

  /* Set the base colorspace */
  if (! pclxl_ps_set_colorspace_internal(color_space, GSC_FILL)){
    (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                                ("Unable to set colorspace in core."));
    return FALSE;
  }

  /* Grab the required colorvalues from the palette */
  for (index = 0; index < cached_palette.size; index++) {
    for (j = 0; j < num_components; j++) {
      color[j] = (USERVALUE) (color_palette[(index * num_components) + j] / 255.0f );
    }

    if (! pclxl_ps_set_color_internal(color)) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                                  ("Unable to set color in core."));
      return FALSE;
    }

    if (! pclPackCurrentColor(page, &cached_palette.colors[index])) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INTERNAL_ERROR,
                                  ("Unable to pack palette color for core."));
      return FALSE;
    }
  }

  return TRUE;
}


Bool
pclxl_ps_set_pattern(PCLXL_CONTEXT pclxl_context,
                     PCLXL_COLOR_DETAILS  color_details)
{
  if ( color_details->pattern_enabled ) {
    PclXLPattern *pattern = pclxl_pattern_find(pclxl_context,
                                               color_details->pattern_id);
    /*
     * It appears that pattern angle is determined by
     * the page angle (and orientation?) that was in effect
     * when the pattern was defined (and captured in color_details->pattern_angle)
     * *AND* by the orientation but *not* page rotation of the current page.
     */

    PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
    PCLXL_MEDIA_DETAILS current_media_details = &non_gs_state->current_media_details;
    static int32 orientation_angles[] = { 0, 90, 180, -90, 0 };
    static int32 leading_edge_angles[] = { 0, -90, 180, 90 };

    int32 pattern_angle = (orientation_angles[current_media_details->orientation] +
                           color_details->pattern_angle +
                           leading_edge_angles[current_media_details->leading_edge]);

    if (! pattern->direct) {
      if (! pclxl_update_cached_palette(pclxl_context, color_details))
        return FALSE;
    }

    setPclXLPattern(pattern,
                    pattern_angle,
                    &color_details->pattern_scale,
                    &color_details->pattern_origin,
                    &color_details->destination_size,
                    (pattern->direct? NULL : &cached_palette));
  }
  else {
    setPclXLPattern(NULL, 0, NULL, NULL, NULL, NULL);
  }

  return TRUE;
}

Bool
pclxl_ps_set_line_width(PCLXL_SysVal line_width)
{
  gstateptr->thestyle.linewidth = (USERVALUE) line_width;

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setlinewidth(line_width = %f)", line_width));

  return TRUE;
}

Bool
pclxl_ps_set_miter_limit(PCLXL_CONTEXT pclxl_context,
                         PCLXL_SysVal miter_limit)
{
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

  /*
   * PCLXL allows the "MiterLimit" (a.k.a. "MiterLength")
   * to range from 0 upwards
   * (I am not quite sure what values less than 1 are supposed to mean)
   *
   * Postscript supports miter limits in the range 1 upwards
   *
   * This means that we cannot allow PCLXL miter limits in the range
   * 0.0 to 0.999999. to be passed through to Postscript
   *
   * In fact a number of PCLXL printers including the hp4700n
   * produce an error if supplied with a miter limit less than 1
   */

#define PCLXL_MINIMUM_MITER_LIMIT 1.0

  if ( miter_limit < PCLXL_MINIMUM_MITER_LIMIT )
  {
    if ( config_params->strict_pclxl_protocol_class )
    {
      /*
       * In order to "quietly" accept a PCLXL miter limit
       * in the range 0.0 to 0.999999
       * we will actually "clamp" the value that is passed
       * to Postscript to a minimum value of 1.0
       */

      miter_limit = PCLXL_MINIMUM_MITER_LIMIT;
    }
    else
    {
      /*
       * If we are not being "strict" about compliance with
       * the PCLXL Protocol Class Spec.
       *
       * But are instead attempting to emulate the behaviour
       * of at least our reference "hp4700n" printer *exactly*
       * then we should actually raise an error "GE Issued Warning: 6") here

      (void) PCLXL_WARNING_HANDLER(pclxl_context,
                                   PCLXL_SS_KERNEL,
                                   6, / * No, don't ask why this is a hard-coded value "6".
                                        * It is because the hp4700n also returns a PCLXL Error Code of "6"
                                        * and this particular code is *not* decoded into a human-readable error name
                                        * /
                                   ("A miter limit/length of less than %f is not supported",
                                    PCLXL_MINIMUM_MITER_LIMIT));

       * However, despite the very non-standard "GE Issued Warning: 6",
       * the hp4700n also appears to map the supplied miter limit/length
       * of 0.0 (or indeed anything less than 1.0) into
       * a default miter limit/length of 10.0
       *
       * Therefore we are going to exhibit the same apparent behaviour
       * But *without* the non-standard warning message
       */

      miter_limit = 10.0;
    }
  }

  gstateptr->thestyle.miterlimit = (USERVALUE)miter_limit;

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setmiterlimit(miter_limit = %f)", miter_limit));

  return TRUE;
}

/*
 * Keep track of the last font that we have requested that the core font
 * machinery should use. Because the requests are purely linear, and there
 * is no save/restore, there is nothing to be gained in requesting the
 * same font over and over again. And in fact, it adds a few percent
 * performance overhead. So filter out repeated identical sequential font
 * calls to avoid this performance overhead. Just need to reset the previous
 * font to NULL after a showpage, and all should be well.
 */
static struct {
  OMATRIX mat;
  uint8 name[32];
  size_t name_len;
} last6font;


void clear_last_font()
{
  last6font.name_len = 0;
}


/* Is this exactly the same font as we asked for before?
 * Record the font name and matrix ready for the next comparison.
 */
static Bool same_font_as_before(PCLXL_FONT_DETAILS font, OMATRIX *mat)
{
  Bool same = (last6font.name_len != 0);
  size_t i;

  if ( last6font.name_len != font->ps_font_name_len )
    last6font.name_len = font->ps_font_name_len, same = FALSE;
  if ( last6font.name_len >= sizeof(last6font.name) )
    last6font.name_len = 0;
  for ( i = 0; i < last6font.name_len; i++ )
    if ( last6font.name[i] != font->ps_font_name[i] )
      last6font.name[i] = font->ps_font_name[i], same = FALSE;
  if ( !matrix_equal(&last6font.mat, mat) ) {
    MATRIX_COPY(&last6font.mat, mat);
    same = FALSE;
  }
  return same;
}

/**
 * \brief pclxl_ps_select_font() takes the current char_details/current_font
 * and constructs a current font matrix
 * and uses this matrix together with the current (Postscript) font name
 * are pushed onto the postscript operand stack
 * and then (the "C" function implementation of) the selectfont operator is called
 */

Bool
pclxl_ps_select_font(PCLXL_CONTEXT pclxl_context, Bool for_error_page)
{
  ps_context_t *pscontext ;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;
  PCLXL_FONT_DETAILS current_font = &char_details->current_font;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  OBJECT ps_font_matrix = OBJECT_NOTVM_NOTHING;

  static OBJECT wmode_key = OBJECT_NOTVM_NAME(NAME_WMode, LITERAL);
  static OBJECT wmode_default_value = OBJECT_NOTVM_INTEGER(0);
  static OBJECT vmode_key = OBJECT_NOTVM_NAME(NAME_WMode, LITERAL);
  static OBJECT vmode_default_value = OBJECT_NOTVM_INTEGER(0);

  OBJECT* o_current_font_dict = NULL;
  OBJECT* o_wmode = NULL;
  OBJECT* o_vmode = NULL;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  non_gs_state->setg_required = TRUE;

  if ( (oName(nnewobj) = cachename(current_font->ps_font_name,
                             current_font->ps_font_name_len)) == NULL )
    return FALSE;

  (void) pclxl_recalculate_font_matrix(pclxl_context, graphics_state,
                                       char_details,
                                       &char_details->font_matrix);

  if ( !same_font_as_before(current_font, &char_details->font_matrix) ) {

    if ( !ps_array(&ps_font_matrix, 6) ||
         !from_matrix(oArray(ps_font_matrix), &char_details->font_matrix,
                      oGlobalValue(ps_font_matrix)) ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to create a Postscript OBJECT(array) to hold font_matrix ((%f, %f)(%f, %f)(%f, %f)), (font_name = \"%s\", point_size = %f)",
                                  MATRIX_00(&char_details->font_matrix),
                                  MATRIX_01(&char_details->font_matrix),
                                  MATRIX_10(&char_details->font_matrix),
                                  MATRIX_11(&char_details->font_matrix),
                                  MATRIX_20(&char_details->font_matrix),
                                  MATRIX_21(&char_details->font_matrix),
                                  current_font->ps_font_name,
                                  current_font->ps_font_point_size));

      return FALSE;
    }

    if ( !push2(&nnewobj, &ps_font_matrix, &operandstack) ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to push font_name = \"%f\" and font_matrix = ((%f, %f)(%f, %f)(%f, %f)), onto Postscript operand stack. (point_size = %f)",
                                  current_font->ps_font_name,
                                  MATRIX_00(&char_details->font_matrix),
                                  MATRIX_01(&char_details->font_matrix),
                                  MATRIX_10(&char_details->font_matrix),
                                  MATRIX_11(&char_details->font_matrix),
                                  MATRIX_20(&char_details->font_matrix),
                                  MATRIX_21(&char_details->font_matrix),
                                  current_font->ps_font_point_size));

      return FALSE;
    }

    if ( !selectfont_(pscontext) ) {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Failed to \"selectfont(font_name = \"%s\", font_matrix = ((%f, %f)(%f, %f)(%f, %f)), (point_size = %f)",
                                  current_font->ps_font_name,
                                  MATRIX_00(&char_details->font_matrix),
                                  MATRIX_01(&char_details->font_matrix),
                                  MATRIX_10(&char_details->font_matrix),
                                  MATRIX_11(&char_details->font_matrix),
                                  MATRIX_20(&char_details->font_matrix),
                                  MATRIX_21(&char_details->font_matrix),
                                  current_font->ps_font_point_size));

      return FALSE;
    }
  }

  /*
   * Ok, it looks like we have successfully "selected" this font
   * But we need to do one more thing. We have to get hold of
   * the (now) current font (which is a Postscript dictionary)
   * and from it find the location of the /WMode key
   * and thus the (integer) location into which to "poke"
   * our PCLXL writing_mode or char_sub_mode[char_code]
   */

  /* Apart from for error or warnings pages we now only only ever call
   * pclxl_ps_select_font immediately before printing text.  So if we
   * haven't had any commands to change the /V or /W mode up till now
   * (actually on this page would be sufficient), and this isn't an
   * error page then we don't need to do all this.  (The reason for doing
   * this anyway if it is printing warnings is in case this selects a
   * font which is later used to print part of the job with no further
   * fontselection having taken place but with a /V or /W mode change
   * having taken place meanwhile.
   * todo This is a bit of a hack - review for trunk.
   * todo Also review the situation for passthrough.
   * todo See if this could be speeded up for when we do need to do it.
   */
  if ( for_error_page || non_gs_state->text_mode_changed ) {
    o_current_font_dict = &(theMyFont(theFontInfo(*gstateptr)));

    if ( ((((o_wmode = fast_extract_hash(o_current_font_dict, &wmode_key)) != NULL) &&
         (oType(*o_wmode) == OINTEGER)) ||
          ((insert_hash_even_if_readonly(o_current_font_dict,
                                         &wmode_key,
                                         &wmode_default_value)) &&
           ((o_wmode = fast_extract_hash(o_current_font_dict, &wmode_key)) != NULL) &&
           (oType(*o_wmode) == OINTEGER))) &&
         ((((o_vmode = fast_extract_hash(o_current_font_dict, &vmode_key)) != NULL) &&
           (oType(*o_vmode) == OINTEGER)) ||
          ((insert_hash_even_if_readonly(o_current_font_dict,
                                         &vmode_key,
                                         &vmode_default_value)) &&
           ((o_vmode = fast_extract_hash(o_current_font_dict, &vmode_key)) != NULL) &&
            (oType(*o_vmode) == OINTEGER)))
       )
    {
      /*
       * We have found (or inserted) the /WMode and /VMode keys
       * into the current font dictionary
       *
       * They are integer values so we can record
       * their original values
       * and initialize the "wmode_object" and "vmode_object"
       * to point at the locations of these key values
       */

      current_font->original_wmode = (uint8) oInteger(*o_wmode);

      current_font->wmode_object = o_wmode;

      current_font->original_vmode = (uint8) oInteger(*o_vmode);

      current_font->vmode_object = o_vmode;

      current_font->pclxl_font_state = PCLXL_FS_POSTSCRIPT_FONT_SELECTED;

      PCLXL_DEBUG((PCLXL_DEBUG_POSTSCRIPT_OPS | PCLXL_DEBUG_FONTS),
                  ("PS: selectfont(font_name = \"%s\", font_matrix = ((%f, %f)(%f, %f)(%f, %f)), wmode = %d, vmode = %d) (point_size = %f)",
                   current_font->ps_font_name,
                   MATRIX_00(&char_details->font_matrix),
                   MATRIX_01(&char_details->font_matrix),
                   MATRIX_10(&char_details->font_matrix),
                   MATRIX_11(&char_details->font_matrix),
                   MATRIX_20(&char_details->font_matrix),
                   MATRIX_21(&char_details->font_matrix),
                   current_font->original_wmode,
                   current_font->original_vmode,
                   current_font->ps_font_point_size));

      return TRUE;
    }
    else
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_INTERNAL_ERROR,
                                 ("Current font dictionary does not contain a valid /WMode key. And we could not insert a valid value"));

      return FALSE;
    }
  }
  else
    return TRUE;
}

Bool
pclxl_ps_set_char_mode(PCLXL_GRAPHICS_STATE graphics_state,
                       uint8                wmode,
                       uint8                vmode)
{
  PCLXL_FONT_DETAILS current_font = &graphics_state->char_details.current_font;
  FONTinfo* ps_font_info = &theFontInfo(*gstateptr);

  HQASSERT(current_font->pclxl_font_state == PCLXL_FS_POSTSCRIPT_FONT_SELECTED,
           "Cannot set WMode+VMode on invalid Postscript font");
  HQASSERT(current_font->wmode_object, "Cannot set new WMode into NULL wmode object");
  HQASSERT(current_font->vmode_object, "Cannot set new VMode into NULL vmode object");
  HQASSERT(oType(*current_font->wmode_object) == OINTEGER, "WMode object is not an OINTEGER object");
  HQASSERT(oType(*current_font->vmode_object) == OINTEGER, "VMode object is not an OINTEGER object");

  object_store_integer(current_font->wmode_object, wmode);
  object_store_integer(current_font->vmode_object, vmode);

  ps_font_info->wmode = (uint8) ((vmode << 1) | wmode);

  PCLXL_DEBUG((PCLXL_DEBUG_POSTSCRIPT_OPS | PCLXL_DEBUG_FONTS),
              ((((wmode == current_font->original_wmode)  &&
                 (vmode == current_font->original_vmode)) ?
                "PS: Resetting WMode to %d and VMode to %d for Postscript font \"%s\"" :
                "PS: Temporarily setting WMode to %d and VMode to %d for Postscript font \"%s\" (original WMode = %d, original VMode = %d)"),
               wmode,
               vmode,
               current_font->ps_font_name,
               current_font->original_wmode,
               current_font->original_vmode));

  return TRUE;
}

/*
 * pclxl_ps_set_rop3() takes the supplied ROP3 code
 * and what is basically a boolean switch that says whether the caller
 * is performing a "stroke" (but not the stroke of an outline character)
 * which is essentially synonymous with the caller doing something
 * using the "PenSource" colour
 *
 * IF the caller is indeed doing a "stroke" operation/using the PenSource
 * then the range of allowed ROP3 codes is "choked" down
 * to be one of a restricted subset
 * and if it is outside this subset then it is substituted by ROP3_P (240)
 * which basically makes it into the solid "Paint" color
 */

Bool pclxl_ps_set_rop3(PCLXL_CONTEXT pclxl_context, PCLXL_ROP3 rop3, Bool stroke_choke)
{
  static uint8 allowed_stroke_rops[] =
  {
    PCLXL_ROP3_0,   /* 0 */
    PCLXL_ROP3_DPa, /* 160 */
    PCLXL_ROP3_D,   /* 170 */
    PCLXL_ROP3_P,   /* 240 */
    PCLXL_ROP3_DPo, /* 250 */
    PCLXL_ROP3_1    /* 255 */
  };

  static uint8 allowed_stroke_rops_len = sizeof(allowed_stroke_rops);
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state ;

  uint8 i;
  uint8 orig_rop = getPclRop();

  for ( i = 0 ;
        ((i < allowed_stroke_rops_len) && (rop3 != allowed_stroke_rops[i])) ;
        i++ );

  if ( !stroke_choke )
  {
    setPclRop(rop3);

    PCLXL_DEBUG(PCLXL_DEBUG_PCL5_OPS,
                ("PCL5: setPcl5Rop(%d)", rop3));
  }
  else if ( i < allowed_stroke_rops_len )
  {
    setPclRop(rop3);

    PCLXL_DEBUG(PCLXL_DEBUG_PCL5_OPS,
                ("PCL5: setPcl5Rop(%d <allowed>)", rop3));
  }
  else
  {
    rop3 = PCLXL_ROP3_P;

    setPclRop(rop3);

    PCLXL_DEBUG(PCLXL_DEBUG_PCL5_OPS,
                ("PCL5: setPcl5Rop(%d <choked>)", rop3));
  }

  /*
   * pclxl_ps_set_rop() cannot actually fail
   * but it is ideally needs to be a function
   * that can be called from within an if-condition
   * specifically the ones in pclxl_paint_path()
   * which then allows the setting of the rop
   * to be short-circuited when there is a null pen or brush source
   */

  if (getPclRop() != orig_rop)
    non_gs_state->setg_required = TRUE;

  return TRUE;
}

void pclxl_pcl_set_source_transparency(PCLXL_CONTEXT pclxl_context, Bool transparent)
{
  PCLXL_NON_GS_STATE non_gs_state ;

  if (isPclSourceTransparent() != transparent) {
     non_gs_state = &pclxl_context->non_gs_state ;
     non_gs_state->setg_required = TRUE ;
  }

  setPclSourceTransparent(transparent);

  PCLXL_DEBUG(PCLXL_DEBUG_PCL5_OPS,
              ("PCL5: setPcl5SourceTransparent(%s, FALSE)",
               (transparent ? "TRUE" : "FALSE")));
}

void pclxl_pcl_set_paint_transparency(PCLXL_CONTEXT pclxl_context, Bool transparent)
{
  PCLXL_NON_GS_STATE non_gs_state ;

  if (isPclPatternTransparent() != transparent) {
     non_gs_state = &pclxl_context->non_gs_state ;
     non_gs_state->setg_required = TRUE ;
  }

  /*
   * Doh! we still have yet another confusion between "Paint" and "Pattern"
   * So we can go ahead and call setPclPatternTransparent()
   */
  setPclPatternTransparent(transparent);
}

void
pclxl_pcl_grestore(PCLXL_CONTEXT pclxl_context, PCLXL_GRAPHICS_STATE graphics_state)
{
  pclxl_ps_set_rop3(pclxl_context, graphics_state->ROP3, FALSE);
  pclxl_pcl_set_source_transparency(pclxl_context,
    graphics_state->source_tx_mode == PCLXL_eTransparent);
  pclxl_pcl_set_paint_transparency(pclxl_context,
    graphics_state->paint_tx_mode == PCLXL_eTransparent);
}

Bool
pclxl_ps_clip(PCLXL_CONTEXT pclxl_context, Bool ext_clip, Bool eo_clip)
{
  int32 clip_flags = 0;

  if ( ext_clip )
    clip_flags = clip_flags | CLIPINVERT;

  clip_flags = clip_flags | ( eo_clip ? EOFILL_TYPE : NZFILL_TYPE );

  if ( ! gs_addclip(clip_flags, &thePathInfo( *gstateptr ), TRUE ) )
  {
   (void) PCLXL_ERROR_HANDLER(pclxl_context,
                              PCLXL_SS_KERNEL,
                              PCLXL_INTERNAL_ERROR,
                              ("Failed to add clip path to gstate"));

   return FALSE;
  }
  else
  {

    PCLXL_DEBUG(PCLXL_DEBUG_PCL5_OPS,
                ("Add clip: (%s,%s)",
                 (ext_clip ? "External" : "Internal"),
                 (eo_clip ? "EO" : "NZW")));

    return TRUE;
  }

  /* NOTREACHED */
}

Bool
pclxl_ps_set_line_cap(PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_LineCap linecap)
{
  UNUSED_PARAM(PCLXL_GRAPHICS_STATE, graphics_state);

  switch ( linecap ) {
  default:
    HQFAIL("Unexpected line cap");
    /* fall thru */
  case PCLXL_eButtCap:
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = BUTT_CAP;
    break;
  case PCLXL_eRoundCap:
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = ROUND_CAP;
    break;
  case PCLXL_eTriangleCap:
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = TRIANGLE_CAP;
    break;
  case PCLXL_eSquareCap:
    gstateptr->thestyle.startlinecap =
      gstateptr->thestyle.endlinecap =
      gstateptr->thestyle.dashlinecap = SQUARE_CAP;
    break;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_POSTSCRIPT_OPS, ("PS: setlinecap"));

  return TRUE;
}

static char szInvokePCLXLScreeningCmd[512];

static char *szInvokePCLXLScreening =
"/PCLXLHalftone /ProcSet resourcestatus\
{\
  pop pop\
  /PCLXLHalftone /ProcSet findresource\
  dup type /dicttype eq\
  {\
    dup\
    /PCLXLSetHalftone known\
    {\
      begin\
      mark\
      << /Text %s\
        /Linework %s\
        /Picture %s\
      >>\
      PCLXLSetHalftone\
      cleartomark\
      end\
    }\
    {pop} ifelse\
  }\
  {\
    pop\
  } ifelse\
} if";

static char *pclxl_screening_to_string(PCLXL_HalftoneMethod halftone)
{
  switch ( halftone )
  {
    case PCLXL_eHighLPI:
      return "/HighLPI";
      break;

    case PCLXL_eMediumLPI:
      return "/MediumLPI";
      break;

    case PCLXL_eLowLPI:
      return "/LowLPI";
      break;

    default:
      break;
  }
  return "/UnknownLPI";
}

/**
 * PCLXL requires the use of a PCLXL_Screening proc set to translate
 * the rather abstract descriptions of PCLXL screening into
 * rip commands.
 */
Bool
pclxl_ps_object_halftone(PCLXL_GRAPHICS_STATE graphics_state)
{
  *szInvokePCLXLScreeningCmd = '\0';

  swncopyf(
      (uint8*)szInvokePCLXLScreeningCmd,
      sizeof(szInvokePCLXLScreeningCmd),
      (uint8*)szInvokePCLXLScreening,
      (uint8*)pclxl_screening_to_string(graphics_state->halftone_method.text),
      (uint8*)pclxl_screening_to_string(graphics_state->halftone_method.vector),
      (uint8*)pclxl_screening_to_string(graphics_state->halftone_method.raster));

  return run_ps_string((uint8*)szInvokePCLXLScreeningCmd);
}

static char szInvokePCLXLDitherPhase[] =
"/PCLXLHalftone /ProcSet 2 copy resourcestatus\
  {pop pop findresource dup /PCLXLSetHalftonePhase known\
    {begin %d %d PCLXLSetHalftonePhase end } {pop} ifelse\
 } \
  { pop pop } ifelse";

Bool
pclxl_ps_set_device_dither_origin(PCLXL_GRAPHICS_STATE graphics_state)
{
  int32 dev_origin_x;
  int32 dev_origin_y;

  SC_C2D_INT(dev_origin_x, graphics_state->dither_anchor.x);
  SC_C2D_INT(dev_origin_y, graphics_state->dither_anchor.y);

  *szInvokePCLXLScreeningCmd = '\0';

  /* invoke the PCLXLHalftone procset handler for setting the
   * dither phase.
   */
  swncopyf( (uint8*)szInvokePCLXLScreeningCmd,
            sizeof(szInvokePCLXLScreeningCmd),
            (uint8*)szInvokePCLXLDitherPhase,
            dev_origin_x,
            dev_origin_y );

  return run_ps_string( (uint8*)szInvokePCLXLScreeningCmd);
}

/******************************************************************************
* Log stripped */
