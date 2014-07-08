/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:lzw.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * LZW Filter implementation
 *
 *  The compression algorithm used in this filter implementation is based
 *  on the Unix compress/uncompress program, which is, in turn, based on
 *  the LZW algorithm which is a subject of a US patent held by
 *  Unisys Corporation.
 *
 * Licencing Details:
 * "LZW licensed under U.S. Patent No. 4,558,302 and foreign counterparts."
 */



/* ----------------------------------------------------------------------------
   file:                LZW               author:              Luke Tunmer
   creation date:       23-Jul-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

#include "core.h"
#include "hqmemcpy.h"
#include "swoften.h" /* public file */
#include "swerrors.h"
#include "swctype.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemset.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "namedef_.h"

#include "fileio.h"
#include "diff.h"
#include "lzw.h"

/*
 * The optional dictionary parameter to the LZW filter was introduced
 * after the publication of the Red Book. Adobe Tech note 5115 "Supporting Data
 * Compression in PostScript Level 2 and the filter Operator". The Predictor
 * parameter will become part of the TIFF definition.
 *
 * The EarlyChange parameter is picked up from the dictionary, but nothing uses
 * this value yet. This is because I'm still unclear how this parameter affects the
 * LZW algorithm. The fact that Adobe cannot realiably decompress what they compressed
 * makes investigating this problem more difficult.
 */

#define LZWBUFFSIZE 4096
#define MAXBITS     12    /* largest number of bits in codes */
#define HASHSIZE    5003  /* 12 bits -> 4096 codes, hash is at most 80% full */
#define MAXIMUMCODE 4095  /* (1 << 12) - 1 */
#define STACK_SIZE  4000  /* number of elements in decode stack */
#define HSHIFT      4     /* shift for hashing */
#define ROGUE_ENTRY (-1)  /* no current entry (for detecting empty streams) */

#if defined( ASSERT_BUILD )
#define TABLE_SIZE  (MAXIMUMCODE)
#define TABLE_DEFAULT (32767)
#else
#define TABLE_SIZE  (256)         /* Size of code table to reset on clear code */
#define TABLE_DEFAULT (0)
#endif

#define MAXCODE( n )      (( 1 << ( n )) - 1 )
#define MAXMAXCODE        ( 1 << MAXBITS )
#define STARTBITS( n )    (( n ) + 1 )
#define CLEARCODE( n )    ( 1 << ( n ))
#define EODCODE( n )      (( 1 << ( n )) + 1 )
#define FIRSTFREE( n )    (( 1 << ( n )) + 2 )

typedef struct PredictorState {
  int32  predictor;        /* 1: normal, 2: do differencing */
  /* the following are only used for predictor == 2 */
  int32  columns;          /* number of cols in differencing image */
  int32  colors;           /* number of components in image */
  int32  bits_per_comp;    /* number of bits/comp in image */
  int32  earlychange;      /* 0 or 1 */
  /* state machine variables */
  int32  current_column;
  int32  current_color;
  uint8  * previous_value; /* allocate 2 for each color (one value plus one work space) */
} PredictorState;

typedef struct LZWENCODESTATE {
  int32  *hash_table ;
  uint16 *code_table ;
  int32  free_entry ;      /* next free code */
  int32  curr_entry ;      /* current code */
  int32  current_bits ;    /* number of bits in current code */
  int32  offset ;          /* number of bits collected in partial_byte */
  int32  partial_byte ;    /* bits waiting to be sent out */
  int32  current_maxcode ; /* maximum code for current no. of bits in code */
  int32  earlychange;      /* 0 or 1 */
  int32  unitsize ;        /* 3010: bit length of each unit - min 3, max 8 */
  int32  lowbitfirst ;     /* 3010: endianness of bits in stream */

  /* These values are trivially derived from unitsize: kept here for speed */
  int32  startbits ;
  int32  clearcode ;
  int32  eodcode ;
  int32  firstfree ;

  /* Working data for the PNG differencing of LZW */
  uint8  *dbuf ;
  int32  dbuflen ;

  PredictorState predictor_state;
  DIFF_STATE diff;                /* For PDF1.2/PS3 PNG differencing */

} LZWENCODESTATE ;


typedef struct LZWDECODESTATE {
  int32  cached_error ;    /* Has an error occured? */
  uint16 *prefix_table ;
  uint8  *suffix_table ;
  uint8  *stack ;
  int32  free_entry ;      /* next free code */
  int32  current_bits ;    /* number of bits in current code */
  int32  offset ;          /* number of bits collected in partial_byte */
  int32  partial_byte ;    /* bits waiting to be sent out */
  int32  current_maxcode ; /* maximum code for current no. of bits in code */
  int32  incode, oldcode ; /* working vars which need to be saved */
  uint8  finchar ;
  uint8  spare1 ;
  uint8  spare2 ;
  uint8  spare3 ;
  uint8  *stackp ;
  int32  earlychange;      /* 0 or 1 */
  int32  unitsize ;        /* 3010: bit length of each unit - min 3, max 8 */
  int32  lowbitfirst ;     /* 3010: endianness of bits in stream */
  int32  bigendian   ;     /* for 16 bit lzw predictor type 2 streams we need to
                                know the endianness */

  int32  eodcount;         /* Hqn Extension - max bytes decoded to return.
                              If non-zero, then the decoder will subtract bytes
                              returned until it is zero and then flag EOF.  If
                              initially zero, then decoder will look for EOD. */

  /* These values are trivially derived from unitsize: kept here for speed */
  int32  startbits ;
  int32  clearcode ;
  int32  eodcode ;
  int32  firstfree ;

  PredictorState predictor_state; /* Just for the original TIFF2 code */
  DIFF_STATE diff;                /* For PDF1.2/PS3 PNG differencing */

  int32  extendunitsize ;       /* allow unitsize to be 2, rather than limiting to >+ 3 */
} LZWDECODESTATE ;


static void lzwDecodeDifferences( PredictorState *predictor_state,
                                  uint8 *ptr,
                                  Bool bigendian,
                                  int32 count);

#if defined( ASSERT_BUILD )
/* Flags used in the debug traces (debug_lzw below): */
#define DEBUG_LZW_OUTPUT_CODE        ( 1 << 0 )
#define DEBUG_LZW_GET_CODE           ( 1 << 1 )
#define DEBUG_LZW_UNITSIZE_OVERFLOW  ( 1 << 2 )
#define DEBUG_LZW_DECODE             ( 1 << 3 )
#define DEBUG_LZW_GET_STREAM         ( 1 << 4 )
#define DEBUG_LZW_DECODE_SPECIAL     ( 1 << 5 )

static int32 debug_lzw = 0 ;

/* To help debugging large amounts of LZW data the following macros can be used
 * to get decoder debug for a selected range of LZW data blocks (i.e. between
 * the intial CC and the final EOD).  Define DEBUG_LZW_COUNTER to enable.
 * cc_dbg_start and cc_dbg_end provide LZW block numbers to turn on and off LZW
 * decode output - you can specify one large range, or multiple discontinous
 * ones.  The arrays must always end with 0 to prevent the macros indexing off
 * the end of the arrays.
 * Note: LZW block numbers start at 1, not 0!
 */
