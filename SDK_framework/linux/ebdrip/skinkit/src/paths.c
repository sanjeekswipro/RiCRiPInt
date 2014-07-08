/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:paths.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * File related utility functions
 */

/**
 * \file
 * \brief File-related utility functions.
 */

#include "hqstr.h"
#include "paths.h"

#include <string.h>


#define PS_DIRECTORY_SEPARATOR        '/'
#define PS_DIRECTORY_SEPARATOR_STRING "/"
#define PS_FILENAME_ESCAPE            '\\'
#define PS_FILENAME_ESCAPE_STRING     "\\"
#define PS_DEVICE_DELIMITER           '%'
#define PS_DEVICE_DELIMITER_STRING    "%"


static int32 PSDeviceNameToPlatform( uint8 * pPlatform, uint8 ** ppPostScript );
static uint32 PSPathUnescapeElement( uint8 *  pOutput, uint8 ** pptbInput );
static int32 PSDeviceNameToPlatform( uint8 * pPlatform, uint8 ** ppPostScript );
static uint32 PSPathElementToPlatform(uint8 *  pOutput, uint8 ** pptbInput );


/* --------------------------- Exported utilities ------------------------------- */

uint8 * SkipPathElement( uint8 * ptbzInput )
{
  uint8 * ptbzEndInput = (uint8 *) strchr( (char *)ptbzInput, DIRECTORY_SEPARATOR );

  if ( ptbzEndInput == NULL )
   ptbzEndInput = ptbzInput + strlen( (char *)ptbzInput );

  return ptbzEndInput;
}


uint32 PSPrefixAndFilenameToPlatform(
  uint8 * pPlatform,
  uint8 * ptbzPSPrefix,
  uint8 * ptbzPSRelative
)
{
  uint32   nInitialLength = strlen_uint32( (char *)pPlatform );
  uint8 *  ptbzPS = ptbzPSPrefix;
  uint8    concatenatedPS[ LONGESTFILENAME ] ;

  /* Handle the device part of ptbzPSPrefix, which must be complete. */
  PSDeviceNameToPlatform( pPlatform, &ptbzPS );

  /* Concatenate remains of ptbzPSPrefix and ptbzPSRelative, unless one of them is "" */
  if ( *ptbzPS == '\0' )
  {
    /* Used all of ptbzPSPrefix */
    ptbzPS = ptbzPSRelative;
  }
  else if ( *ptbzPSRelative != '\0' )
  {
    /* Both strings are not "", concatenate them */
    strcpy( (char *)concatenatedPS, (char *)ptbzPS );
    strcat( (char *)concatenatedPS, (char *)ptbzPSRelative );

    ptbzPS = concatenatedPS;
  }

  /* Append an element at a time */
  while ( *ptbzPS != '\0' )
  {
    PSPathElementToPlatform( pPlatform, &ptbzPS );

    if ( *ptbzPS != '\0' ) /* ie PS_DIRECTORY_SEPARATOR ) */
    {
      /* Replace PostScript separator with platform one */
      ptbzPS++;
      strcat( (char *)pPlatform, DIRECTORY_SEPARATOR_STRING );
    }
  }

  return strlen_uint32( (char *)pPlatform ) - nInitialLength;
}


void PlatformPathElementToPS(uint8 * pszOutput, uint8 ** ppszInput)
{
  uint8 * pIn = *ppszInput;

  while (*pIn != '\0' && *pIn != DIRECTORY_SEPARATOR)
  {
    switch (*pIn)
    {
      case PS_DIRECTORY_SEPARATOR:
      case PS_DEVICE_DELIMITER:
      case PS_FILENAME_ESCAPE:
        *pszOutput++ = PS_FILENAME_ESCAPE;
        break;
    }
    *pszOutput++ = *pIn++;
  }

  *pszOutput = '\0';
  *ppszInput = pIn;
}


uint8 * PlatformFilenameSkipToLeafname(uint8 * ptbzPlatform)
{
  uint8 *  ptbzLeafname = ptbzPlatform;

  ptbzPlatform = SkipPathElement( ptbzLeafname );

  while( *ptbzPlatform != '\0' )
  {
    /* ptbzPlatform points to separator, next element starts at the following byte */
    ptbzLeafname = ++ptbzPlatform;

    ptbzPlatform = SkipPathElement( ptbzLeafname );
  }

  return ptbzLeafname;
}


