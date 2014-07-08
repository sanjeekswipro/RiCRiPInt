/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfgstat.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Graphics State operators
 */

#include "core.h"

#include "gs_spotfn.h"          /* findSpotFunctionObject */
#include "gscdevci.h"           /* gsc_setoverprint */
#include "gschtone.h"           /* gsc_setdefaulthalftoneinfo */
#include "gscsmplk.h"           /* gsc_setblackgeneration */
#include "gscxfer.h"            /* gsc_settransfers */
#include "gstate.h"             /* gs_setdash */
#include "namedef_.h"           /* NAME_* */
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */
#include "shadex.h"             /* gs_setsmooth */
#include "stream.h"             /* streamLookupDict */
#include "swerrors.h"           /* TYPECHECK */
#include "tranState.h"          /* tsSetConstantAlpha */

#include "swpdf.h"              /* PDFXCONTEXT */
#include "pdfhtname.h"          /* pdf_convertpdfnametointernal */
#include "pdfmatch.h"           /* pdf_dictmatch */
#include "pdfmem.h"             /* pdf_create_dictionary */
#include "pdfstrm.h"            /* pdf_createfilte */
#include "pdfxref.h"            /* pdf_lookupxref */

#include "pdfattrs.h"           /* pdf_get_resource */
#include "pdfcolor.h"           /* pdf_set_rendering_intent */
#include "pdfgs4.h"             /* pdf_setBlendMode */
#include "pdfin.h"              /* PDF_IMC_PARAMS */
#include "pdffont.h"            /* PDF_FONTDETAILS */
#include "pdfx.h"               /* pdfxCheckExtGState */

#include "pdfgstat.h"


/* ---------------------------------------------------------------------- */

typedef struct {
  PDFCONTEXT  *pdfc ;
  int32       defaultHalftoneUsed ;
} MAPHALFTONECONTEXT ;

static Bool pdf_mapspotfunction(OBJECT *theo) ;
static Bool pdf_maptransferfunction(PDFCONTEXT *pdfc, OBJECT *thed) ;
static Bool pdf_mapthresholdscreen(PDFCONTEXT *pdfc, OBJECT *thes) ;
static Bool pdf_maphalftone(PDFCONTEXT *pdfc, OBJECT *theo,
                              int32 *defaultHalftoneUsed, uint8 toplevel) ;
static Bool pdf_halftone_dictwalkfn(OBJECT *separation, OBJECT *theo,
                                      void *args) ;
static Bool pdf_sethalftone(PDFCONTEXT *pdfc, STACK *stack, OBJECT *theo) ;
static Bool pdf_settransferfunction(PDFCONTEXT *pdfc, STACK *stack,
                                    OBJECT *theo) ;
static Bool pdf_setdefaulttransferfunction(PDFCONTEXT *pdfc, STACK *stack) ;
static Bool pdf_setdefaultblackgeneration(PDFCONTEXT *pdfc, STACK *stack) ;
static Bool pdf_setdefaultundercolorremoval(PDFCONTEXT *pdfc, STACK *stack) ;

/* ---------------------------------------------------------------------- */

/** The PDF operator d. */
Bool pdfop_d(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;
  STACK *stack ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = & ( imc->pdfstack ) ;

  if ( ! gs_setdash( stack , FALSE ))
    return FALSE ;

  npop( 2 , stack ) ;
  return TRUE ;
}

/** The PDF operator i. */
Bool pdfop_i(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  return gs_setflat( & imc->pdfstack ) ;
}

/** The PDF operator j. */
Bool pdfop_j(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  return gs_setlinejoin( & imc->pdfstack ) ;
}

/** The PDF operator J. */
Bool pdfop_J(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  return gs_setlinecap( & imc->pdfstack ) ;
}

/** The PDF operator M. */
Bool pdfop_M(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  return gs_setmiterlimit( & imc->pdfstack ) ;
}

/** The PDF operator w. */
Bool pdfop_w(PDFCONTEXT *pdfc)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  return gs_setlinewidth( & imc->pdfstack ) ;
}

