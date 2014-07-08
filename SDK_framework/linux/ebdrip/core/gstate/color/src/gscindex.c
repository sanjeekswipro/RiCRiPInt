/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscindex.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS3: Indexed colorspace.
 */

#include "core.h"

#include "control.h"      /* interpreter - @@JJ **** to be removed asap 50800 */
#include "fileio.h"       /* FILELIST */
#include "hqmemcpy.h"     /* HqMemCpy */
#include "gcscan.h"       /* ps_scan_field */
#include "mps.h"          /* mps_res_t */
#include "swerrors.h"     /* TYPECHECK */

#include "gs_colorpriv.h" /* CLINKindexed */
#include "gschcmspriv.h"  /* REPRO_COLOR_MODEL_RGB_WITH_SPOTS */
#include "gscheadpriv.h"  /* gsc_getcolorspacesizeandtype */

#include "gscindex.h"

/* We need to identify all the unique characteristics that identify a CLINK.
 * The CLINK type, colorspace, colorant set are orthogonal to these items.
 * The device color space & colorants are defined as fixed.
 * The following define the number of id slots needed by the various CLINK
 * types in this file.
 *
 * The index links have a private id counter that is simply incremented on every
 * create.  There's an optimisation at a higher level to avoid creating an
 * indexed CLINK for an indexed space already seen.
 */
#define CLID_SIZEindex (1)

struct CLINKindexed {
  int32             base_spaceisCIE;
  int32             base_dimension;
  uint16            hival;
  OBJECT            indextable;
  uint8             *indexedspace_lookup;
  int32             indexedspace_lookupsize;
  GUCR_RASTERSTYLE  *hRasterStyle;
};

static void  indexed_destroy(CLINK *pLink);
static Bool indexed_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool indexed_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t indexed_scan( mps_ss_t ss, CLINK *pLink );

#if defined( ASSERT_BUILD )
static void indexedAssertions(CLINK *pLink);
#else
#define indexedAssertions(_pLink) EMPTY_STATEMENT()
#endif

static size_t indexedStructSize(void);
static void indexedUpdatePtrs(CLINK *pLink);

static CLINKfunctions CLINKindexed_functions =
{
  indexed_destroy,
  indexed_invokeSingle,
  NULL /* indexed_invokeBlock */,
  indexed_scan
};

/* ---------------------------------------------------------------------- */

/*
 * Indexed Link Data Access Functions
 * ==================================
 */
