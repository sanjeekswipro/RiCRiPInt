/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcolor.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF colourspace creation.
 */

#include "core.h"
#include "mm.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "namedef_.h"

#include "gstate.h" /* gs_storedashlist */
#include "gstack.h"
#include "dicthash.h"
#include "control.h"
#include "typeops.h"
#include "gs_color.h"
#include "gschead.h"
#include "gschcms.h"
#include "gu_ctm.h"
#include "miscops.h"
#include "swmemory.h"
#include "pattern.h"
#include "devops.h"
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfxref.h"

#include "pdfexec.h"
#include "pdfattrs.h"
#include "pdfops.h"
#include "pdfvign.h"
#include "pdfgstat.h"
#include "pdfparam.h"
#include "swpdfin.h"

#include "pdfcolor.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdfx.h"
#include "pdfinmetrics.h"

static Bool pdf_makepattern( PDFCONTEXT *pdfc ) ;
static Bool pdf_makepattern_dispatch( PDFCONTEXT *pdfc , OBJECT *dict ,
                                       OBJECT *stream , int32 ptype,
                                       OMATRIX *defaultCTM) ;
static Bool pdf_intercept_colorspace( PDFCONTEXT *pdfc , OBJECT *colorspace ,
                                       int32 parentcspace ) ;
static Bool ignore_n_reals(int32 n, STACK *stack);
static Bool null_sc_op(PDFCONTEXT *pdfc, GS_COLORinfo *colorInfo,
                       STACK *stack , int32 colorType);

#ifdef METRICS_BUILD
/* See header for doc. */
static void updateBlendSpaceMetrics(uint32 spaceId, int32 nComps)
{
  switch (spaceId) {
  case SPACE_CalGray:
  case SPACE_DeviceGray:
    pdfin_metrics.blendSpaceCounts.gray ++;
    break;

  case SPACE_DeviceRGB:
  case SPACE_CalRGB:
    pdfin_metrics.blendSpaceCounts.rgb ++;
    break;

  case SPACE_DeviceCMYK:
    pdfin_metrics.blendSpaceCounts.cmyk ++;
    break;

  case SPACE_ICCBased:
    switch (nComps) {
    default:
      pdfin_metrics.blendSpaceCounts.iccNComponent ++;
      break;

    case 3:
      pdfin_metrics.blendSpaceCounts.icc3Component ++;
      break;

    case 4:
      pdfin_metrics.blendSpaceCounts.icc4Component ++;
      break;
    }
    break;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/** The PDF operator g. */
Bool pdfop_g( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceGray, LITERAL) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT() ) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals( 1, &imc->pdfstack );
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )

    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;

  return  gsc_setgray( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;
}

/** The PDF operator G. */
Bool pdfop_G( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceGray, LITERAL) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals( 1, &imc->pdfstack );
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )
    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;

  return gsc_setgray( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;
}

/** The PDF operator k. */
Bool pdfop_k( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceCMYK, LITERAL) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals(4, &imc->pdfstack);
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )
    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;

  return gsc_setcmykcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;
}

/** The PDF operator K. */
Bool pdfop_K( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceCMYK, LITERAL) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if (CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals(4, &imc->pdfstack);
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )
    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;

  return gsc_setcmykcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;
}

/** The PDF operator rg. */
Bool pdfop_rg( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceRGB, LITERAL) ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals( 3, &imc->pdfstack );
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdfxCheckRgOperator( pdfc, FALSE ))
    return FALSE;

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )
    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace(gstateptr->colorInfo ,  & imc->pdfstack , GSC_FILL ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;

  return gsc_setrgbcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;
}

/** The PDF operator RG. */
Bool pdfop_RG( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  OBJECT colorspace = OBJECT_NOTVM_NAME(NAME_DeviceRGB, LITERAL) ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( DEVICE_INVALID_CONTEXT() ) {
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return ignore_n_reals( 3, &imc->pdfstack );
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( ! pdfxCheckRgOperator( pdfc, TRUE ))
    return FALSE;

  if ( ! pdf_intercept_colorspace( pdfc , & colorspace , NULL_COLORSPACE ))
    return FALSE ;

  if ( oType( colorspace ) != ONAME )
    return push( & colorspace , & imc->pdfstack ) &&
           gsc_setcolorspace( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) &&
           gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;

  return gsc_setrgbcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;
}

/** The PDF operator sc. */
Bool pdfop_sc( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT() ) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return null_sc_op( pdfc, gstateptr->colorInfo, &imc->pdfstack, GSC_FILL ) ;
    else
      return error_handler( UNDEFINED ) ;
  }
  return gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;
}

/** The PDF operator SC. */
Bool pdfop_SC( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return null_sc_op( pdfc, gstateptr->colorInfo, &imc->pdfstack, GSC_STROKE );
    else
      return error_handler( UNDEFINED ) ;
  }
  return gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;
}