static NAMETYPEMATCH extgstate_dictmatch[] = {
/* 0 */ { NAME_Type  | OOPTIONAL, 2, { ONAME, OINDIRECT }},
/* 1 */ { NAME_SA    | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
/* 2 */ { NAME_OP    | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
/* 3 */ { NAME_BG    | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 4 */ { NAME_BG2   | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 5 */ { NAME_UCR   | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 6 */ { NAME_UCR2  | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 7 */ { NAME_TR    | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 8 */ { NAME_TR2   | OOPTIONAL, 6, { ONAME, OARRAY, OPACKEDARRAY, OFILE,
                                      ODICTIONARY, OINDIRECT }},
/* 9 */ { NAME_HT    | OOPTIONAL, 4, { ONAME, OFILE, ODICTIONARY, OINDIRECT }},
/* 10*/ { NAME_HTP   | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 11*/ { NAME_SM    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 12*/ { NAME_Font  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 13*/ { NAME_LW    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 14*/ { NAME_LC    | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
/* 15*/ { NAME_LJ    | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
/* 16*/ { NAME_ML    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 17*/ { NAME_D     | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 18*/ { NAME_FL    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 19*/ { NAME_op    | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
/* 20*/ { NAME_OPM   | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
/* 21*/ { NAME_RI    | OOPTIONAL, 2, { ONAME, OINDIRECT }},
/* 22*/ { NAME_ca    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 23*/ { NAME_CA    | OOPTIONAL, 3, { OREAL, OINTEGER, OINDIRECT }},
/* 24*/ { NAME_SMask | OOPTIONAL, 3, { ONAME, ODICTIONARY, OINDIRECT }},
/* 25*/ { NAME_AIS   | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
/* 26*/ { NAME_BM    | OOPTIONAL, 4, { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 27*/ { NAME_TK    | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
         DUMMY_END_MATCH
} ;


/** The PDF operator gs. */
Bool pdfop_gs(PDFCONTEXT *pdfc)
{
  STACK *stack ;
  int32 stacksize ;
  OBJECT *res ;
  OBJECT *inst ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  inst = TopStack( *stack , stacksize ) ;

  if ( ! pdf_get_resource( pdfc , NAME_ExtGState , inst , &res ) )
    return FALSE ;

  if ( ! pdf_set_extgstate(pdfc, res) )
    return FALSE ;

  pop(stack) ;
  return TRUE ;
}

Bool pdf_set_extgstate(PDFCONTEXT *pdfc, OBJECT *res)
{
  STACK *stack ;
  OBJECT *theo ;
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  stack = & imc->pdfstack ;

  if ( oType( *res ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! pdfxCheckExtGState( pdfc , res ))
    return FAILURE( FALSE ) ;

  if ( ! pdf_dictmatch( pdfc , res , extgstate_dictmatch ))
    return FAILURE( FALSE ) ;

  /* /Type key: absence is only bad if strictpdf is set,
     but if present, it must be right. */
  if ( extgstate_dictmatch[ 0 ].result != NULL &&
       oName( *(extgstate_dictmatch[0].result) ) != system_names + NAME_ExtGState )
    return error_handler( TYPECHECK ) ;

  /* SA  Stroke Adjust */
  theo = extgstate_dictmatch[ 1 ].result ;
  if ( theo )
    thegsDeviceStrokeAdjust(*gstateptr) = ( uint8 ) oBool(*theo) ;

  /* OP  Over-Print Stroke
   * Note that the value of this key is the default value of the /op key
   * too.
   */
  theo = extgstate_dictmatch[ 2 ].result ;
  if ( theo ) {
    int32 overprint = oBool( *theo );
    if (!gsc_setoverprint( gstateptr->colorInfo , GSC_STROKE , overprint ))
      return FAILURE( FALSE ) ;
    if ( ! extgstate_dictmatch[ 19 ].result )
      if (!gsc_setoverprint( gstateptr->colorInfo , GSC_FILL , overprint ))
        return FAILURE( FALSE ) ;
  }

  /* op Over-Print fill */
  theo = extgstate_dictmatch[ 19 ].result ;
  if ( theo )
    if (!gsc_setoverprint( gstateptr->colorInfo , GSC_FILL , oBool( *theo ) ))
      return FAILURE( FALSE ) ;


  /* OPM Over Print Mode */
  theo = extgstate_dictmatch[ 20 ].result ;
  if ( theo ) {
    int32 opmval = oInteger( *theo );

    /* Value can only be 0 or 1 */
    if (opmval < 0 || opmval > 1)
      return error_handler( RANGECHECK ) ;

    if (!gsc_setoverprintmode( gstateptr->colorInfo, opmval ))
      return FAILURE( FALSE ) ;

    /* This may be required in a postscript callout from pdf */
    if (!gsc_setcurrentoverprintmode(gstateptr->colorInfo, opmval))
      return FAILURE( FALSE ) ;
  }

  /* BG, BG2  Black-Generation */
  theo = extgstate_dictmatch[ 4 ].result ;
  if (theo) {
    /* BG2 */
    if ( oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaultblackgeneration( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  } else {
    /* BG, allows in non-strict case for it to be a name like BG2 */
    theo = extgstate_dictmatch[ 3 ].result ;
    if ( theo && !ixc->strictpdf &&
         oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaultblackgeneration( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  }

  if ( theo &&
       ( ! pdf_map_function( pdfc , theo ) ||
         ! push( theo , stack ) ||
         ! gsc_setblackgeneration( pdfxc->corecontext,
                                   gstateptr->colorInfo, stack )))
    return FAILURE( FALSE ) ;

  /* UCR, UCR2 Under-Colour Removal */
  theo = extgstate_dictmatch[ 6 ].result ;
  if (theo) {
    /* UCR2 */
    if ( oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaultundercolorremoval( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  } else {
    /* UCR, allows in non-strict case for it to be a name like UCR2 */
    theo = extgstate_dictmatch[ 5 ].result ;
    if ( theo && !ixc->strictpdf &&
         oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaultundercolorremoval( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  }

  if ( theo &&
       ( ! pdf_map_function( pdfc , theo ) ||
         ! push( theo , stack ) ||
         ! gsc_setundercolorremoval( pdfxc->corecontext,
                                     gstateptr->colorInfo, stack )))
    return FAILURE( FALSE ) ;

  /* TR, TR2  Transfer */
  theo = extgstate_dictmatch[ 8 ].result ;
  if (theo) {
    /* TR2 */
    if ( oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaulttransferfunction( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  } else {
    /* TR, allows in non-strict case for it to be a name like TR2 */
    theo = extgstate_dictmatch[ 7 ].result ;
    if ( theo && !ixc->strictpdf &&
         oType( *theo ) == ONAME &&
         oName( *theo ) == system_names + NAME_Default ) {
      if ( ! pdf_setdefaulttransferfunction( pdfc , stack ))
        return FAILURE( FALSE ) ;

      theo = NULL ;
    }
  }

  if ( theo ) {
    if ( ! pdf_settransferfunction( pdfc , stack , theo ))
      return FAILURE( FALSE ) ;
  }

  /* HT  Half-Tone */
  theo = extgstate_dictmatch[ 9 ].result ;
  if ( theo &&
       ! pdf_sethalftone( pdfc , stack , theo ))
    return FAILURE( FALSE ) ;

  /* HTP Half-Tone Phase */
  theo  = extgstate_dictmatch[ 10 ].result ;
  if ( theo ) {
    OBJECT *obj ;

    if ( theILen( theo ) != 2 )
      return error_handler( SYNTAXERROR ) ;

    obj = oArray( *theo ) ;

    if ( ! push2( obj , &obj[1] , stack ))
      return FAILURE( FALSE ) ;

    if ( ! do_sethalftonephase( stack ))
      return FAILURE( FALSE ) ;
  }

  /* SM smoothness */
  theo = extgstate_dictmatch[ 11 ].result ;
  if ( theo &&
       ( ! push( theo , stack ) ||
         ! gs_setsmooth( stack )))
    return FAILURE( FALSE ) ;

  /* Font */
  /* Largely copied from pdfop_Tf.  Main difference is that the Tf operator
   * requires the font to be a Name (the value of the /Name field in the
   * font dictionary, whereas here it is only an "indirect reference to a
   * font".
   */
  theo = extgstate_dictmatch[ 12 ].result ;
  if ( theo ) {
    OBJECT *olist ;

    if ( theILen( theo ) != 2)
      return error_handler( SYNTAXERROR ) ;

    /* store font object */
    olist = oArray( *theo ) ;

    Copy( &theIPDFFFont( gstateptr ) , olist ) ;
    olist ++ ;

    /* resolve indirect reference to font size and store it.
     */
    if ( oType( *olist ) == OINDIRECT &&
         ! pdf_lookupxref( pdfc , &olist , oXRefID( *olist ) ,
                           theIGen( olist ) , FALSE ))
      return error_handler( UNDEFINEDRESOURCE ) ;

    if ( !object_get_numeric(olist, &theIPDFFFontSize( gstateptr )) )
      return FAILURE( FALSE ) ;
  }

  /* LW line width */
  theo = extgstate_dictmatch[ 13 ].result ;
  if ( theo &&
       ( ! push( theo, stack ) ||
         ! gs_setlinewidth( stack )))
    return FAILURE( FALSE ) ;


  /* LC line cap */
  theo = extgstate_dictmatch[ 14 ].result ;
  if ( theo &&
       ( ! push( theo, stack ) ||
         ! gs_setlinecap( stack )))
    return FAILURE( FALSE ) ;


  /* LJ line join */
  theo = extgstate_dictmatch[ 15 ].result ;
  if ( theo &&
       ( ! push( theo, stack ) ||
         ! gs_setlinejoin( stack )))
    return FAILURE( FALSE ) ;


  /* ML mitre limit */
  theo = extgstate_dictmatch[ 16 ].result ;
  if ( theo &&
       ( ! push( theo, stack ) ||
         ! gs_setmiterlimit( stack )))
    return FAILURE( FALSE ) ;


  /* D dash pattern */
  theo = extgstate_dictmatch[ 17 ].result ;
  if ( theo ) {
    OBJECT *obj ;

    if ( theILen( theo ) != 2 )
      return error_handler( SYNTAXERROR ) ;

    obj = oArray( *theo ) ;

    if ( !push2( obj , & ( obj[ 1 ] ) , stack ))
      return FAILURE( FALSE ) ;

    if ( ! gs_setdash( stack , TRUE ))
      return FAILURE( FALSE ) ;
  }


  /* FL flatness */
  /* note: the setflat operator is defined to have range 0.2 - 100.0, while
   * the pdf i operator allows 0 - 100.0, where 0 means device's default
   * flatness.  If we do use a value less than 0.2, the ps code ought to
   * round it to 0.2. */
  theo = extgstate_dictmatch[ 18 ].result ;
  if ( theo &&
       ( ! push( theo, stack ) ||
         ! gs_setflat( stack )))
    return FAILURE( FALSE ) ;

  /* RI Rendering Intent */
  theo = extgstate_dictmatch[ 21 ].result ;
  if ( theo ) {
    if (!pdf_set_rendering_intent(pdfc, theo) )
      return FAILURE( FALSE ) ;
  }

  /* ca - non-stroking constant alpha */
  theo = extgstate_dictmatch[ 22 ].result ;
  if ( theo ) {
    tsSetConstantAlpha(gsTranState(gstateptr), FALSE,
                       (USERVALUE)object_numeric_value(theo), gstateptr->colorInfo);
  }

  /* CA - stroking constant alpha */
  theo = extgstate_dictmatch[ 23 ].result ;
  if ( theo ) {
    tsSetConstantAlpha(gsTranState(gstateptr), TRUE,
                       (USERVALUE)object_numeric_value(theo), gstateptr->colorInfo);
  }

  /* AIS Alpha is shape (TRUE) or opacity (FALSE) */
  theo = extgstate_dictmatch[ 25 ].result ;
  if ( theo ) {
    tsSetAlphaIsShape( gsTranState( gstateptr ), oBool( *theo ));
  }

  /* BM Name of blend mode or a array of alternative blend modes */
  theo = extgstate_dictmatch[ 26 ].result ;
  if ( theo && ! pdf_setBlendMode( pdfc, *theo ))
    return FAILURE( FALSE ) ;

  /* TK Text knockout parameter */
  theo = extgstate_dictmatch[ 27 ].result ;
  if ( theo ) {
    tsSetTextKnockout( gsTranState( gstateptr ), oBool( *theo ));
  }

  /* SMask - soft mask.
  !!WARNING!! This must be set after all other state has been set (because a group
  will be dispatched for the soft mask, and the group needs to inherit all settings
  specified in the same ExtGState dictionary as the soft mask itself). */
  theo = extgstate_dictmatch[ 24 ].result ;
  if ( theo && ! pdf_setSoftMask( pdfc, *theo ))
    return FAILURE( FALSE ) ;

  return TRUE ;

}

static Bool pdf_mapspotfunction(OBJECT *theo)
{
  OBJECT *res ;

  HQASSERT( theo , "theo is null in pdf_mapspotfunction." ) ;
  HQASSERT( oType( *theo ) == ODICTIONARY ,
            "theo is not a dictionary in pdf_mapspotfunction." ) ;

  if ( NULL == (res = fast_extract_hash_name(theo, NAME_SpotFunction)) )
    return error_handler( TYPECHECK ) ;
  switch ( oType( *res ) ) {
  case ONAME :
    return pdf_convertpdfnametointernal( res );
  case OFILE :
  case OARRAY:
  case OPACKEDARRAY:
    return TRUE;
  default:
    return error_handler( TYPECHECK );
  }
  /* Not reached */
}

Bool pdf_map_function(PDFCONTEXT *pdfc, OBJECT *theo)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS  *ixc ;

  HQASSERT( theo , "theo NULL in pdf_map_function." ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  switch ( oType( *theo )) {
    case OFILE :
      if ( NULL == ( theo = streamLookupDict( theo )))
        return error_handler( UNDEFINED ) ;
      /* fall through */

    case ODICTIONARY :
      return pdf_resolvexrefs( pdfc , theo ) ;

    case ONAME :
      if (oName( *theo ) != system_names + NAME_Identity )
      {
        /* unless strict, accept the NAME_Default as well */
        if (( oName( *theo ) != system_names + NAME_Default ) ||
            (ixc->strictpdf) )
        return error_handler( UNDEFINED ) ;
      }

      theITags( theo ) = OARRAY | EXECUTABLE | UNLIMITED ;
      theILen( theo ) = 0 ;
      oArray( *theo ) = NULL ;

      return TRUE ;

    case OARRAY :
    case OPACKEDARRAY :
      return TRUE ;

    default :
      return error_handler( TYPECHECK ) ;
  }

  /* Not reached. */
}

static Bool pdf_maptransferfunction(PDFCONTEXT *pdfc, OBJECT *thed)
{
  OBJECT *theo ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdfc , "pdfc is NULL in pdf_maptransferfunction." ) ;
  HQASSERT( thed , "theo NULL in pdf_maptransferfunction." ) ;
  HQASSERT( oType( *thed ) == ODICTIONARY ,
            "thed is not a dictionary in pdf_maptransferfunction." ) ;

  if ( NULL == ( theo = fast_extract_hash_name( thed, NAME_TransferFunction )))
    /* /TransferFunction is optional. */
    return TRUE ;

 return pdf_map_function( pdfc , theo ) ;
}

/** \brief
 * Call this to map a stream of threshold values.
 *
 * Pass the stream in "thes".
 * (This gets changed to be the mapped stream object).
 */
static Bool pdf_mapthresholdscreen(PDFCONTEXT *pdfc, OBJECT *thes)
{
  OBJECT onameFilterType = OBJECT_NOTVM_NAME(NAME_ReusableStreamDecode, LITERAL) ;
  OBJECT odictFilterArgs = OBJECT_NOTVM_NOTHING ;

  HQASSERT( thes && oType( *thes ) == OFILE, "bad stream" ) ;

  /* Layer an RSD onto the stream "thes".
   * We create the filter now, to give it PDF lifetime (see 30573).
   * We don't want to pass any args to RSD, but we have to pass it an empty
   * dictionary or it will start hunting about on the operandstack...
   * The args dictionary from pdf_create_dictionary will get freed when
   * the context ends.
   */
  if ( !pdf_create_dictionary( pdfc , 0 , & odictFilterArgs ))
    return FALSE ;
  if ( !pdf_createfilter(pdfc, thes, &onameFilterType, &odictFilterArgs, FALSE) )
    return FALSE ;

  /* Set the Threshold flag, to tell PS that this threshold file has already
   * had its RSD added.
   * Note: In SWv20!src:gu_misc.c(1.127), common_newthreshold_init() first
   * layers on a SubFileDecode, to make sure reading from the file stops at
   * the end of the threshold data; this is unnecessary in PDF because there
   * is already a StreamDecode at the bottom of the chain of filters, which
   * will stop at the "endstream" token.
   */
  SetIThresholdFlag(oFile(*thes));

  return TRUE ;
}


/* pdf_halftone_dictwalkfn
 * -----------------------
 * Call this with each sub-halftone of a type5.
 */
static Bool pdf_halftone_dictwalkfn(OBJECT *separation, OBJECT *theo, void *args)
{
  MAPHALFTONECONTEXT *maphalftoneContext = args ;
  PDFCONTEXT *pdfc = maphalftoneContext->pdfc ;
  NAMECACHE *name ;
  Bool defaultHalftoneUsed = FALSE ;

  HQASSERT( separation , "separation is NULL in pdf_halftone_dictwalkfn." ) ;
  HQASSERT( oType( *separation ) == ONAME ,
            "separation must be an ONAME object in pdf_halftone_dictwalkfn." ) ;
  HQASSERT( theo , "theo is NULL in pdf_halftone_dictwalkfn." ) ;
  HQASSERT( pdfc , "pdfc is NULL in pdf_halftone_dictwalkfn." ) ;

  /* Skip over the dictionary entries which are not separations. */
  name = oName( *separation ) ;
  if ( name == system_names + NAME_Type ||
       name == system_names + NAME_HalftoneType ||
       name == system_names + NAME_HalftoneName )
    return TRUE ;

  /* Ignore things that can't be halftones */
  if ( oType( *theo ) != ODICTIONARY  &&  oType( *theo ) != OFILE )
    return TRUE ;

  /* The FALSE argument indicates that this is not the top
     level halftone. Ie we're inside a type 5 halftone. */
  if ( !pdf_maphalftone( pdfc , theo , &defaultHalftoneUsed , FALSE ) )
    return FALSE ;

  /* If any of the children set defaultHalftoneUsed then return TRUE for the parent */
  if ( defaultHalftoneUsed )
    maphalftoneContext->defaultHalftoneUsed = TRUE ;

  return TRUE ;
}

/** \brief
 * Call this to map a PDF halftone structure into a form acceptable to PS.
 *
 * Pass the PDF halftone structure in "theo".
 * (This gets changed to be the mapped PS-compatible halftone dictionary).
 *
 * Type5 dictionaries cannot be nested; they are only allowed if "toplevel"
 * is true.
 *
 * A 'PDF halftone structure' means the value of the "/HT" key of an
 * ExtGState dictionary.  Parts of the structure may be as-yet unresolved
 * indirect objects; they will get brought into memory during mapping.
 * The structure may be a dictionary or a stream.
 *
 * In PS a threshold halftone (eg. type6) is a dictionary, with the data
 * under the /Thresholds key.  In PDF a threshold halftone is a stream
 * (holding the data) whose stream-dictionary holds the expected key-value
 * pairs (/Width, /Height, /TransferFunction, etc).
 * (See PDF1.3, s.7.16.5, p.284)
 *
 * To map such a stream, the stream's stream-dictionary is retrieved with
 * streamLookupDict(); this dictionary becomes the PS halftone dictionary.
 * The stream is added to its own dictionary under the Thresholds key.
 *
 * After mapping, everything must be in-memory PS objects, as the PS world
 * expects.  The lifetimes of all objects created must match the lifetime
 * of the PDF halftone structure.  (See also [30573]).
 *
 * Returns:
 * The "*theo" object is changed to or overwritten with the mapped halftone.
 */
static Bool pdf_maphalftone(PDFCONTEXT *pdfc, OBJECT *theo,
                            Bool *defaultHalftoneUsed , uint8 toplevel)
{
  OBJECT mappedkey = OBJECT_NOTVM_NAME(NAME_Mapped, LITERAL) ;
  OBJECT *thed = NULL ;
  OBJECT *thes = NULL ;
  OBJECT *halftonetypeobj ;
  OBJECT *halftonenameobj ;
  int32  halftonetype ;
  MAPHALFTONECONTEXT maphalftoneContext ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdfc , "pdfc is NULL in pdf_maphalftone." ) ;
  HQASSERT( theo , "theo is NULL in pdf_maphalftone." ) ;

  *defaultHalftoneUsed = FALSE ;

  /* If the halftone is a stream, retrieve the stream-dictionary */
  if ( oType( *theo ) == OFILE ) {
    thes = theo ;
    thed = streamLookupDict( theo ) ;
    if ( thed == NULL )
      return error_handler( UNDEFINED ) ;
  }
  else if ( oType( *theo ) == ODICTIONARY ) {
    thed = theo ;
  }
  else {
    return error_handler( RANGECHECK ) ;
  }
  HQASSERT( oType( *thed ) == ODICTIONARY ,
            "thed is not an ODICTIONARY in pdf_maphalftone." ) ;

  /* Check for "/Mapped true" entry,
     this indicates the halftone has already been mapped */
  if ( fast_extract_hash( thed , & mappedkey ) != NULL ) {
    /* Dictionary has already been mapped,
       just need swap the stream for the dictionary */
    if ( oType( *theo ) == OFILE )
      Copy( theo , thed ) ;
    return TRUE ; /* Already mapped */
  }

  /* Read in any indirect objects */
  if ( ! pdf_resolvexrefs( pdfc , thed ))
    return FALSE ;

  /* See if the (optional) HalftoneName key is present. */
  if ( NULL != ( halftonenameobj = fast_extract_hash_name( thed , NAME_HalftoneName ))) {
    OBJECT    *systemScreen ;
    NAMECACHE *halftonename ;

    if ( ! pdfxHalftoneNameDetected( pdfc ))
      return FALSE ;

    if ( oType( *halftonenameobj ) != OSTRING )
      return error_handler( TYPECHECK ) ;

    halftonename = cachename( oString( *halftonenameobj ) , (uint32) theILen( halftonenameobj )) ;
    if ( halftonename == NULL )
      return FALSE ;

    /* Resolve the HalftoneName from the known system screens */
    systemScreen = findSpotFunctionObject( halftonename ) ;

    if ( systemScreen != NULL ) {
      if ( oType( *systemScreen ) == ODICTIONARY ) {
        /* If the system screen is a dictionary, copy it into the pdf halftone
         * for reuse. Don't use the original because it will get mangled later.
         * Also, the halftone dictionary is not flagged as mapped so that the
         * required system screen is always used in preference.
         */
        if ( ! pdf_copyobject( NULL, systemScreen, theo ) )
          return FALSE ;

        return TRUE ;
      }
    }
  }

  /* Obtain the (required) HalftoneType value */
  if ( NULL == ( halftonetypeobj = fast_extract_hash_name( thed , NAME_HalftoneType ))) {
    /* Except it's not required if HalftoneName is present, when we set the
     * default screen.
     */
    if (halftonenameobj != NULL) {
      *defaultHalftoneUsed = TRUE ;
      /* The halftone dictionary is not flagged as mapped This is intentional
       * so that default screen is always used.
       */
      return gsc_setdefaulthalftoneinfo( gstateptr->colorInfo,
                                         ixc->pPageStartGState->colorInfo ) ;
    }
    else
      return error_handler( TYPECHECK ) ;
  }
  else if ( oType( *halftonetypeobj ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  halftonetype = oInteger( *halftonetypeobj ) ;

  if ( ! pdfxCheckHalftoneType( pdfc , halftonetype ))
    return FALSE ;

  switch ( halftonetype ) {
  case 1 :

    if ( ! pdf_maptransferfunction( pdfc , thed ))
      return FALSE ;

    if ( ! pdf_mapspotfunction( thed ))
      return FALSE ;

    break ;

  case 5 :

    if ( ! toplevel )
      return error_handler( RANGECHECK ) ; /* Can't have a type 5 within a type 5. */

    maphalftoneContext.pdfc = pdfc ;
    maphalftoneContext.defaultHalftoneUsed = FALSE ;
    if ( !walk_dictionary( thed , pdf_halftone_dictwalkfn , ( void * )&maphalftoneContext ) )
      return FALSE ;

    /* If any of the children set defaultHalftoneUsed then return TRUE for the parent */
    if ( maphalftoneContext.defaultHalftoneUsed ) {
      *defaultHalftoneUsed = TRUE ;
      return TRUE ;
    }

    break ;

  case 6 :
  case 10 :
  case 16 :
    HQASSERT(halftonetype != 16, "UNTESTED: Type 16 halfones: Please send the PDF to development");
    {
      OBJECT key = OBJECT_NOTVM_NAME(NAME_Thresholds, LITERAL) ;

      /* Threshold, so it should be a stream */
      if ( thes == NULL)
        return error_handler( TYPECHECK ) ;

      /* Map the pieces */
      if ( ! pdf_maptransferfunction( pdfc , thed ))
        return FALSE ;

      if ( ! pdf_mapthresholdscreen( pdfc , thes ))
        return FALSE ;

      /* Save stream in dictionary */
      if ( ! pdf_fast_insert_hash( pdfc , thed , & key , thes ))
        return FALSE ;

      /* Use dictionary in place of original object */
      Copy( theo , thed ) ;

      break ;
    }

  default :
    return error_handler( RANGECHECK ) ;
  }

  /* Add "/Mapped true" entry to avoid mapping this halftone again */
  if ( ! pdf_fast_insert_hash( pdfc , thed , & mappedkey , & tnewobj ))
    return FALSE ;

  return TRUE ;
}


/** \brief
 * Call this to take a PDF halftone structure, map it into a form
 * acceptable to PS, and call gs3_setscreens() with it.
 *
 * Pass a stack in "stack" (this is used to communicate with PS).
 *
 * Pass the root of the PDF halftone structure in "theo".
 * (This gets changed to be the mapped PS-compatible halftone dictionary).
 *
 * A 'PDF halftone structure' means the value of the "/HT" key of an
 * ExtGState dictionary; it may be a name, a dictionary, or a stream.
 *
 * Note: although we call it "mapping", we destructively modify the PDF
 * halftone structure, so it should perhaps be called "conversion".
 * richardk 1999-05-28.
 */
static Bool pdf_sethalftone(PDFCONTEXT *pdfc, STACK *stack, OBJECT *theo)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Bool defaultHalftoneUsed = FALSE ;
  OBJECT local = OBJECT_NOTVM_NOTHING ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( theo , "pdf_sethalftone: theo NULL" ) ;
  HQASSERT( stack , "pdf_sethalftone: stack NULL" ) ;
  HQASSERT( ixc->pPageStartGState ,
            "pdf_setdefaulttransferfunction: page start gstate NULL" ) ;

  /* Treat names specially */
  if ( oType( *theo ) == ONAME ) {

    PDFXCONTEXT *pdfxc ;
    PDF_GET_XC( pdfxc ) ;

    if ( oName( *theo ) != system_names + NAME_Default )
      return error_handler( UNDEFINED ) ;

    return gsc_setdefaulthalftoneinfo( gstateptr->colorInfo,
                                       ixc->pPageStartGState->colorInfo ) ;
  }

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED );

  /* Do not overwrite the original halftone object, since this will point to
   * an object in the XREF cache.
   */
  Copy( & local, theo ) ;

  /* Ensure the halftone has been mapped */
  if ( ! pdf_maphalftone( pdfc , & local , &defaultHalftoneUsed, TRUE  ))
    return FALSE ;

  /* The halftone (or one of its children) contained an unresolved HalftoneName
   * without a normal halftone to use as the alternative. Revert to the default.
   */
  if ( defaultHalftoneUsed )
    return TRUE ;

  HQASSERT( oType( local ) == ODICTIONARY ,
            "After mapping, the halftone should be a dictionary." ) ;

  /* Set the halftone */
  if ( ! push( & local , stack ))
    return FALSE ;

  return gsc_setscreens( gstateptr->colorInfo , stack , ST_SETHALFTONE ) ;

  /* Not reached. */
}

static Bool pdf_settransferfunction(PDFCONTEXT *pdfc , STACK *stack ,
                                    OBJECT *theo)
{
  PDFXCONTEXT *pdfxc ;
  OBJECT *olist ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc , "pdf_settransferfunction: pdfc NULL" ) ;
  HQASSERT( theo , "pdf_settransferfunction: theo NULL" ) ;
  HQASSERT( stack , "pdf_settransferfunction: stack NULL" ) ;

  switch ( oType( *theo )) {
    case OARRAY :
    case OPACKEDARRAY :
      {
        int32 i ;

        /* Allow for previously mapped Identity function
         * or an encapsulated function.
         */

        if ( theILen(theo) == 0 || oExecutable(*theo) ) {

          if ( ! push( theo , stack ))
            return FALSE ;

          return gsc_settransfers( pdfxc->corecontext,
                                   gstateptr->colorInfo, stack , 1 ) ;
        }

        /* So that must mean it's a literal array of functions of
         * some kind, which in turn means it's the setcolortransfer
         * variant. The pdf_map_function call handles any flattening.
         */

        if ( theILen( theo ) != 4 )
          return error_handler( RANGECHECK ) ;

        olist = oArray( *theo );

        for ( i = 0 ; i < 4 ; ++i , ++olist ) {

          theo = olist ;
          if ( oType( *theo ) == OINDIRECT ) {
            if ( ! pdf_lookupxref( pdfc , & theo ,
                                   oXRefID( *theo ) ,
                                   theIGen( theo ) ,
                                   FALSE ))
              return FALSE ;
            if ( theo == NULL )
              return error_handler( UNDEFINEDRESOURCE ) ;
          }

          if ( ! pdf_map_function( pdfc , theo ))
            return FALSE ;

          if ( ! push( theo , stack ))
            return FALSE ;
        }

        return gsc_settransfers( pdfxc->corecontext,
                                 gstateptr->colorInfo, stack , 4 ) ;
      }

    case OFILE :
    case ONAME :
    case ODICTIONARY :
      if ( ! pdf_map_function( pdfc , theo ))
        return FALSE ;

      if ( ! push( theo , stack ))
        return FALSE ;

      return gsc_settransfers( pdfxc->corecontext,
                               gstateptr->colorInfo, stack , 1 ) ;

    default :
      return error_handler( TYPECHECK ) ;
  }

  /* Not reached. */
}

static Bool pdf_setdefaulttransferfunction(PDFCONTEXT *pdfc, STACK *stack)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *theo ;
  int32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( stack , "pdf_setdefaulttransferfunction: stack NULL" ) ;
  HQASSERT( ixc->pPageStartGState ,
            "pdf_setdefaulttransferfunction: page start gstate NULL" ) ;

  theo = gsc_gettransferobjects( ixc->pPageStartGState->colorInfo ) ;

  for ( i = 0 ; i < 4 ; ++i ) {
    if ( ! push( theo++ , stack ))
      return FALSE ;
  }

  return gsc_settransfers( pdfxc->corecontext,
                           gstateptr->colorInfo, stack , 4 ) ;
}

static Bool pdf_setdefaultblackgeneration(PDFCONTEXT *pdfc, STACK *stack)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *theo ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( stack , "pdf_setdefaultblackgeneration: stack NULL" ) ;
  HQASSERT( ixc->pPageStartGState ,
            "pdf_setdefaultblackgeneration: page start gstate NULL" ) ;

  theo = gsc_getblackgenerationobject( ixc->pPageStartGState->colorInfo ) ;

  return ( push( theo , stack ) &&
           gsc_setblackgeneration( pdfxc->corecontext,
                                   gstateptr->colorInfo, stack )) ;
}

static Bool pdf_setdefaultundercolorremoval(PDFCONTEXT *pdfc, STACK *stack)
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *theo ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( stack , "pdf_setdefaultundercolorremoval: stack NULL" ) ;
  HQASSERT( ixc->pPageStartGState ,
            "pdf_setdefaultundercolorremoval: page start gstate NULL" ) ;

  theo = gsc_getundercolorremovalobject( ixc->pPageStartGState->colorInfo ) ;

  return ( push( theo , stack ) &&
           gsc_setundercolorremoval( pdfxc->corecontext,
                                     gstateptr->colorInfo, stack )) ;
}

/* Log stripped */