CLINK *cc_indexed_create(GS_COLORinfo       *colorInfo,
                         OBJECT             *PSColorspace,
                         GUCR_RASTERSTYLE   *hRasterStyle,
                         OBJECT             **basePSColorspace)
{
  CLINK *pLink;
  COLORSPACE_ID colorspaceID;
  int32 colorspacedimension;
  int32 number ;
  SYSTEMVALUE avalue ;
  FILELIST *flptr ;
  OBJECT *theo;
  CLINKindexed *indexed ;
  static CLID indexUniqueID = 0;

  /* First of all allocate the link structure and fill in the fixed elements.
   */
  pLink = cc_common_create(1,
                           NULL,
                           SPACE_Indexed,
                           SPACE_notset,
                           CL_TYPEindexed,
                           indexedStructSize(),
                           &CLINKindexed_functions,
                           CLID_SIZEindex);

  if (pLink == NULL)
    return NULL;

  indexedUpdatePtrs(pLink);
  indexed = pLink->p.indexed ;

  theo = oArray(*PSColorspace) ;
  /* theo is a pointer to the first object in the array */
  /* e.g. [ /Indexed /DeviceCMYK 255 <...> ]
   * We know the length of the array is correct;
   * The number says the operating range of the index. There should then
   *   be that number times the dimension of the base color space
   *   bytes in the string, which is the lookup table for the space
   * The base color space is in the same format as the operand to
   *   setcolorspace;
   */

  ++theo ;
  if ( ! gsc_getcolorspacesizeandtype(colorInfo, theo,
                                      &colorspaceID, &colorspacedimension) ) {
    indexed_destroy( pLink );
    return NULL;
  }
  pLink->oColorSpace = colorspaceID;
  *basePSColorspace = theo ;
  indexed->indexedspace_lookup = NULL ;
  indexed->indexedspace_lookupsize = 0 ;

  /* Is the base color space one of the allowed ones? */
  /* Note that SPACE_Separation is allowed now even for LanguageLevel 2 */
  if ( colorspaceID == SPACE_Pattern ||
       colorspaceID == SPACE_Indexed ) {
    ( void )error_handler( RANGECHECK ) ;
    indexed_destroy( pLink );
    return NULL;
  }

  indexed->base_spaceisCIE = ColorspaceIsCIEBased( colorspaceID ) ;
  indexed->base_dimension = colorspacedimension;

  ++theo ;
  if ( ! object_get_numeric(theo, & avalue)) {
    ( void )error_handler( RANGECHECK ) ;
    indexed_destroy( pLink );
    return NULL;
  }
  number = (( int32 )avalue ) ;
  if (( number < 0 ) || ( number > 4095 )) {
    ( void )error_handler( RANGECHECK ) ;
    indexed_destroy( pLink );
    return NULL;;
  }
  indexed->hival = ( uint16 )number;

  ++theo ;
  switch ( oType( *theo )) {
  case OSTRING:
    /* Does it have the right number of elements? */
    /* Use (number + 1) because the indexed color range is 0 to hival
    INCLUSIVE */
    if (( int32 )theLen(*theo) != ( number + 1 ) * colorspacedimension ) {
      ( void )error_handler( RANGECHECK ) ;
      indexed_destroy( pLink );
      return NULL;
    }
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    if ( ! oExecutable( *theo )) {
      ( void )error_handler( TYPECHECK ) ;
      indexed_destroy( pLink );
      return NULL;
    }
    if ( ! oCanExec( *theo ) && !object_access_override(theo) ) {
      ( void )error_handler( INVALIDACCESS ) ;
      indexed_destroy( pLink );
      return NULL;
    }
    break ;
  case OFILE:
    flptr = oFile(*theo) ;
    HQASSERT( flptr , "flptr NULL in cc_indexed_create" ) ;
    if ( ! isIInputFile( flptr ) ||
         ! isIOpenFileFilter( theo , flptr ) ||
         ! isIRewindable( flptr )) {
      ( void )error_handler( IOERROR ) ;
      indexed_destroy( pLink );
      return NULL;
    }
    break;
  default:
    ( void )error_handler( TYPECHECK ) ;
    indexed_destroy( pLink );
    return NULL;
  }

  Copy(object_slot_notvm(&indexed->indextable), theo) ;

  /* Populate CLID slot */
  pLink->idslot[0] = ++indexUniqueID;

  indexed->hRasterStyle = hRasterStyle ;

  indexedAssertions(pLink);

  return pLink;
}

static void indexed_destroy(CLINK *pLink)
{
  CLINKindexed *indexed ;

  indexedAssertions(pLink);

  indexed = pLink->p.indexed ;

  if ( indexed->indexedspace_lookup != NULL ) {
    mm_free( mm_pool_color ,
             indexed->indexedspace_lookup ,
             indexed->indexedspace_lookupsize ) ;
  }
  cc_common_destroy(pLink);
}

