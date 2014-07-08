/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:xpsparams.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * get, set, scan and init and finish funcs for xps subset of xml params
 */

#include "core.h"
#include "coreinit.h"

#include "mmcompat.h"        /* mm_alloc_static */
#include "swerrors.h"        /* error_handler() */
#include "namedef_.h"        /* NAME_* */
#include "dicthash.h"        /* insert_hash */
#include "gcscan.h"          /* ps_scan_field */
#include "coreparams.h"      /* module_params_t */
#include "fileio.h"          /* file_open */
#include "monitor.h"         /* monitorf */

#include "xpsiccbased.h"     /* DEF_SRGB_PROFILE_STR */
#include "xps.h"             /* XPSparams */


static mps_root_t xpsparams_root;

enum {
  xps_xml_match_XPSParams,
  xps_xml_match_dummy
} ;


static NAMETYPEMATCH xps_xml_match[xps_xml_match_dummy + 1] = {
  { NAME_XPSParams                   | OOPTIONAL, 1, { ODICTIONARY }},
  DUMMY_END_MATCH
};


enum {
  xps_match_ImageGrayProfile,
  xps_match_ImageRGBProfile,
  xps_match_ImageScRGBProfile,
  xps_match_ImageCMYKProfile,
  xps_match_Image3ChannelProfile,
  xps_match_Image4ChannelProfile,
  xps_match_Image5to8ChannelProfile,
  xps_match_dummy
} ;

static NAMETYPEMATCH xps_match[xps_match_dummy + 1] = {
  { NAME_ImageGrayProfile        | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_ImageRGBProfile         | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_ImageScRGBProfile       | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_ImageCMYKProfile        | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_Image3ChannelProfile    | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_Image4ChannelProfile    | OOPTIONAL , 2, { ONULL, OSTRING }},
  { NAME_Image5to8ChannelProfile    | OOPTIONAL , 2, { ONULL, OSTRING }},
  DUMMY_END_MATCH
};


/* xps_set_xmlparam - sets the xps params mentioned in the odict */
static Bool xps_set_xmlparam(corecontext_t *context, uint16 name, OBJECT *odict)
{
  OBJECT *theo;
  OBJECT ofile = OBJECT_NOTVM_NOTHING;
  XPSPARAMS *xpsparams = context->xpsparams ;

  switch (name) {
  case NAME_XPSParams:

    if ( oType(*odict) != ODICTIONARY ) {
      return error_handler(TYPECHECK) ;
    }

    if ( !oCanRead(*oDict(*odict)) ) {
      if ( !object_access_override(odict) ) {
        return error_handler(INVALIDACCESS) ;
      }
    }


    /* Validate and merge new xps parameters */
    if (! dictmatch( odict, xps_match ))
      return FALSE;

    if ((theo = xps_match[ xps_match_ImageGrayProfile ].result) != NULL) {

      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if (!file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
            !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->ImageGrayProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_ImageRGBProfile ].result) != NULL) {
      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if (!file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
            !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->ImageRGBProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_ImageScRGBProfile ].result) != NULL) {
      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if (!file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
            !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->ImageScRGBProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_ImageCMYKProfile ].result) != NULL) {

      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if ( !file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
             !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->ImageCMYKProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_Image3ChannelProfile ].result) != NULL) {

      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if ( !file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
             !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->Image3ChannelProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_Image4ChannelProfile ].result) != NULL) {

      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if ( !file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
             !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->Image4ChannelProfile, *theo) ;
    }

    if ((theo = xps_match[ xps_match_Image5to8ChannelProfile ].result) != NULL) {
      if ( oType(*theo) == OSTRING ) {
        /* Check we can open the file */

        if ( !file_open(theo, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile) ||
             !file_close(&ofile))
          return FALSE;

        theTags(*theo) = OSTRING | READ_ONLY | LITERAL ;
      }

      OCopy(xpsparams->Image5to8ChannelProfile, *theo) ;
    }
    break;
  }

  return TRUE ;
}


/* xps_get_xmlparam - makes a dictionary object and fills in the XPSparams */
static Bool xps_get_xmlparam(corecontext_t *context, uint16 name, OBJECT *result)
{
  XPSPARAMS *xpsparams = context->xpsparams ;

  HQASSERT(result, "No object for xmlparam result") ;

  switch (name) {
  case NAME_XPSParams:
    {
      if (! ps_dictionary(result, NUM_ARRAY_ITEMS( xps_match ) - 1)) {
        return FALSE ;
      }

      /* Now fill in the dictionary */

      /* ImageGrayProfile */
      oName(nnewobj) = &system_names[ NAME_ImageGrayProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->ImageGrayProfile))
        return FALSE;

      /* ImageRGBProfile */
      oName(nnewobj) = &system_names[ NAME_ImageRGBProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->ImageRGBProfile))
        return FALSE;

      /* ImageScRGBProfile */
      oName(nnewobj) = &system_names[ NAME_ImageScRGBProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->ImageScRGBProfile))
        return FALSE;

      /* ImageCMYKProfile */
      oName(nnewobj) = &system_names[ NAME_ImageCMYKProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->ImageCMYKProfile))
        return FALSE;

      /* Image3ChannelProfile */
      oName(nnewobj) = &system_names[ NAME_Image3ChannelProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->Image3ChannelProfile))
        return FALSE;

      /* Image4ChannelProfile */
      oName(nnewobj) = &system_names[ NAME_Image4ChannelProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->Image4ChannelProfile))
        return FALSE;

      /* Image5to8ChannelProfile */
      oName(nnewobj) = &system_names[ NAME_Image5to8ChannelProfile ] ;
      if (! insert_hash( result, &nnewobj, &xpsparams->Image5to8ChannelProfile))
        return FALSE;
    }
    break;
  }

  return TRUE;
}