uint32 PlatformFilenameRootToPS(
  uint8 * pOutput,         /**< Escaped PostScript device name */
  uint8 ** pptbInput       /**< Platform filename */
)
{
  uint8    tb;
  uint32   nInitialLength = strlen_uint32( (char *)pOutput );
  uint8    root[ LONGESTFILENAME ];

  root[ 0 ] = '\0';

  /* Handle root part if any */
  if ( PKParseRoot( root, pptbInput ) )
  {
    /* Root part exists */
    /* Output PostScript escaped form between PostScript device delimiters */
    uint8 * pRoot = root;
    uint8 * pRootEnd = pRoot + strlen( (char *) root );

    strcat( (char *)pOutput, PS_DEVICE_DELIMITER_STRING );

    if ( pRoot == pRootEnd )
    {
      /* if no root components generate %/% */
      strcat( (char *)pOutput, PS_DIRECTORY_SEPARATOR_STRING );
    }
    else
    {
      uint8 * pWriteFrom = pRoot;

      do
      {
        tb = *pRoot++;
        switch ( tb )
        {
          /* If platform directory separator is different from the PostScript
           * one then replace it by the PostScript one and escape PostScript
           * directory ones to distinguish them. This means that we get the
           * desired format for PC UNC filenames eg:
           * \\machine\drive\directory\leaf -> %machine/drive%directory/leaf
           *                        instead of %machine\drive%directory/leaf
           */
#if DIRECTORY_SEPARATOR != PS_DIRECTORY_SEPARATOR
         case DIRECTORY_SEPARATOR:
          /* Output up to directory separator (pRoot - 1), plus a PostScript separator */
          if( pRoot - 1 > pWriteFrom )
            strncat( (char *)pOutput, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((pRoot - 1) - pWriteFrom) );
          pWriteFrom = pRoot;
          strcat( (char *)pOutput, PS_DIRECTORY_SEPARATOR_STRING );
          break;

         case PS_DIRECTORY_SEPARATOR:
          /* fall through to escaping */
#endif

          /* Escape % unless already dealt with by DIRECTORY_SEPARATOR
           * case above. All platforms at present have
           * DIRECTORY_SEPARATOR != PS_DEVICE_DELIMITER.
           */
#if DIRECTORY_SEPARATOR != PS_DEVICE_DELIMITER
         case PS_DEVICE_DELIMITER:
          /* fall through to escaping */
#endif

          /* Escape \ unless already dealt with by DIRECTORY_SEPARATOR
           * case above, as will happen on the PC.
           */
#if DIRECTORY_SEPARATOR != PS_FILENAME_ESCAPE
         case PS_FILENAME_ESCAPE:
          /* fall through to escaping */
#endif

          /* Here if character requires escaping */
          /* Output up to escaped character (pRoot - 1), plus a PostScript escape */
          if( pRoot - 1 > pWriteFrom )
            strncat( (char *)pOutput, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((pRoot - 1) - pWriteFrom) );
          pWriteFrom = pRoot - 1;
          strcat( (char *)pOutput, PS_FILENAME_ESCAPE_STRING );
          break;
        }
      }
      while ( pRoot < pRootEnd );

      /* Output remainder */
      if( pRootEnd != pWriteFrom )
        strncat( (char *)pOutput, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET(pRootEnd - pWriteFrom) );
    }

    strcat( (char *)pOutput, PS_DEVICE_DELIMITER_STRING );
  }

  return strlen_uint32( (char *)pOutput ) - nInitialLength;
}


uint32 PlatformFilenameToPS( uint8 * pszOutput, uint8 * pszInput )
{
  uint32 nInitialLength = strlen_uint32( (char *)pszOutput );
  uint8 * pszPlatform = pszInput;

  /* First convert any root part */
  PlatformFilenameRootToPS( pszOutput, &pszPlatform );

  /* Ensure PS name starts with empty element */
  strcat( (char *)pszOutput, PS_DIRECTORY_SEPARATOR_STRING );

  /* Add the remaining path elements, separated by the PostScript separator */
  while ( *pszPlatform != '\0' )
  {
    uint8 * pOutput = pszOutput + strlen( (char *)pszOutput );

    PlatformPathElementToPS( pOutput, &pszPlatform );

    if ( *pszPlatform != '\0' ) /* ie DIRECTORY_SEPARATOR ) */
    {
      /* Replace platform separator with PostScript one */
      pszPlatform++;
      strcat( (char *)pszOutput, PS_DIRECTORY_SEPARATOR_STRING );
    }
  }

  /* Determine number of bytes added to record */
  return strlen_uint32( (char *)pszOutput ) - nInitialLength;
}


