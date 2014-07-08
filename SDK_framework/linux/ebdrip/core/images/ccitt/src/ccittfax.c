/** \file
 * \ingroup ccitt
 *
 * $HopeName: COREccitt!src:ccittfax.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of CCITT fax image decompression
 */

#include "core.h"
#include "coreinit.h"
#include "ccittfax.h"

#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemset.h"
#include "tables.h"
#include "namedef_.h"
#include "hqmemset.h" /* HqMemZero */

#include "fileio.h"
#include "ccitt.h"
#include "ccittdat.h"


#if defined( ASSERT_BUILD )
static int32 debug_ccitt = FALSE ;
static int32 debug_code = FALSE;
static uint8* codeword_names[] = {
  (uint8*)"Number",
  (uint8*)"EOF",
  (uint8*)"EOL",
  (uint8*)"P",
  (uint8*)"H",
  (uint8*)"Extension2D",
  (uint8*)"Data0",
  (uint8*)"Data1",
  (uint8*)"VR3",
  (uint8*)"VR2",
  (uint8*)"VR1",
  (uint8*)"V0",
  (uint8*)"VL1",
  (uint8*)"VL2",
  (uint8*)"VL3"
};
#endif

/* static functions used in file */
static void  initfaxdata( void ) ;
static void  sortsized( TERMCODE codetable[] , SORTED sortedtable[] ) ;
static void  ungetccodings( FILELIST *filter , int32 code ) ;
static int32 searchcodings( int32 bitwidth , int32 value , int32 amwhite , int32 check2dcodes , int32 *retval ) ;
static Bool group12D_decode( FILELIST *filter , int32 twodencoding ) ;
static Bool group12D_encode( FILELIST *filter , int32 twodencoding ) ;
static int32 extract_next_code_bit ( FILELIST *filter) ;
static int32 extract_next_code_word( FILELIST *filter , int32 amwhite ) ;
static int32 extract_next_code_word_bitbybit( FILELIST *filter , int32 amwhite ) ;
static int32 detectnextbit1( uint32 *buf , int32 set , int32 bitp ) ;
static int32 detectnextbit2( uint32 *buf , int32 set , int32 bitp ) ;
static Bool output_code( FILELIST *filter , TERMCODE *termcode ) ;
static Bool output_code_runlen( FILELIST *filter , int32 newcode , int32 amwhite ) ;
static void init_C_globals_ccittfax(void) ;

/* data used in file */
static SORTED white_table[ MaxCodeSize + 1 ];
static SORTED black_table[ MaxCodeSize + 1 ];
static SORTED uncompressed_table[ MaxCodeSize + 1 ];

static TERMCODE *hashblacktable [ CCITTHASHSIZE ];
static TERMCODE *hashwhitetable [ CCITTHASHSIZE ];
static TERMCODE *hash2dcodetable[ CCITTHASHSIZE ];

/*****************************************/
/* functions used from the outside world */
/*****************************************/

static void ccittfaxFilterDispose( FILELIST *filter )
{
  FAXSTATE *state ;

  HQASSERT( filter , "filter NULL in ccittfaxFilterDispose.\n" ) ;

  state = theIFilterPrivate( filter ) ;

  if ( state ) {
    if ( theIFaxRefLine( state )) {
      mm_free_with_header( mm_pool_temp ,
                          ( mm_addr_t )( theIFaxRefLine( state ) - 1 )) ;
      theIFaxRefLine( state ) = NULL ;
    }
    if ( theIFaxBufEdge( state )) {
      mm_free_with_header( mm_pool_temp ,
                          ( mm_addr_t )theIFaxBufEdge( state )) ;
      theIFaxBufEdge( state ) = NULL ;
    }
    if ( theIFaxRefEdge( state )) {
      mm_free_with_header( mm_pool_temp ,
                          ( mm_addr_t )theIFaxRefEdge( state )) ;
      theIFaxRefEdge( state ) = NULL ;
    }
    mm_free( mm_pool_temp ,
            ( mm_addr_t )state ,
            sizeof( FAXSTATE ) + sizeof( FAXINFO )) ;
    theIFilterPrivate( filter ) = NULL ;
  }

  if ( theIBuffer( filter )) {
    mm_free_with_header( mm_pool_temp ,
                         ( mm_addr_t )( theIBuffer( filter ) - 4 )) ;
    theIBuffer( filter ) = NULL ;
  }
}

#define FLUSHWRITEFILTER() \
do { \
  if ( theIFaxLastBits( faxstate ) > 0 ) { \
    int32 outbits ; \
    int32 outword ; \
    /* reinstate the last byte being written from */ \
    outbits = theIFaxLastBits( faxstate ) ; \
    outword = theIFaxLastByte( faxstate ) ; \
    { FILELIST *flptr = theIUnderFile( filter ) ; \
      uint8 outbyte = (uint8) ( outword << ( 8 - outbits )) ; \
      if ( TPutc( outbyte , flptr ) == EOF ) \
        return FALSE ; \
    } \
    theIFaxLastBits( faxstate ) = 0 ; \
  } \
} while ( FALSE )

/* ccittfaxfilterInit */

static Bool ccitt_allocEdges( FAXSTATE *state ,
                               int32 columns ,
                               int32 edgesize )
{
  theIFaxBufEdge( state ) =
    mm_alloc_with_header( mm_pool_temp ,
                          sizeof( int32 ) * edgesize ,
                          MM_ALLOC_CLASS_CCITT_FAX ) ;
  if ( theIFaxBufEdge( state ) == NULL )
    return error_handler(VMERROR) ;

  theIFaxRefEdge( state ) =
    mm_alloc_with_header( mm_pool_temp ,
                          sizeof( int32 ) * edgesize ,
                          MM_ALLOC_CLASS_CCITT_FAX ) ;
  if ( theIFaxRefEdge( state ) == NULL ) {
    mm_free_with_header( mm_pool_temp ,
                         ( mm_addr_t )theIFaxBufEdge( state )) ;
    theIFaxBufEdge( state ) = NULL ;
    return error_handler(VMERROR) ;
  }

  theIFaxBufEdge( state )[ 0 ] = -1 ;
  theIFaxBufEdge( state )[ 1 ] = columns ;
  theIFaxBufEdge( state )[ 2 ] = columns ;
  theIFaxBufEdge( state )[ 3 ] = ( columns + 31 ) & (~31) ;
  theIFaxRefEdge( state )[ 0 ] = -1 ;
  theIFaxRefEdge( state )[ 1 ] = columns ;
  theIFaxRefEdge( state )[ 2 ] = columns ;
  theIFaxRefEdge( state )[ 3 ] = ( columns + 31 ) & (~31) ;

  return TRUE ;
}

static Bool init_ccitt_state1(FILELIST *filter)
{
  FAXSTATE *state;
  FAXINFO *faxinfo;

  /* allocate a state structure */
  state = (FAXSTATE *)mm_alloc(mm_pool_temp, sizeof(FAXSTATE) + sizeof(FAXINFO),
                                MM_ALLOC_CLASS_CCITT_FAX);
  if ( state == NULL )
    return error_handler(VMERROR) ;

  /* and info structure */
  faxinfo = ( FAXINFO * )( state + 1 ) ;
  theIFilterPrivate( filter ) = state ;
  theIFaxInfo( state ) = faxinfo ;
  theIFaxRefLine( state ) = NULL ;
  theIFaxBufEdge( state ) = NULL ;
  theIFaxRefEdge( state ) = NULL ;
  theIUseRefEdge( state ) = TRUE ;
  theIEdgeSize( state ) = 0 ;
  theIBuffer( filter ) = NULL ;

  /* Setup default values */
  theIFaxUncompressed( faxinfo ) = FALSE ;
  theIFaxK( faxinfo ) = 0 ;
  theIFaxEndOfLine( faxinfo ) = FALSE ;
  theIFaxEncodedByteAlign( faxinfo ) = FALSE ;
  theIFaxColumns( faxinfo ) = 1728 ;
  theIFaxRows( faxinfo ) = 0 ;
  theIFaxEndOfBlock( faxinfo ) = TRUE ;
  theIFaxBlackIs1( faxinfo ) = FALSE ;

  return TRUE ;
}