static NAMETYPEMATCH pdfmakepattern_dispatchdict[] = {
/* 0 */ { NAME_PatternType,              2,  { OINTEGER, OINDIRECT }},
/* 1 */ { NAME_XUID | OOPTIONAL,         2,  { OARRAY, OPACKEDARRAY }},
/* 2 */ { NAME_Matrix | OOPTIONAL,       3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
          DUMMY_END_MATCH
} ;

static NAMETYPEMATCH pdfmakepattern1_dispatchdict[] = {
/* 0 */ { NAME_PaintType,                2,  { OINTEGER, OINDIRECT }},
/* 1 */ { NAME_TilingType,               2,  { OINTEGER, OINDIRECT }},
/* 2 */ { NAME_BBox,                     3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 3 */ { NAME_XStep,                    3,  { OINTEGER, OREAL, OINDIRECT }},
/* 4 */ { NAME_YStep,                    3,  { OINTEGER, OREAL, OINDIRECT }},
/* 5 */ { NAME_Resources,                2,  { ODICTIONARY, OINDIRECT }},
          DUMMY_END_MATCH
} ;

static NAMETYPEMATCH pdfmakepattern2_dispatchdict[] = {
/* 0 */ { NAME_Shading,                  3,  { ODICTIONARY, OFILE, OINDIRECT }},
/* 1 */ { NAME_ExtGState    | OOPTIONAL, 2,  { ODICTIONARY, OINDIRECT }},
          DUMMY_END_MATCH
} ;

/* The length of the local dictionary to create: the keys
 * for a PS pattern dict plus Resources and Implementation.
 */

#define PDF_PATTERN_DICT_LEN 10

static Bool pdf_makepattern( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT resname = OBJECT_NOTVM_NOTHING ;
  OBJECT *stream ;
  OBJECT *dict ;
  OBJECT localdict = OBJECT_NOTVM_NOTHING ;
  OBJECT *cached ;
  int32 result, ptype ;
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  stack =  & imc->pdfstack ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  Copy( &resname , theITop( stack )) ;

  if ( oType( resname ) != ONAME )
    return error_handler( TYPECHECK ) ;

  /* Do we already have this pattern cached? Patterns in PDF are
     unique to the marking context in which they're defined. */
  cached = fast_extract_hash( &imc->patterncache , &resname ) ;

  if ( cached != NULL ) {
    npop( 1 , stack ) ;
    return push( cached , stack ) ;
  }

  if ( ! pdf_get_resource( pdfc , NAME_Pattern , &resname , & stream ))
    return FALSE ;

  if ( oType( *stream ) == OFILE ) {
    dict = streamLookupDict( stream ) ;
  } else if ( oType( *stream ) == ODICTIONARY ) {
    dict = stream ;
  } else
    return error_handler( TYPECHECK ) ;

  npop( 1 , stack ) ;

  /* Got the pattern resource. Now bundle up a copy of the dictionary
   * with the relevant keys plus the stream itself as the PaintProc,
   * before sending in on to gs_makepattern.
   */

  if ( ! dict )
    return error_handler( UNDEFINED ) ;

  if ( ! pdf_dictmatch( pdfc , dict , pdfmakepattern_dispatchdict ))
    return FALSE ;

  ptype = oInteger(*pdfmakepattern_dispatchdict[0].result) ;


  switch ( ptype ) {
  case 1:
    if ( oType( *stream ) != OFILE )
      return error_handler( TYPECHECK ) ;
    if ( ! pdf_dictmatch( pdfc , dict , pdfmakepattern1_dispatchdict ))
      return FALSE ;
    break ;
  case 2:
    if ( ! pdf_dictmatch( pdfc , dict , pdfmakepattern2_dispatchdict ))
      return FALSE ;
    /* Typecheck done in pdfsh_prepare called from pdf_makepattern_dispatch */
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  /* Make a local dictionary, populate it, and dispatch it.
   * The Resources key isn't needed by gs_makepattern itself,
   * but by pdf_exec_stream when the pattern is painted.
   */

  if ( ! pdf_create_dictionary( pdfc , PDF_PATTERN_DICT_LEN , & localdict ))
    return FALSE ;

  /* Note that patterns only ever use the initial transform for the contents
  stream in which they are used. */
  result = pdf_makepattern_dispatch( pdfc , & localdict , stream , ptype,
                                     &imc->defaultCTM ) ;

  pdf_destroy_dictionary( pdfc , PDF_PATTERN_DICT_LEN , & localdict ) ;

  return result && insert_hash( &imc->patterncache , &resname ,
                                theTop(*stack)) ;
}

/* A local macro just to cut down on otherwise very verbose code. */

#define PDF_PATTERN_INSERT_KEY( _pdfc , _name , _dictmatch, _number ) \
  MACRO_START \
    oName( nnewobj ) = system_names + ( _name ) ; \
    \
    HQASSERT( theIMName( & (_dictmatch)[ ( _number ) ] ) \
              == system_names + ( _name ) , \
              "Names don't match in PDF_PATTERN_INSERT_KEY." ) ; \
    if ( (_dictmatch)[ ( _number ) ].result && \
         !pdf_fast_insert_hash( _pdfc , dict , & nnewobj , \
                              (_dictmatch)[ ( _number ) ].result )) \
      return FALSE ; \
  MACRO_END

static Bool pdf_makepattern_dispatch( PDFCONTEXT *pdfc , OBJECT *dict ,
                                       OBJECT *stream , int32 ptype,
                                       OMATRIX *defaultCTM )
{
  OBJECT matrix = OBJECT_NOTVM_NOTHING ;
  OBJECT *mptr ;
  OBJECT *xgs = NULL ;
  OBJECT *shading = NULL ;
  OBJECT *sdict = NULL ;
  OBJECT psCopy = OBJECT_NOTVM_NOTHING;
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( dict , "dict null in pdf_makepattern_dispatch." ) ;
  HQASSERT( oType( *dict ) == ODICTIONARY ,
            "dict not ODICTIONARY in pdf_makepattern_dispatch." ) ;
  HQASSERT( stream , "stream null in pdf_makepattern_dispatch." ) ;
  HQASSERT( defaultCTM , "matrix null in pdf_makepattern_dispatch." ) ;
  HQASSERT( oType( *stream ) == OFILE ||
            oType( *stream ) == ODICTIONARY ,
            "stream not OFILE or ODICTIONARY in pdf_makepattern_dispatch." ) ;

  stack =  & imc->pdfstack ;

  PDF_PATTERN_INSERT_KEY( pdfc , NAME_PatternType , pdfmakepattern_dispatchdict, 0 ) ;
  PDF_PATTERN_INSERT_KEY( pdfc , NAME_XUID        , pdfmakepattern_dispatchdict, 1 ) ;

  HQASSERT(ptype == 1 || ptype == 2, "Pattern type not known") ;
  if ( ptype == 1 ) {
    PDF_PATTERN_INSERT_KEY( pdfc , NAME_PaintType   , pdfmakepattern1_dispatchdict, 0 ) ;
    PDF_PATTERN_INSERT_KEY( pdfc , NAME_TilingType  , pdfmakepattern1_dispatchdict, 1 ) ;
    PDF_PATTERN_INSERT_KEY( pdfc , NAME_BBox        , pdfmakepattern1_dispatchdict, 2 ) ;
    PDF_PATTERN_INSERT_KEY( pdfc , NAME_XStep       , pdfmakepattern1_dispatchdict, 3 ) ;
    PDF_PATTERN_INSERT_KEY( pdfc , NAME_YStep       , pdfmakepattern1_dispatchdict, 4 ) ;

    oName( nnewobj ) = system_names + NAME_PaintProc ;
    if ( ! pdf_fast_insert_hash( pdfc , dict , & nnewobj , stream ))
      return FALSE ;

    /* Inline it just in case. */
    if ( ! pdf_resolvexrefs( pdfc , dict ))
      return FALSE ;

    /* Insert Resources AFTER the resolvexrefs - the resources
     * machinery doesn't work when resource dictionaries are
     * inlined behind its back.
     */

    PDF_PATTERN_INSERT_KEY( pdfc , NAME_Resources , pdfmakepattern1_dispatchdict, 5 ) ;
  } else {
    shading = pdfmakepattern2_dispatchdict[0].result ;
    sdict = shading ;

    if ( oType(*shading) == OFILE) {
      /* Get the dictionary associated with the stream. */
      if ( NULL == (sdict = streamLookupDict(shading)) )
        return error_handler( UNDEFINED ) ;
    }

    if ( !pdfsh_prepare(pdfc, sdict, shading) )
      return FALSE ;

    /* Put Shading dictionary into pattern dict */
    oName( nnewobj ) = system_names + NAME_Shading ;
    if ( !pdf_fast_insert_hash(pdfc, dict, &nnewobj, sdict) )
      return FALSE ;

    /* Inline pattern dict just in case. */
    if ( ! pdf_resolvexrefs( pdfc , dict ))
      return FALSE ;

    /* Note ExtGState for use later */
    xgs = pdfmakepattern2_dispatchdict[1].result ;
  }

  /* Copy the local dictionary into postscript and stack it. */
  if ( ! (pdf_copyobject(NULL, dict, &psCopy) &&
          push( &psCopy , stack )))
    return FALSE ;

  /* If there was a matrix in the pattern resource dict, push that.
   * Otherwise, push the identity matrix.
   */

  mptr = pdfmakepattern_dispatchdict[ 2 ].result ;
  if ( ! mptr ) {
    mptr = & matrix ;
    if ( ! pdf_matrix( pdfc , mptr )) {
      pop( stack ) ; /* Must pop to stop hanging reference. */
      return FALSE ;
    }
  }

  /* Go and do the makepattern, making the result readonly. */
  {
    OBJECT patternobj = OBJECT_NOTVM_NOTHING ;
    int32 gid, result ;

    if ( ! gs_gpush( GST_GSAVE ))
      return FALSE ;

    gid = gstackptr->gId ;

    /* Reset ExtGState parameters to default values for pattern gstate */
    gsc_initgray( gstateptr->colorInfo ) ;

    /* Only doing device independent states, so flatness ignored. */
    theLineWidth( theLineStyle( *gstateptr )) = 1.0f ;
    theStartLineCap( theLineStyle( *gstateptr )) = 0 ;
    theEndLineCap( theLineStyle( *gstateptr )) = 0 ;
    theDashLineCap( theLineStyle( *gstateptr )) = 0 ;
    theLineJoin( theLineStyle( *gstateptr )) = 0 ;
    theTags( theDashPattern( theLineStyle( *gstateptr ))) = ONULL | LITERAL ;
    oArray( theDashPattern( theLineStyle( *gstateptr ))) = NULL ;
    theDashOffset( theLineStyle( *gstateptr )) = 0.0f ;
    (void)gs_storedashlist( &theLineStyle( *gstateptr ), NULL, 0 ) ;
    theMiterLimit( theLineStyle( *gstateptr )) = 10.0f ;

    result = ((xgs == NULL || pdf_set_extgstate(pdfc, xgs)) &&
              push( mptr , stack ) &&
              gs_setctm(defaultCTM, FALSE) &&
              gs_makepattern( stack , & patternobj ) &&
              reduceOaccess( READ_ONLY , TRUE , & patternobj ) &&
              push( & patternobj , stack )) ;

    if ( ! gs_cleargstates( gid , GST_GSAVE , NULL ) || !result )
      return FALSE ;
  }

  if ( mptr == & matrix )
    pdf_destroy_array( pdfc , 6 , mptr ) ;

  return TRUE ;
}

/** The PDF operator scn. */
Bool pdfop_scn( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return null_sc_op( pdfc, gstateptr->colorInfo, &imc->pdfstack, GSC_FILL ) ;
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( gsc_getcolorspace ( gstateptr->colorInfo, GSC_FILL ) == SPACE_Pattern )
    if ( ! pdf_makepattern( pdfc ))
      return FALSE ;

  return gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_FILL ) ;
}

/** The PDF operator SCN. */
Bool pdfop_SCN( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      return null_sc_op( pdfc, gstateptr->colorInfo, &imc->pdfstack, GSC_STROKE ) ;
    else
      return error_handler( UNDEFINED ) ;
  }

  if ( gsc_getcolorspace ( gstateptr->colorInfo, GSC_STROKE ) == SPACE_Pattern )
    if ( ! pdf_makepattern( pdfc ))
      return FALSE ;

  return gsc_setcolor( gstateptr->colorInfo , & imc->pdfstack , GSC_STROKE ) ;
}

/* Returns the NameNumber associated with a colorspace object */
int32 pdf_getColorSpace(OBJECT *obj)
{
  int32 colorSpace ;

  if ( oType( *obj ) == ONAME )
    colorSpace = oNameNumber(*obj) ;
  else {
    OBJECT *olist ;
    HQASSERT( oType( *obj ) == OARRAY, "Composite colorspace not an array") ;
    olist = oArray( *obj ) ;
    HQASSERT( oType( *olist ) == ONAME, "Composite colorspace invalid") ;
    colorSpace = oNameNumber(*olist) ;
  }

  return colorSpace ;
}

static Bool pdf_array_copy(OBJECT* o1, OBJECT* o2)
{
  uint16 lo1 , lo2 ;
  int32 i ;
  OBJECT *olist1 ;
  OBJECT *olist2 ;

  HQASSERT((o1 && o2),
           "pdf_array_copy: NULL array pointer");
  HQASSERT(oType(*o1) == OARRAY || oType(*o1) == OPACKEDARRAY,
           "pdf_array_copy: o1 not an array");
  HQASSERT(oType(*o2) == OARRAY || oType(*o2) == OPACKEDARRAY,
           "pdf_array_copy: o1 not an array");

  lo1 = theLen(*o1) ;
  lo2 = theLen(*o2) ;
  if ( lo1 > lo2 )
    return error_handler( RANGECHECK ) ;

  olist1 = oArray(*o1) ;
  olist2 = oArray(*o2) ;

  /*  Copy arrays values across */
  if ( olist1 < olist2 ) {
    for ( i = lo1 ; i > 0 ; --i ) {
      Copy( olist2 , olist1 ) ;
      ++olist1 ; ++olist2 ;
    }
  }
  else {
    olist1 += ( lo1 - 1 ) ;
    olist2 += ( lo1 - 1 ) ;
    for ( i = lo1 ; i > 0 ; --i ) {
      Copy( olist2 , olist1 ) ;
      --olist1 ; --olist2 ;
    }
  }

  return TRUE;
}


/* pdf_mapcolorspace()
 * This function operates recursively to bottom out exactly what colorspace to
 * set & use (e.g. indexed maps to another space, deviceCMYK may map to
 * defaultCMYK which in turn may map to ICCBased, etc.).
 * The 'parentcspace' is used to control the recursion - initially it is always
 * given the value NULL_COLORSPACE.
 */
Bool pdf_mapcolorspace( PDFCONTEXT *pdfc,
                        OBJECT *srcobj,           /* Specified color space */
                        OBJECT *destobj,          /* Returned color space */
                        int32  parentcspace )     /* Returned flag for some caller */
{
  PDF_IMC_PARAMS  *imc ;
  PDFXCONTEXT     *pdfxc ;
  PDF_IXC_PARAMS  *ixc ;

  HQASSERT( srcobj , "srcobj NULL in pdf_mapcolorspace" ) ;
  HQASSERT( destobj , "destobj NULL in pdf_mapcolorspace" ) ;
  HQASSERT( srcobj != destobj , "srcobj == destobj in pdf_mapcolorspace" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* A name here might be the name of a colorspace or of a resource. */

  if ( oType( *srcobj ) == ONAME ) {
    switch ( oNameNumber(*srcobj) ) {
    case NAME_DeviceGray:
    case NAME_DeviceRGB:
    case NAME_DeviceCMYK:
    case NAME_Pattern:
      Copy( destobj, srcobj );
      break;

    case NAME_Lab:
    case NAME_CalGray:
    case NAME_CalRGB:
    case NAME_ICCBased:
    case NAME_Indexed:
    case NAME_Separation:
    case NAME_DeviceN:
      return error_handler( RANGECHECK ) ;

    default:
      {
        OBJECT *res ;

        if ( ! pdf_get_resource( pdfc , NAME_ColorSpace , srcobj , & res ))
          return FALSE ;
        Copy( destobj, res );
      }
      break;
    }
  }
  else
    Copy( destobj, srcobj );

  if ( ! pdf_resolvexrefs( pdfc , destobj ) )
    return FALSE ;

  if (! pdfxCheckColorSpace( pdfc, pdf_getColorSpace( destobj ), parentcspace ))
    return FALSE;

  if ( oType( *destobj ) == ONAME ) {

    /* Check for an intercept, eg "/DefaultGray [ /CalGray ... ]". */
    if ( ! pdf_intercept_colorspace( pdfc , destobj, parentcspace ) )
      return FALSE ;
  }
  else if ( oType( *destobj ) == OARRAY ||
            oType( *destobj ) == OPACKEDARRAY ) {
    OBJECT *olist ;
    OBJECT *csobj ;
    int32 colorspace ;

    if ( theLen(*destobj) < 1 )
      return error_handler( RANGECHECK ) ;

    /* The first element in the array is the color space name. */
    olist = oArray( *destobj ) ;
    if ( oType( *olist ) != ONAME )
      return error_handler( TYPECHECK ) ;

    colorspace = oNameNumber(*olist) ;
    csobj = olist ;

    if ( parentcspace != NULL_COLORSPACE ) {
      /* Check parent/base colorspace combination is valid. */
      switch ( parentcspace ) {
      case NAME_Separation :
      case NAME_DeviceN :
        if ( colorspace == NAME_Separation ||
             colorspace == NAME_DeviceN ||
             colorspace == NAME_Indexed ||
             colorspace == NAME_Pattern )
          return error_handler( RANGECHECK ) ;
        break ;
      case NAME_Indexed :
        if ( colorspace == NAME_Indexed ||
             colorspace == NAME_Pattern )
          return error_handler( RANGECHECK ) ;
        break ;
      case NAME_ICCBased :
      case NAME_Pattern :
        if ( colorspace == NAME_Pattern )
          return error_handler( RANGECHECK ) ;
        break ;
      default :
        HQFAIL( "Should not get here!" ) ;
        return error_handler( RANGECHECK ) ;
      }
    }

    /* Do color space mapping. */
    switch ( colorspace ) {
    case NAME_DeviceGray :
    case NAME_DeviceRGB :
    case NAME_DeviceCMYK :
      if ( theLen(*destobj) != 1 )
        return error_handler( RANGECHECK ) ;
        /* Check for an intercept, eg "/DefaultGray [ /CalGray ... ]". */
      Copy( destobj , olist ) ;
      if ( ! pdf_intercept_colorspace( pdfc , destobj , parentcspace ) )
        return FALSE ;
      break ;

    case NAME_Pattern :
      if ( theLen(*destobj) != 2 ) {
        /* no mapping to do */
        break ;
      }
    /* drop through */
    case NAME_Indexed :
    case NAME_Separation :
    case NAME_DeviceN :
    {
      OBJECT *srcbase ;
      OBJECT srccopy = OBJECT_NOTVM_NOTHING ;
      OBJECT destbase = OBJECT_NOTVM_NOTHING ;
      int32 basepos ;

      /* Already checked by now that if it's a Pattern
       * color space the array length should be 2. */
      if ( (colorspace == NAME_Indexed && theLen(*destobj) != 4 ) ||
         (  colorspace == NAME_Separation && theLen(*destobj) != 4 ) ||
         (  colorspace == NAME_DeviceN && theLen(*destobj) != 4  &&
                                          theLen(*destobj) != 5) )
        return error_handler( TYPECHECK ) ;

      /* Map the base space onto postscript objects. */
      basepos = 2 ;
      if (colorspace == NAME_Pattern  ||  colorspace == NAME_Indexed)
        basepos = 1 ;

      srcbase = &olist[ basepos ] ;

      HQASSERT( oType( *srcbase ) != OINDIRECT,
                "indirect object encountered" ) ;

      if (!pdf_mapcolorspace( pdfc, srcbase, &destbase, colorspace ))
         return FALSE ;

      /* Take a deep copy of the object mapped since this object could
       * point to memory in the XREF cache or PS local VM.
       */
      Copy( &srccopy, srcbase ) ;
      if ( ! pdf_copyobject( pdfc, & destbase, srcbase ) )
        return FALSE ;
      pdf_freeobject( pdfc, & srccopy ) ;

      /* Patterns don't have tint transforms,
       * Indexed lookup procedure streams don't need mapping.
       */
      if (colorspace != NAME_Pattern  &&  colorspace != NAME_Indexed) {
        int32 tintpos = 3 ;
        OBJECT *tint = &olist[ tintpos ];
        if ( oType( *tint ) == OFILE ) {/* Type 0/4 functions */
          if ( NULL == ( tint = streamLookupDict( tint )))
            return error_handler( UNDEFINED ) ;

          if ( !pdf_resolvexrefs( pdfc , tint ))
            return FALSE ;
        }
        else if ( oType( *tint ) != ODICTIONARY ) /* Type 2/3 functions */
          return error_handler( TYPECHECK ) ;
      }
    }
    break;

    case NAME_CalGray :
    case NAME_CalRGB :
    case NAME_Lab :
      {
        OBJECT *srcdict ;
        int32  dictpos = 1 ;

        if ( theLen(*destobj) != 2 )
          return error_handler( RANGECHECK ) ;

        /* The second is the color space dictionary. */
        srcdict = & ( olist[ dictpos ] ) ;

        HQASSERT( oType( *srcdict ) != OINDIRECT,
                 "indirect object encountered" ) ;

        if ( oType( *srcdict ) != ODICTIONARY )
          return error_handler( TYPECHECK ) ;
      }
      break ;

    case NAME_ICCBased :
      {
        OBJECT *dict ;
        OBJECT *srcfile ;
        int32  filepos = 1 ;

        /* As this function is called recursively, we may already have
           added the param dict. */
        if ( theLen(*destobj) == 3 ) {
          /* Already done? Check that the third parameter is a dict. */

          if ( oType( olist[2] ) != ODICTIONARY )
            return error_handler( SYNTAXERROR );

        } else if ( theLen(*destobj) == 2 ) {
          Hq32x2 position;

          /* There are 2 array elements, the 2nd one is the ICC
             profile stream */

          /* Make sure that all indirect objects inside the stream
           * dictionary are resolved before passing to the color chain
           * processing. The stream itself must be indirect.  Also,
           * profiles should be reinitialised here because the
           * StreamDecode filter is typically closed by the time we
           * come to access it a second time. This doesn't matter as
           * long as all necessary tables have been cached and the
           * caches haven't been purged, and the rsd (that is placed
           * on top of the StreamDecode in the colour module) hasn't
           * been restored away - but it will typically be restored
           * away at the end of the page in which it was created.
           */
          srcfile = &olist[filepos] ;

          if ( oType( *srcfile ) != OFILE )
            return error_handler( TYPECHECK ) ;

          Hq32x2FromUint32(&position, 0u);
          if ((*theIMySetFilePos(oFile(*srcfile)))(oFile(*srcfile),
                                                   &position) == EOF)
            return FALSE ;

          dict = streamLookupDict( srcfile ) ;
          if ( dict == NULL )
            return FALSE ;

          if ( !pdf_resolvexrefs( pdfc , dict ))
            return FALSE ;

          /* Add the parameter dict as a third array element (of a new
             array, necessarily) */
          {
            OBJECT newarray = OBJECT_NOTVM_NOTHING;

            if ( !pdf_create_array( pdfc , 3 , &newarray ) ||
                 !pdf_array_copy( destobj , &newarray ) )
              return FALSE ;
            Copy( &oArray(newarray)[2], dict );
            Copy( destobj, &newarray );
          }

          /* Pass the PDF Execution Context ID into the ICCBased colorspace
             stream dictionary to uniquely identify the profile for caching. */
          {
            OBJECT pdfxcid = OBJECT_NOTVM_NOTHING ;

            /* the key */
            oName( nnewobj ) = system_names + NAME_ContextID ;

            /* the value */
            object_store_integer(&pdfxcid, pdfxc->id) ;

            if ( !pdf_fast_insert_hash( pdfc, dict, &nnewobj, &pdfxcid ) ) {
              HQFAIL( "Unable to add PDFXID to ICCBased dictionary" ) ;
              return FALSE ;
            }
          }

          {
            /* Obtain the alternate colorspace for use below.  If
               there's no explicit 'Alternate' colorspace named, then
               use DeviceGray/RGB/CMYK based on 'N' - the number of
               components. */
            OBJECT alternate = OBJECT_NOTVM_NOTHING ;
            OBJECT *theo;

            theo = fast_extract_hash_name( dict , NAME_Alternate ) ;
            if (theo == NULL) {
              theo = fast_extract_hash_name( dict , NAME_N ) ;
              if ( theo == NULL || oType( *theo ) != OINTEGER )
                return error_handler( TYPECHECK ) ;

              switch (oInteger( *theo )) {
              case 1:
                oName(nnewobj) = system_names + NAME_DeviceGray;
                break ;
              case 3:
                oName(nnewobj) = system_names + NAME_DeviceRGB;
                break ;
              case 4:
                oName(nnewobj) = system_names + NAME_DeviceCMYK;
                break ;
              default:
                return error_handler( UNDEFINED ) ;
              }
              Copy(&alternate, &nnewobj);
              theo = &alternate;

              /* Insert a key into the dict for use by the color
               * module. It will be used to provide the original
               * device space for use in cases, (which are alarmingly
               * common due to Distiller 4.0), where the ICC profile
               * is corrupt and cannot be decoded.
               */

              /* the key */
              oName( nnewobj ) = system_names + NAME_Alternate ;

              if ( !pdf_fast_insert_hash(pdfc, dict, &nnewobj, theo ))
                return FALSE ;
            }
          }

        } else {
          /* theLen(*destobj) <2 or >3 */
          return error_handler( RANGECHECK ) ;
        }
      }
      break;

    default :
      return error_handler( RANGECHECK ) ;
    }
  }
  else
    return error_handler( TYPECHECK ) ;

  /* Ensure there are no reference objects in the result (since we're
     typically going to pass it on to Postscript). */
  if (! pdf_resolvexrefs(pdfc, destobj))
    return error_handler(UNDEFINED);

  return TRUE ;
}

/** The PDF operator cs. */
Bool pdfop_cs( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  STACK *stack ;
  OBJECT *theo ;
  OBJECT mappedObj = OBJECT_NOTVM_NOTHING ;
  Bool do_null_op = FALSE;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      do_null_op = TRUE;
    else
      return error_handler( UNDEFINED ) ;
  }

  theo = theITop( stack ) ;

  if ( ! pdf_mapcolorspace( pdfc , theo , &mappedObj , NULL_COLORSPACE ))
    return FALSE ;

  if ( do_null_op ) {
    npop( 1, stack );
    return TRUE;
  }

  Copy( theo, &mappedObj ) ;

  return gsc_setcolorspace( gstateptr->colorInfo , stack , GSC_FILL ) ;
}

/** The PDF operator CS. */
Bool pdfop_CS( PDFCONTEXT *pdfc )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  STACK *stack ;
  OBJECT *theo ;
  OBJECT mappedObj = OBJECT_NOTVM_NOTHING ;
  Bool do_null_op = FALSE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( DEVICE_INVALID_CONTEXT()) {
    if ( CURRENT_DEVICE() == DEVICE_CHAR && !ixc->strictpdf )
      do_null_op = TRUE ;
    else
      return error_handler( UNDEFINED ) ;
  }

  theo = theITop( stack ) ;

  if ( ! pdf_mapcolorspace( pdfc , theo , &mappedObj , NULL_COLORSPACE ))
    return FALSE ;

  if ( do_null_op ) {
    npop( 1, stack );
    return TRUE;
  }

  Copy( theo, &mappedObj ) ;

  return gsc_setcolorspace( gstateptr->colorInfo , stack , GSC_STROKE ) ;
}

