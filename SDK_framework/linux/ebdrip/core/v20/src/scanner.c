/** \file
 * \ingroup psscan
 *
 * $HopeName: SWv20!src:scanner.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript lexical scanner.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "swdevice.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "fileio.h"
#include "ascii85.h"
#include "strfilt.h"
#include "namedef_.h"

#include "control.h"
#include "psvm.h"
#include "bitblts.h"
#include "matrix.h"
#include "stacks.h"
#include "display.h"
#include "graphics.h"
#include "fileops.h"
#include "swmemory.h"
#include "chartype.h"
#include "binscan.h"
#include "progress.h"
#include "swctype.h"
#include "comment.h"
#include "scanner.h"

#define INIT_STORE_SIZE (1024)  /* This is the default buffer size we read into. */

#define file_or_syntax_error( _flptr ) \
 ( isIIOError( (_flptr) ) ? \
  (*theIFileLastError( (_flptr) )) ( (_flptr) ) : error_handler (SYNTAXERROR))

#define inc_line_num( _flptr ) MACRO_START                              \
  if ( theIFileLineNo( (_flptr) ) < 0 )                                 \
    theIFileLineNo( (_flptr) ) = - theIFileLineNo( (_flptr) ) + 1 ;     \
  else                                                                  \
    theIFileLineNo( (_flptr) )++ ;                                      \
MACRO_END

#define prov_inc_line_num( _flptr ) MACRO_START                         \
  if ( theIFileLineNo( (_flptr) ) < 0 )                                 \
    theIFileLineNo( (_flptr) )-- ;                                      \
  else                                                                  \
    theIFileLineNo( (_flptr) ) = - theIFileLineNo( (_flptr) ) - 1 ;     \
MACRO_END

#define not_at_start_of_line( _flptr ) MACRO_START                      \
  if ( theIFileLineNo( (_flptr) ) < 0 )                                 \
    theIFileLineNo( (_flptr) ) = - theIFileLineNo( (_flptr) ) ;         \
MACRO_END


Bool scannedObject = FALSE ;
Bool scannedBinSequence = FALSE ;

static Bool storeextend(int32 c);
static Bool scanner(register FILELIST * flptr, Bool scan_comments,
                    Bool flag_binseq, Bool isfile);
static Bool scanstring(register FILELIST * flptr, Bool isfile);
static int32 scanname(register FILELIST * flptr);
static Bool scanhex(register FILELIST *flptr);
static Bool scanascii85(register FILELIST *flptr);
static Bool scanprocedure(register FILELIST *flptr, Bool isfile);
static int32 scandigits(FILELIST *flptr);
static int32 scansign(register FILELIST *flptr);
static int32 scanperiod(register FILELIST *flptr);
static int32 scanpdigits(register FILELIST *flptr);
static int32 scanexponent(register FILELIST *flptr);
static int32 scanesign(register FILELIST *flptr);
static int32 scansexponent(register FILELIST *flptr);
static int32 scanbase(register FILELIST *flptr);
static int32 scanbdigits(register FILELIST *flptr);

/*
 *   Storage for the current string being scanned - storemax indicates the
 *   maximum number of characters that can be stored - if this needs to get larger
 *   then contents of thestore copied to larger memory and thestore set to point
 *   to this. Most scanned objects (99% of time) will fit in preset buffer init_store.
 */

static int32 storemax , storepos  ;

/* Memory used in the scanning routines */
static uint8 *init_store = NULL ;
static uint8 *thestore = NULL ;

/* Global variables used to accumulate numbers being scanned */
static uint32 thebase = 0 ;
static int32 sign = 0 , signexp = 0 ;
static int32 iexponent = 0 ;
static int32 nexponent = 0 ;
static int32 ileading = 0 ;
static int32 nleading = 0 ;
static int32 ntrailing = 0 ;
static int32 ntotal = 0 ;
static SYSTEMVALUE fexponent = 0.0 ;
static SYSTEMVALUE fleading = 0.0 ;

/* Used to calculate fraction part of number without using pow function */
static SYSTEMVALUE *fdivs = NULL ;

/*
 * WARNING WARNING WARNING:
 *
 * Note that the mystore family of macros can return FALSE if attempts to
 * alloc space to store the char fail.  Right now we can't think of circumstances
 * where this can happen just now, but handling it seems sensible.
 */

#define mystore_( _c ) MACRO_START              \
  HQASSERT( thestore ,                          \
      "thestore somehow NULL" ) ;               \
  if ( storepos < storemax )                    \
    thestore[ storepos++ ] = (uint8)(_c) ;      \
  else if ( ! storeextend( (_c) ) )             \
    return FALSE ;                              \
MACRO_END

/* store digit for a Name/Number */
#define mystoreN( _c ) MACRO_START              \
  HQASSERT( storepos <= MAXPSNAME ,             \
      "storepos somehow overflowed" ) ;         \
  if ( storepos == MAXPSNAME )                  \
    return error_handler( LIMITCHECK ) ;        \
  mystore_( _c ) ;                              \
MACRO_END

/* store first character for anything - faster and avoids warning */
#define mystore1( _c ) MACRO_START              \
  HQASSERT( storepos == 0 ,                     \
      "this should have been mystoreN" ) ;      \
  thestore[ storepos++ ] = (uint8)(_c) ;        \
MACRO_END

/* store digit for a Long number */
#define mystoreL( _c ) MACRO_START              \
  HQASSERT( storepos <= MAXPSSTRING ,           \
      "storepos somehow overflowed" ) ;         \
  if ( storepos == MAXPSSTRING )                \
    return error_handler( LIMITCHECK ) ;        \
  mystore_( _c ) ;                              \
MACRO_END

/* store digit for a String */
#define mystoreS( _c ) MACRO_START              \
  HQASSERT( storepos <= MAXPSSTRING ,           \
      "storepos somehow overflowed" ) ;         \
  if ( storepos == MAXPSSTRING )                \
    return error_handler( LIMITCHECK ) ;        \
  mystore_( _c ) ;                              \
MACRO_END