#undef DEBUG_LZW_COUNTER

#ifdef DEBUG_LZW_COUNTER

#define LZW_DECODE_VARS() \
  static uint32 cc_count = 0; \
  static int32  i_dbg = 0; \
  static uint32 cc_dbg_start[] = {0}; \
  static uint32 cc_dbg_end[]   = {0};

#define LZW_DECODE_BLOCK() \
MACRO_START \
  cc_count++; \
  if ( cc_count == cc_dbg_start[i_dbg] ) { \
    debug_lzw |= DEBUG_LZW_DECODE; \
    HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: block=%d", cc_count)); \
  } else if ( cc_count == cc_dbg_end[i_dbg] ) { \
    debug_lzw &= ~DEBUG_LZW_DECODE; \
    i_dbg++; \
  } \
MACRO_END

#endif /* DEBUG_LZW_COUNTER */
#endif /* ASSERT_BUILD */


/* Provide empty macros if not doing LZW block debugging */
#ifndef DEBUG_LZW_COUNTER
#define LZW_DECODE_VARS()   EMPTY_STATEMENT()
#define LZW_DECODE_BLOCK()  EMPTY_STATEMENT()
#endif /* !DEBUG_LZW_COUNTER */


static uint8 lmask[9] = {0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01, 0x00};
static uint8 rmask[9] = {0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};

/* These are defined as macros because the working variables used in them
 * cannot be global (more than one LZW filter can be running at the same
 * time), and we don't want to use them from the lzwstate structures
 * since it will be too slow.
 */
#define OUTPUT_CODE( code ) \
MACRO_START  \
  bits = current_bits ; \
  HQTRACE( debug_lzw & DEBUG_LZW_OUTPUT_CODE , \
           ( "A:  %d  %02x  %2d  %04x" , \
             offset , partial_byte , bits , code )) ; \
  if ( lzwstate->lowbitfirst ) { \
    if ( bits >= ( 8 - offset )) { \
      partial_byte = ( partial_byte & ( int32 )lmask[ 8 - offset ]) | \
                     ( code << offset ) ; \
      if ( Putc( partial_byte , uflptr ) == EOF ) \
        return error_handler( IOERROR ) ; \
      bits -= ( 8 - offset ) ; \
      if ( bits >= 8 ) {  /* do the middle bits into a separate byte */ \
        partial_byte = code >> ( 8 - offset ) ; \
        if ( Putc( partial_byte , uflptr ) == EOF ) \
          return error_handler( IOERROR ) ; \
        bits -= 8 ; \
      } \
      /* last bits */ \
      if ( bits ) \
        partial_byte = code >> ( current_bits - bits ) ; \
    } \
    else { \
      partial_byte = ( partial_byte & ( int32 )lmask[ 8 - offset ]) | \
                     ( code << offset ) ; \
      bits = 0 ; \
    } \
  } \
  else { \
    if ( bits >= ( 8 - offset )) { \
      partial_byte = ( partial_byte & ( int32 )rmask[ offset ]) | \
                      (( code >> ( bits + offset - 8 )) & \
                       ( int32 )lmask[ offset ]) ; \
      if ( Putc( partial_byte , uflptr ) == EOF ) \
        return error_handler( IOERROR ) ; \
      bits -= ( 8 - offset ) ; \
      if ( bits >= 8 ) {  /* do the middle bits into a separate byte */ \
        partial_byte = code >> ( bits - 8 ) ; \
        if ( Putc( partial_byte , uflptr ) == EOF ) \
          return error_handler( IOERROR ) ; \
        bits -= 8 ; \
      } \
      /* last bits */ \
      if ( bits ) \
        partial_byte = code << ( 8 - bits ) ; \
    } \
    else { \
      partial_byte = ( partial_byte & ( int32 )rmask[ offset ]) | \
                      ( code << -( bits + offset - 8 )) ; \
      bits = 0 ; \
    } \
  } \
  offset = ( offset + current_bits ) & 0x7 ; \
  HQTRACE( debug_lzw & DEBUG_LZW_OUTPUT_CODE , \
           ( "B:  %d  %02x  %2d  %04x\n" , \
             offset , partial_byte , bits , code )) ; \
MACRO_END

#define GET_CODE( code ) \
MACRO_START  \
  bits = current_bits ; \
  HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
           ( "0:  %d  %02x  %2d  %04x\n" , \
             offset , partial_byte , bits , code )) ; \
  if ( offset == 0 ) { \
    if (( partial_byte = Getc( uflptr )) == EOF ) { \
      HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0, \
              ("lzwDecodeBuffer: unexpected EOF (0)")); \
      cached_error = IOERROR ; \
      break ; \
    } \
    HQTRACE(debug_lzw & DEBUG_LZW_GET_STREAM, ("%02x", partial_byte)); \
  } \
  if ( lzwstate->lowbitfirst ) { \
    if ( bits > ( 8 - offset )) { \
      code = partial_byte >> offset ; \
      bits -= ( 8 - offset ) ; \
    } else { \
      code = ( partial_byte & ( int32 )lmask[ 8 - bits - offset ]) >> offset ; \
      bits = 0 ; \
    } \
    HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
             ( "1:  %d  %02x  %2d  %04x" , \
               offset , partial_byte , bits , code )) ; \
    if ( bits >= 8 ) { /* a middle byte must be used */ \
      if (( partial_byte = Getc( uflptr )) == EOF ) { \
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0, \
                ("lzwDecodeBuffer: unexpected EOF (1)")); \
        cached_error = IOERROR ; \
        break ; \
      } \
      HQTRACE(debug_lzw & DEBUG_LZW_GET_STREAM, ("%02x", partial_byte)); \
      code |= partial_byte << (8 - offset); \
      bits -= 8; \
    } \
    HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
             ( "2:  %d  %02x  %2d  %04x" , \
               offset , partial_byte , bits , code )) ; \
    if ( bits ) { \
      if (( partial_byte = Getc( uflptr )) == EOF ) { \
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0, \
                ("lzwDecodeBuffer: unexpected EOF (2)")); \
        cached_error = IOERROR ; \
        break ; \
      } \
      HQTRACE(debug_lzw & DEBUG_LZW_GET_STREAM, ("%02x", partial_byte)); \
      code |= (partial_byte&(int32)lmask[8 - bits]) << (current_bits - bits); \
    } \
  } else { /* Hight bit first */ \
    if ( bits > (8 - offset) ) { \
      code = (partial_byte&(int32)lmask[offset]) << (bits - 8 + offset); \
      bits -= (8 - offset); \
    } else { \
      code = (partial_byte&(int32)lmask[offset]) >> -(bits - 8 + offset); \
      bits = 0 ; \
    } \
    HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
             ( "1:  %d  %02x  %2d  %04x" , \
               offset , partial_byte , bits , code )) ; \
    if ( bits >= 8 ) { /* a middle byte must be used */ \
      if (( partial_byte = Getc( uflptr )) == EOF ) { \
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0, \
                ("lzwDecodeBuffer: unexpected EOF (3)")); \
        cached_error = IOERROR ; \
        break ; \
      } \
      HQTRACE(debug_lzw & DEBUG_LZW_GET_STREAM, ("%02x", partial_byte)); \
      code |= partial_byte << (bits - 8); \
      bits -= 8; \
    } \
    HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
             ( "2:  %d  %02x  %2d  %04x" , \
               offset , partial_byte , bits , code )) ; \
    if ( bits != 0 ) { \
      if (( partial_byte = Getc( uflptr )) == EOF ) { \
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0, \
                ("lzwDecodeBuffer: unexpected EOF (4)")); \
        cached_error = IOERROR ; \
        break ; \
      } \
      HQTRACE(debug_lzw & DEBUG_LZW_GET_STREAM, ("%02x", partial_byte)); \
      code |=  partial_byte >> (8 - bits); \
    } \
  } \
  offset = (offset + current_bits)&0x7; \
  HQTRACE( debug_lzw & DEBUG_LZW_GET_CODE , \
           ( "3:  %d  %02x  %2d  %04x\n" , \
             offset , partial_byte , bits , code )) ; \