uint8 * PSFilenameSkipToLeafname(uint8 * ptbzPS)
{
  /* Set ptbzLeafname in case there is only a device name */
  uint8 *  ptbzLeafname = ptbzPS;
  uint8    tb;

  /* Skip over device name, if any */
  ptbzPS = PSFilenameSkipDevice( ptbzPS );

  if ( *ptbzPS != '\0' )
  {
    ptbzLeafname = ptbzPS;

    do
    {
      tb = *ptbzPS++;

      if ( tb == PS_FILENAME_ESCAPE )
      {
        /* Read the escaped byte */
        tb = *ptbzPS++;
      }
      else if ( tb == PS_DIRECTORY_SEPARATOR )
      {
        /* Unescaped directory separator */
        /* Move ptbzLeafname on to point to following character, which might be the terminator */
        ptbzLeafname = ptbzPS;
      }
    }
    while ( tb != '\0' );
  }
  return ptbzLeafname;
}


/**
 * \brief Helper function to split a PostScript filename into device
 * and path sections.
 *
 * \param ptbzPS  The original PostScript filename.
 * \param ptbzDevice  Pointer to an array (at least \c LONGESTDEVICENAME characters
 * in length) which will be filled with the device name (minus device delimiters).
 * \return A pointer to the character immediately after the second
 * \c PS_DEVICE_DELIMITER, or ptbzPS if there is no device name part.
 */
static uint8 * PSFilenameSplitDeviceAndName(uint8 * ptbzPS, uint8 * ptbzDevice)
{
  ptbzDevice[0] = '\0';
  if ( *ptbzPS == PS_DEVICE_DELIMITER )
  {
    /* Device name part exists, read bytes to the closing delimiter */
    uint8     tb;

    /* Move past opening delimiter */
    ptbzPS++;

    do
    {
      tb = *ptbzPS++;

      switch ( tb )
      {
        case PS_DEVICE_DELIMITER:
          tb = '\0';
          break;

        case PS_FILENAME_ESCAPE:
          /* Read escaped byte */
          tb = *ptbzPS++;
          break;
      }

      *ptbzDevice++ = tb;
    }
    while ( tb != '\0' );

    if ( *(ptbzPS - 1) == '\0' )
    {
      /* Loop terminated on reaching (possibly escaped) terminator */
      /* Move input pointer back to it */
      ptbzPS--;
    }
  }
  return ptbzPS;
}

uint8 * PSFilenameSkipDevice(uint8 * ptbzPS)
{
  uint8 ptbzDevice[LONGESTDEVICENAME];
  return PSFilenameSplitDeviceAndName (ptbzPS, ptbzDevice);
}



/* --------------------------- Private utilities ------------------------------- */

/**
 * \brief Copies from an escaped PostScript path *pptbInput to pOutput
 * removing PS_FILENAME_ESCAPE characters until reaching an unescaped PS_DIRECTORY_SEPARATOR
 * or the terminator.
 * \return Number of bytes written to the output record.
 */
static uint32 PSPathUnescapeElement(
  uint8 *  pOutput,                 /**< Unescaped PostScript path element */
  uint8 ** pptbInput                /**< Escaped PostScript path */
)
{
  uint8    tb;
  uint8 *  ptbInput;
  uint8 *  pWriteFrom;
  uint32   nInitialLength = strlen_uint32( (char *)pOutput );

  pWriteFrom = ptbInput = *pptbInput;

  do
  {
    tb = *ptbInput++;
    switch ( tb )
    {
     case PS_DIRECTORY_SEPARATOR:       /* terminating input separator */
      tb = '\0';
      break;

     case PS_FILENAME_ESCAPE:           /* escaped char (ptbInput - 1) */
      if ( ptbInput - 1 > pWriteFrom )
        strncat( (char *)pOutput, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((ptbInput - 1) - pWriteFrom) );
      pWriteFrom = ptbInput;
      tb = *ptbInput++;
      break;
    }
  }
  while ( tb != '\0' );

  if ( ptbInput - 1 > pWriteFrom )
    strncat( (char *)pOutput, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((ptbInput - 1) - pWriteFrom) );

  /* Set *pptbInput to point to PS_DIRECTORY_SEPARATOR or to '\0' */
  *pptbInput = --ptbInput;

  return strlen_uint32( (char *)pOutput ) - nInitialLength;
}


