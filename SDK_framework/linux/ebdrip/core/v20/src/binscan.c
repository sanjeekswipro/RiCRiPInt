/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:binscan.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements the Level-2 feature of binary tokens and binary sequences as
 * the input to the scanner.
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "fileio.h"
#include "hqmemcpy.h"
#include "namedef_.h"

#include "control.h"
#include "bitblts.h"
#include "matrix.h"
#include "constant.h"
#include "stacks.h"
#include "dicthash.h"
#include "display.h"
#include "graphics.h"
#include "binscan.h"
#include "swcopyf.h"
#include "psvm.h"

#include "swmemory.h"

#include "scanner.h"

#define MAX_SYSTEM_NAMES 477 /* the maximum defined in Appendix F */

/* macros to byte swap fields within an object */

#define HighOrderLength( o ) HighOrder2Bytes(theIBObyte2(o))
#define HighOrderValue( o )  HighOrder4Bytes(theIBObyte4(o))
#define LowOrderLength( o )  LowOrder2Bytes(theIBObyte2(o))
#define LowOrderValue( o )   LowOrder4Bytes(theIBObyte4(o))

static Bool report_undefined_name( int32 index, int32 issystemname, int32 literal ) ;

static Bool unpack_binary_sequence( BINOBJECT *pbinobj,   /* start of this binary object array */
                                    int32     numobjects, /* length of this array */
                                    OBJECT    *olist,     /* current array being filled */
                                    int32     numbytes,   /* no of bytes in sequence - for error reporting */
                                    int32     extbytes,   /* no of bytes in sequence - for error reporting */
                                    int32     code,       /* type of binary sequence - for error reporting */
                                    int32     hiorder,    /* byte order, determined by header */
                                    int32     ieee        /* ieee format, determined in header */ );

static Bool bin_seq_error( int32  error,
                            int32  type,
                            int32  elements,
                            int32  size,
                            uint8  *reason );

static Bool bin_homo_error( int32  error,
                             int32  rep,
                             int32  length,
                             uint8  *reason );

static Bool bin_token_error( int32  error,
                              int32  type );



/*
 * The static global below points to the temp memory for holding the binary
 * sequence. This allows the error routines to free it without having to pass
 * it as an argument to many routines.
 */
static uint8 *bin_seq_buffer = NULL ;

/*
 * The static global below points to boundary between the object area and the
 * strings and names area of the binary sequence.
 */
static uint8 *end_of_objects = NULL ;
static uint8 *beg_of_strings = NULL ;

/* binary scan functions: */
static Bool scan_bin_sequence( int32 code,
                                register FILELIST *flptr );
static Bool scan_bin_integer( int32 code,
                               register FILELIST *flptr );
static Bool scan_bin_fp( int32 code,
                          register FILELIST *flptr );
static Bool scan_bin_real( int32 code,
                            register FILELIST *flptr );
static Bool scan_bin_bool( int32 code,
                            register FILELIST *flptr );
static Bool scan_bin_string( int32 code,
                              register FILELIST *flptr );
static Bool scan_bin_name( int32 code,
                            register FILELIST *flptr );
static Bool scan_bin_user_error( int32 code,
                                  register FILELIST *flptr );
static Bool scan_bin_numberarray( int32 code,
                                   register FILELIST *flptr );
static Bool scan_bin_extnumberarray( int32 code,
                                      register FILELIST *flptr );
static Bool scan_bin_error( int32 code,
                             FILELIST *flptr );


/* the scanner's binary token lookup table: */

scanner_func binary_token_table[32] = {
  /* binary object sequences: 128-131*/
  scan_bin_sequence, scan_bin_sequence, scan_bin_sequence, scan_bin_sequence,
  /* binary integers:  132-136 */
  scan_bin_integer, scan_bin_integer, scan_bin_integer,
  scan_bin_integer, scan_bin_integer,
  /* binary fp: 137 */
  scan_bin_fp,
  /* binary real: 138-140 */
  scan_bin_real, scan_bin_real, scan_bin_real,
  /* binary boolean: 141 */
  scan_bin_bool,
  /* binary string 142-144 */
  scan_bin_string, scan_bin_string, scan_bin_string,
  /* binary name: 145-146 */
  scan_bin_name, scan_bin_name,
  /* binary name, DisplayPS: 147-148 */
  scan_bin_user_error, scan_bin_user_error,
  /* binary array: 149 */
  scan_bin_numberarray,
  /* binary HQN extended HNA: 150 */
  scan_bin_extnumberarray,
  /* unassigned: 151-159 */
  scan_bin_error, scan_bin_error, scan_bin_error, scan_bin_error,
  scan_bin_error, scan_bin_error, scan_bin_error, scan_bin_error,
  scan_bin_error,
};

void init_C_globals_binscan(void)
{
  bin_seq_buffer = NULL ;
  end_of_objects = NULL ;
  beg_of_strings = NULL ;
}