/** File runtime initialisation */
static void init_C_globals_scanner(void)
{
  scannedObject = FALSE ;
  scannedBinSequence = FALSE ;
  storemax = storepos = 0 ;
  init_store = NULL ;
  thestore = NULL ;
  thebase = 0 ;
  sign = 0 ;
  signexp = 0 ;
  iexponent = 0 ;
  nexponent = 0 ;
  ileading = 0 ;
  nleading = 0 ;
  ntrailing = 0 ;
  ntotal = 0 ;
  fexponent = 0.0 ;
  fleading = 0.0 ;
  fdivs = NULL ;
}

/* Utility functions for the scanner */

static Bool ps_scanner_swstart(struct SWSTART *params)
{
  register int32 i ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (init_store = mm_alloc_static(INIT_STORE_SIZE)) == NULL )
    return FALSE ;

  thestore = init_store ;
  storemax = INIT_STORE_SIZE ;
  storepos = 0 ;

  if ( (fdivs = mm_alloc_static(10 * sizeof(SYSTEMVALUE))) == NULL )
    return FALSE ;

  fdivs[ 0 ] = 1.0 ;
  for ( i = 1 ; i < 10 ; ++i )
    fdivs[ i ] = fdivs[ i - 1 ] / 10.0 ;

  return TRUE ;
}

void ps_scanner_C_globals(core_init_fns *fns)
{
  init_C_globals_scanner() ;
  fns->swstart = ps_scanner_swstart ;
}

static void reset_thestore(void)
{
  if ( thestore != init_store ) {
    mm_free(mm_pool_temp, thestore, storemax) ;
    thestore = init_store ;
    storemax = INIT_STORE_SIZE ;
  }
  storepos = 0 ;
}

static Bool storeextend(int32 c)
{
  uint8 *newstore ;
  /* Change extension algorithm to double each time */
  HQASSERT( storepos == storemax , "called storeextend and not extending" ) ;

  if ( NULL == ( newstore = mm_alloc(mm_pool_temp, storemax * 2,
                                     MM_ALLOC_CLASS_TOKEN_STORE))) {
    HQFAIL( "Allocation failure within the scanner detected" ) ;
    /*
     *  Note that it should be perfectly safe to continue after this HQFAIL;
     *  it's here because code in the interpreter is supposed to make sure that
     *  there's enough memory for such allocation attempts to always succeed, so
     *  it's interesting if they start failing.
     */
    return error_handler( VMERROR ) ;
  }

  HqMemCpy( newstore , thestore , storemax ) ;
  if ( thestore != init_store )
    mm_free(mm_pool_temp, thestore, storemax ) ;

  thestore = newstore ;
  storemax *= 2;
  thestore[ storepos++ ] = ( uint8 )c ;
  return TRUE ;
}

static Bool report_scanner_error(void)
{
  int32 clen = 128 ;

  if ( storepos < 128 )
    clen = storepos ;

  if ( !ps_string(&errobject, thestore, clen) )
    return error_handler( VMERROR ) ;

  return FALSE ;
}

/** This procedure is the main scanning procedure for files. */
Bool f_scanner(register FILELIST *flptr, Bool scan_comments, Bool flag_binseq)
{
  Bool res ;
  OBJECT lerrobject = OBJECT_NOTVM_NOTHING ;

  HQASSERT( flptr , "flptr NULL in f_scanner" ) ;

  Copy( & lerrobject , & errobject ) ;

  res = scanner( flptr , scan_comments , flag_binseq , TRUE ) ;

  if ( ! setReadFileProgress( flptr ))
    return FALSE ;

  if ( ! res && OBJECTS_IDENTICAL(lerrobject, errobject) )
    res = report_scanner_error() ;

  reset_thestore();
  return res ;
}

/** This procedure is the main scanning procedure for strings. */
Bool s_scanner(uint8 *str, int32 len, int32 *pos, int32 *lineno,
               Bool scan_comments, Bool flag_binseq)
{
  Bool res ;
  OBJECT lerrobject = OBJECT_NOTVM_NOTHING ;
  FILELIST fl ;

  HQASSERT( str , "str NULL in s_scanner" ) ;
  HQASSERT( len > 0 , "len not +ve in s_scanner" ) ;
  HQASSERT( pos , "pos NULL in s_scanner" ) ;
  HQASSERT( lineno , "lineno NULL in s_scanner" ) ;

  /* set up a FILELIST struct with the string as its "buffer"
   */
  string_decode_filter(&fl) ;
  theFileLineNo( fl ) = *lineno ;
  theBuffer( fl ) = str ;
  theBufferSize( fl ) = len ;
  thePtr( fl )  = str + (*pos) ;
  theCount( fl ) = len - *pos ;

  Copy( & lerrobject , & errobject ) ;

  res = scanner( & fl , scan_comments , flag_binseq , FALSE ) ;

  if ( ! res && OBJECTS_IDENTICAL(lerrobject, errobject) )
    res = report_scanner_error() ;

  reset_thestore() ;

  if ( ! res )
    return FALSE ;

  (*pos) = ( int ) ( thePtr( fl ) - str ) ;
  (*lineno) = theFileLineNo( fl );
  return TRUE ;
}

