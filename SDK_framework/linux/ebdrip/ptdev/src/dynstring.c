/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWptdev!src:dynstring.c(EBDSDK_P.1) $
 */

/**
 * @file
 * @brief Dynamic string implementation based on MFS code.
 *
 * This code uses MFS files to represent strings of arbitrary length.  When a
 * new string is added a file will be created in the root of the virtual file
 * system, identified with a unique name (See \c PSSetStringName()).  Any
 * characters added to the string will be stored as bytes inside that file.
 * The file will exist for the lifetime of its corresponding \c PSString object,
 * being deleted when \c PSStringClose() is called.
 */

#include "dynstring.h"

#include "memfs.h"
#include "swcopyf.h"
#include "swdevice.h" /* SW_CREAT, SW_RDWR */

#include <string.h> /* memcpy */


#define STRING_NAME_LENGTH  (16)  /**< Maximum length of a MFS string filename. */

/**
 * \brief Structure holding information about a string object.
 */
struct PSString
{
  char pbzFilename[STRING_NAME_LENGTH];  /**< Name of the MFS file containing
                                              the string data. */
  MFSFILEDESC* pDesc;  /**< Descriptor of the MFS file. */
};


/**
 * Representation of the virtual file system used to store the strings.
 */
static MFSNODE *rootEntries[] = { NULL };
static MFSDIR rootDir = { 1, rootEntries, FALSE };
static MFSNODE stringRoot = { MFS_Directory, FALSE, FALSE, "root", NULL, &rootDir };


/**
 * \brief Generate and assign a unique MFS filename to the specified string.
 *
 * In this implementation there is an upper limit to the number of string
 * instances which can be stored - See \c kMaxID below.
 *
 * \param pString  The string to assign a filename to.
 * \return \c TRUE on success, \c FALSE otherwise.
 */
static int32 PSSetStringName (PSString* pString) 
{
  uint32 nStringID;
  const uint32 kMaxID = (uint32) -1;

  /* Search for the first unused filename. */
  for (nStringID = 0; nStringID < kMaxID; nStringID ++)
  {
    MFSNODE* pParent;
    uint32 nIndex;

    swncopyf ((uint8*) pString->pbzFilename, sizeof (pString->pbzFilename),
      (uint8*) "id_%04X.txt", nStringID);
    if (! MFSFindRelative (&stringRoot, pString->pbzFilename, &pParent, &nIndex))
    {
      /* An unused filename has been found. */
      return TRUE;
    }
  }

  /* Maximum number of strings in use. */
  return FALSE;
}

int32 PSStringOpen (PSString** ppString)
{
  *ppString = (PSString*) MemAlloc (sizeof (PSString), FALSE, FALSE);

  if (*ppString)
  {
    int32 err;
    PSString* pString = *ppString;

    /* Open a new MFS file. */
    if (PSSetStringName (pString) &&
        MFSOpen (&stringRoot, pString->pbzFilename,
                 SW_CREAT | SW_RDWR, &pString->pDesc, &err))
    {
      /* We don't want any strings to be compressed. */
      if (! MFSSetCompression (pString->pDesc, FALSE))
      {
        PSStringClose (pString);
        return FALSE;
      }
      return TRUE;
    }
  }

  return FALSE;
}

void PSStringClose (PSString* pString)
{
  if (MFSClose (pString->pDesc))
  {
    int32 err;
    (void) MFSDelete (&stringRoot, pString->pbzFilename, &err);
  }
  pString->pbzFilename[0] = '\0';

  MemFree (pString);
}

void PSStringAppendChar (PSString* pString, uint8 ch)
{
  int32 err;
  (void) MFSWrite (pString->pDesc, &ch, sizeof (uint8), &err);
}

void PSStringAppendString (PSString* pString, const char* pbz)
{
  int32 err;
  (void) MFSWriteString (pString->pDesc, (char*) pbz, &err);
}

void PSStringAppendDouble (PSString* pString, double fVal)
{
  char buff[64];
  PSStringAppendString (pString, doubleToStr (buff, sizeof (buff), fVal));
}

void PSStringAppendInt (PSString* pString, int32 nVal)
{
  char buff[64];
  swncopyf ((uint8*) buff, sizeof (buff), (uint8*) "%d", nVal);
  PSStringAppendString (pString, buff);
}

char* PSStringCopyBuffer (PSString* pString)
{
  char* pbzString = NULL;
  MFSFILE* pFile = MFSGetFile (pString->pDesc);
  if (pFile)
  {
    pbzString = (char*) MemAlloc (pFile->cbSize + 1, FALSE, FALSE);
    if (pbzString)
    {
      memcpy (pbzString, pFile->pData, pFile->cbSize);
      pbzString[pFile->cbSize] = '\0';
    }
  }
  return pbzString;
}