MACRO_END

static Bool lzwFilterInit( FILELIST *filter,
                           OBJECT *args ,
                           STACK *stack )
{
  register LZWDECODESTATE *lzwdecstate ;
  register LZWENCODESTATE *lzwencstate ;
  register uint16 *prefix_table ;
  register uint8 *suffix_table ;
  register int32 i ;
  PredictorState *p;
  DIFF_STATE *diff = NULL ;
  int32 pop_args = 0 ;

  enum { params_Predictor, params_Columns, params_Colors,
         params_BitsPerComponent, params_EarlyChange, params_UnitSize,
         params_LowBitFirst, params_UnitLength, params_ExtendUnitLength,
         params_EODCount, params_BigEndian, params_dummy } ;
  NAMETYPEMATCH thematch[params_dummy + 1] = {
    { NAME_Predictor | OOPTIONAL , 1, { OINTEGER }},        /* 0 */
    { NAME_Columns | OOPTIONAL , 1, { OINTEGER }},          /* 1 */
    { NAME_Colors | OOPTIONAL , 1, { OINTEGER }},           /* 2 */
    { NAME_BitsPerComponent | OOPTIONAL , 1, { OINTEGER }}, /* 3 */
    { NAME_EarlyChange | OOPTIONAL , 1, { OINTEGER }},      /* 4 */
    { NAME_UnitSize | OOPTIONAL , 1, { OINTEGER }},         /* 5 */
    { NAME_LowBitFirst | OOPTIONAL , 1, { OBOOLEAN }},      /* 6 */
    { NAME_UnitLength | OOPTIONAL , 1, { OINTEGER }},       /* 7 */
    { NAME_ExtendUnitLength | OOPTIONAL , 1, { OBOOLEAN }}, /* 8 */
    { NAME_EODCount | OOPTIONAL , 1, { OINTEGER }},         /* 9 */
    { NAME_BigEndian | OOPTIONAL , 1, { OBOOLEAN }},        /* 10 */
    DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args != NULL && oType(*args) == ODICTIONARY ) {
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

  theIBuffer( filter ) = ( uint8 * )mm_alloc( mm_pool_temp ,
                                              LZWBUFFSIZE + 1 ,
                                              MM_ALLOC_CLASS_LZW_BUFFER ) ;

  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++ ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = LZWBUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;
  theIFilterPrivate( filter ) = NULL ;

  if ( isIInputFile( filter )) { /* decoding LZW filter */

    lzwdecstate = ( LZWDECODESTATE * )mm_alloc( mm_pool_temp ,
                                                sizeof( LZWDECODESTATE ) ,
                                                MM_ALLOC_CLASS_LZW_STATE ) ;
    if ( lzwdecstate == NULL )
      return error_handler( VMERROR ) ;

    theIFilterPrivate( filter ) = lzwdecstate ;

    /* Iniitialise important things to prevent blowup if an allocation fails */
    lzwdecstate->cached_error = NOT_AN_ERROR ;
    lzwdecstate->prefix_table = NULL ;
    lzwdecstate->suffix_table = NULL ;
    lzwdecstate->stack = NULL ;
    lzwdecstate->predictor_state.previous_value = NULL ;
    lzwdecstate->predictor_state.predictor = 1 ;

    lzwdecstate->prefix_table = prefix_table =
        ( uint16 * )mm_alloc( mm_pool_temp ,
                              HASHSIZE * sizeof( uint16 ) ,
                              MM_ALLOC_CLASS_LZW_PREFIX ) ;
    if ( prefix_table == NULL )
      return error_handler( VMERROR ) ;

    lzwdecstate->suffix_table = suffix_table =
        ( uint8 * )mm_alloc( mm_pool_temp ,
                             ( MAXIMUMCODE + 1) * sizeof( uint8 ) ,
                             MM_ALLOC_CLASS_LZW_SUFFIX ) ;
    if ( suffix_table == NULL )
      return error_handler( VMERROR ) ;

    lzwdecstate->stack =
        ( uint8 * )mm_alloc( mm_pool_temp ,
                             STACK_SIZE * sizeof( uint8 ) ,
                             MM_ALLOC_CLASS_LZW_STACK ) ;
    if ( lzwdecstate->stack == NULL )
      return error_handler( VMERROR ) ;

    HqMemSet16(prefix_table, TABLE_DEFAULT, TABLE_SIZE);
    for ( i = 255 ; i >= 0 ; i-- )  /* init prefix and suffix tables */
      suffix_table[i] = ( uint8 )i ;

    lzwdecstate->offset = 0 ;
    lzwdecstate->partial_byte = 0 ;
    lzwdecstate->stackp = lzwdecstate->stack + STACK_SIZE ;
    lzwdecstate->earlychange = 0;
    lzwdecstate->unitsize = 8 ;
    lzwdecstate->lowbitfirst = FALSE ;
    lzwdecstate->extendunitsize = FALSE ;
    p = &lzwdecstate->predictor_state;
    diff = &lzwdecstate->diff ;
    lzwdecstate->eodcount = 0;

    /* Unpack values from the dictionary (if any) */

    if ( args != NULL ) {
      OBJECT *o1 ;

      if (( o1 = thematch[params_EarlyChange].result ) != NULL )
        lzwdecstate->earlychange = 1 - oInteger(*o1) ;

      /* This is a Hqn extension to handle TIFF LZW strips with the right amount
       * of data encoded but no EOD */
      if (( o1 = thematch[params_EODCount].result ) != NULL )
        lzwdecstate->eodcount = oInteger(*o1);
      if (( o1 = thematch[params_BigEndian].result ) != NULL )
        lzwdecstate->bigendian = oBool(*o1);

      HQASSERT( ! thematch[params_UnitSize].result ,
                "UnitSize seen in LZW params - this has been superceded by UnitLength." ) ;

      if (( o1 = thematch[params_UnitLength].result ) != NULL )
        lzwdecstate->unitsize = oInteger(*o1) ;

      if (( o1 = thematch[params_ExtendUnitLength].result ) != NULL )
        lzwdecstate->extendunitsize = oInteger(*o1) ;

      if (( o1 = thematch[params_LowBitFirst].result ) != NULL )
        lzwdecstate->lowbitfirst = oBool(*o1) ;
    }

    /* Check validity of parameters */

    if ( (( lzwdecstate->unitsize < 2 || ( lzwdecstate->unitsize < 3 && !(lzwdecstate->extendunitsize) ) )
          || lzwdecstate->unitsize > 8 ) ||
         ( lzwdecstate->earlychange != 0 && lzwdecstate->earlychange != 1 ) ||
         ( lzwdecstate->eodcount < 0 ))
      return error_handler( RANGECHECK ) ;

    lzwdecstate->startbits = STARTBITS( lzwdecstate->unitsize ) ;
    lzwdecstate->clearcode = CLEARCODE( lzwdecstate->unitsize ) ;
    lzwdecstate->eodcode = EODCODE( lzwdecstate->unitsize ) ;
    lzwdecstate->firstfree = FIRSTFREE( lzwdecstate->unitsize ) ;

    lzwdecstate->free_entry = lzwdecstate->firstfree ;
    lzwdecstate->current_bits = lzwdecstate->startbits ;
    lzwdecstate->current_maxcode = MAXCODE( lzwdecstate->current_bits ) ;

  } else { /* encoding LZW filter */

    lzwencstate = ( LZWENCODESTATE * )mm_alloc( mm_pool_temp ,
                                                sizeof( LZWENCODESTATE ) ,
                                                MM_ALLOC_CLASS_LZW_STATE ) ;
    if ( lzwencstate == NULL )
      return error_handler( VMERROR ) ;

    theIFilterPrivate( filter ) = lzwencstate ;

    /* Iniitialise important things to prevent blowup if an allocation fails */
    lzwencstate->hash_table = NULL ;
    lzwencstate->code_table = NULL ;
    lzwencstate->dbuf = NULL ;
    lzwencstate->predictor_state.previous_value = NULL ;
    lzwencstate->predictor_state.predictor = 1; ;

    lzwencstate->hash_table =
        ( int32 * )mm_alloc( mm_pool_temp ,
                             HASHSIZE * sizeof( int32 ) ,
                             MM_ALLOC_CLASS_LZW_HASH ) ;
    if ( lzwencstate->hash_table == NULL )
      return error_handler( VMERROR ) ;

    lzwencstate->code_table = ( uint16 * )mm_alloc( mm_pool_temp ,
                                                    HASHSIZE * sizeof( int16 ) ,
                                                    MM_ALLOC_CLASS_LZW_CODES ) ;
    if ( lzwencstate->code_table == NULL )
      return error_handler( VMERROR ) ;

    {
      int32 *htable = lzwencstate->hash_table ;
      HqMemSet32((uint32 *)htable, (uint32)(-1), HASHSIZE);
    }

    lzwencstate->curr_entry = ROGUE_ENTRY ;
    lzwencstate->offset = 0 ;
    lzwencstate->partial_byte = 0 ;
    lzwencstate->earlychange = 0;
    lzwencstate->unitsize = 8 ;
    lzwencstate->lowbitfirst = FALSE ;

    lzwencstate->dbuflen = 1024 + 1; /* same as for flate encode. */
    lzwencstate->dbuf = ( uint8 * )mm_alloc( mm_pool_temp ,
                                       lzwencstate->dbuflen ,
                                       MM_ALLOC_CLASS_LZW_STATE ) ;
    if ( lzwencstate->dbuf == NULL )
      return error_handler( VMERROR ) ;

    p = &lzwencstate->predictor_state;
    diff = &lzwencstate->diff ;

    /* Unpack values from the dictionary (if any) */

    if ( args != NULL ) {
      OBJECT *o1 ;

      if (( o1 = thematch[params_EarlyChange].result ) != NULL )
        lzwencstate->earlychange = 1 - oInteger(*o1) ;

      HQASSERT( ! thematch[params_UnitSize].result ,
                "UnitSize seen in LZW params - this has been superceded by UnitLength." ) ;

      if (( o1 = thematch[params_UnitLength].result ) != NULL )
        lzwencstate->unitsize = oInteger(*o1) ;

      if (( o1 = thematch[params_LowBitFirst].result ) != NULL )
        lzwencstate->lowbitfirst = oBool(*o1) ;
    }

    /* Check validity of parameters */

    if (( lzwencstate->unitsize < 3 || lzwencstate->unitsize > 8 ) ||
        ( lzwencstate->earlychange != 0 && lzwencstate->earlychange != 1 ))
      return error_handler( RANGECHECK ) ;

    HQASSERT( lzwencstate->unitsize == 8 ,
              "UnitLength values other than 8 may not give correct results in "
              "LZWEncode: Adobe didn't define how the packing should work!" ) ;

    lzwencstate->startbits = STARTBITS( lzwencstate->unitsize ) ;
    lzwencstate->clearcode = CLEARCODE( lzwencstate->unitsize ) ;
    lzwencstate->eodcode = EODCODE( lzwencstate->unitsize ) ;
    lzwencstate->firstfree = FIRSTFREE( lzwencstate->unitsize ) ;

    lzwencstate->free_entry = lzwencstate->firstfree ;
    lzwencstate->current_bits = lzwencstate->startbits ;
    lzwencstate->current_maxcode = MAXCODE( lzwencstate->current_bits ) ;
  }

  /* set predictor defaults */
  p->predictor = 1;
  p->columns = 1;
  p->colors = 1;
  p->bits_per_comp = 8;
  p->previous_value = NULL;

  if (args != NULL) {
    OBJECT *theo ;

    /* Extract names from the dictionary. */
    if (( theo = thematch[params_Predictor].result ) != NULL )
      p->predictor = oInteger(*theo) ;

    if (( theo = thematch[params_Columns].result ) != NULL )
      p->columns = oInteger(*theo) ;

    if (( theo = thematch[params_Colors].result ) != NULL )
      p->colors = oInteger(*theo) ;

    if (( theo = thematch[params_BitsPerComponent].result ) != NULL )
      p->bits_per_comp = oInteger(*theo) ;

    /* Check their values etc. */
    if ( p->predictor != 1 ) {
      if ( ! diffInit( diff , args ))
        return FALSE ;
    }

    if (( p->columns < 1 ) ||
        ( p->colors <= 0 ) ||
        ( p->bits_per_comp != 1 && p->bits_per_comp != 2 &&
          p->bits_per_comp != 4 && p->bits_per_comp != 8  &&
          p->bits_per_comp != 16 ))
      return error_handler( RANGECHECK ) ;
  }

  if (p->predictor == 2) {
    if (p->columns == 1) {
      /* only one column is the same thing as doing no differencing */
      p->predictor = 1;
    } else {

      /* initialize predictor state machine */
      p->current_column = 0;
      p->current_color = 0;

      p->previous_value = (uint8 *) mm_alloc( mm_pool_temp ,
                   p->colors * sizeof( uint8 ) * 2,
                   MM_ALLOC_CLASS_LZW_STATE ) ;

      if ( p->previous_value == NULL )
        return error_handler( VMERROR ) ;

      HqMemZero(p->previous_value, p->colors * 2);
    }
  }

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void lzwFilterDispose( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in lzwFilterDispose." ) ;

  if ( theIBuffer( filter ) != NULL ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             LZWBUFFSIZE + 1 ) ;
    theIBuffer( filter ) = NULL ;

    if ( isIInputFile( filter ) ) {
      register LZWDECODESTATE *lzwdecodestate ;

      lzwdecodestate = ( LZWDECODESTATE * )theIFilterPrivate( filter ) ;
      if ( lzwdecodestate != NULL ) {
        if ( lzwdecodestate->predictor_state.predictor != 1 )
          diffClose( &lzwdecodestate->diff ) ;
        if ( lzwdecodestate->prefix_table != NULL )
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwdecodestate->prefix_table ,
                   HASHSIZE * sizeof( uint16 )) ;
        if ( lzwdecodestate->suffix_table != NULL )
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwdecodestate->suffix_table ,
                   ( MAXIMUMCODE + 1 ) * sizeof( uint8 )) ;
        if ( lzwdecodestate->stack != NULL )
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwdecodestate->stack ,
                   STACK_SIZE * sizeof( uint8 )) ;

        if (lzwdecodestate->predictor_state.previous_value != NULL) {
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwdecodestate->predictor_state.previous_value ,
                   lzwdecodestate->predictor_state.colors * sizeof( uint8 ) * 2 ) ;
        }

        mm_free( mm_pool_temp ,
                 ( mm_addr_t )lzwdecodestate ,
                 sizeof( LZWDECODESTATE )) ;
        theIFilterPrivate( filter ) = NULL ;
      }
    } else {
      register LZWENCODESTATE *lzwencodestate ;

      lzwencodestate = ( LZWENCODESTATE * )theIFilterPrivate( filter ) ;
      if ( lzwencodestate != NULL ) {
        if ( lzwencodestate->predictor_state.predictor != 1 )
          diffClose( &lzwencodestate->diff ) ;
        if ( lzwencodestate->hash_table != NULL )
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwencodestate->hash_table ,
                   HASHSIZE * sizeof( int32 )) ;
        if ( lzwencodestate->code_table != NULL )
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwencodestate->code_table ,
                   HASHSIZE * sizeof( int16 )) ;

        if (lzwencodestate->predictor_state.previous_value != NULL) {
          mm_free( mm_pool_temp ,
                   ( mm_addr_t )lzwencodestate->predictor_state.previous_value ,
                   lzwencodestate->predictor_state.colors * sizeof( uint8 ) * 2) ;
        }

        if ( lzwencodestate->dbuf != NULL ) {
          mm_free( mm_pool_temp , ( mm_addr_t )lzwencodestate->dbuf ,
                  lzwencodestate->dbuflen ) ;
        }

        mm_free( mm_pool_temp ,
                 ( mm_addr_t )lzwencodestate ,
                 sizeof( LZWENCODESTATE )) ;

        theIFilterPrivate( filter ) = NULL ;
      }
    }
  }
}

