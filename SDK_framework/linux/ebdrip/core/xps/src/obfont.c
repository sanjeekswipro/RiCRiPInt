/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:obfont.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Obfuscated font filter
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"

#include "obfont.h"

/*----------------------------------------------------------------------------*/
/* this must be at least 32 bytes */
#define OBFONTBUFFSIZE 1024

/*----------------------------------------------------------------------------*/
static Bool obFontFilterInit(FILELIST *filter,
                             OBJECT *args,
                             STACK *stack)
{
  HQASSERT(args == NULL && stack != NULL, "Stack but no args please") ;

  if (theIStackSize(stack) < 2)
    return error_handler(STACKUNDERFLOW);

  args = theITop(stack);
  if ( args && oType(*args) != OSTRING )
    return error_handler(TYPECHECK);

  if ( !filter_target_or_source(filter, stackindex(1, stack)) )
    return FALSE;

  /* allocate a buffer */
  theIBuffer(filter) = (uint8 *)mm_alloc(mm_pool_temp,
                                         OBFONTBUFFSIZE + 1,
                                         MM_ALLOC_CLASS_FILTER_BUFFER);
  if ( theIBuffer(filter) == NULL )
    return error_handler(VMERROR);

  theIBuffer(filter)++;
  theIPtr(filter) = theIBuffer(filter);
  theICount(filter) = 0;
  theIBufferSize(filter) = OBFONTBUFFSIZE;
  theIFilterState(filter) = FILTER_INIT_STATE;

  /* the deobfuscation string is also the flag that it needs applying */
  theIFilterPrivate(filter) = args;

  npop(2, stack);

  return TRUE;
}

/*----------------------------------------------------------------------------*/

static void obFontFilterDispose(FILELIST *filter)
{
  HQASSERT(filter, "filter NULL in obFontFilterDispose.");

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, theIBuffer(filter) - 1, OBFONTBUFFSIZE + 1);
    theIBuffer(filter) = NULL;
  }
  theIFilterPrivate(filter) = 0;
}

/*----------------------------------------------------------------------------*/

static uint8 get_prev_hex_digit(uint8 * string, int32 * i)
{
  uint8 c = 16;
  while (*i>0 && c>15) {
    c = string[--*i];
    if (c>96) c-=32;
    if (c>64) c-=7; else if (c>57) c=64;
    c-=48;
  }
  if (c>15) c=0;
  return c;
}

static Bool obFontDecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  uint8    *ptr,*obs;
  FILELIST *uflptr;
  int32    count, c, i;

  HQASSERT(filter, "filter NULL in obFontDecodeBuffer.");

  uflptr = theIUnderFile(filter);
  obs = ptr = theIBuffer(filter);

  HQASSERT(uflptr, "uflptr NULL in obFontDecodeBuffer.");
  HQASSERT(ptr, "ptr NULL in obFontDecodeBuffer.");

  count = 0;
  for (i=0; i < OBFONTBUFFSIZE; i++) {
    if ( (c=Getc(uflptr)) == EOF ) {
      count=-count;
      break;
    } else {
      *ptr++ = (uint8) c;
      count++;
    }
  }

  if ( count && theIFilterPrivate(filter) ) {
    /* first fill of the buffer, so deobfuscate the first 32 bytes */
    int32 i,j=0;
    uint8 * string = oString(*(OBJECT*)theIFilterPrivate(filter));
    int32 ext = theLen(*(OBJECT*)theIFilterPrivate(filter)) ;
    while ( ext > 0 && string[--ext] != '.' )
      EMPTY_STATEMENT() ;
    for ( i = 0; i < 32; ++i ) {
      uint8 l;
      if ( i % 16 == 0 ) j=ext;
      l = get_prev_hex_digit(string, &j);
      obs[i] ^= ( get_prev_hex_digit(string, &j)<<4 | l);
    }
    theIFilterPrivate(filter) = 0;
  }

  *ret_bytes = count;
  return TRUE;
}

/*----------------------------------------------------------------------------*/

static FILELIST obFontFilter = {tag_LIMIT};

FILELIST * obFont_decode_filter()
{
  if ( obFontFilter.typetag == tag_LIMIT ) {
    init_filelist_struct(&obFontFilter,
                         NAME_AND_LENGTH("ObFontDecode"),
                         FILTER_FLAG | READ_FLAG,
                         0, NULL, 0,
                         FilterFillBuff,                   /* fillbuff */
                         FilterFlushBufError,              /* flushbuff */
                         obFontFilterInit,                 /* initfile */
                         FilterCloseFile,                  /* closefile */
                         obFontFilterDispose,              /* disposefile */
                         FilterBytes,                      /* bytesavail */
                         FilterReset,                      /* resetfile */
                         FilterPos,                        /* filepos */
                         FilterSetPos,                     /* setfilepos */
                         FilterFlushFile,                  /* flushfile */
                         FilterEncodeError,                /* filterencode */
                         obFontDecodeBuffer,               /* filterdecode */
                         FilterLastError,                  /* lasterror */
                         -1, NULL, NULL, NULL);
  }
  return &obFontFilter;
}

/** File runtime initialisation */
void init_C_globals_obfont(void)
{
  FILELIST init = {tag_LIMIT};
  obFontFilter = init ;
}


/*----------------------------------------------------------------------------*/
/* Log stripped */