/* ----------------------------------------------------------------------------
   function:            scan_binary_sequence(..)      author:   Luke Tunmer
   creation date:       8-May-1991         last modification:   ##-###-####
   arguments:           code , flptr  .
   description:

   This procedure is for scanning binary sequences. See page 111 PS-2 Book.

---------------------------------------------------------------------------- */
static Bool scan_bin_sequence( int32 code,
                               register FILELIST *flptr )
{
  int32  hiorder , ieee ;
  int32  numobjects , numbytes , extbytes ;
  int32  c ;
  OBJECT arrayo = OBJECT_NOTVM_NOTHING ;

  switch ( code ) {
  case 128 :
    hiorder = TRUE ; ieee = TRUE ; break ;
  case 129 :
    hiorder = FALSE ; ieee = TRUE ; break ;
  case 130 :
    hiorder = TRUE ; ieee = FALSE ; break ;
  case 131 :
    hiorder = FALSE ; ieee = FALSE ; break ;
  default :
    return error_handler( UNREGISTERED ) ;
  }
  /* get next byte - indicates if its a short or extended header */
  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return bin_token_error( SYNTAXERROR , 159 ) ;
  }

  numbytes = extbytes = 0 ;

  if ( c == 0 ) { /* extended header */
    TWOBYTES   twobytes ;
    FOURBYTES  fourbytes ;

    if ( ! (get_next_bytes( 2 , flptr , (uint8 *)&twobytes )))
      return bin_token_error( SYNTAXERROR , 159 ) ;
    if ( hiorder ) {
      HighOrder2Bytes( asBytes( twobytes )) ;
    } else {
      LowOrder2Bytes( asBytes( twobytes )) ;
    }
    numobjects = (int32)asShort( twobytes ) ;

    if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&fourbytes )))
      return bin_seq_error(SYNTAXERROR, code, numobjects, numbytes + extbytes,
                           (uint8 *)"premature end") ;
    if ( hiorder ) {
      HighOrder4Bytes( asBytes( fourbytes )) ;
    } else {
      LowOrder4Bytes( asBytes( fourbytes )) ;
    }
    numbytes = (int32)asInt( fourbytes ) - 8 ;
    extbytes = 8 ;

  } else { /* short header */
    TWOBYTES  twobytes ;

    numobjects = c ;
    if ( ! (get_next_bytes( 2 , flptr , (uint8 *)&twobytes )))
      return bin_token_error( SYNTAXERROR , 159 ) ;
    if ( hiorder ) {
      HighOrder2Bytes( asBytes( twobytes )) ;
    } else {
      LowOrder2Bytes( asBytes( twobytes )) ;
    }
    numbytes = (int32)asShort( twobytes ) - 4 ;
    extbytes = 4 ;
  }

  if ( numbytes <= 0 ||
       numbytes / 8 < numobjects )
    return bin_seq_error(SYNTAXERROR, code, numobjects, numbytes + extbytes,
                         (uint8 *)"sequence too short") ;

  /* allocate the number of bytes required for entire sequence */
  bin_seq_buffer = mm_alloc_with_header(mm_pool_temp,numbytes,
                                        MM_ALLOC_CLASS_BINARY_SEQ);
  if ( bin_seq_buffer == NULL)
    return bin_seq_error(VMERROR, code, numobjects, numbytes + extbytes,
                         (uint8 *)"not enough memory for buffer") ;

  /* set up object to contain top level array */
  if ( !ps_array(&arrayo, numobjects) )
    return bin_seq_error(VMERROR, code, numobjects, numbytes + extbytes,
                         (uint8 *)"vmerror(olist)") ;

  HQASSERT((theTags(arrayo) & LITERAL) == 0,
           "Cannot be both LITERAL and EXECUTABLE") ;
  theTags(arrayo) |= EXECUTABLE ;

  /* input all the bytes first */
  if ( ! (get_next_bytes( numbytes , flptr , bin_seq_buffer )))
    return bin_seq_error(SYNTAXERROR, code, numobjects, numbytes + extbytes,
                         (uint8 *)"premature end") ;

  /* `end_of_objects' points to the end of the object part of the sequence, and
   * the beginning of the string/names part. As string/name composites are
   * found this pointer moves down to the new estimate of where the boundary
   * exists. We should then run through the sequence once again to check for
   * boundary violations, but is time-consuming for little return.
   */
  end_of_objects = bin_seq_buffer + ( 8 * numobjects ) ;
  beg_of_strings = bin_seq_buffer + numbytes ;
  if (! unpack_binary_sequence( (BINOBJECT *)bin_seq_buffer , numobjects , oArray(arrayo) ,
                               numbytes , extbytes , code , hiorder , ieee ))
    return FALSE ;

  mm_free_with_header(mm_pool_temp, bin_seq_buffer);
  bin_seq_buffer = NULL ;

  /* shove the object on the operand stack - scanner responsible for
   * executing immediately if in outer level */
  scannedBinSequence = TRUE ;
  return push( &arrayo , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            unpack_binary_sequence(..)       author:   Luke Tunmer
   creation date:       16-May-1991           last modification:   ##-###-####
   arguments:           pbinobj , numobjects , olist , numbytes ,
                        code , hiorder , ieee
   description:

   This procedure is for unpacking binary sequences.
   It travels through the buffer `pbinobj' to copy objects to the array pointed
   to by `olist'. This couldn't be done while the characters
   were being read in because the name references are forward, and
   their entries need to be put in the name cache.

---------------------------------------------------------------------------- */
static Bool unpack_binary_sequence( BINOBJECT *pbinobj,   /* start of this binary object array */
                                    int32     numobjects, /* length of this array */
                                    OBJECT    *olist,     /* current array being filled */
                                    int32     numbytes,   /* no of bytes in sequence - for error reporting */
                                    int32     extbytes,   /* no of bytes in sequence - for error reporting */
                                    int32     code,       /* type of binary sequence - for error reporting */
                                    int32     hiorder,    /* byte order, determined by header */
                                    int32     ieee        /* ieee format, determined in header */ )
{
  int32  i ;
  int32  literal ;
  int32  r ;                    /* used for signed object lengths */
  int32  ur;                    /* used for unsigned object lengths */
  int32 val ;

  for ( i = 0 ; i < numobjects ; i++, pbinobj++ , olist++ ) {
    /* unpack the literal bit */
    if ( theIBOliteral( pbinobj ))
      literal = EXECUTABLE ;
    else
      literal = LITERAL ;
    /* test that second byte is zero - decreed in the semantics */
    if ( theIBOzero( pbinobj ))
      return bin_seq_error( SYNTAXERROR , code , numobjects ,
                           numbytes + extbytes , (uint8 *)"non-zero unused field") ;

    switch ( theIBOtype ( pbinobj )) {

    case BNULL : /* null */
      theTags(*olist) = (uint8)(ONULL | literal) ;
      if ( theIBOsignedlen( pbinobj ) != 0 ||
           asInt( theIBOvalue( pbinobj )) != 0 )
        return bin_seq_error( SYNTAXERROR , code , numobjects ,
                             numbytes + extbytes , (uint8 *)"non-zero unused field") ;
      break ;

    case BINTEGER : /* integer */
      theTags(*olist) = (uint8)(OINTEGER | literal) ;
      if ( theIBOsignedlen( pbinobj ) != 0 )
        return bin_seq_error( SYNTAXERROR , code , numobjects ,
                             numbytes + extbytes , (uint8 *)"non-zero unused field") ;
      if ( hiorder ) {
        HighOrderValue( pbinobj ) ;
      } else {
        LowOrderValue( pbinobj ) ;
      }
      oInteger(*olist) = (int32)asInt( theIBOvalue( pbinobj )) ;
      break ;

    case BREAL : /* real */
      theTags(*olist) = (uint8)(OREAL | literal) ;
      if ( hiorder ) {
        HighOrderLength( pbinobj ) ;
        HighOrderValue( pbinobj ) ;
      } else {
        LowOrderLength( pbinobj ) ;
        LowOrderValue( pbinobj ) ;
      }
      r = (int32)theIBOsignedlen( pbinobj ) ;
      if ( r == 0 ) { /* float */
        if (( asInt( theIBOvalue( pbinobj )) & 0x7f800000 ) == 0x7f800000 )
          return bin_seq_error( UNDEFINEDRESULT , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"illegal float") ;
        if ( ieee )
          oReal(*olist) =
            IEEEToFloat( asFloat( theIBOvalue( pbinobj ))) ;
        else
          oReal(*olist) = asFloat( theIBOvalue( pbinobj )) ;
      } else { /* fixed point */
        if ( r < 0 || r > 31 )
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"bad fixed scale") ;
        val = asSignedInt( theIBOvalue( pbinobj )) ;
        oReal(*olist) = (USERVALUE)FixedToFloat( val , r ) ;
      }
      break ;

    case BNAME : /* name */
      theTags(*olist) = (uint8)(ONAME | literal) ;
      if ( hiorder ) {
        HighOrderLength( pbinobj ) ;
        HighOrderValue( pbinobj ) ;
      } else {
        LowOrderLength( pbinobj ) ;
        LowOrderValue( pbinobj ) ;
      }
      r = (int32)theIBOsignedlen( pbinobj ) ;
      val = (int32)asInt( theIBOvalue( pbinobj )) ;
      if ( r > 0 ) { /* name to be found later on */
        uint8 *pname = bin_seq_buffer + val ;
        /* test offset and length of name */
        if (  r > MAXPSNAME )
          return bin_seq_error( LIMITCHECK , code , numobjects ,
                               numbytes + extbytes , NULL) ;
        if ( pname + r > bin_seq_buffer + numbytes )
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"name string out of bounds") ;
        if ( pname < beg_of_strings ) {
          beg_of_strings = pname ;
          if ( beg_of_strings < end_of_objects )
            return bin_seq_error( SYNTAXERROR , code , numobjects ,
                                 numbytes + extbytes , (uint8 *)"arrays/strings out of order") ;

        }
        if ( NULL == ( oName(*olist) = cachename( pname , (uint32)r)))
          return bin_seq_error(VMERROR, code, numobjects,
                               numbytes + extbytes, (uint8 *)"vmerror(ncache)" ) ;
      } else if ( r == 0 ) { /* user table index */
        return report_undefined_name( val, FALSE, literal ) ;
      } else if ( r == -1 ) { /* system table index */
        NAMECACHE *nc ;
        if ( val < 0 ||
             val > MAX_SYSTEM_NAMES )
          return report_undefined_name( val, TRUE, literal ) ;
        /* index into system table with val */
        nc = &system_names[ val ] ;
        if ( theICList( nc ) == NULL )  /* an undefined name */
          return report_undefined_name( val, TRUE, literal ) ;
        oName(*olist) = nc;
      } else
        return bin_seq_error( SYNTAXERROR , code , numobjects ,
                             numbytes + extbytes , (uint8 *)"bad name length") ;
      break ;

    case BBOOLEAN : /* boolean */
      theTags(*olist) = (uint8)(OBOOLEAN | literal) ;
      if ( theIBOsignedlen( pbinobj ) != 0 )
        return bin_seq_error( SYNTAXERROR , code , numobjects ,
                             numbytes + extbytes , (uint8 *)"non-zero unused field") ;
      if ( hiorder ) {
        HighOrderValue( pbinobj ) ;
      } else {
        LowOrderValue( pbinobj ) ;
      }
      switch ( asInt( theIBOvalue( pbinobj ))) {
      case 0 : oBool(*olist) = FALSE ; break ;
      case 1 : oBool(*olist) = TRUE ; break;
      default :
        return bin_seq_error ( SYNTAXERROR , code , numobjects ,
                              numbytes + extbytes , (uint8 *)"bad boolean value") ;
        /*break;*/
      }
      break ;

    case BSTRING : /* string */
      {
        uint8 *pstring ;

        if ( hiorder ) {
          HighOrderLength( pbinobj ) ;
          HighOrderValue( pbinobj ) ;
        } else {
          LowOrderLength( pbinobj ) ;
          LowOrderValue( pbinobj ) ;
        }
        ur = (int32)theIBOunsignedlen( pbinobj ) ;
        val = (int32)asInt( theIBOvalue( pbinobj )) ;
        pstring = bin_seq_buffer + val ;
        /* test offset */
        if ( pstring + ur > bin_seq_buffer + numbytes )
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"string out of bounds") ;
        if ( pstring < beg_of_strings ) {
          beg_of_strings = pstring ;
          if ( beg_of_strings < end_of_objects )
            return bin_seq_error( SYNTAXERROR , code , numobjects ,
                                 numbytes + extbytes , (uint8 *)"arrays/strings out of order") ;

        }
        if ( !ps_string(olist, pstring, ur) )
          return bin_seq_error( VMERROR, code , numobjects ,
                                numbytes + extbytes , (uint8 *)"vmerror(string)") ;
        HQASSERT((theTags(*olist) & (LITERAL|EXECUTABLE)) == 0,
                 "Adding LITERAL/EXECUTABLE, but already have one") ;
        theTags(*olist) |= literal ;
      }
      break ;

    case BINAME : /* immediate eval name */
      {
        OBJECT *pval;
        pval = NULL;  /* initialize to keep compiler quiet */
        if ( hiorder ) {
          HighOrderLength( pbinobj ) ;
          HighOrderValue( pbinobj ) ;
        } else {
          LowOrderLength( pbinobj ) ;
          LowOrderValue( pbinobj ) ;
        }
        r = (int32)theIBOsignedlen( pbinobj ) ;
        val = (int32)asInt( theIBOvalue( pbinobj )) ;
        if ( r > 0 ) { /* name to be found later on  */
          uint8 *pname = bin_seq_buffer + val ;
          if (  r > MAXPSNAME )
            return bin_seq_error( LIMITCHECK , code , numobjects ,
                                 numbytes + extbytes , NULL) ;
          if ( pname + r > bin_seq_buffer + numbytes )
            return bin_seq_error( SYNTAXERROR , code , numobjects ,
                                 numbytes + extbytes , (uint8 *)"name string out of bounds") ;
          if ( pname < beg_of_strings ) {
            beg_of_strings = pname ;
            if ( beg_of_strings < end_of_objects )
              return bin_seq_error( SYNTAXERROR , code , numobjects ,
                                   numbytes + extbytes , (uint8 *)"arrays/strings out of order") ;

          }
          if ( NULL == ( oName(*olist) = cachename( pname , (uint32)r )))
            return bin_seq_error(VMERROR, code, numobjects,
                                 numbytes + extbytes, (uint8 *)"vmerror(ncache)" ) ;
        } else if ( r == 0 ) { /* user table index */
          return report_undefined_name( val, FALSE, EXECUTABLE ) ;
        } else if ( r == -1 ) { /* system table index */
         NAMECACHE *nc ;
          if ( val < 0 ||
               val > MAX_SYSTEM_NAMES )
            return report_undefined_name( val, TRUE, literal ) ;
          /* index into system table with val */
          nc = &system_names[ val ] ;
          if ( theICList( nc ) == NULL )        /* an undefined name */
            return report_undefined_name( val, TRUE, EXECUTABLE ) ;
          oName(*olist) = nc;
        } else
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"bad name length") ;
        theTags(*olist) = ONAME | EXECUTABLE ;
        {           /* perform lookup in dictionaries on dictstack */
          int32  size = theStackSize( dictstack ) ;
          int32  count ;

          error_clear_newerror();
          for ( count = 0 ; count <= size ; ++count )
            if (( pval =
                  extract_hash(stackindex(count, &dictstack), olist)) != NULL )
              break;
            else if ( newerror )
              return FALSE ;
          if ( ! pval ) {
            Copy( & errobject, olist ) ;
            return error_handler( UNDEFINED ) ;
          }
        }
        *olist = *pval ;
      }
      break ;
    case BARRAY : /* array */
      {
        uint8  *pbinarray ;
        if ( hiorder ) {
          HighOrderLength( pbinobj ) ;
          HighOrderValue( pbinobj ) ;
        } else {
          LowOrderLength( pbinobj ) ;
          LowOrderValue( pbinobj ) ;
        }
        ur = (int32)theIBOunsignedlen( pbinobj ) ;
        val = (int32)asInt( theIBOvalue( pbinobj )) ;
        /* check val is on object boundary */
        if ( val & 0x07 )
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"bad array offset") ;
        pbinarray = bin_seq_buffer + val ;
        /* test offset and length of array */
        if ( pbinarray + ur > bin_seq_buffer + numbytes )
          return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"array out of bounds") ;
        if ( pbinarray > end_of_objects ) {
          end_of_objects = pbinarray ;
          if ( beg_of_strings < end_of_objects )
            return bin_seq_error( SYNTAXERROR , code , numobjects ,
                                 numbytes + extbytes , (uint8 *)"arrays/strings out of order") ;

        }

        if ( !ps_array(olist, ur) )
          return bin_seq_error( VMERROR, code , numobjects ,
                               numbytes + extbytes , (uint8 *)"vmerror(array)") ;
        HQASSERT((theTags(*olist) & (LITERAL|EXECUTABLE)) == 0,
                 "Adding LITERAL/EXECUTABLE, but already have one") ;
        theTags(*olist) |= literal ;

        /* recurse on this sub array */
        if ( ! unpack_binary_sequence( (BINOBJECT *)pbinarray , ur , oArray(*olist) ,
                                      numbytes , extbytes , code , hiorder , ieee ))
          return FALSE ;
      }
      break ;
    case BMARK : /* mark */
      theTags(*olist) = (uint8)(OMARK | literal) ;
      if ( theIBOsignedlen( pbinobj ) != 0 ||
           asInt( theIBOvalue( pbinobj )) != 0 )
        return bin_seq_error( SYNTAXERROR , code , numobjects ,
                             numbytes + extbytes , (uint8 *)"non-zero unused field") ;
      break ;
    default : /* type field is invalid */
      return bin_seq_error( SYNTAXERROR , code , numobjects ,
                               numbytes + extbytes , (uint8 *)"bad type") ;
    }
  }
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            scan_binary_integer(..)    author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary integers.