/* ----------------------------------------------------------------------------
   function:            lzwDecodeBuffer   author:              Luke Tunmer
   creation date:       26-Jul-1991       last modification:   ##-###-####
   arguments:   filter , ret_bytes
   description:

   This routine gets characters from the filter's underlying file, decompresses
   them into the filter's buffer. It returns the number of decoded characters
   through the ret_bytes argument. This number is negative if the EOD was
   found while filling the buffer.
   After the closefile/lasterror changes under 21443, the filter is now
   responsible for generating its own errors.
---------------------------------------------------------------------------- */
static Bool lzwDecodeBuffer(
  FILELIST* filter,
  int32*    ret_bytes)
{
  register FILELIST *uflptr ;
  LZWDECODESTATE    *lzwstate ;
  register uint16   *prefix_table ;
  register uint8    *suffix_table ;
  register uint8    *stackp , *stacktop , *stackbot ;
  register int32    code = 0, oldcode , incode , free_entry;
  register int32    offset , bits ;
  register int32    partial_byte ;
  uint8             *ptr ;
  int32             current_bits ;
  int32             current_maxcode ;
  int32             count ;
  int32             n ;
  int32             eodcount ;
  uint8             finchar ;
  int32             cached_error ;

  /* Following must appear before any statements as it declares debug vars */
  LZW_DECODE_VARS();

  HQASSERT( filter , "filter NULL in lzwDecodeBuffer." ) ;
  HQASSERT( ret_bytes , "ret_bytes NULL in lzwDecodeBuffer." ) ;

  /* unpack lzwstate into register variables */
  lzwstate = ( LZWDECODESTATE * )theIFilterPrivate( filter ) ;
  HQASSERT( lzwstate , "lzwstate NULL in lzwDecodeBuffer." ) ;

  /* Unpack as early as possible so rest of code only references
     cached_error. */
  cached_error = lzwstate->cached_error ;
  if (cached_error != NOT_AN_ERROR)
    return error_handler( cached_error ) ;

  prefix_table = lzwstate->prefix_table ;
  suffix_table = lzwstate->suffix_table ;
  stackbot = lzwstate->stack ;
  stacktop = stackbot + STACK_SIZE ;
  free_entry = lzwstate->free_entry ;
  current_bits = lzwstate->current_bits ;
  offset = lzwstate->offset ;
  partial_byte = lzwstate->partial_byte ;
  current_maxcode = lzwstate->current_maxcode ;
  incode = lzwstate->incode ;
  oldcode = lzwstate->oldcode ;
  finchar = lzwstate->finchar ;
  stackp = lzwstate->stackp ;
  eodcount = lzwstate->eodcount;

  HQASSERT((eodcount >= 0),
           "-ve eodcount in lzwDecodeBuffer");

  current_maxcode += lzwstate->earlychange ;

  uflptr = theIUnderFile( filter ) ;
  ptr = (uint8 *) theIBuffer( filter ) ;
  count = 0 ;

  HQASSERT( uflptr , "uflptr NULL in lzwDecodeBuffer." ) ;
  HQASSERT( ptr , "ptr NULL in lzwDecodeBuffer." ) ;

  do {
    /* See if we have no more chars on the stack still to unpack */
    if ( stackp == stacktop ) {
      /* Get a code from the input stream */
      GET_CODE(code);
      if (cached_error != NOT_AN_ERROR)
        break ;
      HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: code=%d", code));
      HQASSERT((code <= MAXIMUMCODE), "lzwDecodeBuffer: code > 4095");

      /* While we can handle a lack of a CC when required, the output cannot
       * be guaranteed, so this gives us just some information that there may
       * be a problem with the file. In an ideal world this would actually
       * fire off a filter error as it means the data is wrong.
       */
      HQTRACE(((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0) &&
              (free_entry > MAXIMUMCODE) && (code != lzwstate->clearcode),
              ("lzwDecodeBuffer: code after 4095 is not a CC!"));

      if ( code == lzwstate->eodcode ) {
        /* End of data code - flag no more and return */
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: EOD"));
        count = -count;
        break;
      }

      if ( code == lzwstate->clearcode ) {
        /* Clear table code - reset the table and unit size etc */
        LZW_DECODE_BLOCK();
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: CC"));
        HqMemSet16(prefix_table, TABLE_DEFAULT, TABLE_SIZE);
        free_entry = lzwstate->firstfree;
        current_bits = lzwstate->startbits;
        current_maxcode = MAXCODE(current_bits);
        current_maxcode += lzwstate->earlychange;

        /* Reprime the decoder by reading the first non-clearcode code now */
        do {
          GET_CODE(code);
          if (cached_error != NOT_AN_ERROR)
            break ;
#ifdef DEBUG_LZW_COUNTER
          if ( code == lzwstate->clearcode ) {
            LZW_DECODE_BLOCK();
          }
#endif /* DEBUG_LZW_COUNTER */
          HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: code=%d", code));
          HQASSERT((code <= MAXIMUMCODE), "lzwDecodeBuffer: code > 4095");
        } while ( code == lzwstate->clearcode ) ;

        /* Break from outer while loop as well. */
        if (cached_error != NOT_AN_ERROR)
          break ;

        if ( code == lzwstate->eodcode ) {
          /* End of data - flag no more and return */
          HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: EOD"));
          count = -count;
          break;
        }

        /* Add decoded byte to buffer and continue by reading next code
         * Note: nothing added to the table! */
        finchar = (uint8)code;
        stackp--;
        if ( stackp == stackbot ) {
          HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0,
                  ("lzwDecodeBuffer: buffer full (0)"));
          return(error_handler(IOERROR));
        }
        *stackp = finchar;
        oldcode = code;
        continue;
      }

      if ( code > (free_entry + 1) ) {
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0,
                ("lzwDecodeBuffer: out of sequence code - no EOD?"));
        return(error_handler(IOERROR));
      }

      HQTRACE((((debug_lzw & DEBUG_LZW_DECODE) != 0) && (code >= lzwstate->firstfree)),
              ("LZW: code not root"));

      /* Remember this code - code is reused */
      incode = code;

      if ( code >= free_entry ) {
        stackp--;
        if ( stackp == stackbot ) {
          HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0,
                  ("lzwDecodeBuffer: buffer full (1)"));
          return(error_handler(IOERROR));
        }
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: string code=%d", code));
        *stackp = finchar;
        code = oldcode;
      }

      while ( code >= lzwstate->firstfree ) {
        /* Code is for string - decode full string */
        HQASSERT((code <= MAXIMUMCODE), "lzwDecodeBuffer: code > 4095");
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: string code=%d", code));
        stackp--;
        if ( stackp == stackbot ) {
          HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0,
                  ("lzwDecodeBuffer: buffer full (2)"));
          return(error_handler(IOERROR));
        }
        HQASSERT((prefix_table[code] != TABLE_DEFAULT),
                 "lzwDecodeBuffer: unwinding undefined code");
        *stackp = suffix_table[code];
        code = prefix_table[code];
      }
      stackp-- ;
      if ( stackp == stackbot ) {
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0,
                ("lzwDecodeBuffer: buffer full (3)"));
        return(error_handler(IOERROR));
      }
      HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: string code=%d", code));
      *stackp = finchar = suffix_table[code];

      /* Adding to the table needs to be done after the string unwinding in
       * order to get the initial character of the string - finchar
       */
      HQTRACE(((debug_lzw & DEBUG_LZW_DECODE_SPECIAL) != 0) &&
              (free_entry > MAXIMUMCODE),
              ("lzwDecodeBuffer: potential code table overflow"));
      if ( free_entry <= MAXIMUMCODE ) {
        /* Prevent codes beyond 4095 overflowing the string tables.  See comment
         * below.  This may mean incorrect output but I think if they emit more
         * than 4095 codes all bets are off anyway!
         */
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0,
                ("LZW: adding entry=%d, oldcode=%d, finchar=%d",
                 free_entry, oldcode, finchar));
        prefix_table[free_entry] = (uint16)oldcode;
        suffix_table[free_entry] = finchar;
        free_entry++;
      }

      if ( free_entry >= current_maxcode ) {
        /* Used up table for current unit size - bump size 1 bit */
        current_bits++;
        if ( current_bits < MAXBITS ) {
          current_maxcode = MAXCODE(current_bits);
          current_maxcode += lzwstate->earlychange;

        } else {
          /* Some software seems to generate LZW data which outputs one more code
           * word after code 4095 before emitting a table CC.  AFAIK this is wrong
           * but there ain't any formal specification to quote chapter and verse.
           * Fixing the number of bits should ensure the 12 bit CC or EOD is
           * finally picked up.
           */
          current_bits = MAXBITS;
          current_maxcode = MAXMAXCODE;
        }
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0,
                ("LZW: extend - bits=%d maxcode=%d", current_bits, current_maxcode));
      }

      /* We need the code originally read in for the next time around */
      oldcode = incode;
    }

    /* Unpack the stack into the buffer */
    n = (int32)(stacktop - stackp);
    if ( (count + n) > theIBufferSize(filter) ) {
      n = theIBufferSize(filter) - count;
    }
    switch ( n ) {
    default:
      HqMemCpy(ptr, stackp, n);
      break;
    case 8: ptr[7] = stackp[7];
    case 7: ptr[6] = stackp[6];
    case 6: ptr[5] = stackp[5];
    case 5: ptr[4] = stackp[4];
    case 4: ptr[3] = stackp[3];
    case 3: ptr[2] = stackp[2];
    case 2: ptr[1] = stackp[1];
    case 1: ptr[0] = stackp[0];
    }
    ptr += n;
    stackp += n;
    count += n;

    /* If counting bytes returned reduce count left to be returned.  If count
     * hits zero or less then treat as EOF */
    if ( eodcount ) {
      eodcount -= n;
      if ( eodcount <= 0 ) {
        /* End of wanted data - adjust for any overread, flag no more, and return */
        HQTRACE((debug_lzw & DEBUG_LZW_DECODE) != 0, ("LZW: EOD (EODCount)"));
        count = -(count + eodcount);
        break;
      }
    }

  } while ( count != theIBufferSize(filter) );

  /* repack local vars into the lzwstate */
  current_maxcode -= lzwstate->earlychange ;

  lzwstate->cached_error = cached_error ;
  lzwstate->free_entry = free_entry ;
  lzwstate->current_bits = current_bits ;
  lzwstate->offset = offset ;
  lzwstate->partial_byte = partial_byte ;
  lzwstate->current_maxcode = current_maxcode ;
  lzwstate->incode = incode ;
  lzwstate->oldcode = oldcode ;
  lzwstate->finchar = finchar ;
  lzwstate->stackp = stackp ;
  lzwstate->eodcount = eodcount;

  if ( lzwstate->predictor_state.predictor == 2 )
    lzwDecodeDifferences( & lzwstate->predictor_state ,
                          theIBuffer( filter ) ,
                          lzwstate->bigendian,
                          count ) ;
  else if ( lzwstate->predictor_state.predictor > 2 ) {
    int32 bytes = ( count > 0 ) ? count : -count ;

    if ( ! diffDecode( & lzwstate->diff ,
                       theIBuffer( filter ) ,
                       bytes ,
                       theIBuffer( filter ) ,
                       & bytes ))
      return FALSE ;

    count = ( count > 0 ) ? bytes : -bytes ;
  }

  *ret_bytes = count ;
  return TRUE ;
}




