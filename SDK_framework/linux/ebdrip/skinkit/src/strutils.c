/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:strutils.c(EBDSDK_P.1) $
 */

#include "c99protos.h"
#include "hqnstrutils.h"
#include "mem.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @file
 * @ingroup skinkit
 * @brief String-related utility functions.
 */

static int print_real( char *pStr, int32 nSize, double fVal )
{
#ifdef VXWORKS
  /* snprintf() cannot handle doubles, so use sprintf() */
  UNUSED_PARAM( int32, nSize );

  return sprintf( pStr, "%g", fVal );
#else
  return snprintf( pStr, (size_t)nSize, "%g", fVal );
#endif
}

const char* doubleToStr(char* pStr, int32 nSize, double fVal)
{
  if (pStr && nSize > 0)
  {
    int nBytes;

    /* Format string */
    nBytes = print_real(pStr, nSize, fVal);
    if (nBytes < 0 || nBytes >= nSize)
    {
      HQFAIL("Output buffer not large enough for double");
      strncpy(pStr, "0", nSize - 1);
      pStr[nSize - 1] = '\0';
    }
    else
    {
      /* String created - Format using C locale */
      struct lconv* pLocaleInfo = localeconv ();
      if (pLocaleInfo)
      {
        char chDecimal = *(pLocaleInfo->decimal_point);
        if (chDecimal != '.')
        {
          char* pch = pStr;
          while (*pch)
          {
            if (*pch == chDecimal)
              *pch = '.';
            pch ++;
          }
        }
      }
    }
  }

  return pStr;
}

double strToDouble (char* pStr, char** ppEndStr)
{
  struct lconv* pLocaleInfo;
  char chDecimal;
  char* pModifiedStr;
  char* pModifiedEndStr;
  char* pch;
  double nResult;

  /* Get locale-specific decimal point */
  pLocaleInfo = localeconv ();
  if (! pLocaleInfo)
  {
    /* Locale unavailable - Use strtod */
    return strtod (pStr, ppEndStr);
  }

  chDecimal = *(pLocaleInfo->decimal_point);

  if (chDecimal == '.')
  {
    /* C locale uses "." - Safe to use strtod */
    return strtod (pStr, ppEndStr);
  }

  /*
   * Format input string for consumption with locale-dependent strtod().
   * E.g. "0.12,0.13" -> "0,12 0,13"
   */
  pModifiedStr = (char*) utl_strdup ((uint8*) pStr);
  if (! pModifiedStr)
  {
    /* Out of memory */
    if (ppEndStr)
      *ppEndStr = pStr;
    return 0.0;
  }

  pch = pModifiedStr;
  while (*pch)
  {
    if (*pch == chDecimal)
      *pch = ' ';
    else if (*pch == '.')
      *pch = chDecimal;

    pch ++;
  }

  nResult = strtod (pModifiedStr, &pModifiedEndStr);

  /* Set ppEndStr */
  if (ppEndStr)
  {
    if (pModifiedEndStr)
    {
      int nOffset = CAST_PTRDIFFT_TO_INT32(pModifiedEndStr - pModifiedStr);
      *ppEndStr = pStr + nOffset;
    }
    else
    {
      *ppEndStr = NULL;
    }
  }

  MemFree (pModifiedStr);

  return nResult;
}

uint8* utl_strdup(
  uint8*  str)
{
  uint8*  dup = (uint8 *) MemAlloc((strlen_uint32((char*)str) + 1)*sizeof(uint8), FALSE, FALSE);

  if ( dup != NULL ) {
    strcpy((char*)dup, (char*)str);
  }
  return(dup);
}
