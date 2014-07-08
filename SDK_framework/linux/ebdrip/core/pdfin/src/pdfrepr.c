/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfrepr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Repair code
 */


#include "core.h"
#include "swctype.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "monitor.h"
#include "hqmemcmp.h"

#include "devices.h"  /* For device_error_handler */
#include "chartype.h"
#include "fileio.h"

#include "swpdf.h"
#include "pdfin.h"
#include "pdfscan.h"
#include "pdfncryp.h"
#include "pdfexec.h"
#include "pdfxref.h"
#include "pdfmem.h"

static Bool pdf_repair_scan_object_id( uint8 *linebuf , uint8 *lineend ,
                                        int32 *objnum , uint16 *objgen ) ;
static int32 pdf_repair_scan_xreftab( uint8 *linebuf , uint8 *lineend ,
                                      int32 *objnum , int32 *number ) ;
static int32 pdf_repair_scan_xrefobj( uint8 *linebuf , uint8 *lineend ,
                                      uint16 *objgen , uint8 *objuse ) ;
static int32 pdf_repair_skipwhitespace( uint8 *linebuf , uint8 *lineend ) ;

#if defined( ASSERT_BUILD )
static Bool pdftrace_repair = FALSE ;
#endif

#define OBJECT_ID_MIN 7
#define XREF_LEN 4
#define TRAILER_LEN 7
#define ENDOBJ_LEN 6

enum {
  REPAIR_INIT_STATE ,
  REPAIR_XREF_STATE ,
  REPAIR_XREFOBJ_STATE ,
  REPAIR_TRAILER_STATE ,
  REPAIR_OBJECT_STATE
} ;

/* This function is based on a number of assumptions about the way PDF
 * files are structured. For jobs that have been updated, and
 * therefore have multiple xrefsections, it is assumed that the
 * updates are applied at the end of the job ; that the xreftables
 * apply to all the objects occurring before the xref table, with the
 * obj use field overriding previous values for a particular xref
 * object.
 */