/* ----------------------------------------------------------------------------
   function:            lzwEncodeBuffer   author:              Luke Tunmer
   creation date:       26-Jul-1991       last modification:   ##-###-####
   arguments:      filter
   description:

   This routine takes a full or partially full buffer in the filter,
   compresses the data, and puts the resulting characters into the filter's
   underlying file.
   After the closefile/lasterror changes under 21443, the filter is now
   responsible for generating its own errors.
---------------------------------------------------------------------------- */
static Bool lzwEncodeBuffer( FILELIST *filter )
{
  FILELIST         *uflptr ;
  LZWENCODESTATE   *lzwstate ;
  register int32   *htable ;
  register uint16  *ctable ;
  register int32   entry , fcode , free_entry;
  register int32   offset , bits ;
  register int32   partial_byte ;
  int32            c , hash , d;
  int32            current_maxcode ;
  int32            current_bits ;
  int32            count ;
  int32            maxval ;
  int32            append_eod = FALSE ;
  uint8                    *src_ptr ;
  uint8                    *ptr ;
  int32            src_count ;

  HQASSERT( filter , "filter NULL in lzwEncodeBuffer." ) ;

  if (( filter->count <= 0 ) && ! isIClosing( filter )) {
    /* Nothing to do. */
    return TRUE ;
  }

  /* Unpack lzwstate into register variables. */

  lzwstate = ( LZWENCODESTATE * )theIFilterPrivate( filter ) ;

  HQASSERT( lzwstate , "lzwstate NULL in lzwEncodeBuffer." ) ;

  htable = lzwstate->hash_table ;
  ctable = lzwstate->code_table ;
  free_entry = lzwstate->free_entry ;
  entry = lzwstate->curr_entry ;
  current_bits = lzwstate->current_bits ;
  offset = lzwstate->offset ;
  partial_byte = lzwstate->partial_byte ;
  current_maxcode = lzwstate->current_maxcode ;
  maxval = MAXCODE( lzwstate->unitsize ) ;

  current_maxcode += lzwstate->earlychange ;

  count = theICount( filter ) ;
  ptr = theIBuffer( filter ) ;
  uflptr = theIUnderFile( filter ) ;

  HQASSERT( uflptr , "uflptr NULL in lzwEncodeBuffer." ) ;

  if ( ! isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr ))
    return error_handler( IOERROR ) ;

  HQASSERT( ptr , "ptr NULL in lzwEncodeBuffer." ) ;

  /* data to be consummed might be processed in chunks.
   * src_count and src_ptr represent the amount of data
   * that is to be processed by the differencer.
   */
  src_count = theICount( filter ) ;
  src_ptr = theIBuffer ( filter ) ;

  /* represents a chunk to LZW encode */
  count = theICount( filter ) ;
  ptr = theIBuffer( filter ) ;

  /* set up initial conditions depending on whether
   * differencing is required.
   */
  if ( lzwstate->predictor_state.predictor >= 2 ) {

    /* differencing required. */

    int32 srcDataConsumed ;

    srcDataConsumed = src_count ;
    count = lzwstate->dbuflen ;
    ptr = lzwstate->dbuf;

    if ( ! diffEncode ( &lzwstate->diff, src_ptr,
                        &srcDataConsumed, ptr, &count ) ) {
      return error_handler( IOERROR ) ;
    }

    /* Update where we are in the processing of the filter's buffer. */
    src_ptr += srcDataConsumed ;
    src_count -= srcDataConsumed ;

  }
  else {
    /* if not differencing, should process all of filter's data in one go. */
    src_count = 0;
  }

  /* At this point, "ptr" references "count" bytes of data to lzw encode.
   * This may or may not be all the data in the filter's own buffer.
   */

  /* Is this the first char? */

  if (( theIFilterState( filter ) == FILTER_INIT_STATE ) && ( count > 0 )) {
    entry = *( ptr++ ) ;
    count-- ;
    HQTRACE( ( entry > maxval ) &&
             (( debug_lzw & DEBUG_LZW_UNITSIZE_OVERFLOW ) != 0 ) ,
             ( "LZW UnitSize of %d means %u gets truncated to %u" ,
               lzwstate->unitsize , entry , entry & maxval )) ;
    entry &= maxval ;

    /* Output the CLEAR_TABLE byte. */

    OUTPUT_CODE( lzwstate->clearcode ) ;
    theIFilterState( filter ) = FILTER_EMPTY_STATE ;
  }

  while (( count > 0 ) || ( isIClosing( filter ))) {
    if ( count > 0 ) {
      c = *(ptr++) ;
      count-- ;

      /* use of PNG differencing might require we do
       * the differencing in chunks. src_count > 0
       * means more data to difference.
       */
      if ( count == 0  && src_count > 0 )
      {
        int32 srcDataConsumed ;

        /* Refill dbuf, and set count and ptr variables ther
         * that control the output, so this output loop continues
         * to iterate. This loop continuation must be set up early
         * in case isIClosing is true - we want to process all data
         * before considering the closing.
         * This should only occur if the PNG differencers were used.
         */
        HQASSERT ( lzwstate->predictor_state.predictor >= 2 ,
                   "Invalid difference state in LZWDecode" ) ;

        srcDataConsumed = src_count ;
        count = lzwstate->dbuflen ;
        ptr = lzwstate->dbuf;

        if ( ! diffEncode ( &lzwstate->diff, src_ptr,
                            &srcDataConsumed, ptr, &count) ) {
          return error_handler( IOERROR ) ;
        }

        HQASSERT( count >= 0 , "diffEncoode failed in LZWEncode" ) ;

        /* Update where we are in the processing of the filter's buffer. */
        src_ptr += srcDataConsumed ;
        src_count -= srcDataConsumed ;

        /* count and ptr now ready to carry on processing.
         * fall through to processing last byte read.
         */
      }
    }
    else {
      HQASSERT( isIClosing( filter ) ,
                "Should only get here when filter is closing." ) ;

      /* Is there a final code to write? */

      if ( entry == ROGUE_ENTRY )
        append_eod = TRUE ;

      if ( append_eod ) {
        c = entry = lzwstate->eodcode ;
        ClearIClosingFlag( filter ) ;
      }
      else {
        c = entry ;
        append_eod = TRUE ;
      }
      hash = 0 ;
      fcode = 0 ;
      goto emptyslot ;
    }
    HQTRACE( ( c > maxval ) &&
             (( debug_lzw & DEBUG_LZW_UNITSIZE_OVERFLOW ) != 0 ) ,
             ( "LZW UnitSize of %d means %u gets truncated to %u" ,
               lzwstate->unitsize , c , c & maxval )) ;
    c &= maxval ;
    fcode = ( c << 12 ) + entry ;
    hash = ( c << HSHIFT ) ^ entry ; /* calculate hash */

    HQASSERT( hash >= 0 &&
              hash < HASHSIZE ,
              "Hash out of range of table." ) ;
    if ( htable[ hash ] == fcode ) {
      entry = ctable[ hash ] ;
      continue ;
    } else if ( htable[ hash ] < 0 )
      goto emptyslot ;
    d = HASHSIZE - hash ;
    if ( hash == 0 )
      d = 1 ;
  retry:
    if (( hash -= d ) < 0 )
      hash += HASHSIZE ;
    if ( htable[ hash ] == fcode ) {
      entry = ctable[ hash ] ;
      continue ;
    }
    if ( htable[ hash ] >= 0 )
      goto retry ;
  emptyslot:
    OUTPUT_CODE( entry ) ;
    entry = c ;
    if ( free_entry >= current_maxcode ) {
      if ( free_entry >= MAXIMUMCODE ) {
        HqMemSet32((uint32 *)htable , (uint32)(-1), HASHSIZE);
        free_entry = lzwstate->firstfree ;
        OUTPUT_CODE( lzwstate->clearcode ) ;
        current_bits = lzwstate->startbits ;
        current_maxcode = MAXCODE( current_bits ) ;
        current_maxcode += lzwstate->earlychange ;
        continue ;
      }
      current_bits++ ;
      if ( current_bits == MAXBITS )
        current_maxcode = MAXMAXCODE ;
      else {
        current_maxcode = MAXCODE( current_bits ) ;
        current_maxcode += lzwstate->earlychange ;
      }
    }
    ctable[ hash ] = ( uint16 )free_entry++ ;
    htable[ hash ] = fcode ;
  }


  /* Write any bits left over if we're about to close. */

  if ( append_eod && offset )
    if ( Putc( partial_byte , uflptr ) == EOF )
      return error_handler( IOERROR ) ;

  /* repack register variables into lzwstate */
  current_maxcode -= lzwstate->earlychange ;

  lzwstate->free_entry = free_entry ;
  lzwstate->curr_entry = entry ;
  lzwstate->current_bits = current_bits ;
  lzwstate->offset = offset ;
  lzwstate->partial_byte = partial_byte ;
  lzwstate->current_maxcode = current_maxcode ;

  theICount( filter ) = count ;
  theIPtr( filter ) = theIBuffer( filter ) + count ;

  return TRUE ;
}

