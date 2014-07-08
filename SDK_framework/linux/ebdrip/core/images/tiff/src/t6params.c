/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:t6params.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Access routines to read TIFF level 6 parameters
 */

#include "core.h"

#include "objects.h"    /* OBJECT */
#include "objstack.h"   /* push(), pop(), etc. */
#include "namedef_.h"   /* NAME_* */
#include "dictscan.h"   /* NAMETYPEMATCH */
#include "swerrors.h"   /* STACKUNDERFLOW */

#include "t6params.h"   /* TIFF6PARAMS */
#include "tiffmem.h"    /* tiff_insert_hash */

/** \todo ajcd 2007-12-13: This breaks the Core RIP coding standard, for the
    purpose of modularity. We haven't yet got a clean way of getting access
    to operandstack without requiring the whole PS interpreter and all of the
    unwanted anciliary stuff. */
extern STACK operandstack ;

enum {
  etiffparam_AbortOnUnknown,
  etiffparam_Verbose,
  etiffparam_ListIFDEntries,
  etiffparam_IgnoreOrientation,
  etiffparam_AdjustCTM,
  etiffparam_DoSetColorSpace,
  etiffparam_InstallICCProfile,
  etiffparam_InvertImage,
  etiffparam_DoImageMask,
  etiffparam_DoPageSize,
  etiffparam_Strict,
  etiffparam_DefaultResolution,
  etiffparam_IgnoreES0,
  etiffparam_TreatES0asES2,
  etiffparam_NoUnitsSameAsInch,

  etiffparam_max
};

static NAMETYPEMATCH tiff6_match[etiffparam_max + 1] = {
  { NAME_AbortOnUnknown | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_Verbose | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_ListIFDEntries | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_IgnoreOrientation | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_AdjustCTM | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_DoSetColorSpace | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_InstallICCProfile | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_InvertImage | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_DoImageMask | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_DoPageSize | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_Strict | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_DefaultResolution | OOPTIONAL , 2, { OPACKEDARRAY,OARRAY }},
  { NAME_IgnoreES0 | OOPTIONAL , 1, { OBOOLEAN }},
  { NAME_TreatES0asES2 | OOPTIONAL , 1, { OBOOLEAN }},

  { NAME_NoUnitsSameAsInch | OOPTIONAL , 1, { OBOOLEAN }},

  DUMMY_END_MATCH
};

/*
 * Add dictionary to operand stack with current tiff6 input params.
 */