static Bool init_ccitt_state2(FILELIST *filter)
{
  FAXSTATE *state;
  FAXINFO *faxinfo;
  uint32 *buff1 , *buff2 ;
  int32 edgesize ;

  state = theIFilterPrivate( filter );
  faxinfo = theIFaxInfo( state );

  theIFaxBlackIs1( faxinfo ) =
    theIFaxBlackIs1( faxinfo ) ? 0x00000000u : 0xffffffffu ;

  /* allocate enough space for a linebuffer */
  buff1 = (uint32 *)mm_alloc_with_header(mm_pool_temp ,(((theIFaxColumns(
                    faxinfo) + 31) >> 5) << 2) + 8, MM_ALLOC_CLASS_CCITT_FAX);

  if ( buff1 == NULL )
    return error_handler(VMERROR) ;
  buff1 += 1 ;
  theIBuffer( filter ) = ( uint8 * )buff1 ;
  {
    int32 i ;
    uint32 v ;
    v = theIFaxBlackIs1( faxinfo ) ;
    for ( i = ( theIFaxColumns( faxinfo ) + 31 ) >> 5  ; i != 0 ; --i )
      *buff1++ = v ;
    /* These are back stop markers, so that detectnextbit[12] doesn't have to
     * check for end of buffer. Need 4 since PC are backwards...
     */
    *buff1 = 0xAAAAAAAA ;
  }

  /* if 2D fax or group4 then allocate space for the reference line */
  if ( theIFaxK( faxinfo ) != 0 ) {
    /* allocate enough space for a linebuffer */
    buff2 = ( uint32 * )mm_alloc_with_header( mm_pool_temp ,
                                              ((( theIFaxColumns( faxinfo ) + 31 ) >> 5 ) << 2 ) + 8 ,
                                              MM_ALLOC_CLASS_CCITT_FAX ) ;
    if ( buff2 == NULL )
      return error_handler( VMERROR ) ;
    buff2 += 1 ;
    theIFaxRefLine( state ) = buff2 ;
    {
      int32 i ;
      uint32 v ;
      v = theIFaxBlackIs1( faxinfo ) ;
      for ( i = ( theIFaxColumns( faxinfo ) + 31 ) >> 5  ; i != 0  ; --i )
        *buff2++ = v ;
      /* These are back stop markers, so that detectnextbit[12] doesn't have to
       * check for end of buffer. Need 4 since PC are backwards...
       */
      *buff2 = 0xAAAAAAAA ;
    }
  }

  /* Always allocate edge table for speed. */
  edgesize = 0 ;
  if ( theIFaxK( faxinfo ) != 0 ) {
    edgesize = theIFaxColumns( faxinfo ) ;
    if ( edgesize > EDGE_LIMIT )
      edgesize = EDGE_LIMIT ;
    if (( edgesize & 1 ) != 0 )
      ++edgesize ;
    edgesize += EDGE_EXTRA ;
    if ( ! ccitt_allocEdges( state , theIFaxColumns( faxinfo ) , edgesize )) {
      edgesize = EDGE_SMALL + EDGE_EXTRA ;
      if ( ! ccitt_allocEdges( state , theIFaxColumns( faxinfo ) , edgesize ))
        return FALSE;
    }
  }
  theIEdgeSize( state ) = edgesize - EDGE_EXTRA ;

  /* initialise the filter structure */
  theIPtr( filter ) = ( uint8 * )theIBuffer( filter ) ;
  theIBufferSize( filter ) = ( theIFaxColumns( faxinfo ) + 7 ) >> 3 ;
  theICount( filter ) = 0 ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  /* the state structure */
  theIFaxLineCount( state ) = 0 ;
  theIFaxMaxWidth( state ) = theIFaxColumns( faxinfo ) ;
  theIFaxCurHeight( state ) = 0 ;
  theIFaxLastBits( state ) = 0 ;

  if ( theIFaxK( faxinfo ) == 0 )
    theIFaxCheck2DCodes( state ) = FALSE ;
  else if ( theIFaxK( faxinfo ) > 0 )
    theIFaxCheck2DCodes( state ) = FALSE /* Don't actually know yet */;
  else if ( theIFaxK( faxinfo ) < 0 )
    theIFaxCheck2DCodes( state ) = TRUE ;

  return TRUE;
}

