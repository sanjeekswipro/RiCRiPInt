/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfinlop.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF inline operators Implementation
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"   /* for system_names */

#include "chartype.h"
#include "stacks.h"
#include "fileio.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "pdfexec.h"
#include "pdfops.h"
#include "pdfinlop.h"

#include "pdfin.h"


/* Static function prototypes */

typedef void (* PDFINLOP_EXPNAMFN)( OBJECT *theo ) ;

static void pdfinlop_expandnames( OBJECT *theo, PDFINLOP_EXPNAMFN thefn ) ;
static void pdfinlop_expand_key_name( OBJECT *thek ) ;
static void pdfinlop_expand_filter_name( OBJECT *theo ) ;
static void pdfinlop_expand_colorspace_name( OBJECT *theo ) ;


static NAMETYPEMATCH pdfinlop_inlinedict[] = {
/* 0 */ { NAME_Filter | OOPTIONAL,       4,  { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
/* 1 */ { NAME_DecodeParms | OOPTIONAL,  0,  { 0 }},
          DUMMY_END_MATCH
} ;


/* Begin an inline operator - count items on the stack */

Bool pdfop_BIV( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  HQASSERT( stack , "stack NULL in pdfop_BIV" ) ;

  imc->pdf_inlineopbase = theIStackSize( stack ) ;

  return TRUE ;
}


/* End an inline operator - the work should already be done */

Bool pdfop_EIV( PDFCONTEXT *pdfc )
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;

  PDF_CHECK_MC( pdfc ) ;

  return TRUE ;
}


/* Begin data for an inline operator. The key/value pairs
 * on the stack (up to the base pointed to by BIV) are rolled into
 * a dictionary with any abbreviations expanded on the way and
 * then passed to the operator's dispatcher.
 */

Bool pdfop_IVD( PDFCONTEXT *pdfc , PDFINLOP_DISPATCHFN thefn )
{
  STACK *stack ;
  int32 objcount ;
  int32 len ;
  int32 type ;
  int32 i ;
  OBJECT *theo ;
  OBJECT localdict = OBJECT_NOTVM_NOTHING ;
  Bool result = FALSE ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  /* Make a new dictionary for the arguments */

  stack = ( & imc->pdfstack ) ;
  objcount = theIStackSize( stack ) - imc->pdf_inlineopbase ;

  HQASSERT( objcount >= 0, "Object count is zero in pdfop_IVD") ;

  if (( objcount & 1 ) != 0 )
    return error_handler( UNDEFINEDRESULT ) ;

  len = (objcount / 2L) ;

  if ( ! pdf_create_dictionary( pdfc , len , & localdict ))
    return FALSE ;

  /* Loop through each key-value pair, expanding abbreviations
  * for the key and possibly the value, then stuffing the pair
  * into the dictionary.
  */

  for ( i = objcount - 1 ; i >= 1 ; i -= 2 ) {
    OBJECT *thek ;

    thek = stackindex( i , stack ) ;
    type = oType(*thek) ;
    if ( type != ONAME )
      return error_handler( TYPECHECK ) ;

    theo = stackindex( i - 1 , stack ) ;

    /* Resolve any indirect reference in the value */

    if ( oType(*theo) == OINDIRECT ) {
      if ( ! pdf_lookupxref(pdfc, &theo, oXRefID(*theo), theGen(*theo), FALSE) )
        return FALSE ;
      if ( theo == NULL )
        return error_handler( UNDEFINEDRESOURCE ) ;
    }

    /* Expand the key name if necessary */

    pdfinlop_expand_key_name( thek ) ;

    /* Expand any abbreviations in the value, too */

    if ( oName(*thek) == system_names + NAME_Filter ) {
      pdfinlop_expandnames( theo, pdfinlop_expand_filter_name );
    }
    else if ( oName(*thek) == system_names + NAME_ColorSpace ) {
      pdfinlop_expandnames( theo, pdfinlop_expand_colorspace_name );
    }

    if ( !pdf_fast_insert_hash( pdfc , & localdict , thek , theo ))
      return FALSE ;
  }

  if ( ! pdf_dictmatch( pdfc , & localdict , pdfinlop_inlinedict ))
    return FALSE ;

  /* Dispatcher works on a copy of the contents stream since the
   * data comes from a temporary filter chain on top of it.
   */

  {
    int32 ch;
    OBJECT datasource = OBJECT_NOTVM_NOTHING ;
    FILELIST *flptr ;

    /* Impose any filters on the data source (the contents stream) */
    HQASSERT( pdfc->contentsStream , "No content stream in pdfop_IVD" ) ;
    HQASSERT( oType(*pdfc->contentsStream) == OFILE ,
              "Invalid content stream in pdfop_IVD" ) ;
    Copy( & datasource , pdfc->contentsStream ) ;
    if ( ! pdf_createfilterlist( pdfc ,
                                 & datasource ,
                                 pdfinlop_inlinedict[ 0 ].result ,
                                 pdfinlop_inlinedict[ 1 ].result ,
                                 FALSE, FALSE ))
      return FALSE ;

    /* Bundle things up and send to the appropriate PS code */
    result = thefn( pdfc , & localdict , & datasource ) ;

    /* Reset the rewindable flags of any filters on top of the
     * contents stream - that way their FILELIST slots become
     * available for re-use.
     */

    flptr = oFile( datasource ) ;
    HQASSERT( flptr , "flptr NULL in pdfop_IVD" ) ;

    while ( flptr != oFile(*pdfc->contentsStream)) {
      ClearIRewindableFlag( flptr ) ;
      flptr = theIUnderFile( flptr ) ;
    }

    if ( result ) {
      /* Some apps write extra data before any whitespace before EI which Acrobat
       * seems to happily skip.  Need to skip ahead to either whitespace or E
       * which for now we will assume is the start of the EI operator.  Only
       * push E back into the stream since any whitespace will be skipped by the
       * scanner anyway.  Hitting EOF is not a problem here since it will be
       * detected and handled again in the PDF scanner.
       */
      flptr = oFile(*pdfc->contentsStream);
      do {
        ch = Getc(flptr);
        if ( ch == EOF ) {
          break;
        }
        if ( ch == 'E' ) {
          UnGetc(ch, flptr);
          break;
        }
      } while ( !IsWhiteSpace(ch) );
    }
  }

  npop( objcount, stack ) ;

  /* Shallow free, since no compound objects were stuffed in. */

  pdf_destroy_dictionary( pdfc , len, & localdict ) ;

  return result ;
}


/* Expand any abbreviated names in the given object */

static void pdfinlop_expandnames( OBJECT *theo , PDFINLOP_EXPNAMFN thefn )
{
  HQASSERT( theo , "theo NULL in pdfinlop_expandnames" ) ;
  HQASSERT( thefn , "thefn NULL in pdfinlop_expandnames" ) ;

  if ( oType(*theo) == ONAME ) {
    /* The object is a name - try to expand it */

    ( *thefn )( theo ) ;
  }
  else if ( oType(*theo) == OARRAY ||
            oType(*theo) == OPACKEDARRAY ) {
    /* It's an array - try expand any name members */

    OBJECT *member = oArray(*theo) ;
    int32 len = theLen(*theo) ;

    while ((--len) >= 0 ) {
      if ( oType(*member) == ONAME ) {
        ( *thefn )( member ) ;
      }
      ++member ;
    }
  }
  else {
    /* Don't complain - just don't do anything */
  }
}

/* If the given key name is abbreviated, replace it
 * with the full version, otherwise do nothing.
 * See PDF 1.2 spec, ss 8.9.
 */

static void pdfinlop_expand_key_name( OBJECT *thek )
{
  HQASSERT( thek , "thek NULL in pdfinlop_expand_key_name") ;
  HQASSERT( oType(*thek) == ONAME ,
            "Object not a name in pdfinlop_expand_key_name") ;

  switch ( oNameNumber(*thek) ) {
  /* Required keys first */
  case NAME_BPC:
    oName(*thek) = system_names + NAME_BitsPerComponent;
    break;
  case NAME_CS:
    oName(*thek) = system_names + NAME_ColorSpace;
    break;
  case NAME_H:
    oName(*thek) = system_names + NAME_Height;
    break;
  case NAME_W:
    oName(*thek) = system_names + NAME_Width;
    break;
  /* Now the optional ones */
  case NAME_D:
    oName(*thek) = system_names + NAME_Decode;
    break;
  case NAME_DP:
    oName(*thek) = system_names + NAME_DecodeParms;
    break;
  case NAME_F:
    oName(*thek) = system_names + NAME_Filter;
    break;
  case NAME_IM:
    oName(*thek) = system_names + NAME_ImageMask;
    break;
  case NAME_I:
    oName(*thek) = system_names + NAME_Interpolate;
    break;
  /* Vignette specific stuff */
  case NAME_NS:
    oName(*thek) = system_names + NAME_NSteps;
    break;
  case NAME_NC:
    oName(*thek) = system_names + NAME_NColors;
    break;
  case NAME_G:
    oName(*thek) = system_names + NAME_Gamma;
    break;
  case NAME_M:
    oName(*thek) = system_names + NAME_Matrix;
    break;
  case NAME_S:
    oName(*thek) = system_names + NAME_Shape;
    break;
  case NAME_SL:
    oName(*thek) = system_names + NAME_Scale;
    break;
  case NAME_SK:
    oName(*thek) = system_names + NAME_Stroke;
    break;
  case NAME_B:
    oName(*thek) = system_names + NAME_Blend;
    break;
  }
}

/* If the given filter name is abbreviated, replace it
 * with the full version, otherwise do nothing.
 * See PDF 1.2 spec, ss 8.9.
 */

static void pdfinlop_expand_filter_name( OBJECT *theo )
{
  HQASSERT( theo , "theo NULL in pdfinlop_expand_filter_name") ;
  HQASSERT( oType(*theo) == ONAME ,
            "Object not a name in pdfinlop_expand_filter_name") ;

  switch ( oNameNumber(*theo) ) {
  case NAME_A85:
    oName(*theo) = system_names + NAME_ASCII85Decode;
    break;
  case NAME_AHx:
    oName(*theo) = system_names + NAME_ASCIIHexDecode;
    break;
  case NAME_CCF:
    oName(*theo) = system_names + NAME_CCITTFaxDecode;
    break;
  case NAME_DCT:
    oName(*theo) = system_names + NAME_DCTDecode;
    break;
  case NAME_Fl:
    oName(*theo) = system_names + NAME_FlateDecode;
    break;
  case NAME_LZW:
    oName(*theo) = system_names + NAME_LZWDecode;
    break;
  case NAME_RL:
    oName(*theo) = system_names + NAME_RunLengthDecode;
    break;
  }
}

/* If the given colorspace name is abbreviated, replace it
 * with the full version, otherwise do nothing.
 * See PDF 1.2 spec, ss 8.9.
 */

static void pdfinlop_expand_colorspace_name( OBJECT *theo )
{
  HQASSERT( theo , "theo NULL in pdfinlop_expand_colorspace_name") ;
  HQASSERT( oType(*theo) == ONAME ,
            "Object not a name in pdfinlop_expand_colorspace_name") ;

  switch ( oNameNumber(*theo) ) {
  case NAME_CMYK:
    oName(*theo) = system_names + NAME_DeviceCMYK;
    break;
  case NAME_G:
    oName(*theo) = system_names + NAME_DeviceGray;
    break;
  case NAME_RGB:
    oName(*theo) = system_names + NAME_DeviceRGB;
    break;
  case NAME_I:
    oName(*theo) = system_names + NAME_Indexed;
    break;
  }
}


/* Log stripped */