Bool pdf_repair( PDFCONTEXT *pdfc , FILELIST *flptr )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  DEVICELIST *dev ;
#define BUFFER_SIZE 256
  uint8 linebuf[ BUFFER_SIZE ] ;
  uint8 *lineptr , *lineend , objuse = 0 ;
  uint16 objgen = 0u;
  int32 objnum = 0, number , fstobj ;
  int32 state , partial_line , n , whitespace ;
  Bool result ;
  XREFOBJ *xrefobj ;
  OBJECT encrypt = OBJECT_NOTVM_NULL ;
  OBJECT id = OBJECT_NOTVM_NULL ;
  Hq32x2 offset;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;
  HQASSERT( flptr , "flptr is null in pdf_repair" ) ;

  if ( ixc->ErrorOnPDFRepair )
  {
    monitorf( ( uint8 * )"Corrupt PDF file detected but not repairing.\n" ) ;
    return FALSE;
  }

  if ( ixc->repaired || ixc->strictpdf )
    /* Already repaired it once or running strict mode, thats all folks! */
    return FALSE ; /* Do not override error already setup. */

  /* Now give a nice message to the user. */
  monitorf( ( uint8 * )"Corrupt PDF file detected, attempting repair.\n" ) ;

  /* xrefsections etc could be any state, so free them and start again. */
  pdf_flushxrefsec( pdfc ) ;
  Hq32x2FromInt32(&offset, 1) ; /* xrefsec offset unknown. */
  pdfxc->xrefsec = pdf_allocxrefsec( pdfc , offset ) ;
  if ( pdfxc->xrefsec == NULL )
    return error_handler( VMERROR ) ;

  dev = theIDeviceList( flptr ) ;
  HQASSERT( dev , "dev is null in pdf_repair" ) ;

  if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

  if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                 & pdfxc->filepos , SW_SET ))
    return device_error_handler( dev ) ;

  /* Initialisation. */
  lineptr = linebuf ;
  lineend = lineptr + BUFFER_SIZE ;

  state = REPAIR_INIT_STATE ;
  partial_line = FALSE ;
  n = 0 ;
  whitespace = 0 ;

  fstobj = 0 ;
  number = 0 ;

  result = TRUE ;

  while ( result ) {
    Bool doscan = TRUE ;

    if ( state == REPAIR_INIT_STATE || state == REPAIR_XREFOBJ_STATE) {
      if ( (*theIMyFilePos(flptr))(flptr, &offset) == EOF ) {
        result = error_handler( IOERROR ) ;
        break ;
      }
    }

    if ( state != REPAIR_TRAILER_STATE ) {
      /* Read line by line, except when in trailer state when the
       * trailer dictionary is read.
       */
      n = pdf_readdata_delimited( flptr , linebuf , linebuf + BUFFER_SIZE ) ;

      doscan = FALSE ;
      if ( n == EOF ) {
        /* Line too long to fit in the buffer. */
        if ( state == REPAIR_XREF_STATE ||
             state == REPAIR_XREFOBJ_STATE )
          /* Must not have a line too long when in either of these states. */
          result = error_handler( SYNTAXERROR ) ;
        /* Skip line - too long for an object id or xref table. */
        partial_line = TRUE ;
      }
      else {
        if ( n == 0 ) {
          /* Empty line. */
          int32 ch = Getc( flptr ) ;
          if ( ch == EOF )
            break ; /* Really is EOF. */
          UnGetc( ch , flptr ) ;
        }
        else if ( ! partial_line ) {
          /* Got a complete line that fits in the buffer. Skip over
           * whitespace and scan for objects and xref tables. */
          lineend = linebuf + n ;
          whitespace = pdf_repair_skipwhitespace( linebuf , lineend ) ;
          lineptr = linebuf + whitespace ;
          n -= whitespace ;
          doscan = TRUE ;
        }
        partial_line = FALSE ;
      }
    } else {
      /* We've had a line and detected the 'trailer' keyword. Due to the
         possibility of the trailer keyword having some of the actual trailer
         dictionary on the same line, re-read this line being careful to stop
         at the opening '<<' of the trailer dict. */
      Hq32x2 file_pos;
      if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
        return (*theIFileLastError( flptr ))( flptr ) ;
      file_pos = offset ;
      if (!(*theISeekFile( dev ))( dev, theIDescriptor(flptr), &file_pos, SW_SET ))
        return device_error_handler( dev );

      n = pdf_readdata_delimited( flptr, linebuf, linebuf + BUFFER_SIZE );
    }

    if ( doscan ) {
      /* In trailer state or have a line no more than BUFFER_SIZE length. */

      switch ( state ) {
      case REPAIR_INIT_STATE :
        /* Try to scan an object id. */
        if ( n >= OBJECT_ID_MIN &&
             pdf_repair_scan_object_id( lineptr , lineend , & objnum , & objgen )) {
          Hq32x2AddInt32( &offset, &offset, whitespace) ; /* Update the object offset, to skip whitespace. */
          HQTRACE( pdftrace_repair , ("obj %d %d, offset %d\n" , objnum , objgen , offset )) ;
          state = REPAIR_OBJECT_STATE ;
        }
        /* Failed to scan an object id, try "trailer". */
        else if ( n >= TRAILER_LEN &&
                  HqMemCmp( lineptr , TRAILER_LEN ,
                            ( uint8 * ) "trailer" , TRAILER_LEN ) == 0 )
          state = REPAIR_TRAILER_STATE ;
        /* Failed to scan "trailer", try "xref". */
        else if ( n >= XREF_LEN &&
                  ( HqMemCmp( lineptr , XREF_LEN ,
                              ( uint8 * ) "xref" , XREF_LEN ) == 0 ))
          state = REPAIR_XREF_STATE ;
        break ; /* did not recognise anything this time around. */
      case REPAIR_XREFOBJ_STATE :
        /* Found "xref" and '%d %d', now look for xrefobjs of the form
         * '%d %d [n|f]'. It is necessary to try to scan the xref
         * tables in case an object has been marked free.
         */
        if ( pdf_repair_scan_xrefobj( lineptr , lineend , & objgen , & objuse )) {
          result = pdf_getxrefobj( pdfc , objnum , objgen , & xrefobj ) &&
                   pdf_setxrefobjuse( xrefobj , objgen , objuse ) ;
          ++objnum ;
          break ;
        }
        /* fall thro */
      case REPAIR_XREF_STATE :
        /* Found "xref", now look for "trailer" or xref objects that look like '%d %d'. */
        if ( pdf_repair_scan_xreftab( lineptr , lineend , & fstobj , & number )) {
          objnum = fstobj ;
          state = REPAIR_XREFOBJ_STATE ;
        }
        else if ( HqMemCmp( lineptr , TRAILER_LEN ,
                            ( uint8 * ) "trailer" , TRAILER_LEN ) == 0 )
          state = REPAIR_TRAILER_STATE ;
        break ;
      case REPAIR_TRAILER_STATE :
        /* Read the trailer dictionary for the root object, and the
         * info and encryption dictionaries. */
        result = pdf_read_trailerdict( pdfc , flptr , & encrypt , & id ) ;
        state = REPAIR_INIT_STATE ;
        break ;
      case REPAIR_OBJECT_STATE :
        /* Look for "endobj" */
        if ( n == ENDOBJ_LEN &&
             ( HqMemCmp( lineptr , ENDOBJ_LEN ,
                         ( uint8 * ) "endobj" , ENDOBJ_LEN ) == 0 )) {
          /* Scanned an object... */
          result = pdf_getxrefobj( pdfc , objnum , objgen , & xrefobj ) &&
                   pdf_setxrefobjoffset( xrefobj , offset , objgen ) ;
          state = REPAIR_INIT_STATE ;
        }
        break ;
      default :
        HQFAIL( "Bizzare repair state in pdf_repair" ) ;
        result = FALSE ;
      }
    }
  }

  if ( state != REPAIR_INIT_STATE ) {
    /* Must finish back in the init state. */
    result = error_handler( SYNTAXERROR ) ;
  }

  if ( result ) {
    /* Must have picked up a root object by now. */
    if ( oType( ixc->pdfroot ) != OINDIRECT )
      result = error_handler( TYPECHECK ) ;
    ClearIEofFlag( flptr ) ;
    ixc->repaired = TRUE ;
  }

  /* Start decryption, if not already done so. */
  if ( result &&
       oType( encrypt ) != ONULL &&
       pdfxc->crypt_info == NULL ) {
    HQASSERT( oType( encrypt ) == ODICTIONARY ||
              oType( encrypt ) == OINDIRECT ,
              "encrypt is of unexpected type in pdf_repair" ) ;
    if ( oType( id ) != OARRAY &&
         oType( id ) != OPACKEDARRAY )
      /* Got an encryption dictionary, but no file id. */
      result = error_handler( TYPECHECK ) ;
    else {
      result = pdf_begin_decryption(pdfc, &encrypt, &id) ;
    }
  }

  result = (result && pdf_validate_info_dict(pdfc));

  /* Free encryption dictionary and id array if necessary. */
  if ( oType( encrypt ) == ODICTIONARY )
    pdf_freeobject( pdfc , & encrypt ) ;
  if ( oType( id ) == OARRAY ||
       oType( id ) == OPACKEDARRAY )
    pdf_freeobject( pdfc , & id ) ;
  return result ;
}