static Bool ccittfaxFilterInit( FILELIST *filter ,
                                 OBJECT *args ,
                                 STACK *stack )
{
  FAXSTATE *state ;                     /* cached pointer */
  FAXINFO *faxinfo ;                    /* cached pointer */
  int32 pop_args = 0 ;

  enum {
    ccf_Uncompressed, ccf_K, ccf_EndOfLine, ccf_EncodedByteAlign,
    ccf_Columns, ccf_Rows, ccf_EndOfBlock, ccf_BlackIs1,
    ccf_n_entries
  } ;
  NAMETYPEMATCH thematch[ccf_n_entries + 1] = {
    { NAME_Uncompressed | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_K | OOPTIONAL, 1, { OINTEGER }},
    { NAME_EndOfLine | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_EncodedByteAlign | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_Columns | OOPTIONAL, 1, { OINTEGER }},
    { NAME_Rows | OOPTIONAL, 1, { OINTEGER }},
    { NAME_EndOfBlock | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_BlackIs1 | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  if ( ! init_ccitt_state1(filter) )
    return FALSE ;

  state = theIFilterPrivate( filter );
  faxinfo = theIFaxInfo( state );

  if (args) {
    OBJECT *theo ;
    if ((theo = thematch[ccf_Uncompressed].result) != NULL )
      theIFaxUncompressed( faxinfo ) = oBool(*theo) ;
    if ((theo = thematch[ccf_K].result) != NULL )
      theIFaxK( faxinfo ) = oInteger(*theo) ;
    if ((theo = thematch[ccf_EndOfLine].result) != NULL )
      theIFaxEndOfLine( faxinfo ) = oBool(*theo) ;
    if ((theo = thematch[ccf_EncodedByteAlign].result) != NULL )
      theIFaxEncodedByteAlign( faxinfo ) = oBool(*theo) ;
    if ((theo = thematch[ccf_Columns].result) != NULL ) {
      theIFaxColumns( faxinfo ) = oInteger(*theo) ;
      if ( theIFaxColumns( faxinfo ) <= 0 )
        return error_handler( RANGECHECK ) ;
    }
    if ((theo = thematch[ccf_Rows].result) != NULL ) {
      theIFaxRows( faxinfo ) = oInteger(*theo) ;
      if ( theIFaxRows( faxinfo ) < 0 )
        return error_handler( RANGECHECK ) ;
    }
    if ((theo = thematch[ccf_EndOfBlock].result) != NULL )
      theIFaxEndOfBlock( faxinfo ) = oBool(*theo) ;
    if ((theo = thematch[ccf_BlackIs1].result) != NULL )
      theIFaxBlackIs1( faxinfo ) = oBool(*theo) ;
  }

  if ( ! init_ccitt_state2(filter) )
    return FALSE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static Bool ccittfaxDecodeBuffer( FILELIST *filter , int32 *ret_bytes )
{
  int32    kval ;
  int32    code ;
  int32    codebit ;
  int32    eolmax ;
  int32    eolcount ;
  int32    firsteof ;
  FAXSTATE *faxstate ;
  FAXINFO  *faxinfo ;

  codebit = 0;                  /* init to keep compiler quiet */

  HQASSERT( filter , "filter NULL in ccittfaxDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "ret_bytes NULL in ccittfaxDecodeBuffer." ) ;
  faxstate = theIFilterPrivate( filter ) ;
  HQASSERT( faxstate , "faxstate NULL in ccittfaxDecodeBuffer." ) ;
  faxinfo = theIFaxInfo( faxstate ) ;
  HQASSERT( faxinfo , "faxinfo NULL in ccittfaxDecodeBuffer." ) ;

  kval = theIFaxK( faxinfo ) ;

  code = 0 ;
  eolcount = 0 ;
  eolmax = ( kval < 0 ? G40DEndCount : G31DEndCount ) ;
  firsteof = TRUE ;
  do {
    if (( theIFaxEndOfLine( faxinfo )) || ( kval <= 0 ) || TRUE ) { /* Always check for EOL */
      code = extract_next_code_word( filter , TRUE ) ;
      if ( code == EOF ) {
        if ( firsteof ) {
          *ret_bytes = 0 ;
          return TRUE ;
        }
        return error_handler( IOERROR ) ;
      }
      firsteof = FALSE ;
      if ( code != EolCode ) {
        if ( kval > 0 ) {
          if ( eolcount == 1 )
            break ;
          /* Need to ungetc the code, since we want the codebit */
          ungetccodings( filter , code ) ;
        }
        else
          break ;
      }
      else
        eolcount++ ;
    }
    if ( kval > 0 ) {
      codebit = extract_next_code_bit( filter ) ;
      if ( codebit == EOF )
        return error_handler( IOERROR ) ;
      if ( codebit == 0 ) {
        theIFaxCheck2DCodes( faxstate ) = TRUE ;
        theIFaxLineCount( faxstate )++ ;
        if ( theIFaxLineCount( faxstate ) >= kval ) {
          HQTRACE( debug_ccitt , ("linecount exceeds K: %d,%d",theIFaxLineCount( faxstate ),kval)) ;
        }
        code = extract_next_code_word( filter , TRUE ) ;
        if ( code == EOF )
          return error_handler( IOERROR ) ;
        break ;
      }
      else {
        theIFaxCheck2DCodes( faxstate ) = FALSE ;
        theIFaxLineCount( faxstate ) = 0 ;
        if ( code != EolCode ) {
          code = extract_next_code_word( filter , TRUE ) ;
          if ( code == EOF )
            return error_handler( IOERROR ) ;
          break ;
        }
      }
    }
  } while ( eolcount < eolmax ) ;

  if ( eolcount == 0 ) {
    if ( theIFaxRows( faxinfo ) &&
        ( theIFaxCurHeight( faxstate ) == theIFaxRows( faxinfo ))) {
      HQTRACE( debug_ccitt , ("ending rows but with 0 eol")) ;
      return error_handler( IOERROR ) ;
    }
    if ( theIFaxEndOfLine( faxinfo )) {
      HQTRACE( debug_ccitt , ("EndOfLine expected")) ;
      return error_handler( IOERROR ) ;
    }
    theIFaxLookAhead( faxstate ) = code ;
  }
  else if ( eolcount == 1 ) {
    if ( theIFaxRows( faxinfo ) &&
        ( theIFaxCurHeight( faxstate ) == theIFaxRows( faxinfo ))) {
      HQTRACE( debug_ccitt , ("ending rows but with 1 eol")) ;
      return error_handler( IOERROR ) ;
    }
    HQTRACE( debug_ccitt &&
             ! theIFaxEndOfLine( faxinfo ) ,
             ("EndOfLine got when 'possibly' shouldn't")) ;
    theIFaxLookAhead( faxstate ) = code ;
  }
  else if ( eolcount < eolmax ) {
    HQTRACE( debug_ccitt , ("Funny number of EolCodes: %d",eolcount)) ;
    return error_handler( IOERROR ) ;
  }
  else { /* ( eolcount == eolmax ) */
    *ret_bytes = 0 ;
    return TRUE ;
  }

  if ( theIFaxRefLine( faxstate )) {
    uint32 *tmp = theIFaxRefLine( faxstate ) ;
    theIFaxRefLine( faxstate ) = ( uint32 * )theIBuffer( filter ) ;
    theIBuffer( filter ) = ( uint8 * )tmp ;
  }
  theIPtr( filter ) = theIBuffer( filter ) ;

  if ( theIFaxBufEdge( faxstate )) {
    int32 *tmp = theIFaxBufEdge( faxstate ) ;
    theIFaxBufEdge( faxstate ) = theIFaxRefEdge( faxstate ) ;
    theIFaxRefEdge( faxstate ) = tmp ;
  }

  /* choose the correct fax decode algorithm, see page 135 */
  if ( kval == 0 ) { /* Group 3, 1 dimensional fax */
    if ( ! group12D_decode( filter , 0 ))
      return FALSE ;
  }
  else if ( kval > 0 ) { /* group 3, 2 dimensional fax */
    if ( codebit == 1 ) {
      if ( ! group12D_decode( filter , 0 ))
        return FALSE ;
    }
    else if ( codebit == 0 ) {
      if ( ! group12D_decode( filter , 1 ))
        return FALSE ;
    }
    else {
      HQTRACE( debug_ccitt , ("funny codebit: %d",codebit)) ;
      return error_handler( IOERROR ) ;
    }
  }
  else if ( kval < 0 ) { /* Group 4 fax */
    if ( ! group12D_decode( filter , 1 ))
      return FALSE ;
  }

  theICount( filter ) = ( theIFaxMaxWidth( faxstate ) + 7 ) >> 3 ;

  theIFaxCurHeight( faxstate )++ ;
  if ( theIFaxRows( faxinfo ) &&
      ( theIFaxCurHeight( faxstate ) > theIFaxRows( faxinfo ))) {
    HQTRACE( debug_ccitt , ("too many rows: %d",theIFaxCurHeight( faxstate ))) ;
    return error_handler( IOERROR ) ;
  }

  if ( ! theIFaxEndOfBlock( faxinfo ))
    if ( theIFaxRows( faxinfo ))
      if ( theIFaxCurHeight( faxstate ) >= theIFaxRows( faxinfo ))
        theICount( filter ) = - theICount( filter ) ;

  if ( theIFaxEncodedByteAlign( faxinfo ))
    if ( ! theIFaxEndOfLine( faxinfo ))
      theIFaxLastBits( faxstate ) = 0 ;

  *ret_bytes = theICount( filter ) ;
  return TRUE ;
}

static Bool ccittfaxEncodeBuffer( FILELIST *filter )
{
  int32 kval ;
  FAXSTATE *faxstate ;
  FAXINFO  *faxinfo ;

  HQASSERT( filter , "filter NULL in ccittfaxEncodeBuffer." ) ;
  faxstate = theIFilterPrivate( filter ) ;
  HQASSERT( faxstate , "faxstate NULL in ccittfaxEncodeBuffer." ) ;
  faxinfo = theIFaxInfo( faxstate ) ;
  HQASSERT( faxinfo , "faxinfo NULL in ccittfaxEncodeBuffer." ) ;

  /* If there's going to be something to write, make sure there's
   * a viable underlying file.
   */

  if ( theICount( filter ) ||
       ( isIClosing( filter ) && (( theIFaxLastBits( faxstate ) > 0 ) ||
                                    theIFaxEndOfBlock( faxinfo )))) {
    FILELIST *uflptr = theIUnderFile( filter ) ;

    HQASSERT( uflptr , "uflptr NULL in ccittFaxEncodeBuffer." ) ;

    if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
      return error_handler( IOERROR ) ;
  }

  if ( theICount( filter )) {
    if ( theICount( filter ) != (( theIFaxMaxWidth( faxstate ) + 7 ) >> 3 )) {
      HQTRACE( debug_ccitt , ("not enough bytes in fax ccitt compression line: %d,%d",theICount( filter ),( theIFaxMaxWidth( faxstate ) + 7 ) / 8 )) ;
      return error_handler( IOERROR ) ;
    }

    if ( theIFaxEndOfLine( faxinfo )) {
      if ( theIFaxEncodedByteAlign( faxinfo )) {
        int32 sparebits = 8 - theIFaxLastBits( faxstate ) ;
        while ( sparebits > ( EolCodeAlignBits % 8 )) {
          if ( ! output_code( filter , & Data0 ))
            return FALSE ;
          sparebits-- ;
        }
      }
      if ( ! output_code( filter , & EndOfLine))
        return FALSE ;
    }

    kval = theIFaxK( faxinfo ) ;
    if ( kval == 0 ) {
      if ( ! group12D_encode( filter , 0 ))
        return FALSE ;
    }
    else if ( kval > 0 ) {
      if ( theIFaxLineCount( faxstate ) == 0 ) {
        if ( ! output_code( filter , & Data1 ))
          return FALSE ;
        if ( ! group12D_encode( filter , 0 ))
          return FALSE ;
      }
      else {
        if ( ! output_code( filter , & Data0 ))
          return FALSE ;
        if ( ! group12D_encode( filter , 1 ))
          return FALSE ;
      }
      theIFaxLineCount( faxstate )++ ;
      if ( theIFaxLineCount( faxstate ) >= kval )
        theIFaxLineCount( faxstate ) = 0 ;
    }
    else if ( kval < 0 ) {
      if ( ! group12D_encode( filter , 1 ))
        return FALSE ;
    }

    if ( theIFaxRefLine( faxstate )) {
      uint32 *tmp = theIFaxRefLine( faxstate ) ;
      theIFaxRefLine( faxstate ) = ( uint32 * )theIBuffer( filter ) ;
      theIBuffer( filter ) = ( uint8 * )tmp ;
    }

    if ( theIFaxEncodedByteAlign( faxinfo )) {
      if ( theIFaxEndOfLine( faxinfo )) {
        int32 sparebits = 8 - theIFaxLastBits( faxstate ) ;
        if ( sparebits < ( EolCodeAlignBits % 8 )) {
          FLUSHWRITEFILTER() ;
          sparebits = 8 ;
        }
        while ( sparebits > ( EolCodeAlignBits % 8 )) {
          if ( ! output_code( filter , & Data0 ))
            return FALSE ;
          sparebits-- ;
        }
      }
      else
        FLUSHWRITEFILTER() ;
    }
  }

  if ( isIClosing( filter )) {
    if ( theIFaxEndOfBlock( faxinfo )) {
      int32 eolmax ;
      int32 eolcount ;

      kval = theIFaxK( faxinfo ) ;
      eolmax = ( kval < 0 ? G40DEndCount : G31DEndCount ) ;

      for ( eolcount = 0 ; eolcount < eolmax ; ++eolcount ) {
        if ( ! output_code( filter , & EndOfLine ))
          return FALSE ;
        if ( kval > 0 ) {
          if ( ! output_code( filter, & Data1 ))
            return FALSE ;
        }
      }
    }
    FLUSHWRITEFILTER() ;
  }

  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;

  return TRUE ;
}

/*****************************/
/* Internal functions        */
/*****************************/

/*
  runs through the code table provided and places elements in the
  sorttable according to bit size specified in the code entry
*/
static void sortsized( TERMCODE codetable[] , SORTED sortedtable[] )
{
  TERMCODE *entry ;
  SORTED *sort ;

  for ( entry = codetable ,
       sort = ( sortedtable + (int32) theINumOfBits( entry )) ;
       theINumOfBits( entry ) > 0 ;
       entry++ , sort = ( sortedtable + (int32) theINumOfBits( entry )) ) {
    theISortTable( sort )[ theISortCount( sort ) ] = entry ;
    theISortCount( sort )++ ;
  }
}

static void sorthashtable( TERMCODE **hashtable , TERMCODE *codes )
{
  int32 i ;
  TERMCODE *ptr ;

  for ( ptr = codes ; theINumOfBits( ptr ) > 0 ; ptr++ ) {
    int32 bits = (int32) theINumOfBits( ptr ) ;
    if ( bits <= CCITTHASHBITS ) {
      int32 word = (int32) theICodeWord( ptr ) ;
      int32 lowbits = CCITTHASHBITS - bits ;
      word <<= lowbits ;
      for ( i = 0 ; i < ( 1 << lowbits ) ; i++ ) {
        HQASSERT( hashtable[ word + i ] == NULL , "sorthashtable failed" ) ;
        hashtable[ word + i ] = ptr ;
      }
    }
  }
}

static void finalisehashtable( TERMCODE **hashtable )
{
  int32 i ;
  static TERMCODE invalid_code = { 0x7fffffff , 0x7fffffff , 0x7fffffff } ;

  for ( i = 0 ; i < CCITTHASHSIZE ; ++i ) {
    TERMCODE *ptr = hashtable[ i ] ;
    if ( ptr == NULL )
      hashtable[ i ] = & invalid_code ;
  }
}

/*
  initialises all of the data involved in the fax decode/encode
  The tables are sorted according to the bit size of each code
  to allow faster matching of termination and make-up codes
*/
static void initfaxdata( void )
{
  int32 i ;             /* loop variable */

  /* reset counters */
  for ( i = 0 ; i < ( MaxCodeSize + 1 ) ; i++ ) {
    theSortCount( white_table[i] ) = 0 ;
    theSortCount( black_table[i] ) = 0 ;
    theSortCount( uncompressed_table[i] ) = 0;
  }

  /* plow through all of the tables putting them in order */
  sortsized( white_terminators , white_table ) ;
  sortsized( black_terminators , black_table ) ;
  sortsized( white_makeup_codes , white_table ) ;
  sortsized( black_makeup_codes , black_table ) ;
  /* note that extended makeups are added to both black and white */
  sortsized( extended_makeup_codes , white_table ) ;
  sortsized( extended_makeup_codes , black_table ) ;

  /* create the uncompressed code table */
  sortsized( uncompressed_codes, uncompressed_table ) ;

  sorthashtable( hashwhitetable  , white_terminators ) ;
  sorthashtable( hashwhitetable  , white_makeup_codes ) ;
  sorthashtable( hashwhitetable  , extended_makeup_codes ) ;

  sorthashtable( hashblacktable  , black_terminators ) ;
  sorthashtable( hashblacktable  , black_makeup_codes ) ;
  sorthashtable( hashblacktable  , extended_makeup_codes ) ;

  sorthashtable( hash2dcodetable , twod_codetable ) ;

  finalisehashtable( hashwhitetable ) ;
  finalisehashtable( hashblacktable ) ;
  finalisehashtable( hash2dcodetable ) ;
}

static void ungetccodings( FILELIST *filter , int32 code )
{
  int32    outbits ;
  int32    outword ;
  int32    byte ;
  int32    bits ;
  TERMCODE *ptr ;
  FAXSTATE *faxstate ;

  faxstate = theIFilterPrivate( filter ) ;

  for ( ptr = &twod_codetable[ 0 ] ; theINumOfBits( ptr ) > 0 ; ptr++ )
    if ( code == theICodeValue( ptr ))
      break ;
  if ( theINumOfBits( ptr ) < 0 ) {
    for ( ptr = white_terminators ; theINumOfBits( ptr ) > 0 ; ptr++ )
      if ( code == theICodeValue( ptr ))
        break ;
    if ( theINumOfBits( ptr ) < 0 ) {
      for ( ptr = white_makeup_codes ; theINumOfBits( ptr ) > 0 ; ptr++ )
        if ( code == theICodeValue( ptr ))
          break ;
      if ( theINumOfBits( ptr ) < 0 ) {
        for ( ptr = extended_makeup_codes ; theINumOfBits( ptr ) > 0 ; ptr++ )
          if ( code == theICodeValue( ptr ))
            break ;
      }
    }
  }
  HQASSERT( theINumOfBits( ptr ) > 0 , "ungetc failed" ) ;

  outword = theIFaxLastByte( faxstate ) ;
  outbits = theIFaxLastBits( faxstate ) ;
  outword = outword & (( 1 << outbits ) - 1 ) ;

  byte = theICodeWord( ptr ) ;
  bits = theINumOfBits( ptr ) ;

  bits  += outbits ;
  byte <<= outbits ;
  byte  |= outword ;

  theIFaxLastByte( faxstate ) = byte ;
  theIFaxLastBits( faxstate ) = bits ;
}

static Bool searchcodings( int32 bitwidth , int32 value , int32 amwhite , int32 check2dcodes , int32 *retval )
{
  SORTED   *table ;             /* pointer to the correct sort table */
  TERMCODE *ptr ;               /* pointer to code in table */
  TERMCODE **pptr ;
  TERMCODE **plim ;

  /* search for coding modes if required */
  if ( check2dcodes ) {
    for ( ptr = &twod_codetable[ 0 ] ; theINumOfBits( ptr ) > 0 ; ptr++ ) {
      if (( theINumOfBits( ptr ) == bitwidth ) &&
          ( theICodeWord( ptr ) == value )) {
        *retval = (int32) theICodeValue( ptr ) ;
        return ( TRUE ) ;
      }
    }
  }

  /* search table of correct width */
  table = amwhite ? &white_table[ bitwidth ] : &black_table[ bitwidth ] ;
  pptr = theISortTable( table ) ;
  plim = pptr + theISortCount( table ) ;

  while ( pptr < plim ) {
    if ( value == theICodeWord( pptr[ 0 ] )) {
      *retval = (int32) theICodeValue( pptr[ 0 ]) ;
      return( TRUE ) ;
    }
    pptr++ ;
  }

  /* check for end of line */
  if (( bitwidth == theNumOfBits( EndOfLine )) &&
      ( value == theCodeWord( EndOfLine ))) {
    *retval = (int32) theCodeValue( EndOfLine ) ;
    return ( TRUE ) ;
  }

  return ( FALSE ) ;
}

static Bool searchuncompressed( int32 bitwidth , int32 value , int32 *retval )
{
  SORTED *table ;               /* pointer to the correct sort table */
  TERMCODE **pptr ;
  TERMCODE **plim ;

  /* search table of correct width */
  table = &uncompressed_table[ bitwidth ] ;
  pptr = theISortTable( table ) ;
  plim = pptr + theISortCount( table ) ;

  while ( pptr < plim ) {
    TERMCODE *ptr = pptr[ 0 ] ;
    if ( value == theICodeWord( ptr )) {
      *retval = theICodeValue( ptr ) ;
      return TRUE ;
    }
    ++pptr ;
  }

  return FALSE ;
}

static int32 extract_next_code_bit( FILELIST *filter )
{
  int32 byte ;          /* temporary store for word from file */
  int32 bits ;          /* current position in lastest byte */

  FAXSTATE *faxstate ;

  faxstate = theIFilterPrivate( filter ) ;

  /* are there any bits left from last byte read */
  byte = theIFaxLastByte( faxstate ) ;
  bits = theIFaxLastBits( faxstate ) ;
  if ( bits <= CCITTHASHBITS ) {
    int32 nextbyte = Getc( theIUnderFile( filter )) ;
    if ( nextbyte == EOF ) {
      if ( bits == 0 )
        return EOF ;
    }
    else {
      byte = ( byte << 8 ) | nextbyte ;
      theIFaxLastByte( faxstate ) = byte ;
      bits += 8 ;
    }
  }

  theIFaxLastBits( faxstate ) = --bits ;
  return (( byte >> bits ) & 0x01 ) ;
}

#define EXTRACT_NEXT_CODE_WORD( _filter , _faxstate , _amwhite , _code ) MACRO_START \
do { \
  int32 _byte_ ; \
  int32 _bits_ ; \
  TERMCODE *_hashptr_ ; \
  \
  _byte_ = theIFaxLastByte( _faxstate ) ; \
  _bits_ = theIFaxLastBits( _faxstate ) ; \
  if ( _bits_ <= CCITTHASHBITS ) { \
    int32 _nextbyte_ = Getc( theIUnderFile( (_filter) )) ; \
    if ( _nextbyte_ == EOF ) { \
      if ( _bits_ == 0 ) \
        _code = EOF ; \
      else \
        _code = extract_next_code_word_bitbybit( (_filter) , (_amwhite) ) ; \
      \
      /* This break is here to jump out of the do {...} while ( FALSE ), since */ \
      /* we've been forced to use the slow method of getting the next code value. */ \
      break ; \
    } \
    else { \
      _byte_ = ( _byte_ << 8 ) | _nextbyte_ ; \
      theIFaxLastByte( _faxstate ) = _byte_ ; \
      _bits_ += 8 ; \
    } \
  } \
  \
  /* Use a look up table to speed up determination of op code */ \
  _hashptr_ = ( theIFaxCheck2DCodes( _faxstate ) ? \
                hash2dcodetable : \
                ( (_amwhite) ? hashwhitetable : hashblacktable )) \
    [ ( _byte_ >> ( _bits_ - CCITTHASHBITS )) & CCITTHASHMASK ] ; \
  HQASSERT( _hashptr_ , "_hashptr_ should have been filled in" ) ; \
  _bits_ -= theINumOfBits( _hashptr_ ) ; \
  if ( _bits_ >= 0 ) { \
    theIFaxLastBits( _faxstate ) = _bits_ ; \
    _code = theICodeValue( _hashptr_ ) ; \
  } \
  else { \
    _bits_ += theINumOfBits( _hashptr_ ) ; \
    theIFaxLastBits( _faxstate ) = _bits_ ; \
    _code = extract_next_code_word_bitbybit( (_filter) , (_amwhite) ) ; \
  } \
} while ( FALSE ) ; \
MACRO_END

static int32 extract_next_code_word( FILELIST *filter , int32 amwhite )
{
  int32 code ;
  FAXSTATE *faxstate ;
  faxstate = theIFilterPrivate( filter ) ;
  EXTRACT_NEXT_CODE_WORD( filter , faxstate , amwhite , code ) ;
  return code ;
}

static int32 extract_next_code_word_bitbybit( FILELIST *filter , int32 amwhite )
{
  int32 code = 0 ;              /* value of code in table */
  int32 byte ;                  /* temporary store for word from file */
  int32 bits ;                  /* current position in lastest byte */
  int32 numofbits;              /* number of bits to look for */
  int32 wordvalue ;             /* shift into this for testing */
  int32 check2dcodes ;
  FAXSTATE *faxstate ;

  HQASSERT( filter ,
            "filter NULL in extract_next_code_word_bitbybit (ccittfax)." ) ;

  faxstate = theIFilterPrivate( filter ) ;

  byte = theIFaxLastByte( faxstate ) ;
  bits = theIFaxLastBits( faxstate ) ;
  HQASSERT( bits > 0 , "should only call this routine if got some bits" ) ;
  if ( bits <= CCITTHASHBITS ) {
    numofbits = 1 ;
    bits -= 1 ;
    wordvalue = ( byte >> bits ) ;
    wordvalue &= 1 ;
  }
  else {
    numofbits = CCITTHASHBITS + 1 ;
    bits -= CCITTHASHBITS + 1 ;
    wordvalue = ( byte >> bits ) ;
    wordvalue &= (( CCITTHASHMASK << 1 ) | 1 ) ;
  }

  check2dcodes = theIFaxCheck2DCodes( faxstate ) ;

  /* now loop until found a match (hash table failed) */
  while ( ! searchcodings( numofbits, wordvalue , amwhite , check2dcodes , & code )) {
    if ( bits == 0 ) {
      byte = Getc( theIUnderFile( filter )) ;
      if ( byte == EOF )
        return EOF ;
      theIFaxLastByte( faxstate ) = byte ;
      bits = 0x08 ;
    }
    numofbits++ ; bits-- ;
    wordvalue <<= 1 ;
    wordvalue |= (( byte >> bits ) & 0x01 ) ;

    /* For fill codes dont let numofbits increment, in this way we can
     * skip all leading zero's until 11 zeros and a 1 matches.
     */
    if (( numofbits > 11 ) && ( wordvalue == (int16)0 )) numofbits = 11 ;

    /* is this code too large */
    if ( numofbits > MaxCodeSize ) {
      HQTRACE( debug_ccitt , ("MaxCodeSize overflowed(extract_next_code_word): %d",numofbits)) ;
      return ( EOF ) ;
    }
  }
  theIFaxLastBits( faxstate ) = bits ;
  return ( code ) ;
}

static int32 get_next_uncompressed_code( FILELIST *filter )
{
  int32 code = 0 ;              /* value of code in table */
  int32 byte ;                  /* temporary store for word from file */
  int32 bits ;                  /* current position in lastest byte */
  int32 numofbits;              /* number of bits to look for */
  int32 wordvalue ;             /* shift into this for testing */

  FAXSTATE *faxstate ;

  faxstate = theIFilterPrivate( filter ) ;

  byte = theIFaxLastByte( faxstate ) ;
  bits = theIFaxLastBits( faxstate ) ;
  if ( bits <= CCITTHASHBITS ) {
    int32 nextbyte = Getc( theIUnderFile( filter )) ;
    if ( nextbyte == EOF )
      return EOF ;
    byte = ( byte << 8 ) | nextbyte ;
    theIFaxLastByte( faxstate ) = byte ;
    bits += 8 ;
  }

  numofbits = 1 ;
  bits -= 1 ;
  wordvalue = ( byte >> bits ) ;
  wordvalue &= 1 ;

  /* now loop until found a match  */
  while ( ! searchuncompressed( numofbits , wordvalue , & code )) {
    if ( bits == 0 ) {
      byte = Getc( theIUnderFile( filter )) ;
      if ( byte == EOF )
        return EOF ;
      theIFaxLastByte( faxstate ) = byte ;
      bits = 0x08 ;
    }
    numofbits++ ; bits-- ;
    wordvalue <<= 1 ;
    wordvalue |= (( byte >> bits ) & 0x01 ) ;

    /* is this code too large */
    if ( numofbits > MaxCodeSize ) {
      HQTRACE( debug_ccitt , ("MaxCodeSize overflowed(get_next_uncompressed_code): %d",numofbits)) ;
      return ( EOF ) ;
    }
  }
  theIFaxLastBits( faxstate ) = bits ;
  return ( code ) ;
}

static Bool output_code( FILELIST *filter , TERMCODE *termcode )
{
  int32 outbits ;
  int32 outword ;

  int32 numofbits ;
  int32 wordvalue ;

  FAXSTATE *faxstate ;

  HQASSERT( filter , "filter NULL in output_code (ccittfax)." ) ;
  faxstate = theIFilterPrivate( filter ) ;

  outbits = theIFaxLastBits( faxstate ) ;
  outword = theIFaxLastByte( faxstate ) ;

  numofbits = (int32) theINumOfBits( termcode ) ;
  wordvalue = (int32) theICodeWord( termcode ) ;

  outbits += numofbits ;
  outword <<= numofbits ;
  outword |= wordvalue ;

  { FILELIST *flptr = theIUnderFile( filter ) ;
    HQASSERT( flptr , "uflptr NULL in output_code (ccittfax)." ) ;
    while ( outbits >= 8 ) {
      uint8 outbyte = ( uint8 )( outword >> ( outbits - 8 )) ;
      if ( TPutc( outbyte , flptr ) == EOF )
        return error_handler( IOERROR ) ;
      outbits -= 8 ;
    }
  }
  theIFaxLastBits( faxstate ) = outbits ;
  theIFaxLastByte( faxstate ) = outword ;

  return TRUE ;
}

static Bool output_code_runlen( FILELIST *filter , int32 newcode , int32 amwhite )
{
  TERMCODE *termcode ;

  HQASSERT( newcode >= 0   , "unknown negative code" ) ;

  /* get the value to output from the coding tables */
  while ( newcode >= MaxExtendedMakeUp ) {
    int32 index = ( MaxExtendedMakeUp - MinExtendedMakeUp ) / CodeDivider ;
    termcode = extended_makeup_codes + index ;
    if ( ! output_code( filter , termcode ))
      return FALSE ;

    newcode -= MaxExtendedMakeUp ;
  }
  if ( newcode >= MinExtendedMakeUp ) {
    int32 index = ( newcode - MinExtendedMakeUp ) / CodeDivider ;
    termcode = extended_makeup_codes + index ;
    if ( ! output_code( filter , termcode ))
      return FALSE ;

    newcode -= ( index * CodeDivider + MinExtendedMakeUp ) ;
  }
  if ( newcode >= MinMakeUp ) {
    int32 index = ( newcode - MinMakeUp ) / CodeDivider ;
    termcode = ( amwhite ? white_makeup_codes : black_makeup_codes ) + index ;
    if ( ! output_code( filter , termcode ))
      return FALSE ;

    newcode -= ( index * CodeDivider + MinMakeUp ) ;
  }
  termcode = ( amwhite ? white_terminators : black_terminators ) + newcode ;

  return output_code( filter , termcode ) ;
}

#ifdef highbytefirst
/* both 1 operation */
#define BIW_LMASK(_x) ( 0xffffffffu >> (_x))
#define BIW_RMASK(_x) ( 0xffffffffu << (_x))

#define BIW_START       24
#define BIW_TEST(_i)    ((_i) >=  0 )
#define BIW_STEP        -8
#define BIW_OFFSET(_i)  ( 24 - (_i))

/* 4 operations */
#define BIW_XTNDnSTATE( _bits , _bitp , _state ) MACRO_START            \
  int32 _tmp_ = (_bitp) & 31 ;                                          \
  (_bits) <<= _tmp_ ; /* Make leading bits the same. */                 \
  BIT_SHIFT32_SIGNED_RIGHT((_bits), (_bits), _tmp_) ;                   \
  (_state) = SIGN32_NEG(_bits) ; /* state now contains all 0 or 1s. */  \
MACRO_END
#else
/* both 5 operations */
#define BIW_LMASK(_x) (( 0xffffff00u | ( 0x000000ffu >> ((_x) & 7 ))) << ((_x) & 24 ))
#define BIW_RMASK(_x) (( 0x00ffffffu | ( 0xff000000u << ((_x) & 7 ))) >> ((_x) & 24 ))

#define BIW_START        0
#define BIW_TEST(_i)    ((_i) <= 24 )
#define BIW_STEP         8
#define BIW_OFFSET(_i)  (_i)

/* 13 operations */
#define BIW_XTNDnSTATE( _bits , _bitp , _state ) MACRO_START \
  int32 _mask_ ; \
  int32 _tmp_ = (_bitp) & 31 ; \
  (_state) = ((_bits) << (_tmp_ ^ 24)) ; \
  (_state) = SIGN32_NEG(_state) ;        \
  _mask_ = BIW_LMASK( _tmp_ ) ; \
  (_bits) &= _mask_ ; \
  (_bits) |= (( ~_mask_ ) & (_state)) ; \
MACRO_END
#endif

static int32 detectnextbit1( uint32 *buf , int32 set , int32 bitp )
{
  int32 bits ;
  int32 state ;

  --bitp ; /* Find the longest span with this bit value, then
            * more bits until we hit the required set bit value.
            */
  buf += ( bitp >> 5 ) ;
  bits = *buf++ ;
  BIW_XTNDnSTATE( bits , bitp , state ) ;

  for (;;) {
    if ( bits != state ) {
      int32 i ;
      for ( i = BIW_START ; BIW_TEST( i ) ; i += BIW_STEP ) {
        uint8 byte = ( uint8 )( bits >> i ) ;
        if ( byte != ( uint8 )state ) {
          int32 inside ;
          byte = ( uint8 )( byte ^ state ) ;
          inside = rlelength[ byte ] ;
          while ( inside != 8 ) {
            state = ~state ;
            if ( state == set ) {
              return ( bitp & (~31)) + BIW_OFFSET( i ) + inside ;
            }
            byte = ( uint8 )( ~byte ) ;
            byte &= 0xffu >> inside ;
            inside = rlelength[ byte ] ;
          }
        }
      }
    }
    bitp += 32 ;
    bits = *buf++ ;
  }
}

#if defined( ASSERT_BUILD )
#define TEST_DETECTNEXTEDGE1() \
  int32 check1 = detectnextbit1( theIFaxRefLine( faxstate ) , ~bufset , a1pos ) ;
#define CHCK_DETECTNEXTEDGE1() MACRO_START \
  if ( check1 > abmax ) check1 = abmax ; \
  HQASSERT( check1 == a1pos , "DETECTNEXTEDGE1 != detectnextbit1" ) ; \
MACRO_END
#else
#define TEST_DETECTNEXTEDGE1() EMPTY_STATEMENT() ;
#define CHCK_DETECTNEXTEDGE1() EMPTY_STATEMENT() ;
#endif

#define DETECTNEXTEDGE1( _a1pos , \
                         _refedge , _lrefedge , \
                         _bufset , _refset ) MACRO_START \
  TEST_DETECTNEXTEDGE1() ; \
  if ((_lrefedge) >= (_a1pos)) { \
    do { \
      (_refedge) -= 1 ; \
      (_lrefedge) = (_refedge)[ 0 ] ; \
      (_refset) = ~(_refset) ; \
    } while ((_lrefedge) >= (_a1pos)) ; \
  } \
  else { \
    (_lrefedge) = (_refedge)[ 1 ] ; \
    while ((_lrefedge) < (_a1pos)) { \
      (_refedge) += 1 ; \
      (_lrefedge) = (_refedge)[ 1 ] ; \
      (_refset) = ~(_refset) ; \
    } \
  } \
  (_refedge) += 2 ; \
  if ( (_refset) == (_bufset)) { \
    (_refedge) -= 1 ; \
    (_refset) = ~(_refset) ; \
  } \
  (_lrefedge) = (_refedge)[ 0 ] ; \
  (_a1pos) = (_lrefedge) ; \
  CHCK_DETECTNEXTEDGE1() ; \
MACRO_END

static int32 detectnextbit2( uint32 *buf , int32 set , int32 bitp )
{
  int32 bits ;
  int32 state ;
  int32 cnt = 2 ;

  --bitp ; /* Find the longest span with this bit value, then
            * more bits until we hit the required set bit value.
            */
  buf += ( bitp >> 5 ) ;
  bits = *buf++ ;
  BIW_XTNDnSTATE( bits , bitp , state ) ;

  for (;;) {
    if ( bits != state ) {
      int32 i ;
      for ( i = BIW_START ; BIW_TEST( i ) ; i += BIW_STEP ) {
        uint8 byte = ( uint8 )( bits >> i ) ;
        if ( byte != ( uint8 )state ) {
          int32 inside ;
          byte = ( uint8 )( byte ^ state ) ;
          inside = rlelength[ byte ] ;
          while ( inside != 8 ) {
            state = ~state ;
            if ( state == set ) {
              if ((--cnt) == 0 )
                return ( bitp & (~31)) + BIW_OFFSET( i ) + inside ;
              set = ~set ;
            }
            byte = ( uint8 )( ~byte ) ;
            byte &= 0xffu >> inside ;
            inside = rlelength[ byte ] ;
          }
        }
      }
    }
    bitp += 32 ;
    bits = *buf++ ;
  }
}

#if defined( ASSERT_BUILD )
#define TEST_DETECTNEXTEDGE2() \
  int32 check2 = detectnextbit2( theIFaxRefLine( faxstate ) , ~bufset , a1pos ) ;
#define CHCK_DETECTNEXTEDGE2() MACRO_START \
  if ( check2 > abmax ) check2 = abmax ; \
  HQASSERT( check2 == a1pos , "DETECTNEXTEDGE2 != detectnextbit2" ) ; \
MACRO_END
#else
#define TEST_DETECTNEXTEDGE2() EMPTY_STATEMENT() ;
#define CHCK_DETECTNEXTEDGE2() EMPTY_STATEMENT() ;
#endif

#define DETECTNEXTEDGE2( _a1pos , \
                         _refedge , _lrefedge , \
                         _bufset , _refset ) MACRO_START \
  TEST_DETECTNEXTEDGE2() ; \
  if ((_lrefedge) >= (_a1pos)) { \
    do { \
      (_refedge) -= 1 ; \
      (_lrefedge) = (_refedge)[ 0 ] ; \
      (_refset) = ~(_refset) ; \
    } while ((_lrefedge) >= (_a1pos)) ; \
  } \
  else { \
    (_lrefedge) = (_refedge)[ 1 ] ; \
    while ( (_lrefedge) < (_a1pos)) { \
      (_refedge) += 1 ; \
      (_lrefedge) = (_refedge)[ 1 ] ; \
      (_refset) = ~(_refset) ; \
    } \
  } \
  (_refedge) += 2 ; \
  if ( (_refset) != (_bufset)) { \
    (_refedge) += 1 ; \
    (_refset) = ~(_refset) ; \
  } \
  (_lrefedge) = (_refedge)[ 0 ] ; \
  (_a1pos) = (_lrefedge) ; \
  CHCK_DETECTNEXTEDGE2() ; \
MACRO_END

#define CCITTFILL( _buf , _x1 , _x2 , _set ) MACRO_START \
{ \
  uint32 _mask_ ; \
  uint32 *_ptr_ , *_end_ ; \
  int32 _x1_ , _x2_ ; \
  \
  HQASSERT( _x1 < _x2 , "x1,x2 swapped over" ) ; \
  \
  /* \
    Keep this one simple; we clear the line to black to start with \
    and in general not many runs (50% in fact) are called through here. \
  */ \
  \
  _x1_ = (_x1) ; \
  _x2_ = (_x2) - 1 ; \
  \
  _ptr_ = ( uint32 * )(_buf) + ( _x1_ >> 5 ) ; \
  _end_ = ( uint32 * )(_buf) + ( _x2_ >> 5 ) ; \
  \
  _x1_ = _x1_ & 31 ; \
  _x2_ = 31 - ( _x2_ & 31 ) ; \
  \
  if ( _ptr_ == _end_ ) { \
    _mask_ = BIW_LMASK( _x1_ ) & BIW_RMASK( _x2_ ) ; \
    _ptr_[ 0 ] = ( _ptr_[ 0 ] & ~_mask_ ) | ((_set) & _mask_ ) ; \
  } \
  else { \
    /* Partial left-byte. */ \
    _mask_ = BIW_LMASK( _x1_ ) ; \
    _ptr_[ 0 ] = ( _ptr_[ 0 ] & ~_mask_ ) | ((_set) & _mask_ ) ; \
    _ptr_++ ; \
    \
    /* Middle bytes. */ \
    while ( _ptr_ < _end_ ) { \
      _ptr_[ 0 ] = (_set) ; \
      _ptr_++ ; \
    } \
    \
    /* Partial right-byte. */ \
    _mask_ = BIW_RMASK( _x2_ ) ; \
    _ptr_[ 0 ] = ( _ptr_[ 0 ] & ~_mask_ ) | ((_set) & _mask_ ) ; \
  } \
} \
MACRO_END

#ifdef CCITTFILL_IS_FUNCTION
static void ccittfill( uint32 *buf , int32 x1 , int32 x2 , uint32 set )
{
  CCITTFILL( buf , x1 , x2 , set ) ;
}
#endif

#define STOREFIRSTEDGE( _bufedge , _lbufedge ) MACRO_START \
  (*(_bufedge)++) = (_lbufedge) = -1 ; \
MACRO_END

#define STORELASTEDGE( _bufedge , _abmax , _set ) MACRO_START \
  if ((_bufedge) != NULL ) { \
    (_bufedge)[ 0 ] = (_abmax) ; \
    (_bufedge)[ 1 ] = (_abmax) ; \
    (_bufedge)[ 2 ] = ((_abmax) + 31 ) & (~31) ; \
  } \
MACRO_END

#define STORENEXTEDGE( _a0pos , _a1pos , _a01postest , \
                       _bufedge , _buflimit , _lbufedge , \
                       _bufset , _faxset ) MACRO_START \
  if ((_bufset) == (_faxset)) { \
    if ( _a01postest ) { \
      if ((_bufedge) != NULL ) { \
        if ((_a0pos) > (_lbufedge)) { \
          (_bufedge)[ 0 ] = (_a0pos) ; \
          (_bufedge)[ 1 ] = (_a1pos) ; \
          (_bufedge) += 2 ; \
          if ((_bufedge) > (_buflimit)) { \
            /* Possible in very large images so now an active debug message only */ \
            HQTRACE( debug_ccitt , ("Must stop using edges; buf edge table overflowed" )) ; \
            (_bufedge) = NULL ; \
            theIUseRefEdge( faxstate ) = FALSE ; \
          } \
          (_lbufedge) = (_a1pos) ; \
        } \
        else if ((_a0pos) == (_lbufedge) || \
                 (_a0pos) >= (_bufedge)[ -2 ] ) { \
          (_bufedge)[ -1 ] = (_a1pos) ; \
          (_lbufedge) = (_a1pos) ; \
        } \
        else { \
          HQTRACE( TRUE , ("Must stop using edges; bizare edges" )) ; \
          (_bufedge) = NULL ; \
          theIUseRefEdge( faxstate ) = FALSE ; \
        } \
      } \
      CCITTFILL(( uint32 * )theIBuffer( filter ) , a0pos , a1pos , (_faxset)) ; \
    } \
  } \
MACRO_END

/*******************************************************************/
/* Decoding side of FACSIMILE transmissions                        */
/*******************************************************************/

static Bool group12D_decode(  FILELIST *filter , int32 twodencoding )
{
  int32 code ;
  int32 userefedge ;
  int32 a0pos , a1pos , abmax ;
  int32 lbufedge , lrefedge ;
  int32 *bufedge , *refedge , *buflimit ;
  uint32 bufset , refset , faxset ;
  FAXSTATE *faxstate ;

  faxstate = theIFilterPrivate( filter ) ;

  abmax = theIFaxMaxWidth( faxstate ) ;

  bufset = theIFaxBlackIs1( theIFaxInfo( faxstate )) ;
  faxset = ~bufset ;
  refset = ~faxset ;

  if ( twodencoding )
    theIFaxRefLine( faxstate )[ -1 ] = bufset ;

  refedge = NULL ;
  lrefedge = 0 ;
  userefedge = theIUseRefEdge( faxstate ) ;
  if ( userefedge ) {
    refedge = theIFaxRefEdge( faxstate ) ;
    if ( refedge )
      lrefedge = refedge[ 0 ] ;
  }
  theIUseRefEdge( faxstate ) = TRUE ;

  bufedge = theIFaxBufEdge( faxstate ) ;
  lbufedge = 0 ;
  buflimit = NULL ;
  if ( bufedge ) {
    STOREFIRSTEDGE( bufedge , lbufedge ) ;
    buflimit = bufedge + theIEdgeSize( faxstate ) ;
  }
  HqMemSet32((uint32 *)theIBuffer(filter), (int32)bufset, (abmax + 31) >> 5);

  a0pos = 0 ;
  a1pos = 0 ;
  code = theIFaxLookAhead( faxstate ) ;
  HQTRACE(debug_code,
          ("group12D_decode: image line = %d", theIFaxCurHeight(faxstate)));

  switch ( twodencoding ) {
    /* Since twodencoding is always 0 or 1 we always jump to case 0/1 and use the
     * look ahead code. This is to remove a compiler warning from SGI about the
     * loop not being reachable from the preceding code.
     */
  default:
    HQFAIL( "twodencoding must be 0 or 1 or following code will fail" ) ;
    do {
      EXTRACT_NEXT_CODE_WORD( filter , faxstate , bufset != faxset , code ) ;
      if ( code == EOF )
        return error_handler( IOERROR ) ;
    case 0:
    case 1:

      HQTRACE(debug_code,
              ("group12D_decode: code = %s, a0 = %u", codeword_names[abs(code)], a0pos));

      if ( code == PassModeCode ) {
        if ( refedge )
          DETECTNEXTEDGE2( a1pos , refedge , lrefedge , bufset , refset ) ;
        else {
          a1pos = detectnextbit2( theIFaxRefLine( faxstate ) , ~bufset , a1pos ) ;
          INLINE_MIN32(a1pos, a1pos, abmax) ;
        }
        HQASSERT((a0pos < a1pos),
                 "group12D_decode: a0pos/a1pos out of order (P)");
        HQASSERT((a1pos <= abmax),
                 "group12D_decode: a1pos should be less than abmax (P)");
        STORENEXTEDGE( a0pos , a1pos , TRUE ,
                       bufedge , buflimit , lbufedge ,
                       bufset , faxset ) ;
      }
      else if ( VerticalCode( code )) {
        if ( refedge )
          DETECTNEXTEDGE1( a1pos , refedge , lrefedge , bufset , refset ) ;
        else {
          a1pos = detectnextbit1( theIFaxRefLine( faxstate ) , ~bufset , a1pos ) ;
          INLINE_MIN32(a1pos, a1pos, abmax) ;
        }
        a1pos += ( code - Vertical0Code ) ;
        INLINE_MIN32(a1pos, a1pos, abmax) ;
        /* An initial vertical code can leave a1 at 0 where a0 starts instead of -1 */
        HQASSERT((a0pos == 0 ? a1pos >= 0 : a1pos > a0pos ),
                 "group12D_decode: a0pos/a1pos out of order (V)");
        HQASSERT((a1pos <= abmax),
                 "group12D_decode: a1pos should be less than abmax (V)");
        STORENEXTEDGE( a0pos , a1pos , a0pos < a1pos ,
                       bufedge , buflimit , lbufedge ,
                       bufset , faxset ) ;
        bufset = ~bufset ;
      }
      else if ( code == Extension2DCode ) {
        bufset = ~faxset ;
        do {
          int32 i = 1 ;
          IMAGETABLE *these_bits ;
          code = get_next_uncompressed_code( filter ) ;
          if ( code == EOF )
            return error_handler( IOERROR ) ;
          these_bits = ( & uncompressed_image[ code ] ) ;
          do {
            a1pos = a0pos + these_bits->numofwhiteblack[ i ] ;
            INLINE_MIN32(a1pos, a1pos, abmax) ;
            HQASSERT((a0pos <= a1pos),
                     "group12D_decode: a0pos/a1pos out of order (E)");
            HQASSERT((a1pos <= abmax),
                     "group12D_decode: a1pos should be less than abmax (E)");
            STORENEXTEDGE( a0pos , a1pos , a0pos < a1pos ,
                           bufedge , buflimit , lbufedge ,
                           bufset , faxset ) ;
            a0pos = a1pos ;
            bufset = ~bufset ;
          } while ((--i) >= 0 ) ;
        } while ( CCITT_MORE_UNCI( code ) && a0pos < abmax ) ;
        if ( code & 1 )  /* black */
          bufset = ~bufset ;
      }
      else {
        int32 i = twodencoding ;
        theIFaxCheck2DCodes( faxstate ) = FALSE ;
        do {
          if ( twodencoding )
            EXTRACT_NEXT_CODE_WORD( filter , faxstate , bufset != faxset , code ) ;
            if ( code == EOF )
              return error_handler( IOERROR ) ;
          a1pos = a0pos + code ;
          if ( ! IsTerminatingCode( code )) {
            do {
              EXTRACT_NEXT_CODE_WORD( filter , faxstate , bufset != faxset , code ) ;
              if ( code == EOF )
                return error_handler( IOERROR ) ;
              a1pos += code ;
            } while ( ! IsTerminatingCode( code )) ;
          }
          INLINE_MIN32(a1pos, a1pos, abmax) ;
          HQASSERT((a0pos <= a1pos),
                   "group12D_decode: a0pos/a1pos out of order (1D)");
          HQASSERT((a1pos <= abmax),
                   "group12D_decode: a1pos should be less than abmax (1D)");
          STORENEXTEDGE( a0pos , a1pos , a0pos < a1pos ,
                         bufedge , buflimit , lbufedge ,
                         bufset , faxset ) ;
          a0pos = a1pos ;
          bufset = ~bufset ;
        } while ((--i) >= 0 ) ;
        theIFaxCheck2DCodes( faxstate ) = twodencoding ;
      }

      a0pos = a1pos ;
      a1pos = a0pos + 1 ;
    } while ( a0pos < abmax ) ;
  }

  STORELASTEDGE( bufedge , abmax , ~faxset ) ;

  /* Need to ungetc a whole character if one buffered up. */
  HQASSERT( theIFaxLastBits( faxstate ) < 16 , "Cached too many bits" ) ;
  if ( theIFaxLastBits( faxstate ) >= 8 ) {
    UnGetc(( uint8 )theIFaxLastByte( faxstate ) , theIUnderFile( filter )) ;
    theIFaxLastBits( faxstate )  -= 8 ;
    theIFaxLastByte( faxstate ) >>= 8 ;
  }

#if defined( ASSERT_BUILD )
  /* Turn off debug otherwise large images generate buckets of output */
  debug_code = FALSE;
#endif

  return TRUE ;
}

/*******************************************************************/
/* Encoding side of FACSIMILE transmissions                        */
/*******************************************************************/
/* Note this can go faster by using an edge table (like the decode)
 * for the reference line.
 */
static Bool group12D_encode( FILELIST *filter , int32 twodencoding )
{
  int32 a0pos , a1pos , abmax ;
  int32 b1pos , b2pos ;
  uint32 bufset , faxset ;
  uint32 *buf , *ref ;
  FAXSTATE *faxstate ;

  ref = NULL ;              /* init to keep compiler quiet */

  faxstate = theIFilterPrivate( filter ) ;

  abmax = theIFaxMaxWidth( faxstate ) ;

  bufset = theIFaxBlackIs1( theIFaxInfo( faxstate )) ;
  faxset = ~bufset ;

  buf = ( uint32 * )theIBuffer( filter ) ;
  buf[ -1 ] = bufset ;

  if ( twodencoding ) {
    ref = theIFaxRefLine( faxstate ) ;
    ref[ -1 ] = bufset ;
  }

  a0pos = 0 ;
  a1pos = 0 ;
  b1pos = 0 ;
  do {
    a1pos = detectnextbit1( buf , ~bufset , a1pos ) ;
    INLINE_MIN32(a1pos, a1pos, abmax) ;

    if ( twodencoding ) {
      b1pos = detectnextbit1( ref , ~bufset , b1pos ) ;
      INLINE_MIN32(b1pos, b1pos, abmax) ;
      b2pos = b1pos + 1 ;
      if ( b2pos < abmax )
        b2pos = detectnextbit1( ref , bufset , b2pos ) ;
      INLINE_MIN32(b2pos, b2pos, abmax) ;

      if ( b2pos < a1pos ) {
        if ( ! output_code( filter , PassMode ))
          return FALSE ;
        a0pos = b2pos ;
      }
      else {
        int32 d = b1pos - a1pos ;
        int32 ad ;
        INLINE_ABS32(ad, d) ;
        if ( ad <= 3 ) {
          if ( ! output_code( filter , Vertical0 + d ))
            return FALSE ;
          a0pos = a1pos ;
          bufset = ~bufset ;
        }
        else {
          int32 a2pos ;
          if ( ! output_code( filter , HorizontalMode ))
            return FALSE ;
          a2pos = a1pos + 1 ;
          if ( a2pos < abmax )
            a2pos = detectnextbit1( buf ,  bufset , a2pos ) ;
          INLINE_MIN32(a2pos, a2pos, abmax) ;
          if ( ! output_code_runlen( filter , a1pos - a0pos , bufset != faxset ))
            return FALSE ;
          if ( ! output_code_runlen( filter , a2pos - a1pos , bufset == faxset ))
            return FALSE ;
          a0pos = a2pos ;
        }
      }
    }
    else {
      if ( ! output_code_runlen( filter , a1pos - a0pos , bufset != faxset ))
        return FALSE ;
      a0pos = a1pos ;
      bufset = ~bufset ;
    }

    a1pos = a0pos + 1 ;
    b1pos = a1pos ;
  } while ( a0pos < abmax ) ;
  return TRUE ;
}

static void ccittfax_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* ccittfax encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("CCITTFaxEncode"),
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       ccittfaxFilterInit,                   /* initfile */
                       FilterCloseFile,                      /* closefile */
                       ccittfaxFilterDispose,                /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       ccittfaxEncodeBuffer ,                /* filterencode */
                       FilterDecodeError ,                   /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

static void ccittfax_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* ccittfax decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("CCITTFaxDecode"),
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       ccittfaxFilterInit,                   /* initfile */
                       FilterCloseFile,                      /* closefile */
                       ccittfaxFilterDispose,                /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       ccittfaxDecodeBuffer ,                /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}