/** The PDF operator ri. */
Bool pdfop_ri( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;
  OBJECT *theo ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theITop( stack ) ;
  if ( oType( *theo ) != ONAME )
    return error_handler( TYPECHECK ) ;

  if ( theo && ! pdf_set_rendering_intent(pdfc, theo) )
    return FALSE ;

  npop( 1 , stack ) ;

  return TRUE ;
}

Bool pdf_set_rendering_intent( PDFCONTEXT *pdfc, OBJECT *theo )
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;

  if (! pdfxCheckRenderingIntent(pdfc, theo))
    return FALSE;

  /* gsc_setrenderingintent sets the renderingintent appropriate to the
   * intent.
   */
  if (!gsc_setrenderingintent(gstateptr->colorInfo, theo))
    return FALSE;

  return TRUE;
}

/* DeviceGray, DeviceRGB and DeviceCMYK can be intercepted by specifying
 * DefaultGray, DefaultRGB and DefaultCMYK in the Resources dictionary.
 * DefaultGray can be DeviceGray (no change), Separation, DeviceN,
 *   CalGray or ICCBased.
 * DefaultRGB can be DeviceRGB (no change), DeviceN, CalRGB.or ICCBased
 * DefaultCMYK can be DeviceCMYK (no change), DeviceN.or ICCBased
 */