/* Takes a line and scans for an object id, objnum objgen "obj",
 * allowing for whitespace between the items and comments at the
 * end.
 */

static Bool pdf_repair_scan_object_id( uint8 *linebuf , uint8 *lineend ,
                                       int32 *objnum , uint16 *objgen )
{
  uint8 *lineptr = linebuf ;
  uint8 *endptr ;
  int32 tmp ;

#define OBJ_TOKEN_LEN 3

  HQASSERT( linebuf , "linebuf is null in pdf_repair_scan_object_id" ) ;
  HQASSERT( lineend , "lineend is null in pdf_repair_scan_object_id" ) ;
  HQASSERT( objnum , "objnum is null in pdf_repair_scan_object_id" ) ;
  HQASSERT( objgen , "objgen null in pdf_repair_scan_object_id" ) ;

  /* Find the next space after the potential object number. */
  for ( endptr = lineptr ; endptr < lineend ; ++endptr )
    if ( *endptr == ' ' )
      break ;

  if ( lineptr == endptr || endptr == lineend || endptr - lineptr > 10 )
    return FALSE ;

  /* Scan object number. */
  if ( ! pdf_scan_integer( lineptr , endptr , objnum ))
    return FALSE ;

  lineptr = endptr + 1 ;
  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Find the next space after the potential object generation. */
  for ( endptr = lineptr ; endptr < lineend ; ++endptr )
    if ( *endptr == ' ' )
      break ;

  if ( lineptr == endptr || endptr == lineend || endptr - lineptr > 5 )
    return FALSE ;

  /* Scan object generation. */
  if ( ! pdf_scan_integer( lineptr , endptr , & tmp ))
    return FALSE ;
  *objgen = ( uint16 ) tmp ;

  lineptr = endptr + 1 ;
  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Scan "obj" token. */
  if ( lineend - lineptr < OBJ_TOKEN_LEN ||
       ( HqMemCmp( lineptr , OBJ_TOKEN_LEN ,
                   ( uint8 * )"obj" , OBJ_TOKEN_LEN ) != 0 ))
    return FALSE ;

  lineptr += OBJ_TOKEN_LEN ;

  return TRUE ;
}

/* Assume of the form '%d %d', with any amount of whitespace before,
 * after or inbetween the numbers.
 */