/** This procedure is the  main scanning procedure for files & strings. */
static Bool scanner(register FILELIST * flptr, Bool scan_comments,
                    Bool flag_binseq, Bool isfile )
{
  register int32 c = 0 ; /* Keep the compiler quiet */
  SYSTEMVALUE fthenumber ;
  OBJECT newobject = OBJECT_NOTVM_NOTHING ;

  HQASSERT( flptr , "flptr NULL in scanner" ) ;

  scannedObject = FALSE ;
  scannedBinSequence = FALSE ;

  not_at_start_of_line(flptr);

  for ( ;; ) {
    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr )) {
        return (*theIFileLastError( flptr )) (flptr) ;
      } else {
        return TRUE ;
      }
    }

    if ( IsWhiteSpace( c ) ) {
      if ( c == CR ) {
        SetICRFlags( flptr ) ;
        inc_line_num( flptr ) ;
      } else if ( c == LF ) {
        if ( isICRFlags( flptr ))
          ClearICRFlags( flptr ) ;
        else
          inc_line_num( flptr ) ;
      } else {
        ClearICRFlags( flptr ) ;
      }
    }
    else if ( c == '%' ) {
      ClearICRFlags( flptr ) ;
      if ( !handle_comments( flptr , scan_comments ) )
        return FALSE ;
      if ( !isIOpenFile(flptr) ) /* File may be closed in callback binding */
        return TRUE ;
    }
    else if ( (c == '\x04') && isICTRLD(flptr) ) {
      /* Special EOF indicator - close the stream being scanned */
      return((*theIMyCloseFile(flptr))(flptr, CLOSE_EXPLICIT) == 0);
    }
    else {
      /* We like this character, use it */
      break;
    }
    /* We didn't like that character, loop again to get another */
  }

  ClearICRFlags( flptr ) ;

  /* Set up the string space in which to store the scanned string */
  reset_thestore();

  /* The first character scanned determines the type of PS object to follow. */

  /* Most common type - a name that begins with an alphabetical character.   */
  if ( isalpha( c )) {
    mystore1( c ) ;
    if ( ! scanname( flptr ))
      return FALSE ;

    HQASSERT( storepos <= MAXPSNAME, "name length not been checked" ) ;

    if ( NULL == (oName(nnewobje) =
                  cachename(storepos > 0 ? thestore : NULL, (uint32)storepos)) )
      return FALSE ;
    scannedObject = TRUE ;
    return push( & nnewobje , & operandstack ) ;
  }

  /* Next most common type - perhaps a number of sorts - function
   * returns type. */
  else if ( isdigit( c )) {
    sign = 1 ;
    nleading = 1 ;
    ileading = c - '0' ;
    mystore1( c ) ;
    if ( '\000' == ( c = scandigits( flptr )))
      return FALSE ;
  }

  else if ( IsSpecialChar( c )) {   /* Special char cases. */
    switch ( c ) {

    /* A literal name - or maybe an immediately evaluated name. */
    case '/':
      if (( c = Getc( flptr )) != EOF )
        if ( c != '/' )
          UnGetc( c , flptr ) ;

      if ( ! scanname( flptr ))
        return FALSE ;
      HQASSERT( storepos <= MAXPSNAME, "name length not checked" ) ;

      if ( NULL == (oName(nnewobj) =
                    cachename(storepos > 0 ? thestore : NULL, (uint32)storepos)) )
        return FALSE ;
      if ( c == '/' ) {
        register int32 maxsize ;
        register OBJECT *theo ;

        maxsize = theStackSize( dictstack ) ;
        for ( c = 0 ; c <= maxsize ; ++c )
          if ( (theo = fast_extract_hash(stackindex(c, &dictstack), &nnewobj)) != NULL) {
            scannedObject = TRUE ;
            return push( theo , & operandstack ) ;
          }
        return error_handler( UNDEFINED ) ;
      }
      else {
        scannedObject = TRUE ;
        return push( & nnewobj , & operandstack ) ;
      }
    /* An executable procedure. */
    case '{':
      return scanprocedure( flptr , isfile ) ;

    /* A string ( hexadecimal or normal ). */
    case '(':
      if ( ! scanstring( flptr, isfile ))
        return FALSE ;

      --storepos ;
      HQASSERT( storepos <= MAXPSSTRING, "string length not checked" ) ;

      if ( !ps_string(&newobject, thestore, storepos) )
        return FALSE ;

      scannedObject = TRUE ;
      return push( &newobject , & operandstack ) ;

    case '<':
      if ( theISaveLangLevel( workingsave ) >= 2 ) {
        if (( c = Getc( flptr )) == EOF )
          return file_or_syntax_error (flptr);
        if ( c == '<' ) {
          oName(nnewobje) = system_names + NAME_OpenDict;
          scannedObject = TRUE ;
          return push( & nnewobje , & operandstack ) ;
        }
        else if ( c == '~' ) {
          /* an ASCII85 string */
          if ( ! scanascii85( flptr ))
            return FALSE ;
        }
        else {
          /* a hex string */
          UnGetc( c , flptr );

          if ( ! scanhex( flptr ))
            return FALSE ;
        }
      }
      else { /* Level 1 : a hex string */
        if ( ! scanhex( flptr ))
          return FALSE ;
      }
      HQASSERT( storepos <= MAXPSSTRING, "string length not checked" ) ;

      if ( !ps_string(&newobject, thestore, storepos) )
        return FALSE ;

      scannedObject = TRUE ;
      return push( &newobject , & operandstack ) ;

    /* A mark object */
    case '[':
      oName(nnewobje) = system_names + NAME_OpenArray;
      scannedObject = TRUE ;
      return push( & nnewobje , & operandstack ) ;

    /* An endmark object */
    case ']':
      oName(nnewobje) = system_names + NAME_CloseArray;
      scannedObject = TRUE ;
      return push( & nnewobje , & operandstack ) ;

    case '>':
      mystore1( c ) ;
      if ( theISaveLangLevel( workingsave ) == 1 )
        return error_handler( SYNTAXERROR ) ;
      if (( c = Getc( flptr )) == EOF )
        return file_or_syntax_error (flptr);
      if ( c == '>' ) {
        /* a close dictionary object */
        oName(nnewobje) = system_names + NAME_CloseDict;
        scannedObject = TRUE ;
        return push( &nnewobje , &operandstack ) ;
      }
      return  error_handler( SYNTAXERROR ) ;
    case ')':
    case '}':
      mystore1( c ) ;
      return  error_handler( SYNTAXERROR ) ;
    }
  }

  /* check for binary token */
  else if ( IsBinaryToken( c )) {
    /* look up and call the appropriate scanner function */
#ifdef rs6000
    {
      scanner_func fkscannerfunc;

      fkscannerfunc = (binary_token_table[ ( c ) & 0x7F ]);
    if ( ! (* fkscannerfunc)( c , flptr ))
      return FALSE ;
    }
#else
    if ( ! (* theScannerFunc( c ))( c , flptr ))
      return FALSE ;
#endif
    HQASSERT( scannedBinSequence , "thought we'd scanned a binary object but didn't" ) ;
    if ( ! ( flag_binseq && ( c >= 128 ) && ( c <= 131 ) ) ) {
    /* I really want to check for top-level, to force immediate execution
     * of binary sequences. The binary token was a sequence, so flag that
     * the array must be executed immediately
     */
      scannedObject = TRUE ;
      scannedBinSequence = FALSE ;
    }
    return TRUE ;
  }

  else {
    switch ( c ) {
      /* Perhaps a number of sorts or a name - function returns type. */
    case '-':
      sign = -1 ;
      mystore1( c ) ;
      c = scansign( flptr ) ;
      break ;
    case '+':
      sign = 1 ;
      mystore1( c ) ;
      c = scansign( flptr ) ;
      break ;
    case '.':
      sign = 1 ;
      mystore1( c ) ;
      c = scanperiod( flptr ) ;
      break ;

    /* Must be a name beginning with a weird ( punctuation or
     * non-ascii ) char. */
    default:
      mystore1( c ) ;
      if ( '\000' == ( c = scanname( flptr )))
        return FALSE ;
    }
  }
  /* If scanner gets here, then c is set to the type
   * of result - either aninteger , real , or name.
   */
  if ( c == ONAME ) {
    HQASSERT( storepos <= MAXPSNAME, "name length not been checked" ) ;

    if ( NULL == (oName(nnewobje) =
                  cachename(storepos > 0 ? thestore : NULL, (uint32)storepos)) )
      return FALSE ;
    scannedObject = TRUE ;
    return push( & nnewobje , & operandstack ) ;
  }

  if ( c == OINTEGER ) {
    /* Definitely an integer */
    if ( nleading < 10 ) {
      if ( sign < 0 )
        ileading = -ileading ;
      oInteger(inewobj) = ileading ;
      scannedObject = TRUE ;
      return push( & inewobj , & operandstack ) ;
    }
    /* May be too long for an integer */
    fthenumber = ( sign >= 0 ) ? fleading : -fleading ;
    if ( intrange( fthenumber )) {
      oInteger(inewobj) = (int32)fthenumber ;
      scannedObject = TRUE ;
      return push( & inewobj , & operandstack ) ;
    }
    else {
      static OBJECT real = OBJECT_NOTVM_REAL(OBJECT_0_0F) ;
      SYSTEMVALUE d ;
      int16 dint = 0 ;

      if ( ! realrange( fthenumber ))
        return error_handler( LIMITCHECK ) ;
      oReal(real) = (USERVALUE)fthenumber ;

      /* See if we can extend the precision */
      d = fthenumber - (SYSTEMVALUE)oReal(real) ;
      dint = (int16) d ;
      theLen(real) = (fthenumber < 0 || (SYSTEMVALUE)dint != d) ? 0 :
                     (d < 0) ? (uint16)(0x10000 + d) :
                     (uint16) d ;

      HQASSERT((oReal(real) >= XPF_MINIMUM && oReal(real) <= XPF_MAXIMUM) ||
               theLen(real) == 0, "Scanner made malformed XPF") ;

      scannedObject = TRUE ;
      return push( &real , & operandstack ) ;
    }
  }
  else {
    int32 checkrange = FALSE ;
    int32 checkprecision = FALSE ;

    if ( ntotal < 10 ) {
      if ( sign < 0 )
        ileading = -ileading ;
      fthenumber = ( SYSTEMVALUE )ileading * fdivs[ ntrailing ] ;
    }
    else {
      checkrange = nleading > 10 ;
      if ( ! ntrailing )
        fthenumber = fleading ;
      else if ( ntrailing < 10 )
        fthenumber = fleading * fdivs[ ntrailing ] ;
      else {
        checkprecision = TRUE ;
        fthenumber = fleading / pow( 10.0 , ( SYSTEMVALUE )ntrailing ) ;
      }

      if ( sign < 0 )
        fthenumber = -fthenumber ;
    }

    if ( nexponent > 0 ) {
      checkprecision |= signexp < 0 ;
      checkrange |= signexp > 0 ;

      if ( nexponent < 10 ) {
        if ( signexp < 0 )
          iexponent = -iexponent ;
        fexponent = ( SYSTEMVALUE )iexponent ;
      }
      else {
        if ( signexp < 0 )
          fexponent = -fexponent ;
      }

      fthenumber = fthenumber * pow( 10.0 , fexponent ) ;
    }

    if ( checkrange && ! realrange( fthenumber ))
      return error_handler( LIMITCHECK ) ;

    if ( checkprecision && ! realprecision( fthenumber ))
      fthenumber = 0.0 ;

    oReal(rnewobj) = (USERVALUE)fthenumber ;
    scannedObject = TRUE ;
    return push( & rnewobj , & operandstack ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scanstring(..)     author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure scans the input for a string, that is for a sequence of
   characters that is delimited by a pair of parenthesis.  Any charcaters
   that are preceded by a '\', the escape character, are used for special
   purposes such as including unbalanced parenthesis in the string.

---------------------------------------------------------------------------- */
static Bool scanstring(register FILELIST * flptr, Bool isfile)
{
  register int32 c , temp ;
  register int32 paren_count ;

  HQASSERT( flptr , "flptr NULL in scanstring" ) ;

  paren_count = 1 ;
/*
  Scan the input as a string until  the  opening  parenthesis is balanced
  with a closing parenthesis.  The variable 'last' above is used to store
  the last character read in, so that the escaped  characters may be read
  correctly - ( only when scanning a file ).
*/
  do {
    if (( c = Getc( flptr )) == EOF )
      return file_or_syntax_error (flptr);
/*
  If the 'escape' is followed by an octal number, then it and possibly
  up to two more octal digits  are  read in to form an 8-bit character
  constant in the string - ( only when scanning a file ).
*/
    if (( c == '\\' ) && (( theISaveLangLevel( workingsave ) >= 2 ) || ( isfile ))) {
      if (( c = Getc( flptr )) == EOF )
        return file_or_syntax_error (flptr);

/* Check for an octal number in the string */
      if (( c >= '0' ) && ( c <= '7' )) {
        temp = c - '0' ;
        if (( c = Getc( flptr )) == EOF )
          return file_or_syntax_error (flptr);
        if (( c >= '0' ) && ( c <= '7' )) {
          temp = ( temp << 3 ) + c - '0' ;
          if (( c = Getc( flptr )) == EOF )
            return file_or_syntax_error (flptr);
          if (( c >= '0' ) && ( c <= '7' ))
            temp = ( temp << 3 ) + c - '0' ;
          else
            UnGetc( c , flptr ) ;
        }
        else
          UnGetc( c , flptr ) ;
        mystoreS( temp ) ;
        continue ;
      }
      else
        switch ( c ) {
        case 'n':  c = LF ;  break ;  /* linefeed           */
        case 'r':  c = CR ;  break ;  /* carriage return    */
        case 't':  c = '\t'; break ;  /* horizontal tab     */
        case 'b':  c = '\b'; break ;  /* backspace          */
        case 'f':  c = '\f'; break ;  /* formfeed           */
        case '\\':  c = '\\'; break ; /* backslash          */
        case '(':  c = '(' ; break ;  /* left parenthesis   */
        case ')':  c = ')' ; break ;  /* right parenthesis  */
        case CR : /* no character - newline ignored, but is a character in
                      the input file */
          /* check for CR-LF pair */
          if (( c = Getc( flptr )) == EOF )
            return file_or_syntax_error (flptr);
          if ( c != LF )
            UnGetc( c , flptr ) ;
        case LF :
          c = 0;
          inc_line_num( flptr );
          break;
        }
      if ( ! ( c == 0 ))
        mystoreS( c ) ;
      continue ;
    }
    else {
      switch ( c ) {
      case '(':
        ++paren_count;
        break;

      case ')':
        --paren_count;
        break;
      case CR :
        /* any sort of newline must be converted to a linefeed in strings */
        /* check for CR-LF pair */
        if (( c = Getc( flptr )) == EOF )
          return file_or_syntax_error (flptr);
        if ( c != LF ) {
          UnGetc( c , flptr ) ;
          c = LF ;
        }
      case LF :
        inc_line_num( flptr );
        break;
      }
      mystoreS( c ) ;
    }
  }  while ( paren_count > 0 ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            scanname(..)       author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure scans either a name or number from the input. The
   returned value of flag indicates the type of the name or number.

---------------------------------------------------------------------------- */
static int32 scanname(register FILELIST * flptr)
{
  register int32 c ;

  HQASSERT( flptr , "flptr NULL in scanname" ) ;

  for (;;) {
    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr )) (flptr) ;
      else
        return ONAME ;
    }
/*
  Keep scanning characters  and  constructing the name until  we  reach either
  white-space, or a special character, '(',')','<','>','[',']','{','}' or '%'.
  These charcters delimit  the  name object  from the next one  in  the input.
*/
    if ( IsEndMarkerPS( c )) {
      if ( IsEndOfLine( c )) {
        if ( c == CR ) {
          prov_inc_line_num( flptr ) ;
          SetICRFlags( flptr ) ;
        }
        else { /* it is LF */
          if ( isICRFlags( flptr ))
            ClearICRFlags( flptr ) ;
          else
            prov_inc_line_num( flptr ) ;
        }
        return ONAME ;
      }
      else if (! IsWhiteSpace(c))
        UnGetc( c , flptr ) ;
      return ONAME ;
    }
    mystoreN( c ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scanhex(..)        author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure scans the input for a hexadecimal string. If any white-
   space characters are encountered, then they are ignored.  If any other
   characters are encountered, then an error occurs.

---------------------------------------------------------------------------- */
static Bool scanhex(register FILELIST *flptr)
{
  register int32 c , r ;
  register int32 flag ;
  register int32 hexvalue ;
  register int8 *lhex_table ;

  HQASSERT( flptr , "flptr NULL in scanhex" ) ;

  flag = 0 ;
  hexvalue = 0 ;
  lhex_table = char_to_hex_nibble ;

  for (;;) {
    if (( c = Getc( flptr )) == EOF )
      return file_or_syntax_error (flptr);

    /* Check for hex-digit, and if so store value in hex_value. */
    r = lhex_table[ c ] ;
    /*
     * next line is a hack: the invalid chars have -1 in the table, this is
     * extended to -1 or 255 as an integer. Doing an unsigned comparison
     * checks for both of these since (unsigned)-1 == 0xffffffff.
     */
    if ((r & ~0xf) == 0 ) {
      ++flag ;   /* flag stores the number of digits read in. */
      hexvalue = ( hexvalue << 4 ) + r ;
    }
    /* Whoops - one too many, end of hex-string. */
    else
      switch ( c ) {

      case '>':
        /* If odd number of hex-digits , then the last one is taken
           to be zero. */
        if ( flag == 1 ) {
          mystoreS( hexvalue << 4 ) ;
        }
        return TRUE ;

        /* White-space is ignored in the hex-string, other characters
           are illegal. */
      case ' ':
      case '\t':
      case '\f':  /* formfeed */
      case '\0':
        break ;
      case CR :
        /* check for CR-LF pair */
        if (( c = Getc( flptr )) == EOF )
          return file_or_syntax_error (flptr);
        if ( c != LF ) {
          UnGetc( c , flptr ) ;
          c = CR ;
        }
      case LF :
        inc_line_num( flptr );
        break;
      default:
        if ( flag == 1 )
          mystoreS( hexvalue << 4 ) ;
        mystoreS( c ) ;
        return error_handler( SYNTAXERROR ) ;
      }

    /* After reading in two hex-digits, store their character. */
    if ( flag == 2 ) {
      mystoreS( hexvalue ) ;
      hexvalue = flag = 0 ;    /* Reset flags to indicate no hex-digit yet. */
    }
  }
}





/* ----------------------------------------------------------------------------
   function:            scan_ascii85      author:              Luke Tunmer
   creation date:       14-Aug-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool scanascii85(register FILELIST *flptr)
{
  register int32 c , i , j ;
  FOURBYTES fb ;

  HQASSERT( flptr , "flptr NULL in scanascii85" ) ;

  i = 0 ; /* count modulo 5 */
  asInt( fb ) = 0 ;
  for (;;) {
    if (( c = Getc( flptr )) == EOF )
      return file_or_syntax_error (flptr);

    if ( c == '~' ) { /* end of string marker */
      if (( c = Getc( flptr )) != '>' )
        return error_handler( SYNTAXERROR ) ;
      if ( i > 0 ) {  /* partial 5-tuple read in */
        if ( i == 1 )
          return error_handler( SYNTAXERROR ) ;
        if ( asInt( fb ) > MAXHIGH4BYTES )
          return error_handler( SYNTAXERROR ) ;
        asInt( fb ) = POWER4 * (int32)asBytes(fb)[BYTE_INDEX(0)] +
          POWER3 * (int32)asBytes(fb)[BYTE_INDEX(1)] +
            POWER2 * (int32)asBytes(fb)[BYTE_INDEX(2)] +
              POWER1 * (int32)asBytes(fb)[BYTE_INDEX(3)] ;
        if ( (int32)asBytes(fb)[ BYTE_INDEX(i - 1) ] >= 128 )
          /* carry one over */
          asInt(fb) += 1 << (( 4 - i + 1 ) * 8 ) ;
        HighOrder4Bytes( asBytes(fb) ) ;
        i-- ;
        for ( j = 0 ; j < i ; j++ )
          mystoreS( (int32)asBytes(fb)[ j ] ) ;
      }
      return TRUE ;
    }
    if ( ! IsWhiteSpace( c )) {
      if ( c == 'z') {
        if ( i != 0 ) {
          return error_handler( IOERROR ) ;
        }
        for ( j = 0 ; j < 4 ; j++ ) {
          mystoreS( 0 ) ;
        }
      }
      else if (( c < '!') || ( c > 'u')) {
        return error_handler( SYNTAXERROR ) ;
      }
      else {
        if ( i == 4 ) {
          if ( asInt( fb ) > MAXHIGH4BYTES )
            return error_handler( SYNTAXERROR ) ;
          asInt( fb ) = POWER4 * (int32)asBytes(fb)[BYTE_INDEX(0)] +
            POWER3 * (int32)asBytes(fb)[BYTE_INDEX(1)] +
            POWER2 * (int32)asBytes(fb)[BYTE_INDEX(2)] +
            POWER1 * (int32)asBytes(fb)[BYTE_INDEX(3)] + c - 33 ;
          HighOrder4Bytes( asBytes(fb) ) ;
          for ( j = 0 ; j < 4 ; j++ ) {
            c = asBytes(fb)[ j ] ;
            mystoreS( c ) ;
          }
          i = 0 ;
          asInt( fb ) = 0 ;
        }
        else {
          asBytes(fb)[BYTE_INDEX(i++)] = (uint8) ( c - 33 );
        }
      }
    }
  }
}




/**
   This procedure  scans the input for a PostScript procedure.  That  is  a
   sequence of objects delimited by  a pair of parenthesis ('{','}').  This
   is done by repeatedly calling the main scanner (which leaves  an  object
   on the top of the operand stack) until the end parentheis is reached. As
   the other  objects  in  the  PostScript procedure  are  scanned they are
   removed from  the  operand stack  and  stored in temporary objects. When
   reaching the end parenthesis, these temporary objects are then stored in
   an array object, which is set up on the top of the operand stack.
*/
static Bool scanprocedure(register FILELIST *flptr, Bool isfile)
{
  register int32 c = 0 ; /* silence compiler */
  register int32 count ;
  register OBJECT *theo, *olist ;
  OBJECT newobject = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT( flptr , "flptr NULL in scanprocedure" ) ;

  count = 0;

  for (;;) {
    for (;;) {
      if (( c = Getc( flptr )) == EOF ) {
        /* Must obtain closing parenthesis '}', before reaching end-of-file. */
        npop (count, & operandstack);
        return file_or_syntax_error (flptr);
      }

      if ( IsWhiteSpace( c ) ) {
        if ( c == CR ) {
          SetICRFlags( flptr ) ;
          inc_line_num( flptr ) ;
        } else if ( c == LF ) {
          if ( isICRFlags( flptr ))
            ClearICRFlags( flptr ) ;
          else
            inc_line_num( flptr ) ;
        } else {
          ClearICRFlags( flptr ) ;
        }
      }
      else if ( c == '%' ) {
        ClearICRFlags( flptr ) ;
        not_at_start_of_line( flptr ) ;
        /* Never scan comments within a procedure */
        if ( ! handle_comments( flptr , FALSE ) )
          return FALSE ;
      }
      else {
        /* We like this character, use it */
        break;
      }
      /* We didn't like that character, loop again to get another */
    }
    ClearICRFlags( flptr ) ;

    if ( c == '}' )
      break;

    /* Whoops - One too many - So put it back. */
    UnGetc( c , flptr ) ;

    if (count >= MAXPSARRAY) {
      npop (count, & operandstack);
      return error_handler( RANGECHECK ) ;
    }

    /* Recursively call scanner(..), to obtain the next object.  This remains
       on the operand stack until we've got all we want - encountering the '}'
     */

    if ( ! scanner( flptr , FALSE , FALSE, isfile )) {
      npop (count, & operandstack);
      return FALSE ;
    }

    HQASSERT( scannedObject ^
              scannedBinSequence ,
              "thought we'd scanned an object but didn't" ) ;

    count++; /* there is now one more on the operand stack */

    /* double slash could provoke a local into global condition, so check it */
    if (corecontext->glallocmode &&
        illegalLocalIntoGlobal(theTop(operandstack), corecontext) )
    {
      npop (count, & operandstack);
      return error_handler( INVALIDACCESS ) ;
    }
  }

  /* we have now got count objects on the operand stack to go into the array -
     we know it will fit - that test was done inside the loop */
  if ( theIPacking( workingsave ))
    theTags( newobject ) = OPACKEDARRAY | EXECUTABLE | READ_ONLY ;
  else
    theTags( newobject ) = OARRAY | EXECUTABLE | UNLIMITED ;
  SETGLOBJECT(newobject, corecontext) ;
  theLen( newobject ) = (uint16) count ;
  if (count == 0)
    oArray(newobject) = NULL;
  else if ( NULL == (oArray(newobject) = get_omemory( count ))) {
    npop(count, & operandstack);
    return error_handler(VMERROR) ;
  }

  olist = oArray(newobject) + count;
  while (--count >= 0) {
    --olist;
    theo = theTop(operandstack);
    Copy(olist, theo);
    pop(& operandstack);
  }

  scannedObject = TRUE ;
  HQASSERT( !scannedBinSequence ,
            "flagging scanned object when already flagged binary sequence" ) ;
  return push( & newobject , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            scandigits()       author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static Bool flshdigits(FILELIST *flptr)
{
  register int32 c ;
  register int32 ccount = storepos ;

  HQASSERT( flptr , "flptr NULL in flshdigits" ) ;

  while ( ccount <= MAXPSSTRING ) {
    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr )) (flptr) ;
      else
        return error_handler( LIMITCHECK ) ;
    }
/*
  Keep scanning characters until  we  reach either white-space, or a special
  character, '(',')','<','>','[',']','{','}' or '%'.
  These charcters delimit the flushed object  from the next one in the input.
*/
    if ( IsEndMarkerPS( c )) {
      if ( IsEndOfLine( c )) {
        if ( c == CR ) {
          prov_inc_line_num( flptr ) ;
          SetICRFlags( flptr ) ;
        }
        else { /* it is LF */
          if ( isICRFlags( flptr ))
            ClearICRFlags( flptr ) ;
          else
            prov_inc_line_num( flptr ) ;
        }
        return error_handler( LIMITCHECK ) ;
      }
      else if (! IsWhiteSpace(c))
        UnGetc( c , flptr ) ;
      return error_handler( LIMITCHECK ) ;
    }
    ++ccount;
  }
  return error_handler( LIMITCHECK ) ;
}

static int32 scandigits(FILELIST *flptr)
{
  register int32 c ;
  register int32 temp ;

  HQASSERT( flptr , "flptr NULL in scandigits" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return OINTEGER ;
  }

  while ( isdigit( c )) {
    mystoreL( c ) ;

    ++nleading ;
    c = c - '0' ;
    if ( nleading < 10 ) {
      temp = ileading ;
      ileading = ( temp << 3 ) + ( temp << 1 ) + c ;
    }
    else if ( nleading > 10 )
      fleading = ( fleading * 10.0 ) + c ;
    else
      fleading = ( ileading * 10.0 ) + c ;

    /* Special case here; if we're scanning a number (N.M) and we run out of
     * space then as long as the number fits into a signed integer we're ok.
     * Otherwise we scan to end of token.
     * This is what Adobe do; don't ask me why.
     */
    if ( storepos > MAXPSNAME ) {
      SYSTEMVALUE tmp ;
      tmp = ( nleading < 10 ) ?
             (( sign >= 0 ) ?
              ( SYSTEMVALUE )( ileading ) :
              ( SYSTEMVALUE )( -ileading )) :
             (( sign >= 0 ) ?
              ( fleading ) :
              ( -fleading )) ;
      if ( ! intrange( tmp ))
        return flshdigits( flptr ) ;
    }

    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr )) (flptr) ;
      else
        return OINTEGER ;
    }
  }
  switch ( c ) {

  case '.':
    ntrailing = 0 ;
    ntotal = nleading ;
    nexponent = 0 ;
    mystoreN( c ) ;
    return scanpdigits( flptr ) ;

  case 'e':
  case 'E':
    ntrailing = 0 ;
    ntotal = nleading ;
    nexponent = 0 ;
    mystoreN( c ) ;
    return scanexponent( flptr ) ;

  case CR :
    SetICRFlags( flptr ) ;
    prov_inc_line_num( flptr ) ;
    return OINTEGER ;

  case LF :
    prov_inc_line_num( flptr ) ;
    ClearICRFlags( flptr ) ;
    return OINTEGER ;

  case '#':
    if ( nleading < 10 )
      if ( sign > 0 )
        if (( ileading >= 2 ) && ( ileading <= 36 )) {
          thebase = ileading ;
          mystoreN( c ) ;
          return scanbase( flptr ) ;
        }
  /* DROP THROUGH */
  default:
    if ( IsEndMarkerPS( c )) {
      if (! IsWhiteSpace( c ))
        ( void ) UnGetc( c , flptr ) ;
      return OINTEGER ;
    }
    else {
      mystoreN( c ) ;
      return scanname( flptr ) ;
    }
  }
}