static Bool pdf_intercept_colorspace( PDFCONTEXT *pdfc , OBJECT *colorspace ,
                                       int32 parentcspace )
{
  OBJECT key = OBJECT_NOTVM_NOTHING , *pResrc ;
  int32 interceptkey , device_cspace ;
  int32 pdfx_icept_cspace ;
  int32 numColors ;
  PDFXCONTEXT     *pdfxc ;
  PDF_IXC_PARAMS  *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( colorspace , "colorspace is null in pdf_intercept_colorspace" ) ;
  HQASSERT( oType( *colorspace ) == ONAME ,
            "colorspace is not a name in pdf_intercept_colorspace" ) ;

  /* Derive the key to look for the appropriate colorspace intercept. */
  switch ( oNameNumber(*colorspace) ) {
  case NAME_DeviceGray :
    interceptkey = NAME_DefaultGray ;
    device_cspace = NAME_DeviceGray ;
    numColors = 1 ;
    break ;
  case NAME_DeviceRGB :
    interceptkey = NAME_DefaultRGB ;
    device_cspace = NAME_DeviceRGB ;
    numColors = 3 ;
    break ;
  case NAME_DeviceCMYK :
    interceptkey = NAME_DefaultCMYK ;
    device_cspace = NAME_DeviceCMYK ;
    numColors = 4 ;
    break ;
  default :
    /* No interception. */
    return TRUE ;
  }

  theTags( key ) = ONAME | LITERAL ;
  oName( key ) = system_names + interceptkey ;

  if ( ! pdf_get_resource( pdfc , NAME_ColorSpace , & key , & pResrc ) )
    return FALSE ;

  pdfx_icept_cspace = device_cspace ;

  /* Validate intercept colorspace and map if necessary. */
  if ( pResrc == NULL ) {
    /* Default... is not in the resources dictionary therefore there
     * is no colorspace interception to do.
     */
  }
  else if ( oType( *pResrc ) == ONAME ) {
    if ( oNameNumber(*pResrc) != device_cspace )
      return error_handler( RANGECHECK ) ;
    /* Intercept colorspace is the same as current. */
  }
  else if ( oType( *pResrc ) == OARRAY ||
            oType( *pResrc ) == OPACKEDARRAY ) {
    OBJECT *olist;

    /* Resolve any indirect references */
    if ( !pdf_resolvexrefs( pdfc , pResrc ) )
      return FALSE;

    olist = oArray( *pResrc ) ;

    pdfx_icept_cspace = oNameNumber(*olist) ;

    /* If we are mapping an alternative colorspace, we don't want to
     * intercept it because of the possibility that the Default colorspace
     * will be the original, eg. ICCBased one, thus creating a circular
     * reference and ending up with a infinite loop. */
    if ( parentcspace != NULL_COLORSPACE &&
         parentcspace == oNameNumber(*pResrc) )
      return TRUE ;

    switch ( theLen(*pResrc) ) {
    case 1 :
      if ( oType( *olist ) != ONAME )
        return error_handler( TYPECHECK ) ;
      if ( oNameNumber(*olist) != device_cspace )
        return error_handler( RANGECHECK ) ;
      /* Intercept colorspace is the same as current. */
      break ;

    case 2 :  /* of type:-  [ /name -dict-or-stream- ]  - ie. cal & ICC */
      {
        int32 cspace, new_numColors ;
        COLORSPACE_ID new_cpsaceId ;

        if ( oType( *olist ) != ONAME )
          return error_handler( TYPECHECK ) ;
        cspace = oNameNumber(*olist) ;

        switch ( cspace ) {
        case NAME_CalGray :
          if ( device_cspace != NAME_DeviceGray )
            return error_handler( RANGECHECK ) ;
          if ( !pdf_mapcolorspace( pdfc , pResrc , colorspace ,
                                   NULL_COLORSPACE ))
            return FALSE ;
          break ;
        case NAME_CalRGB :
          if ( device_cspace != NAME_DeviceRGB )
            return error_handler( RANGECHECK ) ;
          if ( !pdf_mapcolorspace( pdfc , pResrc , colorspace ,
                                   NULL_COLORSPACE ))
            return FALSE ;
          break ;
        case NAME_ICCBased :
          if ( !pdf_mapcolorspace( pdfc , pResrc , colorspace ,
                                   NULL_COLORSPACE ))
            return FALSE ;
          if (! gsc_getcolorspacesizeandtype( gstateptr->colorInfo, colorspace,
                                              &new_cpsaceId, &new_numColors ))
            return FALSE ;
          if ( numColors != new_numColors )
            return error_handler( RANGECHECK ) ;
          break ;
        default :
          return error_handler( TYPECHECK ) ;
        }
      }
      break ;

    case 4 :  /* of type 'separation' or 'deviceN' */
    case 5 :
      {
        int32 cspace, new_numColors ;
        COLORSPACE_ID new_cpsaceId ;

        if ( oType( *olist ) != ONAME )
          return error_handler( TYPECHECK ) ;
        cspace = oNameNumber(*olist) ;

        switch ( cspace ) {
        case NAME_Separation :
        case NAME_DeviceN :
          if ( !pdf_mapcolorspace( pdfc , pResrc , colorspace ,
                                   NULL_COLORSPACE ))
            return FALSE ;
          if (! gsc_getcolorspacesizeandtype( gstateptr->colorInfo, colorspace,
                                              &new_cpsaceId, &new_numColors ))
            return FALSE ;
          HQASSERT( (cspace == NAME_Separation &&
                     new_cpsaceId == SPACE_Separation) ||
                    (cspace == NAME_DeviceN && new_cpsaceId == SPACE_DeviceN),
                    "Inconsistent colorspaces" ) ;
          if ( numColors != new_numColors )
            return error_handler( RANGECHECK ) ;
          break ;
        default :
          return error_handler( TYPECHECK ) ;
        }
      }
      break ;

    default :
      return error_handler( TYPECHECK ) ;
    }
  }
  else
    return error_handler( TYPECHECK ) ;

  if (pResrc != NULL) {
    if (! pdfxCheckDefaultColorSpace( pdfc, pdfx_icept_cspace ))
      return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool pdf_mapBlendSpace(PDFCONTEXT* pdfc,
                       OBJECT colorspace,
                       OBJECT* mappedcolorspace)
{
  COLORSPACE_ID mappedSpaceId;
  int32 nComps;

  GET_PDFXC_AND_IXC;

  HQASSERT(mappedcolorspace != NULL, "mappedcolorspace is null");

  if (! pdf_resolvexrefs(pdfc, &colorspace))
    return FALSE;

  /* First map the blend space in the same way as any other color space. */
  if (! pdf_mapcolorspace(pdfc, &colorspace, mappedcolorspace, NULL_COLORSPACE))
    return FALSE;

  /* Validate the mapped color space. In addition to the rules for object color
   * spaces, there are additional rules for blend spaces.
   * NB. CalGray and CalRGB are remapped back to their device spaces because
   * although it goes against the spec, in practice all device independent blend
   * spaces are ICC.
   */
  if (!gsc_getcolorspacesizeandtype(gstateptr->colorInfo, mappedcolorspace,
                                    &mappedSpaceId, &nComps))
    return FALSE;

  switch (mappedSpaceId) {
  case SPACE_DeviceGray:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMYK:
    break;
  case SPACE_CalGray:
    HQFAIL("We've found a job with a Gray blend space. Deal with it.");
    if (!gsc_getInternalColorSpace(SPACE_DeviceGray, &mappedcolorspace))
      return FALSE;
    break;
  case SPACE_CalRGB:
    HQFAIL("We've found a job with a CalRGB blend space. Deal with it.");
    if (!gsc_getInternalColorSpace(SPACE_DeviceRGB, &mappedcolorspace))
      return FALSE;
    break;
  case SPACE_ICCBased:
    if (!gsc_isInvertible(gstateptr->colorInfo, mappedcolorspace))
      return detail_error_handler(RANGECHECK, "Invalid blend space");
    break;
  case SPACE_Pattern:
  case SPACE_Indexed:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_Lab:
    /* Not a valid color space for a group dictionary */
    return error_handler(RANGECHECK);
  default:
    HQFAIL("Unrecognised PDF color space in Group dictionary");
    return error_handler(RANGECHECK);
  }

#if defined(METRICS_BUILD)
  updateBlendSpaceMetrics(mappedSpaceId, nComps);
#endif

  return TRUE;
}

static Bool ignore_n_reals(int32 n, STACK *stack)
{
  int32 stacksize, i;

  HQASSERT(n > 0, "Cannot ignore 0 operands");
  HQASSERT(stack != NULL, "NULL stack");

  stacksize= theIStackSize( stack ) ;
  if ( stacksize < n-1 )
    return error_handler( STACKUNDERFLOW ) ;

  for ( i = 0 ; i < n ; i++ ) {
    OBJECT *theo ;
    USERVALUE arg ;

    theo = stackindex( i , stack ) ;
    if ( !object_get_real(theo, &arg) )
      return FALSE ;
  }

  npop(n, stack);
  return TRUE;
}

/* Implement a setcolor like operation, that does only type checking on
 * operands. This is so contexts that ignore color setting operations can do
 * so safe in the knowledge that the ignore operations are called in a
 * syntactically correct fashion. Syntax errors are not to be ignored.
 */
static Bool null_sc_op(PDFCONTEXT *pdfc, GS_COLORinfo *colorInfo,
                       STACK *stack , int32 colorType)
{
  int32 ndims ;

  switch ( gsc_getcolorspace( colorInfo, colorType ) ) {
  case SPACE_DeviceGray:
  /* When an scn SCN sc or SC is executed within a cached type3 char
   * content stream, if the glyph is being cached the color space will be
   * Gray I believe.
   */
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMYK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_Lab:
  case SPACE_CalGray:
  case SPACE_CalRGB:
  case SPACE_ICCBased:
    ndims = gsc_dimensions(colorInfo, colorType) ;
    return ignore_n_reals( ndims, stack );

  case SPACE_Indexed:
    ndims = 1;
    return ignore_n_reals( ndims, stack );

  case SPACE_CMM:
    HQFAIL("CMM colorspace NYI");
    break;

  case SPACE_Pattern:
    {
      OBJECT resname = OBJECT_NOTVM_NOTHING, *stream ;

      /* require a name on top of stack, followed by component values for
       * underlying color space if pattern is uncolored. The name should
       * refer to a pattern dictionary in the resources for the current
       * context. Colorspace dimensions are the dimensions of the underlying
       * colorspace for uncolored patterns, and 0 otherwise.
       */

      ndims = gsc_dimensions( colorInfo, colorType ) ;

      if ( theIStackSize( stack ) < 0 )
        return error_handler( STACKUNDERFLOW ) ;

      Copy( &resname , theITop( stack )) ;

      if ( oType( resname ) != ONAME )
        return error_handler( TYPECHECK ) ;

      /* How much checking of the dictionary is required here? Add content
       * checks here if ever needed. We check the resource is at least a type
       * that would be usable.
       */
      if ( ! pdf_get_resource( pdfc , NAME_Pattern , &resname , & stream ))
        return FALSE ;

      if ( oType( *stream ) != OFILE && oType(*stream) != ODICTIONARY )
        return error_handler( TYPECHECK ) ;

      npop( 1, stack );

      if ( ndims != 0 )
        return ignore_n_reals( ndims, stack);
    }

  default:
    HQFAIL( "unrecognized color space" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  HQFAIL("Should NOT be here");
  return TRUE ;
}

/* Log stripped */