---------------------------------------------------------------------------- */
static Bool scan_bin_integer( int32 code,
                              register FILELIST *flptr )
{
  FOURBYTES  buff ;
  int32      val ;

  switch ( code ) {
  case 132 : /* 32 bits hi-first */
    if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 155 ) ;
    HighOrder4Bytes( asBytes( buff )) ;
    val = asSignedInt( buff ) ;
    break ;
  case 133 : /* 32 bits lo-first */
    if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 156 ) ;
    LowOrder4Bytes( asBytes( buff )) ;
    val = asSignedInt( buff ) ;
    break ;
  case 134 : /* 16 bits hi-first */
    if ( ! (get_next_bytes( 2 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 155 ) ;
    HighOrder2Bytes( asBytes( buff )) ;
    val = (int32)asSignedShort( buff ) ;
    break ;
  case 135 : /* 16 bits lo-first */
    if ( ! (get_next_bytes( 2 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 156 ) ;
    LowOrder2Bytes( asBytes( buff )) ;
    val = (int32)asSignedShort( buff ) ;
    break ;
  case 136 : /* 8 bits signed */
    if ( ! (get_next_bytes( 1 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 157 ) ;
    val = (int32)asChars(buff)[0] ;
    break ;
  default :
    return error_handler( UNREGISTERED ) ;
  }
  scannedBinSequence = TRUE ;
  /* setup val as integer result */
  return stack_push_integer(val, &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            scan_bin_fp(..)            author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary fixed-points.

---------------------------------------------------------------------------- */
static Bool scan_bin_fp( int32 code,
                         register FILELIST *flptr )
{
  Bool      hiorder = TRUE;
  uint32     c;
  FOURBYTES  buff;
  int32      ival;

  UNUSED_PARAM(int32, code);

  if (( c = (uint32)(Getc( flptr ))) == (uint32)EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return bin_token_error( SYNTAXERROR , 158 ) ;
  }
  if ( c >= 128 ) {
    hiorder = FALSE ; c -= 128;
  }
  if ( c <= 31 ) { /* 32 bit fp */
    if ( ! ( get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 158 ) ;
    if ( hiorder ) {
      HighOrder4Bytes( asBytes( buff )) ;
    } else {
      LowOrder4Bytes( asBytes( buff )) ;
    }
    ival = asSignedInt( buff ) ;
  } else { /* 16 bit fp */
    c -= 32; /* c now holds the fp scale factor */
    if ( c > 15 )
      return bin_token_error( SYNTAXERROR , 158 ) ;
    if ( ! ( get_next_bytes( 2 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 158 ) ;
    if ( hiorder ) {
      HighOrder2Bytes( asBytes( buff )) ;
    } else {
      LowOrder2Bytes( asBytes( buff )) ;
    }
    ival = (int32)asSignedShort( buff ) ;
  }

  scannedBinSequence = TRUE ;
  if ( c == 0 )
    return stack_push_integer(ival, &operandstack) ;
  else
    return stack_push_real(FixedToFloat( ival , c ), &operandstack) ;
}



/* ----------------------------------------------------------------------------
   function:            scan_bin_real(..)       author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary reals.

---------------------------------------------------------------------------- */
static Bool scan_bin_real( int32 code,
                           register FILELIST *flptr )
{
  FOURBYTES buff ;
  float     fval ;

  switch ( code ) {
  case 138 : /* IEEE hi-first */
    if ( ! ( get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 155 ) ;
    HighOrder4Bytes( asBytes( buff )) ;
    fval = IEEEToFloat( asFloat( buff )) ;
    break ;
  case 139 : /* IEEE lo-first */
    if ( ! ( get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 156 ) ;
    LowOrder4Bytes( asBytes( buff )) ;
    fval = IEEEToFloat( asFloat( buff )) ;
    break ;
  case 140 : /* native */
    if ( ! ( get_next_bytes( 4 , flptr , (uint8 *)&buff )))
      return bin_token_error( SYNTAXERROR , 157 ) ;
    /* just use bytes in the order they arrived */
    fval = asFloat( buff ) ;
    break ;
  default :
    return error_handler( UNREGISTERED ) ;
  }

  /* This condition can only be TRUE if we have a nan et al. */
  if (( asInt( buff ) & 0x7f800000 ) == 0x7f800000 )
    return error_handler( UNDEFINEDRESULT ) ;

  scannedBinSequence = TRUE ;
  return stack_push_real((SYSTEMVALUE)fval, &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            scan_bin_bool(..)          author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary booleans.

---------------------------------------------------------------------------- */
static Bool scan_bin_bool( int32 code,
                           register FILELIST *flptr )
{
  int32  c;
  OBJECT *o ;

  UNUSED_PARAM(int32, code) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return bin_token_error( SYNTAXERROR , 157 ) ;
  }
  if ( c == 0 )
    o = &fnewobj ;
  else if ( c == 1 )
    o = &tnewobj ;
  else
    return bin_token_error( SYNTAXERROR , 157 ) ;
  scannedBinSequence = TRUE ;
  return push( o , &operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            scan_bin_string(..)        author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary strings.

---------------------------------------------------------------------------- */
static Bool scan_bin_string( int32 code,
                             register FILELIST *flptr )
{
  int32  c1, c2;
  int32 newcode ;
  int32 len;
  OBJECT stringo = OBJECT_NOTVM_NOTHING ;

  if ( code == 142 ) {/* length of string < 255 */
    newcode = 157 ;
    if (( c1 = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr ))( flptr ) ;
      else
        return bin_token_error( SYNTAXERROR , newcode ) ;
    }
    len = c1;
  } else {
    newcode = code == 143 ? 155 : 156 ;
    HQASSERT( code == 143 || code == 144 , "unknown code" ) ;
    if ((( c1 = Getc( flptr )) == EOF ) ||
        (( c2 = Getc( flptr )) == EOF )) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr ))( flptr ) ;
      else
        return bin_token_error( SYNTAXERROR , newcode ) ;
    }
    if ( code == 143 ) /* length hi order */
      len = ( c1 << 8 ) + c2 ;
    else if ( code == 144 ) /* length in lo order */
      len = c1 + ( c2 << 8 ) ;
    else
      return error_handler( UNREGISTERED ) ;
  }

  if ( !ps_string(&stringo, NULL, len) )
    return FALSE ;

  /* read in len bytes of string.. */
  if ( ! get_next_bytes( len , flptr , oString(stringo)) )
    return bin_token_error( SYNTAXERROR , newcode ) ;

  scannedBinSequence = TRUE ;

  return push( &stringo , & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            scan_bin_name(..)       author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary names.

---------------------------------------------------------------------------- */
static Bool report_undefined_name( int32 index, int32 issystemname, int32 literal )
{
  uint8 *errstring = (uint8 *)"%s%d" ;
  uint8 buffer[ 16 ] ;
  OBJECT name = OBJECT_NOTVM_NOTHING ;

  theTags( name ) = (uint8)(ONAME | literal) ;

  swcopyf( buffer, errstring, issystemname ? ( uint8 * )"system" : ( uint8 * )"user" , index ) ;

  if (( oName( name ) =
          cachename( (uint8*)buffer ,
                     ( uint32 )strlen(( char * )buffer ))) != NULL ) {
    Copy( & errobject, & name ) ;
  }
  else { /* Setup some name so we don't crash. */
    oName( name ) = system_names + NAME_unregistered ;
    Copy( & errobject, & name ) ;
  }
  return error_handler( UNDEFINED ) ;
}

static Bool scan_bin_user_error( int32 code,
                                  register FILELIST *flptr )
{
  int32  index;
  int32  literal;

  if (( index = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return  bin_token_error( SYNTAXERROR , 157 ) ;
  }
  switch ( code ) {
  case 147 : /* literal name */
    literal = LITERAL ;
    break ;
  case 148 : /* executable name */
    literal = EXECUTABLE ;
    break ;
  default :
    return error_handler( UNREGISTERED ) ;
  }
  return report_undefined_name( index, FALSE, literal ) ;
}

static Bool scan_bin_name( int32 code,
                           register FILELIST *flptr )
{
  int32  index;
  int32  literal;
  NAMECACHE *nc ;
  OBJECT * theo;

  if (( index = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return  bin_token_error( SYNTAXERROR , 157 ) ;
  }
  switch ( code ) {
  case 145 : /* literal name */
    theo = & nnewobj;
    literal = LITERAL ;
    break ;
  case 146 : /* executable name */
    theo = & nnewobje;
    literal = EXECUTABLE ;
    break ;
  default :
    return error_handler( UNREGISTERED ) ;
  }
  nc = &system_names[ index ] ;
  if ( theICList( nc ) == NULL )        /* an undefined name */
    return report_undefined_name( index, TRUE, literal ) ;
  oName(*theo) = nc ;
  scannedBinSequence = TRUE ;
  return push( theo , & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            scan_bin_numberarray(..)    author:      Luke Tunmer
   creation date:       8-May-1991       last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure is for scanning binary arrays.

---------------------------------------------------------------------------- */
static Bool scan_bin_numberarray( int32 code,
                                  register FILELIST *flptr )
{
  int32      r, l1, l2 , len ;
  int32      hiorder ;
  int32      i ;
  int32      ival ;
  FOURBYTES  buff ;
  OBJECT     *pobj;
  OBJECT arrayo = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(int32, code);

  /* get the header bytes */
  if ((( r = Getc( flptr )) == EOF ) ||
      (( l1 = Getc( flptr )) == EOF ) ||
      (( l2 = Getc( flptr )) == EOF )) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return bin_token_error( SYNTAXERROR , 159 ) ;
  }
  if ( r >= 128 ) {
    hiorder = FALSE ; r -= 128 ; len = l1 + ( l2 << 8 ) ;
  } else {
    hiorder = TRUE; len = ( l1 << 8 ) + l2 ;
  }

  if ( r >= 50 )
    return bin_homo_error(SYNTAXERROR, r, len,
                          (uint8 *)"bad representation") ;

  /* set up object to take array */
  if ( !ps_array(&arrayo, len) )
    return FALSE ;

  pobj = oArray(arrayo) ;

  /* handle the different number types */
  if ( r <= 31 ) {
    /* 32 bit fp */
    for ( i = 0 ; i < len ; i++ ) {
      if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&buff )))
        return bin_homo_error(SYNTAXERROR, r, len,
                              (uint8 *)"premature end") ;
      if ( hiorder ) {
        HighOrder4Bytes( asBytes( buff )) ;
      } else {
        LowOrder4Bytes( asBytes( buff )) ;
      }
      if ( r == 0 ) { /* they are integers */
        theTags(*pobj) = OINTEGER ;
        oInteger(*pobj) = asSignedInt( buff ) ;
      } else { /* they are reals */
        theTags(*pobj) = OREAL ;
        ival = asSignedInt( buff ) ;
        oReal(*pobj) = (USERVALUE)FixedToFloat( ival , r ) ;
      }
      pobj++;
    }
  } else if ( r <= 47 ) {
    /* 16 bit fp */
    r -= 32 ;
    for ( i = 0 ; i < len ; i++ ) {
      if ( ! (get_next_bytes( 2 , flptr , (uint8 *)&buff )))
        return bin_homo_error(SYNTAXERROR, r, len,
                              (uint8 *)"premature end") ;
      if ( hiorder )
        HighOrder2Bytes( asBytes( buff )) ;
      else
        LowOrder2Bytes( asBytes( buff )) ;
      if ( r == 0 ) { /* they are integers */
        theTags(*pobj) = OINTEGER ;
        oInteger(*pobj) = (int32)asShort( buff ) ;
      } else { /* they are all reals */
        theTags(*pobj) = OREAL ;
        ival = (int32)asSignedShort( buff ) ;
        oReal(*pobj) = (USERVALUE)FixedToFloat( ival, r ) ;
      }
      pobj++;
    }
  } else if ( r == 48 ) { /* 32 bit IEEE */
    for ( i = 0 ; i < len ; i++ ) {
      if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&buff )))
        return bin_homo_error(SYNTAXERROR, r, len,
                              (uint8 *)"premature end") ;
      if ( hiorder ) {
        HighOrder4Bytes( asBytes( buff )) ;
      } else {
        LowOrder4Bytes( asBytes( buff )) ;
      }
      theTags(*pobj) = OREAL ;
      if (( asInt( buff ) & 0x7f800000 ) == 0x7f800000 )
        return bin_homo_error(UNDEFINEDRESULT, r, len,
                              NULL) ;
      oReal(*pobj) = IEEEToFloat( asFloat( buff )) ;
      pobj++ ;
    }
  } else if ( r == 49 ) { /* 32 bit native */
    for ( i = 0 ; i < len ; i++ ) {
      if ( ! (get_next_bytes( 4 , flptr , (uint8 *)&buff )))
        return bin_homo_error(SYNTAXERROR, r, len,
                              (uint8 *)"premature end") ;
      theTags(*pobj) = OREAL ;
      if (( asInt( buff ) & 0x7f800000 ) == 0x7f800000 )
        return bin_homo_error(UNDEFINEDRESULT, r, len,
                              NULL) ;
      oReal(*pobj) = asFloat( buff ) ;
      pobj++ ;
    }
  } else {
    HQFAIL( "unknown representation" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  scannedBinSequence = TRUE ;
  return push( &arrayo , &operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:          scan_bin_extnumberarray(..)    author:      Eric Penfold
   creation date:     19-Feb-1998         last modification:      ##-###-####
   arguments:         code , flptr .
   description:

   This procedure is for scanning binary extended homogenous number arrays.
   Note that this is a bit of a hack since all we actually do is create a
   longstring of suitable length and copy the binary array into this string

---------------------------------------------------------------------------- */
static Bool scan_bin_extnumberarray( int32 code,
                                     register FILELIST *flptr )
{
  int32      r, len ;
  FOURBYTES  buff ;
  OBJECT     newobj = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(int32, code);

  /* get the header bytes. Format for Ext-HNA is:
   *   (1 byte)  150
   *   (1 byte)  number representation
   *   (2 bytes) dummy bytes
   *   (4 bytes) actual data length
   *   (n bytes) data
   */
  if ((( r = Getc( flptr )) == EOF ) ||
      ( Getc( flptr ) == EOF ) || ( Getc( flptr ) == EOF )) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    else
      return bin_token_error( SYNTAXERROR , 159 ) ;
  }

  if ( ! ( get_next_bytes( 4 , flptr , (uint8 *)&buff )))
    return bin_token_error( SYNTAXERROR , 159 ) ;
  /* we need to know the number representation so we decode the length */
  if ( r < HNA_REP_LOWORDER ) {
    HighOrder4Bytes( asBytes( buff )) ;
  }
  else {
    LowOrder4Bytes( asBytes( buff )) ;
  }
  len = asInt( buff ) ;
  if ( len < 0 )
    return bin_homo_error( SYNTAXERROR, r, len, ( uint8 * )"invalid length" ) ;

  /* Now create the object */
  if ( !ps_longstring(&newobj, NULL, len) )
    return FALSE ;

  if ( ! ( get_next_bytes(( int32 )len, flptr, theILSCList(oLongStr(newobj)))))
    return bin_homo_error( SYNTAXERROR, r, len, ( uint8 * )"premature end" ) ;

  scannedBinSequence = TRUE ;
  return push( &newobj , &operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            scan_bin_error(..)      author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           code , flptr .
   description:

   This procedure produces a syntax error.

---------------------------------------------------------------------------- */
/*ARGSUSED*/
static Bool scan_bin_error( int32 code,
                            FILELIST *flptr )
{
  UNUSED_PARAM(int32, code) ;
  UNUSED_PARAM(FILELIST *, flptr) ;

  return error_handler( SYNTAXERROR ) ;
}


/* ----------------------------------------------------------------------------
   function:            get_next_bytes(..)         author:      Luke Tunmer
   creation date:       8-May-1991      last modification:      ##-###-####
   arguments:           n, flptr, buffer .
   description:

   This procedure reads in the next n bytes into a buffer of uint8
   from the flptr stream. Calls error_handler on EOF.

---------------------------------------------------------------------------- */
Bool get_next_bytes( int32 n,
                     register FILELIST *flptr,
                     register uint8  *buffer )
{
  register int32 c ;

  while ( n-- ) {
    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr ))( flptr ) ;
      else
        return error_handler( SYNTAXERROR ) ;
    }
    *(buffer++) = (uint8 ) c ;
  }
  return TRUE ;
}




/* ----------------------------------------------------------------------------
   function:            bin_seq_error(..)  author:                 Luke Tunmer
   creation date:       10-May-1991        last modification:      ##-###-####
   arguments:           error , type , elements , size , reason
   description:

   This procedure calls the error handler with an object describing the
   object that caused the error pushed onto the operand stack. p.115 PS-L2.
   The error type is given in error, and a string is given which describes
   the reason for the error.

---------------------------------------------------------------------------- */
static Bool bin_seq_error( int32  error,
                            int32  type,
                            int32  elements,
                            int32  size,
                            uint8  *reason )
{
  uint8 *errstring = (uint8 *)"bin obj seq, type %d, elements %d, size %d%s%s" ;

#define ERROR_STRING_LENGTH 80  /* enough to take stuff above, plus a bit */

  if ( bin_seq_buffer ) {
    mm_free_with_header(mm_pool_temp, bin_seq_buffer);
    bin_seq_buffer = NULL ;
  }

  if ( !ps_string(&errobject, NULL, ERROR_STRING_LENGTH) )
    return FALSE ; /* is this all that can be done ? */

  swcopyf(oString(errobject) ,
          errstring ,
          type , elements , size ,
          reason ? ( uint8 * )", " : ( uint8 * )"" ,
          reason ? reason : ( uint8 * )"" ) ;
  theLen( errobject ) = CAST_TO_UINT16(strlen((char *)oString(errobject))) ;

  return error_handler( error ) ;
}

/* ----------------------------------------------------------------------------
   function:            bin_homo_error(..)         author:      Luke Tunmer
   creation date:       10-May-1991      last modification:      ##-###-####
   arguments:           error , type
   description:

   This procedure is called by the binary token scanning procedures when
   an error occurs. It places a string object in the errobject describing
   the type of binary token that caused the error.

---------------------------------------------------------------------------- */
static Bool bin_homo_error( int32  error,
                            int32  rep,
                            int32  length,
                            uint8  *reason )
{
  uint8 *errstring = (uint8 *)"bin num array, rep=%d, length=%d%s%s" ;

  if ( !ps_string(&errobject, NULL, ERROR_STRING_LENGTH) )
    return FALSE ; /* is this all that can be done ? */

  swcopyf(oString(errobject) , errstring ,
          rep , length ,
          reason ? ( uint8 * )", " : ( uint8 * )"" ,
          reason ? reason : ( uint8 * )"" ) ;
  theLen( errobject ) = CAST_TO_UINT16(strlen((char *)oString(errobject))) ;

  return error_handler( error ) ;
}



/* ----------------------------------------------------------------------------
   function:            bin_token_error(..)         author:      Luke Tunmer
   creation date:       10-May-1991      last modification:      ##-###-####
   arguments:           error , type
   description:

   This procedure is called by the binary token scanning procedures when
   an error occurs. It places a string object in the errobject describing
   the type of binary token that caused the error.

---------------------------------------------------------------------------- */
static Bool bin_token_error( int32  error,
                             int32  type )
{
  uint8 *errstring = (uint8 *)"binary token, type %d" ;

  if ( !ps_string(&errobject, NULL, ERROR_STRING_LENGTH) )
    return FALSE ;

  swcopyf(oString(errobject) , errstring , type ) ;
  theLen( errobject ) = CAST_TO_UINT16(strlen((char *)oString(errobject))) ;

  return error_handler( error ) ;
}


/* Log stripped */
