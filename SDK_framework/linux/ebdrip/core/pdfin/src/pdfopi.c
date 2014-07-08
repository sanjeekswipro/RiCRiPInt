/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfopi.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Open Pre-press Interface (OPI) Implementation
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileioh.h"
#include "dictscan.h"
#include "namedef_.h"

#include "control.h"
#include "bitblts.h"
#include "constant.h"
#include "display.h"
#include "fileops.h"
#include "forms.h"
#include "graphics.h"
#include "matrix.h"
#include "stacks.h"

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "pdfattrs.h"
#include "pdfexec.h"
#include "pdfimg.h"
#include "pdfops.h"
#include "pdfxobj.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdfx.h"


static Bool runOPI(Bool *rendered)
{
  /* try to ensure that operand stack does not accumulate junk, whether opi
   * is run or not. Stack clean up in error case is in Postscript, as we
   * cannot be 100% sure of state stack is in, when error occurs.
   */

  char *runOPI = "mark exch /HqnOPI /ProcSet resourcestatus {\n"
                 "  pop pop\n"
                 "  <<>> /HqnOPI /ProcSet findresource /HqnOPIimage get exec\n"
                 " { cleartomark true } { cleartomark false } ifelse "
                 "} {\n"
                 "  cleartomark false\n"
                 "} ifelse";


  OBJECT obj = OBJECT_NOTVM_NOTHING ;

  theTags( obj ) = OSTRING | EXECUTABLE | READ_ONLY ;
  theLen ( obj ) = strlen_uint16( runOPI ) ;
  oString( obj ) = ( uint8 * )runOPI ;

  execStackSizeNotChanged = FALSE ;
  *rendered = FALSE ;

  if ( push( & obj , & executionstack )) {
    if ( interpreter( 1 , NULL )) {
      if ( theStackSize( operandstack ) > EMPTY_STACK ) {
        OBJECT *theo = theTop( operandstack ) ;

        if ( oType( *theo ) != OBOOLEAN )
          return error_handler( TYPECHECK ) ;

        *rendered = oBool( *theo ) ;
        pop( & operandstack ) ;
      }
      else
        return error_handler( STACKUNDERFLOW ) ;
    }
    else
      return FALSE;
  }

  return TRUE ;
}


enum {
 e_opi_point3 = 0,
 e_opi_point0,

 e_opi_max
};

static NAMETYPEMATCH pdfOPImatch[e_opi_max + 1] = {
  { NAME_OPI1point3 | OOPTIONAL ,  2 ,  { ODICTIONARY , OINDIRECT }} ,
  { NAME_OPI2point0 | OOPTIONAL ,  2 ,  { ODICTIONARY , OINDIRECT }} ,
  DUMMY_END_MATCH
} ;

enum {
  e_opi13_type = 0,
  e_opi13_version,
  e_opi13_F,
  e_opi13_croprect,
  e_opi13_color,
  e_opi13_colortype,
  e_opi13_comments,
  e_opi13_cropfixed,
  e_opi13_graymap,
  e_opi13_ID,
  e_opi13_imagetype,
  e_opi13_overprint,
  e_opi13_position,
  e_opi13_resolution,
  e_opi13_size,
  e_opi13_tags,
  e_opi13_tint,
  e_opi13_tansparency,

  e_opi13_max
};

static NAMETYPEMATCH pdfOPI13match[e_opi13_max + 1] = {
  { NAME_Type | OOPTIONAL ,         2 ,  { ONAME , OINDIRECT }} ,
  { NAME_Version ,                  3 ,  { OREAL , OINTEGER, OINDIRECT }} ,
  { NAME_F ,                        3 ,  { OSTRING , ODICTIONARY , OINDIRECT }} ,
  { NAME_CropRect ,                 3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Color | OOPTIONAL ,        3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_ColorType | OOPTIONAL ,    2 ,  { ONAME, OINDIRECT }} ,
  { NAME_Comments | OOPTIONAL ,     2 ,  { OSTRING, OINDIRECT }} ,
  { NAME_CropFixed | OOPTIONAL ,    3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_GrayMap | OOPTIONAL ,      3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_ID | OOPTIONAL ,           2 ,  { OSTRING, OINDIRECT }} ,
  { NAME_ImageType | OOPTIONAL ,    3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Overprint | OOPTIONAL ,    2 ,  { OBOOLEAN, OINDIRECT }} ,
  { NAME_Position ,                 3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Resolution | OOPTIONAL ,   3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Size ,                     3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Tags | OOPTIONAL ,         3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Tint | OOPTIONAL ,         3 ,  { OREAL , OINTEGER, OINDIRECT }} ,
  { NAME_Transparency | OOPTIONAL , 2 ,  { OBOOLEAN, OINDIRECT }} ,

  DUMMY_END_MATCH
} ;

enum {
  e_opi20_type,
  e_opi20_version,
  e_opi20_F,
  e_opi20_croprect,
  e_opi20_includeimagedims,
  e_opi20_includeimagequality,
  e_opi20_inks,
  e_opi20_mainimage,
  e_opi20_overprint,
  e_opi20_tags,
  e_opi20_size,

