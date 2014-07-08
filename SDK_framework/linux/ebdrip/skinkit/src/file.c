/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:file.c(EBDSDK_P.1) $
 */
#include "hqstr.h"
#include "file.h"

#include <string.h>
#include <stdio.h>

/**
 * \file
 * \ingroup skinkit
 * \brief Platform independent implementation of the skinkit file I/O interfaces.
 */

/**
 * \brief Utility function to search for the SW folder relative to a specific root.
 *
 * \param abzSearchRoot  The root folder to search from.
 * \param abzSWDir  Container (big enough to store at least LONGESTFILENAME characters)
 * which will contain the path to the SW folder (if found).
 * \return \c TRUE if the SW folder was found.
 */
static int32 findSWDir (const uint8* abzSearchRoot, uint8* abzSWDir)
{
#define DIR_UP  ".." DIRECTORY_SEPARATOR_STRING
/* GG Development superproduct RIPs */
#ifdef EMBEDDED
#define SUFFIX(x) x, x "-ebdrip"
#else
#define SUFFIX(x) x, x "-clrip"
#endif
  const char* kSearchSuffix[] = {
    SUFFIX("SW"),
    SUFFIX(DIR_UP "SW"),
    NULL
  };
  int32 i ;

  for (i = 0; kSearchSuffix[i] != NULL; i ++)
  {
    char abzTestDir[LONGESTFILENAME];
    void* dirHandle;
    int32 error;
    uint32 nLength;

    /* Format search path */
    strcpy (abzTestDir, (const char*) abzSearchRoot);
    if ((nLength = strlen_uint32 (abzTestDir)) > 0 && abzTestDir[nLength - 1] != DIRECTORY_SEPARATOR)
    {
      if (nLength + 1 < LONGESTFILENAME)
        strcat (abzTestDir, DIRECTORY_SEPARATOR_STRING);
      else
        return FALSE;
    }

    if (strlen_uint32 (abzTestDir) + strlen_uint32 (kSearchSuffix[i]) < LONGESTFILENAME)
    {
      strcat (abzTestDir, kSearchSuffix[i]);

      if ((dirHandle = PKDirOpen ((uint8*) abzTestDir, &error)) != NULL)
      {
        /* Found SW directory */
        PKDirClose(dirHandle, &error);
        strcpy ((char*) abzSWDir, abzTestDir);
        return TRUE;
      }
    }
  }

  /* SW directory not found */
  return FALSE;
}

int32 PKSWDir( uint8 * pSWDir )
{
  static uint8 abzSWDir[LONGESTFILENAME] = {0};

  if (abzSWDir[0] == 0)
  {
    uint32 cbSize;
    uint8 abzCurrDir[LONGESTFILENAME];

    /* First try the current directory */
    if (! (PKCurrDir (abzCurrDir) && findSWDir (abzCurrDir, abzSWDir)))
    {
      uint8 abzAppDir[LONGESTFILENAME];

      /* Now try the application directory, if different from abzCurrDir */
      if (! PKAppDir (abzAppDir) ||
          strcmp ((char*) abzCurrDir, (char*) abzAppDir) == 0 ||
          ! findSWDir (abzAppDir, abzSWDir))
        return FALSE;
    }

    /* Terminate with a directory separator */
    cbSize = strlen_uint32 ((char*) abzSWDir);
    if (cbSize + 1 >= LONGESTFILENAME)
      return FALSE;
    abzSWDir[cbSize++] = DIRECTORY_SEPARATOR;
    abzSWDir[cbSize] = 0;
  }

  strcpy ((char*) pSWDir, (const char*) abzSWDir);
  return TRUE;
}

