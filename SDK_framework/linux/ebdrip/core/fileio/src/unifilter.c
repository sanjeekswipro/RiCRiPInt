/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:unifilter.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode Encode/Decode filter pair. UnicodeDecode is not yet implemented.
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqunicode.h"
#include "namedef_.h"
#include "monitor.h"

#define UNICODEFILTERBUFFSIZE 1024

/* \brief Filter initialisation for both Unicode read and write filters.

   Both UnicodeEncode and UnicodeDecode can be used to perform transforms to
   and from Unicode (or any other encoding). The difference is that Encode is
   driven by push, Decode by pull. Both filters take the arguments /To and
   /From. /To and /From are encoding names; these will be looked up in ICU's
   encoding tables and on disc. /From may also be "Unicode", in which case
   one of the Unicode encodings will be selected as appropriate, depending on
   the byte order mark detected.

   The option /ByteOrderMark determines if a BOM will be added or removed
   from the intermediate Unicode output (and hence passed through to the
   output if the output encoding is a Unicode form). If not specified, a BOM
   will be present only if it existed on an incoming Unicode form. If false,
   BOM will be removed from the output. If true, BOM will be added to the
   intermediate output. It is advisable to set /ByteOrderMark to false if not
   encoding to a Unicode form, because BOMs are not generally representable in
   other encodings.

   The option /Substitute can be used to substitute a sequence of bytes in
   the output encoding for any characters that are not representable or are
   incorrectly formed in the input encoding. If the substitute string is
   empty, these characters will be skipped.
*/
static Bool unicodeFilterInit(FILELIST *filter,
                              OBJECT *args,
                              STACK *stack)
{
  int32 pop_args = 0 ;
  uint8 *from, *to, *subs ;
  uint32 fromlen, tolen, subslen ;
  int bom ;

  enum {
    match_From, match_To, match_ByteOrderMark, match_Substitute, match_dummy
  } ;
  static NAMETYPEMATCH match[match_dummy + 1] = {
    { NAME_From, 2, { OSTRING, ONAME }},
    { NAME_To, 2, { OSTRING, ONAME }},
    { NAME_ByteOrderMark|OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_Substitute|OOPTIONAL, 1, { OSTRING }},
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
    if ( !oCanRead(*oDict(*args)) &&
         !object_access_override(oDict(*args)) )
      return error_handler(INVALIDACCESS) ;
    if ( !dictmatch(args, match) )
      return FALSE ;
    if ( ! FilterCheckArgs(filter, args) )
      return FALSE ;
    OCopy(theIParamDict(filter), *args) ;
  } else /* Must have argument dictionary for From/To. */
    return error_handler(TYPECHECK) ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer(filter) = (uint8 *)mm_alloc(mm_pool_temp ,
                                         UNICODEFILTERBUFFSIZE,
                                         MM_ALLOC_CLASS_UNICODE_FILTER_BUFFER) ;
  if ( theIBuffer(filter) == NULL )
    return error_handler(VMERROR) ;

  /* get args out of the the dictionary match structure */
  switch ( oType(*match[match_From].result) ) {
  case OSTRING:
    fromlen = theLen(*match[match_From].result) ;
    from = oString(*match[match_From].result) ;
    break ;
  case ONAME:
    fromlen = theINLen(oName(*match[match_From].result)) ;
    from = theICList(oName(*match[match_From].result)) ;
    break ;
  default:
    HQFAIL("Unicode From encoding not a known type") ;
    return error_handler(UNREGISTERED) ;
  }

  switch ( oType(*match[match_To].result) ) {
  case OSTRING:
    tolen = theLen(*match[match_To].result) ;
    to = oString(*match[match_To].result) ;
    break ;
  case ONAME:
    tolen = theINLen(oName(*match[match_To].result)) ;
    to = theICList(oName(*match[match_To].result)) ;
    break ;
  default:
    HQFAIL("Unicode To encoding not a known type") ;
    return error_handler(UNREGISTERED) ;
  }

  /* Should we emit a Byte Order Mark if the output is Unicode? */
  bom = UCONVERT_BOM_LEAVE ; /* Leave BOM alone if it exists */
  if ( match[match_ByteOrderMark].result ) {
    if ( oBool(*match[match_ByteOrderMark].result) )
      bom = UCONVERT_BOM_ADD ;
    else
      bom = UCONVERT_BOM_REMOVE ;
  }

  /* Should we substitute for invalid characters? */
  subslen = 0 ;
  subs = NULL ;
  if ( match[match_Substitute].result ) {
    subslen = theLen(*match[match_Substitute].result) ;
    subs = oString(*match[match_Substitute].result) ;
    if ( subs == NULL ) {
      /* Strings have oString NULL if len == 0. We want to pass a real
         pointer through to distinguish the case where we want to substitute
         nothing (i.e. ignore invalid chars) from the case when we don't want
         to do substitution. So enforce a real pointer value here. */
      static uint8 subs_dummy[1] = { '\0' } ;
      subs = subs_dummy ;
    }
  }

  if ( (theIFilterPrivate(filter) = unicode_convert_open(from, fromlen,
                                                         to, tolen,
                                                         bom, subs, subslen)) == NULL )
  {
    monitorf( UVM( "Warning: unable to open encoding converter for %.*s -> %.*s\n" ),
      fromlen, from, tolen, to );
    return error_handler(RANGECHECK) ;
  }

  theIFilterState(filter) = FILTER_INIT_STATE ;
  theIPtr(filter) = theIBuffer(filter) ;
  theICount(filter) = 0 ;
  theIBufferSize(filter) = UNICODEFILTERBUFFSIZE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

/* \brief Common filter dispose for Unicode read and write. */
static void unicodeFilterDispose(FILELIST *filter)
{
  typedef MAY_ALIAS(unicode_convert_t *) unicode_convert_a ;

  HQASSERT(filter , "No filter to dispose with.") ;

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, (mm_addr_t)theIBuffer(filter), UNICODEFILTERBUFFSIZE) ;
    theIBuffer(filter) = NULL ;
  }

  if ( theIFilterPrivate(filter) )
    unicode_convert_close((unicode_convert_a *)&theIFilterPrivate(filter)) ;
}