/* ----------------------------------------------------------------------------
   function:            scansign(..)       author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scansign(register FILELIST *flptr)
{
  int32 c ;

  HQASSERT( flptr , "flptr NULL in scansign" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return ONAME ;
  }

  if ( isdigit( c )) {
    nleading = 1 ;
    ileading = c - '0' ;
    mystoreN( c ) ;
    return scandigits( flptr ) ;
  }
  else if ( c == '.' ) {
    mystoreN( c ) ;
    return scanperiod( flptr ) ;
  }
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if ( ! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return ONAME ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scanperiod(..)     author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanperiod(register FILELIST *flptr)
{
  int32 c ;

  HQASSERT( flptr , "flptr NULL in scanperiod" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return ONAME ;
  }

  if ( isdigit( c )) {
    nleading = 0 ;
    ntrailing = 1 ;
    ntotal = 1 ;
    nexponent = 0 ;
    ileading = c - '0' ;
    mystoreN( c ) ;
    return scanpdigits( flptr ) ;
  }
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if (! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return ONAME ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

/* There is no overflow protection on value accumulated during parsing
 * integer values. It is necessary to limit the number of digits
 * considered by the parser, so that the largest number representable will
 * not cause overflow of the data types used to hold intermediate values.
 * NB. This limit used to be the maximum name length, but since that
 * increased, this parsing limit is now independent of name length. See 367306.  */