/**
 * \brief Converts a PostScript path element to
 * a platform path element by doing PostScript unescaping.
 * Input may be terminated by PS_DIRECTORY_SEPARATOR.
 */
static uint32 PSPathElementToPlatform(
  uint8 *  pOutput,             /**< Platform element */
  uint8 ** pptbInput            /**< Escaped PostScript path */
)
{
  uint32   nBytesWritten;

  /* Strip leading directory separators */
  while ( **pptbInput == PS_DIRECTORY_SEPARATOR )
  {
    (*pptbInput)++;
  }

  /* Unescape path element, putting output into pOutput */
  nBytesWritten = PSPathUnescapeElement( pOutput, pptbInput );

  return nBytesWritten;
}


/**
 * \brief Converts a PostScript device name to a platform-specific
 * drive or volume name.
 */
static int32 PSDeviceNameToPlatform( uint8 * pPlatform, uint8 ** ppPostScript )
{
  uint8 * ptbInput;
  uint32  nInitialLength = strlen_uint32( (char *)pPlatform );

  ptbInput = *ppPostScript;

  /* Do we have a device name part? */
  if ( *ptbInput == PS_DEVICE_DELIMITER )
  {
    int32       fRootBuilt = FALSE;

    /* Yes */
    /* Move past delimiter */
    ptbInput++;

    if ( *ptbInput == PS_DIRECTORY_SEPARATOR && *(ptbInput+1) == PS_DEVICE_DELIMITER )
    {
      /* Device name is just an unescaped directory separator */
      /* Build root from a null device name, which may be illegal on this platform */
      fRootBuilt = PKBuildRoot( pPlatform, (uint8 *)( "" ) );
      ptbInput += 2;
    }
    else
    {
      uint8   tb;
      uint8 * pWriteFrom = ptbInput;
      uint8 unescapedDevice[ LONGESTFILENAME ] ;

      /* Unescape the device part */
      unescapedDevice[0] = '\0';

      do
      {
        tb = *ptbInput++;

        switch ( tb )
        {
        case PS_DEVICE_DELIMITER:
          tb = '\0';
          break;

#if DIRECTORY_SEPARATOR != PS_DIRECTORY_SEPARATOR
        case PS_DIRECTORY_SEPARATOR:
          /* Output up to directory separator (ptbInput-1), plus appropriate separator for platform */
          if ( ptbInput - 1 > pWriteFrom )
            strncat( (char *)unescapedDevice, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((ptbInput - 1) - pWriteFrom) );
          pWriteFrom = ptbInput;
          strcat( (char *)unescapedDevice, DIRECTORY_SEPARATOR_STRING );
          break;
#endif

        case PS_FILENAME_ESCAPE:
          /* Output up to escape... (ptbInput-1) */
          if ( ptbInput - pWriteFrom > 1 )
            strncat( (char *)unescapedDevice, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((ptbInput - pWriteFrom) - 1) );
          pWriteFrom = ptbInput;
          /* ...and suppress the escape */
          tb = *ptbInput++;
          break;
        }
      }
      while ( tb != '\0' );

      /* Output remainder */
      if ( ptbInput - 1 > pWriteFrom )
        strncat( (char *)unescapedDevice, (char *)pWriteFrom, CAST_UNSIGNED_TO_SIZET((ptbInput - 1) - pWriteFrom) );

      /* Build root from the unescaped device name */
      fRootBuilt = PKBuildRoot( pPlatform, unescapedDevice );

      if ( *(ptbInput-1) == '\0' )
      {
        /* Reached (possibly escaped) terminator before finding second delimiter */
        /* Move input pointer back to point to it */
        ptbInput--;
      }
    }

    /* Set input pointer on to point to character after PS_DEVICE_DELIMITER */
    *ppPostScript = ptbInput;
  }

  return strlen_int32( (char *)pPlatform ) - nInitialLength;
}

extern uint32 PSFilenameToDevice (uint8 * pszOutput, uint8 * pszInput)
{
  (void) PSFilenameSplitDeviceAndName (pszInput, pszOutput);
  return strlen_uint32( (char *)pszOutput );
}