static Bool ccitt_swstart(struct SWSTART *params)
{
  FILELIST *flptr ;
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST) * 2)) == NULL )
    return FALSE ;

  /* initialise the fax coding tables */
  HqMemZero(hashblacktable, CCITTHASHSIZE * sizeof(TERMCODE*));
  HqMemZero(hashwhitetable, CCITTHASHSIZE * sizeof(TERMCODE*));
  HqMemZero(hash2dcodetable, CCITTHASHSIZE * sizeof(TERMCODE*));
  initfaxdata();

  ccittfax_encode_filter(&flptr[0]) ;
  filter_standard_add(&flptr[0]) ;
  ccittfax_decode_filter(&flptr[1]) ;
  filter_standard_add(&flptr[1]) ;

  return TRUE ;
}

void ccitt_C_globals(struct core_init_fns *fns)
{
  init_C_globals_ccittfax() ;
  fns->swstart = ccitt_swstart ;
}

/*
 * Interface for accessing the CCITT filter directly (i.e. not via PS/PDF
 * language). This is used by the JBIG2 code for reading embedded MMR
 * encoded data.
 */

FILELIST *ccitt_open(FILELIST *source, int32 columns, int32 rows,
                       Bool endofblock)
{
  FILELIST *filter;
  FAXSTATE *state;
  FAXINFO *faxinfo;
  Bool ok = TRUE;

  filter = (FILELIST *)mm_alloc(mm_pool_temp, sizeof(FILELIST),
                                MM_ALLOC_CLASS_CCITT_FAX);
  if ( filter == NULL )
    ok = error_handler(VMERROR);

  if ( ok )
  {
    ccittfax_decode_filter(filter);
    if ( ! init_ccitt_state1(filter) )
      ok = FALSE;
  }
  if ( ok )
  {
    state = theIFilterPrivate( filter );
    faxinfo = theIFaxInfo( state );

    theIFaxColumns( faxinfo ) = columns;
    theIFaxRows( faxinfo ) = rows;
    theIFaxEndOfBlock( faxinfo ) = endofblock;
    theIFaxK( faxinfo ) = -1;
    theIFaxBlackIs1( faxinfo ) = TRUE;

    if ( ! init_ccitt_state2(filter) )
      ok = FALSE;
  }
  if ( ok )
  {
    theIUnderFile( filter ) = source;
    theIUnderFilterId( filter ) = theIFilterId(source) ;
  }
  else
  {
    if ( filter )
      mm_free(mm_pool_temp, (mm_addr_t)filter, sizeof(FILELIST));
    filter = NULL;
  }
  return filter;
}

void ccitt_close(FILELIST *filter)
{
  ccittfaxFilterDispose(filter);
  mm_free(mm_pool_temp, (mm_addr_t)filter, sizeof(FILELIST));
}


static void init_C_globals_ccittfax(void)
{
  EMPTY_STATEMENT();
}

/* Log stripped */