mps_res_t MPS_CALL xpsparams_scan(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  XPSPARAMS *params;

  UNUSED_PARAM( size_t, s );
  params = (XPSPARAMS *)p;

  res = ps_scan_field( ss, &params->ImageGrayProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->ImageRGBProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->ImageScRGBProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->ImageCMYKProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->Image3ChannelProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->Image4ChannelProfile );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->Image5to8ChannelProfile );

  return res;
}

static module_params_t xps_xml_params = {
  xps_xml_match,
  xps_set_xmlparam,
  xps_get_xmlparam,
  NULL
} ;

/* init_xps_xml_params - initialise the XPSparams (which belong to the
   xmlparams) and link the accessors for them into the global
   xmlparamlist.
*/
Bool xps_xml_params_swstart(struct SWSTART *params)
{
  Bool fFileExists;
  STAT fileState;
  XPSPARAMS *xpsparams ;
  corecontext_t *context = get_core_context() ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(xps_xml_params.next == NULL, "Already linked xml params accessor") ;

  if ( (xpsparams = mm_alloc_static(sizeof(XPSPARAMS))) == NULL )
    return FAILURE(FALSE) ;

  context->xpsparams = xpsparams ;

  /* Initialise the XPSparams.
   * In the case of filename strings check the files will open
   */
  xpsparams->ImageGrayProfile =
    xpsparams->ImageRGBProfile =
    xpsparams->ImageScRGBProfile =
    xpsparams->ImageCMYKProfile =
    xpsparams->Image3ChannelProfile =
    xpsparams->Image4ChannelProfile =
    xpsparams->Image5to8ChannelProfile = onull; /* Struct copy to set slot properties */

  /* ImageGrayProfile */
  if ( !ps_string(&(xpsparams->ImageGrayProfile),
                  DEF_SRGB_PROFILE_STR, DEF_SRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->ImageGrayProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for Gray XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->ImageGrayProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* ImageRGBProfile */
  if ( !ps_string(&(xpsparams->ImageRGBProfile),
                 DEF_SRGB_PROFILE_STR, DEF_SRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->ImageRGBProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for RGB XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->ImageRGBProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* ImageScRGBProfile */
  if ( !ps_string(&(xpsparams->ImageScRGBProfile),
                 DEF_SCRGB_PROFILE_STR, DEF_SCRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->ImageScRGBProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for scRGB XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->ImageScRGBProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* ImageCMYKProfile */
  if ( !ps_string(&(xpsparams->ImageCMYKProfile),
                 DEF_CMYK_PROFILE_STR, DEF_CMYK_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->ImageCMYKProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for CMYK XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->ImageCMYKProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* Image3ChannelProfile */
  if ( !ps_string(&(xpsparams->Image3ChannelProfile),
                 DEF_SRGB_PROFILE_STR, DEF_SRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->Image3ChannelProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for 3 Channel XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->Image3ChannelProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* Image4ChannelProfile */
  if ( !ps_string(&(xpsparams->Image4ChannelProfile),
                 DEF_CMYK_PROFILE_STR, DEF_CMYK_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->Image4ChannelProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for 4 Channel XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->Image4ChannelProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* Image5to8ChannelProfile */
  if ( !ps_string(&(xpsparams->Image5to8ChannelProfile),
                 DEF_CMYK_PROFILE_STR, DEF_CMYK_PROF_STRLEN))
    return FALSE;

  if ( !file_stat(&(xpsparams->Image5to8ChannelProfile), &fFileExists, &fileState) ||
      !fFileExists )
  {
    monitorf(UVS("Warning: Default ICC profile for 5 to 8 Channel XPS images not found\n"));
  }
  else
  {
    theTags(xpsparams->Image5to8ChannelProfile) = OSTRING | READ_ONLY | LITERAL ;
  }

  /* Link accessors into global list */
  xps_xml_params.next = context->xmlparamlist ;
  context->xmlparamlist = &xps_xml_params ;


  /* Create root last so we force cleanup on success. */
  if ( mps_root_create(&xpsparams_root, mm_arena, mps_rank_exact(),
                       0, xpsparams_scan, xpsparams, 0) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE;
}


/* Tidy up the xpsparams */
void xps_xml_params_finish( void )
{
  corecontext_t *context = get_core_context() ;
  mps_root_destroy(xpsparams_root);
  context->xpsparams = NULL ;
}

void init_C_globals_xpsparams(void)
{
  xpsparams_root = NULL ;
  xps_xml_params.next = NULL;
}

/* ============================================================================
* Log stripped */