Bool currenttiffparams_(ps_context_t *pscontext)
{
  OBJECT odict = OBJECT_NOTVM_NOTHING ;
  OBJECT oarray = OBJECT_NOTVM_NOTHING ;
  corecontext_t *context = ps_core_context(pscontext) ;
  TIFF6PARAMS *tiff6params = context->tiff6params ;

  /* Create tiff6params dictionary */
  if ( !ps_dictionary(&odict, etiffparam_max) ) {
    return FALSE;
  }

  /* AbortOnUnknown */
  if ( !tiff_insert_hash(&odict, NAME_AbortOnUnknown,
                         (tiff6params->f_abort_on_unknown
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* Verbose */
  if ( !tiff_insert_hash(&odict, NAME_Verbose,
                         (tiff6params->f_verbose
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* ListIFDEntries */
  if ( !tiff_insert_hash(&odict, NAME_ListIFDEntries,
                         (tiff6params->f_list_ifd_entries
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* IgnoreOrientation */
  if ( !tiff_insert_hash(&odict, NAME_IgnoreOrientation,
                         (tiff6params->f_ignore_orientation
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* AdjustCTM */
  if ( !tiff_insert_hash(&odict, NAME_AdjustCTM,
                         (tiff6params->f_adjust_ctm
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* DoSetColorSpace */
  if ( !tiff_insert_hash(&odict, NAME_DoSetColorSpace,
                         (tiff6params->f_do_setcolorspace
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* InstallICCProfile */
  if ( !tiff_insert_hash(&odict, NAME_InstallICCProfile,
                         (tiff6params->f_install_iccprofile
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* InvertImage */
  if ( !tiff_insert_hash(&odict, NAME_InvertImage,
                         (tiff6params->f_invert_image
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* DoImageMask */
  if ( !tiff_insert_hash(&odict, NAME_DoImageMask,
                         (tiff6params->f_do_imagemask
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* DoPageSize */
  if ( !tiff_insert_hash(&odict, NAME_DoPageSize,
                         (tiff6params->f_do_pagesize
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* Strict */
  if ( !tiff_insert_hash(&odict, NAME_Strict,
                         (tiff6params->f_strict
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* IgnoreES0 */
  if ( !tiff_insert_hash(&odict, NAME_IgnoreES0,
                         (tiff6params->f_ignore_ES0
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  /* ES0 as ES2 */
  if ( !tiff_insert_hash(&odict, NAME_TreatES0asES2,
                         (tiff6params->f_ES0_as_ES2
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }
  /* NoUnitsSameAsInch */
  if ( !tiff_insert_hash(&odict, NAME_NoUnitsSameAsInch,
                         (tiff6params->f_no_units_same_as_inch
                          ? &tnewobj
                          : &fnewobj)) ) {
    return FALSE;
  }

  if (!ps_array(&oarray, 2))
    return FALSE;

  object_store_real(&oArray(oarray)[0], tiff6params->defaultresolution[0]);
  object_store_real(&oArray(oarray)[1], tiff6params->defaultresolution[1]);

  if ( !tiff_insert_hash(&odict, NAME_DefaultResolution, &oarray) ) {
    return FALSE;
  }

  /* Put initialised dictionary on the stack */
  return push(&odict, &operandstack);

} /* Function currenttiffparams_ */


/*
 * Parse dictionary to set current tiff6 input params.
 */
Bool settiffparams_(ps_context_t *pscontext)
{
  OBJECT*         odict;
  OBJECT*         obj;
  NAMETYPEMATCH*  matchdict = tiff6_match;
  corecontext_t *context = ps_core_context(pscontext) ;
  TIFF6PARAMS *tiff6params = context->tiff6params ;

  if ( isEmpty(operandstack) ) {
    return error_handler(STACKUNDERFLOW);
  }

  odict = theTop(operandstack);

  if ( oType(*odict) != ODICTIONARY ) {
    return error_handler(TYPECHECK);
  }

  if ( !oCanRead(*oDict(*odict)) ) {
    if ( !object_access_override(odict) ) {
      return error_handler(INVALIDACCESS);
    }
  }

  if ( !dictmatch(odict, matchdict) ) {
    return FALSE;
  }

  /* AbortOnUnknown */
  if ( (obj = matchdict[etiffparam_AbortOnUnknown].result) != NULL ) {
    tiff6params->f_abort_on_unknown = oBool(*obj);
  }

  /* Verbose */
  if ( (obj = matchdict[etiffparam_Verbose].result) != NULL ) {
    tiff6params->f_verbose = oBool(*obj);
  }

  /* ListIFDEntries */
  if ( (obj = matchdict[etiffparam_ListIFDEntries].result) != NULL ) {
    tiff6params->f_list_ifd_entries = oBool(*obj);
  }

  /* IgnoreOrientation */
  if ( (obj = matchdict[etiffparam_IgnoreOrientation].result) != NULL ) {
    tiff6params->f_ignore_orientation = oBool(*obj);
  }

  /* AdjustCTM */
  if ( (obj = matchdict[etiffparam_AdjustCTM].result) != NULL ) {
    tiff6params->f_adjust_ctm = oBool(*obj);
  }

  /* DoSetColorSpace */
  if ( (obj = matchdict[etiffparam_DoSetColorSpace].result) != NULL ) {
    tiff6params->f_do_setcolorspace = oBool(*obj);
  }

  /* InstallICCProfile */
  if ( (obj = matchdict[etiffparam_InstallICCProfile].result) != NULL ) {
    tiff6params->f_install_iccprofile = oBool(*obj);
  }

  /* InvertImage */
  if ( (obj = matchdict[etiffparam_InvertImage].result) != NULL ) {
    tiff6params->f_invert_image = oBool(*obj);
  }

  /* DoImageMask */
  if ( (obj = matchdict[etiffparam_DoImageMask].result) != NULL ) {
    tiff6params->f_do_imagemask = oBool(*obj);
  }

  /* DoPageSize */
  if ( (obj = matchdict[etiffparam_DoPageSize].result) != NULL ) {
    tiff6params->f_do_pagesize = oBool(*obj);
  }

  /* Strict */
  if ( (obj = matchdict[etiffparam_Strict].result) != NULL ) {
    tiff6params->f_strict = oBool(*obj);
  }

  /* NoUnitsSameAsInch */
  if ( (obj = matchdict[etiffparam_NoUnitsSameAsInch].result) != NULL ) {
    tiff6params->f_no_units_same_as_inch = oBool(*obj);
  }

  /* IgnoreES0 */
  if ( (obj = matchdict[etiffparam_IgnoreES0].result) != NULL ) {
    tiff6params->f_ignore_ES0 = oBool(*obj);
  }

    /* ES0 as ES2*/
  if ( (obj = matchdict[etiffparam_TreatES0asES2].result) != NULL ) {
    tiff6params->f_ES0_as_ES2 = oBool(*obj);
  }
  if ( (obj = matchdict[etiffparam_DefaultResolution].result) != NULL ) {
    OBJECT * arr = oArray(*obj);
    int32 len = theLen(*obj);
    USERVALUE  xres, yres;

    if (len != 2)
      return error_handler( RANGECHECK ) ;

    if (!object_get_real(arr++, &xres))
      return FALSE;

    if (!object_get_real(arr, &yres))
      return FALSE;

    /* valid resolutions must be > 0 */
    if ((xres <= 0.0)||(yres <= 0.0))
      return error_handler( RANGECHECK ) ;

    tiff6params->defaultresolution[0]  = xres;
    tiff6params->defaultresolution[1]  = yres;
  }

  /* Remove original dict from stack */
  pop(&operandstack);

  return TRUE;

} /* Function settiffparams_ */


/* Log stripped */