  e_opi20_max
};

static NAMETYPEMATCH pdfOPI20match[  e_opi20_max + 1 ] = {
  { NAME_Type | OOPTIONAL ,                    2 ,  { ONAME , OINDIRECT }} ,
  { NAME_Version ,                             3 ,  { OREAL , OINTEGER, OINDIRECT }} ,
  { NAME_F ,                                   3 ,  { OSTRING , ODICTIONARY , OINDIRECT }} ,
  { NAME_CropRect | OOPTIONAL ,                3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_IncludedImageDimensions | OOPTIONAL , 3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_IncludedImageQuality | OOPTIONAL ,    3 ,  { OREAL , OINTEGER, OINDIRECT }} ,
  { NAME_Inks | OOPTIONAL ,                    4 ,  { ONAME , OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_MainImage | OOPTIONAL ,               2 ,  { OSTRING, OINDIRECT }} ,
  { NAME_Overprint | OOPTIONAL ,               2 ,  { OBOOLEAN, OINDIRECT }} ,
  { NAME_Tags | OOPTIONAL ,                    3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,
  { NAME_Size | OOPTIONAL ,                    3 ,  { OARRAY , OPACKEDARRAY, OINDIRECT }} ,

  DUMMY_END_MATCH
} ;

/* Deal with a Form XObject OPI dictionary. */

Bool pdfOPI_dispatch( PDFCONTEXT *pdfc , OBJECT *dict , Bool *rendered )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Bool result = FALSE ;
  OBJECT *pdfXdict = NULL ;
  USERVALUE version;
  OBJECT dictPS = OBJECT_NOTVM_NOTHING ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( dict , "dict NULL in pdfform_dispatch." ) ;
  HQASSERT( oType( *dict ) == ODICTIONARY,
            "dict must be an ODICTIONARY." ) ;

  if (! pdfxOPIDetected(pdfc))
    return FALSE;

  /* Read the OPI version dictionary. */
  if ( ! pdf_dictmatch( pdfc , dict , pdfOPImatch ))
    return FALSE ;

  if ( pdfOPImatch[ e_opi_point3 ].result ) {
    /* Check the contents of the 1.3 dictionary. */

    if ( ! pdf_dictmatch( pdfc , pdfOPImatch[ e_opi_point3 ].result , pdfOPI13match ))
      return FALSE ;

    /* If the type isn't "OPI" then return a TypeCheck. */

    if ( pdfOPI13match[ e_opi13_type ].result != NULL &&
         theINameNumber(oName( *pdfOPI13match[ e_opi13_type ].result )) != NAME_OPI )
      return error_handler ( TYPECHECK ) ;

    /* If the version isn't 1.3, then we don't know what to do. */
    if (oType(*pdfOPI13match[ e_opi13_version ].result) != OREAL)
      return error_handler ( RANGECHECK ) ;

    version = oReal( *pdfOPI13match[ e_opi13_version].result );

    if ( ( version <= 1.2 ) || ( version >= 1.4 ) )
      return error_handler ( UNDEFINEDRESULT ) ;

    pdfXdict = pdfOPImatch[ e_opi_point3 ].result ;
  }

  if ( pdfOPImatch[ e_opi_point0 ].result ) {
    /* Check the contents of the 2.0 dictionary. */

    if ( ! pdf_dictmatch( pdfc , pdfOPImatch[ e_opi_point0 ].result , pdfOPI20match ))
      return FALSE ;

    /* If the type isn't "OPI" then return a TypeCheck. */

    if ( pdfOPI20match[ e_opi20_type ].result != NULL &&
         theINameNumber( oName( *pdfOPI20match[ e_opi20_type ].result )) != NAME_OPI )
      return error_handler ( TYPECHECK ) ;

    /* If the version isn't 2.0, then we don't know what to do. */

    if ( oType( *pdfOPI20match[ e_opi20_version ].result ) == OREAL ) {
      if ( fabs( oReal( *pdfOPI20match[ e_opi20_version ].result ) - 2.0 ) > EPSILON )
        return error_handler ( UNDEFINEDRESULT ) ;
    }
    else {
      if ( oInteger( *pdfOPI20match[ e_opi20_version ].result ) != 2 )
        return error_handler ( UNDEFINEDRESULT ) ;
    }

    pdfXdict = pdfOPImatch[ e_opi_point0 ].result ;
  }

  /* It might be that the dictionary contained neither a 1.3 or a 2.0
   * subdictionary. Intentionally I'll pass it on to the ProcSet with
   * no checking rather than failing, on the grounds that this gives
   * us the best chance of upward compatibility.
   */

  if ( ! pdf_resolvexrefs( pdfc , dict ))
    return FALSE ;

  if ( ! pdf_copyobject( NULL, dict, &dictPS ) ||
       ! push( &dictPS , & operandstack ))
    return FALSE ;

  result = runOPI( rendered ) ;

  return result ;
}

/* Log stripped */
