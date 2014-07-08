/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:cryptFilter.c(EBDSDK_P.1) $
 *
 * Copyright (c) 2007 Global Graphics Software Ltd. All Rights Reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PDF Crypt filter. Only identify is supported.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "cryptFilter.h"

#include "objects.h"
#include "stacks.h"
#include "swdevice.h"
#include "swerrors.h"
#include "uvms.h"
#include "dictscan.h"
#include "namedef_.h"
#include "mm.h"

#define CRYPT_BUFFER_SIZE 1024

/**
 * Check the passed crypt filter parameter dictionary for specifying the
 * identity filter; an error will be thrown if it is anything else.
 *
 * \return FALSE on error.
 */
static Bool checkCryptFilterParams(OBJECT* cryptParamDict)
{
  enum { params_Name, params_dummy } ;
  static NAMETYPEMATCH paramsMatch[params_dummy + 1] = {
    {NAME_Name | OOPTIONAL, 2, {ONAME, OINDIRECT}},
    DUMMY_END_MATCH
  };
  OBJECT* name;

  if (cryptParamDict == NULL || oType(*cryptParamDict) == ONULL) {
    /* No dictionary - default to identity. */
    return TRUE;
  }

  HQASSERT(oType(*cryptParamDict) == ODICTIONARY,
           "'cryptParamDict' must be a dictionary.");

  if (! dictmatch(cryptParamDict, paramsMatch))
    return FALSE;

  name = paramsMatch[params_Name].result;
  /* Note that the default if name is missing is Identity. */
  if (name == NULL || oNameNumber(*name) == NAME_Identity)
    return TRUE;
  else
    return detail_error_handler(UNDEFINED, "Only Identity Crypt filters are supported");
}

/**
 * Crypt filter initialisation. Note that currently only Identity filters are
 * supported.
 */
static Bool cryptFilterInit(FILELIST *filter, OBJECT *params, STACK *stack)
{
  HQASSERT(stack != NULL && ! isEmpty(*stack),
           "'stack' should never be NULL or empty.");

  if (! checkCryptFilterParams(params))
    return FALSE;

  /* Set underlying filter. */
  if (! filter_target_or_source(filter, theTop(*stack)) )
    return FALSE;

  /* Allocate the read buffer. We add one onto the buffer size because we're
  going to offset the buffer so that there is a single byte available at
  filter->buffer[-1]; this is required by the filter machinery. */
  filter->buffer = (uint8*)mm_alloc(mm_pool_temp, CRYPT_BUFFER_SIZE + 1,
                                    MM_ALLOC_CLASS_CRYPT_FILTER);
  if (filter->buffer == NULL)
    return error_handler(VMERROR);

  /* Required for ungetc() behavior. */
  filter->buffer ++;

  filter->ptr = filter->buffer;
  filter->count = 0;
  filter->buffersize = CRYPT_BUFFER_SIZE;
  filter->filter_state = FILTER_INIT_STATE;
  filter->u.filterprivate = NULL;

  /* Pop underlying filter. */
  pop(stack);

  return TRUE;
}

/**
 * Destructor. Note that this method may be called multiple times.
 */
static void cryptFilterDispose(FILELIST *filter)
{
  if (filter->buffer != NULL) {
    mm_free(mm_pool_temp, filter->buffer - 1, CRYPT_BUFFER_SIZE + 1);
    filter->buffer = NULL;
  }
}

/**
 * Read data from the filter.
 */
static Bool cryptDecodeBuffer(FILELIST *filter, int32 *bytesAvailable)
{
  int32 count = 0;
  int32 c;
  uint8* buffer = filter->buffer;

  while (count < filter->buffersize) {
    c = Getc(filter->underlying_file);
    if (c == EOF) {
      *bytesAvailable = -count;
      return TRUE;
    }
    *buffer = (uint8)c;
    buffer ++;
    count ++;
  }

  *bytesAvailable = count;
  return TRUE;
}

/**
 * Initialise the passed filter as a Crypt filter.
 */
void crypt_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* stream decode filter */
  init_filelist_struct(flptr,
                       NAME_AND_LENGTH("Crypt"),
                       FILTER_FLAG | READ_FLAG | DELIMITS_FLAG,
                       0, NULL, 0,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       cryptFilterInit,                      /* initfile */
                       FilterCloseFile,                      /* closefile */
                       cryptFilterDispose,                   /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       cryptDecodeBuffer,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL);
}


/* Log stripped */

