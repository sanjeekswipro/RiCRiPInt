/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfencod.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Font Encoding Implementation
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "namedef_.h"

#include "stacks.h"
#include "encoding.h"

#include "swpdf.h"
#include "pdfmatch.h"

#include "pdfexec.h"
#include "pdfencod.h"
#include "pdffont.h"
#include "pdfin.h"

/* -------------------------------------------------------------------------- */
static NAMECACHE **pdf_Encodings[ 5 ] = {
  NULL , NULL ,
  MacRomanEncoding ,
  MacExpertEncoding ,
  WinAnsiEncoding
} ;

/* -------------------------------------------------------------------------- */
static Bool pdf_baseEncoding( PDFCONTEXT *pdfc , NAMECACHE *encodingname ,
                              int32 *encodetype )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  HQASSERT( encodingname , "encodingname NULL in pdf_baseEncoding" ) ;
  HQASSERT( encodetype , "encodetype NULL in pdf_baseEncoding" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* Check if encoding is one of standard 3. */
  switch (( int32 )theINameNumber( encodingname )) {
  case NAME_MacRomanEncoding:
    (*encodetype) = ENC_MacRoman ;
    break ;
  case NAME_MacExpertEncoding:
    (*encodetype) = ENC_MacExpert ;
    break ;
  case NAME_WinAnsiEncoding:
    (*encodetype) = ENC_WinAnsi ;
    break ;
  default:
    if ( ixc->strictpdf )
      return error_handler( RANGECHECK ) ;
    else
      return FALSE ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* This routine takes a PDF Font dictionary and fills in a PDF_ENCODEDETAILS
 * structure. This structure contains cached information which is used when
 * we render chars.
 */
Bool pdf_setencodedetails( PDFCONTEXT *pdfc ,
                           PDF_ENCODEDETAILS *pdf_encodedetails ,
                           OBJECT *encoding )
{
  NAMECACHE *encodingname ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  static NAMETYPEMATCH encoding_match[] = {
  /* 0 */ { NAME_Type | OOPTIONAL,      2, { ONAME, OINDIRECT }},
  /* 1 */ { NAME_BaseEncoding | OOPTIONAL,
                                        2, { ONAME, OINDIRECT }},
  /* 2 */ { NAME_Differences | OOPTIONAL,
                                        3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
     DUMMY_END_MATCH
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdf_encodedetails , "pdf_encodedetails NULL inpdf_setencodedetails" ) ;

  pdf_encodedetails->enc_type = ENC_None ;
  if ( encoding ) {
    int32 type = oType(*encoding) ;
    switch ( type ) {
    case ONAME:
      encodingname = oName(*encoding) ;
      if ( ! pdf_baseEncoding( pdfc , encodingname , & pdf_encodedetails->enc_type )) {
        if ( ixc->strictpdf )
          return FALSE ;
        else
          return TRUE ;
      }
      break ;
    case ODICTIONARY:
      pdf_encodedetails->enc_type = ENC_Embedded ;
      if ( ! pdf_dictmatch( pdfc , encoding , encoding_match ))
        return FALSE ;

      /* Is /Type entry in object /Encoding? (Maybe shouldn't do this?) */
      if ( encoding_match[ 0 ].result )
        if ( oName(*encoding_match[ 0 ].result) !=
          system_names + NAME_Encoding )
          return error_handler( UNDEFINED ) ;

      pdf_encodedetails->enc_subtype = ENC_None ;
      if ( encoding_match[ 1 ].result ) {
        encodingname = oName(*encoding_match[ 1 ].result) ;
        if ( ! pdf_baseEncoding( pdfc , encodingname ,
               & pdf_encodedetails->enc_subtype )) {
          if ( ixc->strictpdf )
            return FALSE ;
        }
      }

      /* Now the meat of the routine; produce an offset mapping from character code
       * to array index from where we can find the glyph name. A -1 implies that we
       * use the default name from the base encoding or the font's encoding.
       */
      pdf_encodedetails->enc_diffs = NULL ;
      if ( encoding_match[ 2 ].result ) {
        int32 i ;
        int32 len ;
        OBJECT *diffs ;
        diffs = encoding_match[ 2 ].result ;
        len = theLen(*diffs) ;
        diffs = oArray(*diffs) ;
        pdf_encodedetails->enc_diffs = diffs ;

        /* Set everything to 'default'. */
        for ( i = 0 ; i < 256 ; ++i )
          pdf_encodedetails->enc_offset[ i ] = ( int16 )-1 ;

        i = 0 ;
        while ( i < len ) {
          int32 index ;
          if ( oType(*diffs) != OINTEGER ) {
            if ( i == 0 )
              index = 0 ;
            else
              return FALSE ;
          } else
            index = oInteger(*diffs) ;
          if ( index < 0 || index > 255 )
            return error_handler( RANGECHECK ) ;
          ++i ;
          ++diffs ;
          while ( i < len ) {
            if ( oType(*diffs) != ONAME )
              break ;
            if ( index < 256 ) {
              /* Remove test if you want the last to take precendence. */
              if ( pdf_encodedetails->enc_offset[ index ] < 0 )
                pdf_encodedetails->enc_offset[ index ] = ( int16 )i ;
            }
            ++index ;
            ++i ;
            ++diffs ;
          }
        }
      }
      break ;
    case OFILE:
      return error_handler( UNDEFINED ) ;
    default:
      HQFAIL( "Bad case found in Encoding Type" ) ;
      return error_handler( UNREGISTERED ) ;
    }
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Given a PDF_ENCODEDETAILS structure, this routine works out what actual
 * glyph name (or character index) should be used for drawing the character.
 */
Bool pdf_getencoding( PDF_ENCODEDETAILS *pdf_encodedetails ,
                      int32 *charcode , NAMECACHE **charname )
{
  int32 type ;

  HQASSERT( pdf_encodedetails , "pdf_encodedetails NULL pdf_getencoding" ) ;
  HQASSERT( charname , "charname NULL pdf_getencoding" ) ;

  HQASSERT( *charcode >= 0 && *charcode < 256 , "charcode out of range" ) ;

  type = pdf_encodedetails->enc_type ;
  switch ( type ) {
  case ENC_None:
    *charname = NULL ;
    return TRUE ; /* As is */

  case ENC_Embedded:
    type = pdf_encodedetails->enc_subtype ;
    switch ( type ) {
      OBJECT *enc_diffs ;
    case ENC_None:
      enc_diffs = pdf_encodedetails->enc_diffs ;
      if ( enc_diffs ) {
        int16 enc_offset = pdf_encodedetails->enc_offset[ *charcode ] ;
        if ( enc_offset >= 0 ) {
          /* Use Difference glyph */
          enc_diffs += enc_offset ;
          if ( oType(*enc_diffs) != ONAME )
            return error_handler(TYPECHECK) ;
          *charname = oName(*enc_diffs) ;
          *charcode = -1 ;
          return TRUE ;
        }
      }
      *charname = NULL ;
      return TRUE ; /* As is */

    case ENC_MacRoman:
    case ENC_MacExpert:
    case ENC_WinAnsi:
      enc_diffs = pdf_encodedetails->enc_diffs ;
      if ( enc_diffs ) {
        int16 enc_offset = pdf_encodedetails->enc_offset[ *charcode ] ;
        if ( enc_offset >= 0 ) {
          /* Use Difference glyph */
          enc_diffs += enc_offset ;
          if ( oType(*enc_diffs) != ONAME )
            return error_handler(TYPECHECK) ;
          *charname = oName(*enc_diffs) ;
          *charcode = -1 ;
          return TRUE ;
        }
      }
      break ;
    default:
      HQFAIL( "Unknown sub encoding type" ) ;
      return error_handler( UNREGISTERED ) ;
    }
    /* Fall through */
  case ENC_MacRoman:
  case ENC_MacExpert:
  case ENC_WinAnsi:
   *charname = pdf_Encodings[ type ][ *charcode ] ;
   *charcode = -1 ;
   if ( ! *charname )
     return error_handler( VMERROR ) ;
   return TRUE ;
  default:
    HQFAIL( "Unknown encoding type" ) ;
    return error_handler( UNREGISTERED ) ;
  }
  /* Not reached */
}



/* Log stripped */