static int32 pdf_repair_scan_xreftab( uint8 *linebuf , uint8 *lineend ,
                                      int32 *objnum , int32 *number )
{
  uint8 *lineptr = linebuf ;
  uint8 *endptr ;

  HQASSERT( linebuf , "linebuf is null in pdf_repair_scan_xreftab" ) ;
  HQASSERT( lineend , "lineend is null in pdf_repair_scan_xreftab" ) ;
  HQASSERT( objnum , "objnum is null in pdf_repair_scan_xreftab" ) ;
  HQASSERT( number , "number null in pdf_repair_scan_xreftab" ) ;

  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Find the next space after the potential first object number. */
  for ( endptr = lineptr ; endptr < lineend ; ++endptr )
    if ( *endptr == ' ' )
      break ;

  if ( lineptr == endptr || endptr == lineend || endptr - lineptr > 10 )
    return FALSE ;

  /* Scan initial object number. */
  if ( ! pdf_scan_integer( lineptr , endptr , objnum ))
    return FALSE ;
  lineptr = endptr + 1 ;

  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Find the next space after the potential number of objects. */
  for ( endptr = lineptr ; endptr < lineend ; ++endptr )
    if ( *endptr == ' ' )
      break ;

  if ( endptr - lineptr > 10 )
    return FALSE ;

  /* Scan number of objects. */
  if ( ! pdf_scan_integer( lineptr , endptr , number ))
    return FALSE ;
  lineptr = endptr ;

  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Should be at the end of the line. */
  return lineptr == lineend ;
}

/* Assume of the form '%d %d [n|f]'. Particularly interested in the
 * usage character, because recognising an 'f' and marking the
 * corresponding object as free is necessary to rip the job
 * correctly. Although the spec states that the offset is 10 digits
 * and the generation number 5 digits I allow less. Whitespace is
 * allowed before, after and in between items.
 */

static int32 pdf_repair_scan_xrefobj( uint8 *linebuf , uint8 *lineend ,
                                      uint16 *objgen , uint8 *objuse )
{
  uint8 *lineptr = linebuf ;
  uint8 *endptr ;
  int32 tmp ;

  HQASSERT( linebuf , "linebuf is null in pdf_repair_scan_xrefobj" ) ;
  HQASSERT( lineend , "lineend is null in pdf_repair_scan_xrefobj" ) ;
  HQASSERT( objgen , "objgen null in pdf_repair_scan_xrefobj" ) ;
  HQASSERT( objuse , "objuse is null in pdf_repair_scan_xrefobj" ) ;

  /* Scan offset, ignore its value. */
  for (;;) {
    if ( lineptr == lineend )
      return FALSE ;
    if ( isdigit( *lineptr ))
      ++lineptr ;
    else if ( *lineptr == ' ' )
      break ;
    /* and since the value is to be ignored, we can afford to ignore stuff
       which some bad jobs have been known to provide ...*/
    else if ( *lineptr == '-' )   /* ... a minus sign! */
      ++lineptr ;
    else
      return FALSE ;
  }

  if ( lineptr == linebuf || lineptr == lineend || lineptr - linebuf > 10 )
    return FALSE ;
  ++lineptr ;

  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  linebuf = lineptr ;

  /* Find the next space after the potential object generation. */
  for ( endptr = lineptr ; endptr < lineend ; ++endptr )
    if ( *endptr == ' ' )
      break ;

  if ( lineptr == endptr || endptr == lineend || endptr - lineptr > 5 )
    return FALSE ;

  /* Scan object generation. */
  if ( ! pdf_scan_integer( lineptr , endptr , & tmp ))
    return FALSE ;
  *objgen = ( uint16 ) tmp ;

  lineptr = endptr + 1 ;
  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  if ( lineptr == lineend )
    return FALSE ;

  if ( *lineptr != 'n' && *lineptr != 'f' )
    return FALSE ;

  *objuse = *lineptr ;
  ++lineptr ;

  lineptr += pdf_repair_skipwhitespace( lineptr , lineend ) ;

  /* Should be at the end. */
  return lineptr == lineend ;
}


static int32 pdf_repair_skipwhitespace( uint8 *linebuf , uint8 *lineend )
{
  uint8 *lineptr = linebuf ;

  HQASSERT( linebuf , "linebuf is null in pdf_repair_skipwhitespace" ) ;
  HQASSERT( lineend , "lineend is null in pdf_repair_skipwhitespace" ) ;
  HQASSERT( linebuf <= lineend , "linebuf > lineend in pdf_repair_skipwhitespace" ) ;

  while ( lineptr < lineend ) {
    if ( IsWhiteSpace( *lineptr ))
      ++lineptr ;
    else
      break ;
  }
  return CAST_PTRDIFFT_TO_INT32(lineptr - linebuf) ;
}

void init_C_globals_pdfrepr(void)
{
#if defined( ASSERT_BUILD )
  pdftrace_repair = FALSE ;
#endif
}

/* Log stripped */