/* \brief Encode a buffer. */
static Bool unicode_encode_buffer(FILELIST *filter, Bool flush)
{
  register FILELIST *uflptr ;
  unicode_convert_t *converter ;
  uint8 *in ;
  int32 count ;

  HQASSERT(filter, "No filter to encode.") ;

  count = filter->count ;
  if ( count == 0 && !flush )
    return TRUE ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr, "No underlying file encoding Unicode buffer.") ;

  if ( !isIOpenFileFilterById(theIUnderFilterId(filter), uflptr) )
    return error_handler(IOERROR) ;

  converter = theIFilterPrivate(filter) ;
  HQASSERT(converter, "Unicode filter has no converter state") ;

  in = filter->buffer ;
  HQASSERT(in, "Filter buffer has got lost") ;

  /* Encode from the filter buffer to a temporary buffer, then use
     TPutc to push this to the underlying file/filter. */
  for (;;) {
    uint8 tmpbuf[UNICODEFILTERBUFFSIZE], *out, *converted = tmpbuf ;
    int32 remaining = UNICODEFILTERBUFFSIZE ;
    int result = unicode_convert_buffer(converter,
                                        &in, &count,
                                        &converted, &remaining,
                                        flush) ;

    for ( out = tmpbuf ; out < converted ; ++out ) {
      if ( TPutc(*out, uflptr) == EOF )
        return FALSE ;
    }

    switch ( result ) {
    case UTF_CONVERT_INPUT_EXHAUSTED:
      /* If we did not have all of the data and we are closing, throw an
         error. We don't do this on flushfile. */
      if ( isIClosing(filter) )
        return error_handler(IOERROR) ;
      /*@fallthrough@*/
    case UTF_CONVERT_OK:
      /* Update the filter reflect the amount used. The input in the buffer may
         be incomplete, so we retain any unused bytes from the conversion. */
      if ( count > 0 && filter->buffer != in )
        HqMemMove(filter->buffer, in, count) ;

      filter->ptr = filter->buffer + count ;
      filter->count = count ;

      return TRUE ;
    case UTF_CONVERT_OUTPUT_EXHAUSTED:
      /* We have output this buffer to the underlying file, loop round again
         for the next part. */
      continue ;
    default:
      return error_handler(IOERROR) ;
    }
  }
}

/* \brief Encode a buffer. */
static Bool unicodeEncodeBuffer(FILELIST *filter)
{
  return unicode_encode_buffer(filter, isIClosing(filter) != 0) ;
}

/* \brief Flush the encode filter buffer.

   The encode filter does not use FilterFlushBuff because the converter
   does not guarantee to flush data unless closing. */
static int32 unicodeEncodeFlush(FILELIST *filter)
{
  register FILELIST *uflptr ;

  /* Force flushing of Unicode converter data. */
  if ( !unicode_encode_buffer(filter, TRUE) )
    return EOF ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr, "uflptr NULL in unicodeEncodeFlush.") ;

  /* Flush the underlying file. */
  return (*theIMyFlushFile(uflptr))(uflptr) ;
}

void unicode_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* flate encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("UnicodeEncode") ,
                       FILTER_FLAG | WRITE_FLAG,
                       0, NULL, 0,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       unicodeFilterInit,                    /* initfile */
                       FilterCloseFile,                      /* closefile */
                       unicodeFilterDispose,                 /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       unicodeEncodeFlush,                   /* flushfile */
                       unicodeEncodeBuffer,                  /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/*
Log stripped */