#define MAXDIGITS (127)

/* ----------------------------------------------------------------------------
   function:            scanpdigits(..)    author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanpdigits(register FILELIST *flptr)
{
  register int32 c ;
  register int32 temp ;

  HQASSERT( flptr , "flptr NULL in scanpdigits" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return OREAL ;
  }

  while ( isdigit( c )) {
    mystoreL( c ) ;

    ++ntrailing ;
    ++ntotal ;
    c = c - '0' ;
    if ( ntotal < 10 ) {
      temp = ileading ;
      ileading = ( temp << 3 ) + ( temp << 1 ) + c ;
    }
    else if ( ntotal > 10 )
      fleading = ( fleading * 10.0 ) + c ;
    else
      fleading = ( ileading * 10.0 ) + c ;

    /* Special case here; if we're scanning a number (N.M) and we run out of
     * space then as long as the number fits into a signed integer we're ok.
     * Otherwise we scan to end of token. Also, > 9 dp is an error too.
     * This is what Adobe do; don't ask me why.
     */
    if ( storepos > MAXDIGITS ) {
      SYSTEMVALUE tmp ;
      if ( ntrailing > 9 )      /* Implies about to add the 10th character. */
        return flshdigits( flptr ) ;
      tmp = ( ntotal < 10 ) ?
             (( sign >= 0 ) ?
              ( SYSTEMVALUE )( ileading ) :
              ( SYSTEMVALUE )( -ileading )) :
             (( sign >= 0 ) ?
              ( fleading ) :
              ( -fleading )) ;
      if ( ! intrange( tmp ))
        return flshdigits( flptr ) ;
    }

    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr )) (flptr) ;
      else
        return OREAL ;
    }
  }

  if (( c == 'e' ) || ( c == 'E' )) {
    mystoreN( c ) ;
    return scanexponent( flptr ) ;
  }
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if (! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return OREAL ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

#undef MAXDIGITS

/* ----------------------------------------------------------------------------
   function:            scanexponent(..)   author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanexponent(register FILELIST *flptr)
{
  register int32 c ;

  HQASSERT( flptr , "flptr NULL in scanexponent" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return ONAME ;
  }

  signexp = 1 ;
  if ( isdigit( c )) {
    nexponent = 1 ;
    iexponent = c - '0' ;
    mystoreN( c ) ;
    return scansexponent( flptr ) ;
  }
  else if ( c == '-' ) {
    signexp = -1 ;
    mystoreN( c ) ;
    return scanesign( flptr ) ;
  }
  else if ( c == '+' ) {
    mystoreN( c ) ;
    return scanesign( flptr ) ;
  }
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if ( ! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return ONAME ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scanesign(..)      author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanesign(register FILELIST *flptr)
{
  int32 c ;

  HQASSERT( flptr , "flptr NULL in scanesign" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return ONAME ;
  }

  if ( isdigit( c )) {
    nexponent = 1 ;
    iexponent = c - '0' ;
    mystoreN( c ) ;
    return scansexponent( flptr ) ;
  }
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if ( ! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return ONAME ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scansexponent(..)  author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scansexponent(register FILELIST *flptr)
{
  register int32 c ;
  register int32 temp ;

  HQASSERT( flptr , "flptr NULL in scansexponent" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return OREAL ;
  }

  while ( isdigit( c )) {
    mystoreN( c ) ;

    ++nexponent ;
    c = c - '0' ;
    if ( nexponent < 10 ) {
      temp = iexponent ;
      iexponent = ( temp << 3 ) + ( temp << 1 ) + c ;
    }
    else if ( nexponent > 10 )
      fexponent = ( fexponent * 10.0 ) + c ;
    else
      fexponent = ( iexponent * 10.0 ) + c ;
    if (( c = Getc( flptr )) == EOF ) {
      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr )) (flptr) ;
      else
        return OREAL ;
    }
  }

  if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if ( ! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return OREAL ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            scanbase(..)       author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanbase(register FILELIST *flptr)
{
  int32 c ;

  HQASSERT( flptr , "flptr NULL in scanbase" ) ;

  if (( c = Getc( flptr )) == EOF ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr )) (flptr) ;
    else
      return ONAME ;
  }

  if ( isdigit( c ))
    ileading = c - '0' ;
  else if ( isalpha( c ))
    if ( islower( c ))
      ileading = c - 'a' + 10 ;
    else
      ileading = c - 'A' + 10 ;
  else if ( IsEndMarkerPS( c )) {
    if ( c == CR ) {
      SetICRFlags( flptr ) ;
      prov_inc_line_num( flptr ) ;
    }
    else if ( c == LF ) {
      prov_inc_line_num( flptr ) ;
    }
    else if ( ! IsWhiteSpace( c ))
      ( void ) UnGetc( c , flptr ) ;
    return ONAME ;
  }
  else {
    mystoreN( c ) ;
    return scanname( flptr ) ;
  }
  mystoreN( c ) ;
  if ( (uint32)ileading < thebase ) {
    nleading = 1 ;
    return scanbdigits( flptr ) ;
  }
  else
    return scanname( flptr ) ;
}

/* ----------------------------------------------------------------------------
   function:            scanbdigits(..)    author:              Andrew Cave
   creation date:       21-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure is part of the F.S.M. for scanning either numbers or names.

---------------------------------------------------------------------------- */
static int32 scanbdigits(register FILELIST *flptr)
{
  register int32 c ;
  register uint32 uleading , umax , temp ;

  HQASSERT( flptr , "flptr NULL in scanbdigits" ) ;

  uleading = ( uint32 ) ileading ;
  umax = thebase == 10 ? 0x7fffffff : 0xffffffff ;

  while (( c = Getc( flptr )) != EOF ) {

    if ( isdigit( c )) {
      mystoreN( c ) ;
      temp = c - '0' ;
    }
    else if ( isalpha( c )) {
      mystoreN( c ) ;
      if ( islower( c ))
        temp = c - 'a' + 10 ;
      else
        temp = c - 'A' + 10 ;
    }
    else if ( IsEndMarkerPS( c )) {
      if ( c == CR ) {
        SetICRFlags( flptr ) ;
        prov_inc_line_num( flptr ) ;
      }
      else if ( c == LF ) {
        prov_inc_line_num( flptr ) ;
      }
      else if ( ! IsWhiteSpace( c ))
        ( void ) UnGetc( c , flptr ) ;
      break ;
    }
    else {
      mystoreN( c ) ;
      return scanname( flptr ) ;
    }

    if ( temp < thebase ) {
      if ( nleading >= 10 ) {
        /* for base 10 only - others won't get this far */
        fleading = ( fleading * thebase ) + temp ;
      }
      else if ( uleading <= (( umax - temp ) / thebase )) {
        /* next result will still fit happily into an UNSIGNED integer in
           the non base 10 case, or a SIGNED integer in the base 10 case
           (umax is set accordingly) */
        uleading = ( uleading * thebase ) + temp ;
      }
      else if ( thebase == 10 ) {
        /* base 10 numbers convert to a float when they get too big. Note that
           while this is true also on the LaserWriter in the simple case,
           when the number is preceded by 10#... the result overflows, which
           is a bug. We implememnt it here the same as the straightforward
           decimal integer */
        fleading = ( uleading * thebase ) + temp ;
        nleading = 10 ; /* i.e. overflow occurred into a float */
      }
      else {
        /* non-decimal numbers are interpreted as a name if they
           overflow an integer (as per LaserWriter) */
        return scanname( flptr ) ;
      }
    }
    else
      return scanname( flptr ) ;
  }
  ileading = ( int32 ) uleading ; /* only used in the decimal case if
                                   overflow into fleading didn't happen;
                                   otherwise, this may mean that ileading
                                   is negative in the non-decimal case;
                                   however, sign is still + so end result
                                   is negative */
  return OINTEGER ;
}






/* Log stripped */