static Bool indexed_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  SYSTEMVALUE     colorValue;
  uint16          hival;
  int32           base_dimension;
  int32           i;
  uint8           *clist ;
  OBJECT          *theo ;
  int32           base_spaceisCIE;
  CLINKindexed    *indexed ;

  indexedAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  /* The action depends on the type of the stored index table object;
   * if it is a string we index into it, if it is a procedure we call
   * it to munge the input index. We've type and length checked the
   * object already, and the input color is known to be in range.
   *
   * Note that values derived from a string are in the range 0..1. This
   * is defined, but probably incorrect for CIE base spaces. However,
   * I have carried the same principle over to the procedure form of
   * color table by narrowing to the range 0..1. It would be possible
   * there to narrow to the CIE range (different in each dimension) instead,
   * or simply not bother narrowing to a range at all.
   */

  indexed = pLink->p.indexed ;

  colorValue = pLink->iColorValues[ 0 ];
  hival = indexed->hival;
  base_dimension = indexed->base_dimension;
  base_spaceisCIE = indexed->base_spaceisCIE;

  if ( colorValue < 0.0f )
    colorValue = 0.0f ;
  else if ( colorValue > hival )
    colorValue = ( USERVALUE )(( int32 )hival ) ;

  theo = & indexed->indextable ;
  if ( oType(*theo) == OSTRING ) {
    clist = oString(*theo) + base_dimension * ( int32 )( colorValue ) ;
    for ( i = 0 ; i < base_dimension ; ++i ) {
      if ( base_spaceisCIE ) {
        SYSTEMVALUE range[2];
        SYSTEMVALUE bottom ;
        SYSTEMVALUE rangesize ;
        uint8 val = *clist ;

        cc_getCieRange( pLink->pnext, i, range );
        bottom = range[0];
        rangesize = range[1] - bottom;
        oColorValues[ i ] = ( USERVALUE )( bottom + val *  rangesize / 255.0 ) ;
      }
      else
        oColorValues[ i ] = ( USERVALUE )(( SYSTEMVALUE )( *clist ) / 255.0 ) ;
      ++clist ;
    }
  }
  else if ( oType(*theo) == OFILE ) {
    /* Use (hival + 1) because the indexed color range is 0 to hival
    INCLUSIVE */
    int32 slen = ( hival + 1 ) * base_dimension ;
    int32 bytes ;
    FILELIST *flptr ;
    Hq32x2 filepos;

    /* (re)allocate the static buffer if necessary */
    if ( indexed->indexedspace_lookup == NULL ||
         indexed->indexedspace_lookupsize != slen ) {
      if ( indexed->indexedspace_lookup != NULL ) {
        mm_free( mm_pool_color ,
                 indexed->indexedspace_lookup ,
                 indexed->indexedspace_lookupsize ) ;
      }
      indexed->indexedspace_lookup = mm_alloc(mm_pool_color ,
                                              slen ,
                                              MM_ALLOC_CLASS_INDEX_LUT) ;
      if ( ! indexed->indexedspace_lookup )
        return error_handler( VMERROR ) ;
      indexed->indexedspace_lookupsize = slen ;

      /* Rewind the file */
      flptr = oFile(*theo) ;
      HQASSERT( flptr , "flptr NULL in indexed_invokeSingle" ) ;
      if ( ! isIOpenFileFilter( theo , flptr ) ||
           ! isIInputFile( flptr ))
        return error_handler( IOERROR ) ;

      if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
        return error_handler(IOERROR) ;
      Hq32x2FromInt32(&filepos, 0);
      if ((*theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
        return error_handler(IOERROR) ;

      /* Load the lookup table */
      if ( file_read(flptr, indexed->indexedspace_lookup, slen, &bytes) <= 0 )
        return error_handler( IOERROR ) ;

      HQASSERT(slen == bytes, "Expected bytes not read") ;
    }

    HQASSERT( indexed->indexedspace_lookup ,
              "indexedspace_lookup NULL in indexed_invokeSingle" ) ;

    /* Fetch the values */

    clist = indexed->indexedspace_lookup +
              base_dimension * ( int32 )( colorValue ) ;
    for ( i = 0 ; i < base_dimension ; ++i ) {
      if ( base_spaceisCIE ) {
        SYSTEMVALUE range[2];
        SYSTEMVALUE bottom ;
        SYSTEMVALUE rangesize ;
        uint8 val = *clist ;

        cc_getCieRange( pLink->pnext, i, range);
        bottom = range[0];
        rangesize = range[1] - bottom;
        oColorValues[ i ] = ( USERVALUE )( bottom + val *  rangesize / 255.0 ) ;
      }
      else
        oColorValues[ i ] = ( USERVALUE )(( SYSTEMVALUE )( *clist ) / 255.0 ) ;
      ++clist ;
    }
  }
  else {
    OBJECT cv = OBJECT_NOTVM_NOTHING;

    /* It is a procedure */
    /* @@JJ PROTECT against gsave/grestore abuse */
    object_store_integer(&cv, (int32)colorValue);
    if ( ! push( &cv , &operandstack ))
      return FALSE ;
    if ( ! push( theo , &executionstack ))
      return FALSE ;
    if ( ! interpreter( 1 , NULL ))
      return FALSE ;
    if ( ! stack_get_reals(&operandstack, oColorValues, base_dimension) )
      return FALSE ;
    npop( base_dimension , & operandstack ) ;
    switch ( pLink->oColorSpace ) {
    case SPACE_DeviceN:
    case SPACE_Separation:
    case SPACE_DeviceGray:
    case SPACE_DeviceCMYK:
    case SPACE_DeviceRGB:
      for ( i = 0 ; i < base_dimension ; ++i )
        NARROW_01( oColorValues[ i ] ) ;
      break ;
    }
  }

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool indexed_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  indexedAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* indexed_scan - scan the indexed c.s. section of a CLINK */
static mps_res_t indexed_scan( mps_ss_t ss, CLINK *pLink )
{
  return ps_scan_field( ss, &pLink->p.indexed->indextable );
}


static size_t indexedStructSize(void)
{
  return sizeof(CLINKindexed);
}

static void indexedUpdatePtrs(CLINK *pLink)
{
  pLink->p.indexed = (CLINKindexed *) ((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void indexedAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEindexed,
                      indexedStructSize(),
                      &CLINKindexed_functions);
}
#endif

int16 cc_getindexedhival( CLINK *pLink )
{
  return pLink->p.indexed->hival;
}

/* eof */

/* Log stripped */