#define ENCODE_DIFF(v, v_type, prev, prev_type, c) \
MACRO_START \
  if (c == 0) { \
    prev = (prev_type)v; \
  } else { \
    temp = (int32)v; \
    v = (v_type)((int32)v - (int32)prev); \
    prev = (prev_type)temp; \
  } \
MACRO_END

static void lzwDecodeDifferences( PredictorState *predictor_state ,
                                  uint8 *ptr ,
                                  Bool bigendian,
                                  int32 count )
{
  register int32 c, cc, columns, colors, n;
  HQASSERT((   predictor_state->bits_per_comp == 16
            || predictor_state->bits_per_comp == 8
            || predictor_state->bits_per_comp == 4
            || predictor_state->bits_per_comp == 2
            || predictor_state->bits_per_comp == 1 ), "incorrect bits_per_comp");

  if ( count == 0 )
    return;
  if ( count < 0 ) /* negative means eof was found */
    count = -count ;

  if (predictor_state->bits_per_comp == 16) {
    uint16 val1,val2;
    int32 colors,i;
     uint8 *prev_values;

    HQASSERT((count % 2 == 0 ), "odd number of bytes in 16 bit call");

    prev_values = predictor_state->previous_value;
    c = predictor_state->current_column;
    cc = predictor_state->current_color;
    columns = predictor_state->columns;
    colors = predictor_state->colors;
    while ( count )
    {
      for ( i=cc;i< colors && count ;i++)
      {
        if (c != 0)
        {
          if ( bigendian )
          {
            /* Big endian 16 bit add */
            val1 = (uint16)ptr[1] ;
            val1 |= ((uint16)ptr[0])<<8 ;
            val2 = (uint16)prev_values[(i*2)+1] ;
            val2 |= ((uint16)prev_values[i*2])<<8 ;
            val1 = (uint16)(val1 + val2) ;
            ptr[1] = (uint8)(val1 & 0xff) ;
            ptr[0] = (uint8)(val1 >> 8) ;
          }
          else
          {
            /* Little endian 16 bit add */
            val1 = (uint16)ptr[0] ;
            val1 |= ((uint16)ptr[1])<<8 ;
            val2 = (uint16)prev_values[i*2] ;
            val2 |= ((uint16)prev_values[(i*2)+1])<<8 ;
            val1 = (uint16)(val1 + val2) ;
            ptr[0] = (uint8)(val1 & 0xff) ;
            ptr[1] = (uint8)(val1 >> 8) ;
          }
        }
        prev_values[i*2] = ptr[0];
        prev_values[(i*2)+1] = ptr[1];
        ptr +=2;
        count -= 2;
      }
      cc  = i % colors;
      if (cc == 0)
      {
        if (++c == columns)
          c = 0;
      }
    }
    predictor_state->current_column = c;
    predictor_state->current_color = cc;
  }
  else if (predictor_state->bits_per_comp == 8 &&
      predictor_state->colors == 1) {
    /* optimise for this common case: 8 bits, 1 comp */
    register int32 prev_value;

    prev_value = predictor_state->previous_value[0];
    c = predictor_state->current_column;
    columns = predictor_state->columns;
    while (count--) {
      if (c != 0)
        *ptr = (uint8)((int32)*ptr + prev_value);
      prev_value = *ptr++;
      if (++c == columns)
        c = 0;
    }
    predictor_state->previous_value[0] = (uint8) prev_value;
    predictor_state->current_column = c;
  } else {
    int32 v;
    int32 s, o;
    int32 spb; /* samples per byte */
    int32 bpc; /* bits per component */
    int32 mask;
    uint8 * prev_values;


    bpc = predictor_state->bits_per_comp;
    spb = 8 / bpc;
    mask = (1 << bpc ) - 1;
    columns = predictor_state->columns;
    colors = predictor_state->colors;

    prev_values = predictor_state->previous_value + colors;

    c = predictor_state->current_column;
    for (n = 0; n < colors; n++)
      prev_values[n] = predictor_state->previous_value[n];
    n = predictor_state->current_color;

    while (count--) {
      o = 0;
      for (s = spb - 1; s >= 0; s--) {
        v = (int32)*ptr >> (s * bpc);
        v &= mask;
        if (c != 0) {
          v += prev_values[n];
          v &= mask;
        }
        prev_values[n] = (uint8) v;
        o |= v << (s * bpc);
        if (++n == colors) {
          n = 0;
          if (++c == columns) {
            c = 0;
            /* next row starts on byte-boundary */
            if (s > 0) {
              /* leave the last bits in the byte without differencing */
              o = o | (*ptr & ~(0xff << (s * bpc)));
              s = 0 ; /* break out of inner loop */
            }
          }
        }
      }
      *ptr++ = (uint8)o;
    }
    predictor_state->current_column = c;
    predictor_state->current_color = n;
    for (n = 0; n < colors; n++)
      predictor_state->previous_value[n] = prev_values[n];
  }
}

void lzw_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* lzw encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("LZWEncode") ,
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       lzwFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       lzwFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       lzwEncodeBuffer ,                     /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void lzw_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* lzw decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("LZWDecode"),
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       lzwFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       lzwFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       lzwDecodeBuffer ,                     /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

void init_C_globals_lzw(void)
{
#ifdef ASSERT_BUILD
  debug_lzw = 0 ;
#endif
}

/* Log stripped */
