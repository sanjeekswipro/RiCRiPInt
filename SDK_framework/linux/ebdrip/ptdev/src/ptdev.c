/* Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * Microsoft PrintTicket device which handles the merge, validation and RIP
 * configuration for XPS print tickets passed to it while processing a
 * XPS job.
 *
 * $HopeName: SWptdev!src:ptdev.c(EBDSDK_P.1) $
 */

/**
 * \file
 * \brief Private interface for Microsoft PrintTicket.
 */

#include <string.h>
#include <stdlib.h>

#include "dynstring.h"
#include "ptincs.h"
#include "printticket.h"

#include "ggxml.h"
#include "swdevice.h"
#include "xpsconf.h"

#include "ptdev.h"

#ifdef INRIP_PTDEV
#include "file.h"
#include "skindevs.h"
#else
#define LONGESTFILENAME  256
#endif

#define MAX_PAGERANGE  256

/**
 * Structure used when converting file descriptor to PrintTicket level and
 * start/end flag.
 */
typedef struct PT_FILE {
  char*   name;       /* Allowed name */
  int32   openflags;  /* Required open flags */
  int32   isopen;     /* File is currently open */
  DEVICE_FILEDESCRIPTOR (*open)(DEVICE_FILEDESCRIPTOR fd); /* Open callback */
  int32   (*write)(uint8* buffer, int32* len); /* Write callback */
  int32   (*read)(uint8* buffer, int32* len); /* Read callback */
  int32   (*close)(DEVICE_FILEDESCRIPTOR fd, int32 abort); /* Close/abort callback */
  int32   (*iseof)(DEVICE_FILEDESCRIPTOR fd); /* eof callback */
} PT_FILE;


/**
 * @brief  Structure to hold device-specific state.
 */
typedef struct DeviceState
{
  uint8 pszOSFontDir[LONGESTFILENAME];
  uint8 pszPageRange[MAX_PAGERANGE];
} DeviceState;


/**
 * PT Device parameters.
 */

#define PARAM_READONLY  (0x00) /** Param cannot be set. */
#define PARAM_SET       (0x01) /** Param has been set. */
#define PARAM_WRITEABLE (0x02) /** Param can be set. */
#define PARAM_RANGE     (0x04) /** Param has range to be checked on being set. */
#undef  MAXINT
#define MAXINT          0x7fffffff

typedef struct PT_DEVICE_PARAM {
  DEVICEPARAM param;        /**< Param details. */
  int32       flags;        /**< Flags controlling setting and access of the parameter. */
  int32       minval;       /**< Parameter minimum value where applicable. */
  int32       maxval;       /**< Parameter maximum value where applicable. */
} PT_DEVICE_PARAM;

#define PARAM_NAME(n)         STRING_AND_LENGTH(n)
#define INT32_VALUE(n)        ((void*)((intptr_t)(n)))  /* To avoid casting warnings */

static PT_DEVICE_PARAM pt_device_params[] = {
#define PT_PARAM_TYPE     (0)
  { {PARAM_NAME("Type"), ParamString, STRING_AND_LENGTH("FileSystem")},
    PARAM_READONLY|PARAM_SET, 0, 0},
#define PT_PARAM_ERRNO    (1)
  { {PARAM_NAME("ErrorNo"), ParamInteger, NULL, 0},
    PARAM_READONLY|PARAM_SET, 0, 0},
#define PT_PARAM_ERRLINE  (2)
  { {PARAM_NAME("ErrorLine"), ParamInteger, NULL, 0},
    PARAM_READONLY|PARAM_SET, 0, 0},
#define PT_PARAM_ERRCOL   (3)
  { {PARAM_NAME("ErrorColumn"), ParamInteger, NULL, 0},
    PARAM_READONLY|PARAM_SET, 0, 0},
#define PT_PARAM_ERRMSG   (4)
  { {PARAM_NAME("ErrorMessage"), ParamString, STRING_AND_LENGTH("")},
    PARAM_READONLY|PARAM_SET, 0, 0},
#define PT_PARAM_ABORTJOB (5)
  { {PARAM_NAME("AbortJob"), ParamBoolean, NULL, 0},
    PARAM_WRITEABLE, 0, 0},

#define PT_PARAM_NEXTPAGE (6)
  { {PARAM_NAME("NextPage"), ParamInteger, INT32_VALUE(XPSPT_PAGES_ALL), 0},
    PARAM_READONLY|PARAM_SET, XPSPT_PAGES_NOMORE, MAXINT},
#define PT_PARAM_PAGERANGE (7)
  { {PARAM_NAME("PageRange"), ParamString, STRING_AND_LENGTH("")},
    PARAM_WRITEABLE|PARAM_SET, 0, 0},
#define PT_PARAM_PAGERANGE_GROUPINDEX (8)
  { {PARAM_NAME("PageRangeGroupIndex"), ParamInteger, NULL, 0},
    PARAM_READONLY|PARAM_SET, 0, MAXINT},
#define PT_PARAM_PAGERANGE_GROUPOFFSET (9)
  { {PARAM_NAME("PageRangeGroupOffset"), ParamInteger, NULL, 0},
    PARAM_WRITEABLE|PARAM_SET, 0, MAXINT},

/* The number of times each page should be printed. */
#define PT_PARAM_PAGEREPEATCOUNT (10)
  { {PARAM_NAME("PageRepeatCount"), ParamInteger, INT32_VALUE(1), 0},
    PARAM_WRITEABLE|PARAM_SET, 0, MAXINT},

/* The number of times the current page has been printed. */
#define PT_PARAM_CURRENTPAGEREPEAT (11)
  { {PARAM_NAME("CurrentPageRepeat"), ParamInteger, NULL, 0},
    PARAM_WRITEABLE|PARAM_SET, 0, MAXINT},

/* Whether we're getting RIP callbacks specifically for counting pages. */
#define PT_PARAM_NEEDPAGECOUNT (12)
  { {PARAM_NAME("NeedPageCount"), ParamBoolean, NULL, 0},
    PARAM_WRITEABLE|PARAM_SET, 0, 0},

/* Job page count. */
#define PT_PARAM_PAGECOUNT (13)
  { {PARAM_NAME("PageCount"), ParamInteger, INT32_VALUE(-1), 0},
    PARAM_WRITEABLE|PARAM_SET, -1, MAXINT},

#define PT_PARAM_FONTFOLDER (14)
  { {PARAM_NAME("FontFolder"), ParamString, STRING_AND_LENGTH("")},
    PARAM_WRITEABLE|PARAM_SET, 0, 0},

#define PT_PARAM_DIGSIG_STATUS (15)
  { {PARAM_NAME("DigitalSignatureStatus"), ParamString,
    STRING_AND_LENGTH("NotChecked")},
    PARAM_WRITEABLE|PARAM_SET, 0, 0},
};

#define NUM_PT_PARAMS   (sizeof(pt_device_params)/sizeof(pt_device_params[0]))

#define PT_PARAM_SET(i) ((pt_device_params[i].flags & PARAM_SET) != 0)


/* Wrapper functions to convert file descriptor to PrintTicket level and
 * start/end flag */
#define LEVEL_FROM_FD(fd)   DEVICE_FILEDESCRIPTOR_TO_INT32(((fd) % 3))
#define LEVEL_IS_START(fd)  ((fd) < 3)


/**
 * \brief Set a page range of the form '1-n', where n is the page count held
 * in the \c PT_PARAM_PAGECOUNT device param.
 */
static void setDefaultPageRange () 
{
  PSString* strFullRange;
  if (PSStringOpen (&strFullRange))
  {
    char* pbz;
    PSStringAppendString (strFullRange, "1-");
    PSStringAppendInt (strFullRange, theDevParamInteger(pt_device_params[PT_PARAM_PAGECOUNT].param));
    pbz = PSStringCopyBuffer (strFullRange);
    if (pbz)
    {
      uint8* pFullRange = theDevParamString(pt_device_params[PT_PARAM_PAGERANGE].param);
      strncpy ((char*) pFullRange, pbz, MAX_PAGERANGE);
      theDevParamStringLen(pt_device_params[PT_PARAM_PAGERANGE].param) = strlen_int32 ((char*) pFullRange);
      MemFree (pbz);
    }

    PSStringClose (strFullRange);
  }
}

static
DEVICE_FILEDESCRIPTOR config_open(DEVICE_FILEDESCRIPTOR fd)
{
  int32 nLevel = LEVEL_FROM_FD(fd);
  int32 fStart = LEVEL_IS_START(fd);

  switch (nLevel)
  {
  case 1: /* Document level */
    if (fStart)
    {
      /* Reset page range processing state when starting a new document */
      theDevParamStringLen(pt_device_params[PT_PARAM_PAGERANGE].param) = 0;
      theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPINDEX].param) = 0;
      theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPOFFSET].param) = 0;
      theDevParamInteger(pt_device_params[PT_PARAM_CURRENTPAGEREPEAT].param) = theDevParamInteger(pt_device_params[PT_PARAM_PAGEREPEATCOUNT].param);

      /* Set a default page range covering the entire document when we want
         to output each page >1 times. */
      if (theDevParamStringLen(pt_device_params[PT_PARAM_PAGERANGE].param) == 0 &&
          theDevParamInteger(pt_device_params[PT_PARAM_PAGEREPEATCOUNT].param) > 1 &&
          theDevParamInteger(pt_device_params[PT_PARAM_PAGECOUNT].param) > 0)
      {
        setDefaultPageRange ();
      }
    }
    else
    {
      /* Reset page counting flag.  This will be done when the RIP has finished
         calling us back with page count info after we returned XPSPT_COUNT_PAGES. */
      theDevParamBoolean(pt_device_params[PT_PARAM_NEEDPAGECOUNT].param) = FALSE;
    }
    break;

  case 2: /* Page level */
    if (! fStart)
    {
      /* We now know the page to print, but we may need it printing multiple times. */
      if (-- theDevParamInteger(pt_device_params[PT_PARAM_CURRENTPAGEREPEAT].param) == 0)
      {
        /* Advance to next page */
        theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPOFFSET].param) ++;
        theDevParamInteger(pt_device_params[PT_PARAM_CURRENTPAGEREPEAT].param) = theDevParamInteger(pt_device_params[PT_PARAM_PAGEREPEATCOUNT].param);
      }
    }
    break;

  default:
    break;
  }

  return(cfg_open(nLevel, fStart));
}

static
int32 config_close(
                   DEVICE_FILEDESCRIPTOR fd,
                   int32 abort)
{
  return(cfg_close(LEVEL_FROM_FD(fd), LEVEL_IS_START(fd), abort));
}

static
int32 config_eof(
                 DEVICE_FILEDESCRIPTOR fd)
{
  return(cfg_eof(LEVEL_FROM_FD(fd), LEVEL_IS_START(fd)));
}


/**
 * Array of files that a PT device must support.  The first is for receiving
 * the FixedPage page details, the next 3 are for receiving PrintTicket XML and
 * sending start configuration PostSciript, and the last 6 are for sending
 * end configuration PostScript.
 */
static PT_FILE pt_files[] = {
  /* Files to receive PrintTicket and give start configuration PS */
  {"JS", SW_RDWR, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},
  {"DS", SW_RDWR, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},
  {"PS", SW_RDWR, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},

  /* Files to give end configuration PS */
  {"JE", SW_RDONLY, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},
  {"DE", SW_RDONLY, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},
  {"PE", SW_RDONLY, 0,
    config_open, cfg_write, cfg_read, config_close, config_eof},

  /* File to receive FixedPage details */
  {"PD", SW_WRONLY, 0,
    gg_open, gg_write, gg_read, gg_close, gg_eof}
};

#define NUM_PT_FILES  (sizeof(pt_files)/sizeof(pt_files[0]))


/**
 * The PT device callbacks
 */

#ifndef INRIP_PTDEV
#include "threadapi.h"
/* ---------------------------------------------------------------------- */
/*
 * skindevices_last_error/skindevices_set_last_error are not provided by the rip
 * so ptdev must define them for itself.
 */
static pthread_key_t ptdev_key;

int32 RIPCALL skindevices_last_error(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  return CAST_INTPTRT_TO_INT32((intptr_t)pthread_getspecific(ptdev_key));
}

void skindevices_set_last_error(int32 error)
{
  (void)pthread_setspecific(ptdev_key, (void*)((intptr_t)error));
}

int32 ptdev_start_last_error(void)
{
  if ( pthread_key_create(&ptdev_key, NULL) != 0 )
    return -1;
  skindevices_set_last_error(DeviceNoError);
  return 0;
}

void ptdev_finish_last_error(void)
{
  (void)pthread_key_delete(ptdev_key);
}
#endif /* !INRIP_PTDEV */

#define RESET_ERROR()     skindevices_set_last_error(DeviceNoError)

static
int32 RIPCALL ptdev_set_error(
  int32   error,
  int32   retcode)
{
  XML_ERR_DETAILS details;

  /* Get details of any XML parsing error */
  if ( pt_xml_error_details(&details) ) {
    theDevParamInteger(pt_device_params[PT_PARAM_ERRNO].param) = details.code;
    theDevParamInteger(pt_device_params[PT_PARAM_ERRLINE].param) = details.lineno;
    theDevParamInteger(pt_device_params[PT_PARAM_ERRCOL].param) = details.columnno;
    theDevParamString(pt_device_params[PT_PARAM_ERRMSG].param) = details.errmsg;
    theDevParamStringLen(pt_device_params[PT_PARAM_ERRMSG].param) = details.errmsglen;
  }

  skindevices_set_last_error(error);
  return(retcode);
}

static
int32 RIPCALL ptdev_device_init(
  DEVICELIST* dev)
{
  DeviceState* pDeviceState = (DeviceState*) dev->private_data;

  RESET_ERROR();

  if ( !cfg_start() ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

#ifdef INRIP_PTDEV
  /* Set the /FontFolder device parameter */
  if (PKOSFontDir (pDeviceState->pszOSFontDir))
  {
    theDevParamString(pt_device_params[PT_PARAM_FONTFOLDER].param) = pDeviceState->pszOSFontDir;
    theDevParamStringLen(pt_device_params[PT_PARAM_FONTFOLDER].param) = strlen_int32 ((char*) pDeviceState->pszOSFontDir);
  }

#endif
  /* Set a default (empty) page range. */
  theDevParamString(pt_device_params[PT_PARAM_PAGERANGE].param) = pDeviceState->pszPageRange;
  theDevParamStringLen(pt_device_params[PT_PARAM_PAGERANGE].param) = 0;

  return(0);
}

static
DEVICE_FILEDESCRIPTOR RIPCALL ptdev_open_file(
  DEVICELIST* dev,
  uint8*      filename,
  int32       openflags)
{
  int32 fd;

  UNUSED_PARAM(DEVICELIST*, dev);

  RESET_ERROR();

  /* Get descriptor for filename if known */
  if ( strlen((char*)filename) != 2 ) {
    return(ptdev_set_error(DeviceUndefined, -1));
  }
  for ( fd = 0; fd < NUM_PT_FILES; fd++ ) {
    if ( strcmp(pt_files[fd].name, (char*)filename) == 0 ) {
      break;
    }
  }
  if ( fd == NUM_PT_FILES ) {
    return(ptdev_set_error(DeviceUndefined, -1));
  }

  /* Check that file is being opened as expected and is not already open */
  if ( ((openflags & pt_files[fd].openflags) != pt_files[fd].openflags) ||
       pt_files[fd].isopen ) {
    return(ptdev_set_error(DeviceInvalidAccess, -1));
  }

  /* Open the file */
  if ( !pt_files[fd].open(fd) ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* Mark the file as now open */
  pt_files[fd].isopen = TRUE;

  /* Use the pt_files index as the file descriptor */
  return(fd);
}

static
int32 RIPCALL ptdev_read_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  uint8*           buff,
  int32            len)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  RESET_ERROR();

  /* Invalid descriptor or file is not open */
  if ( (descriptor < 0) || (descriptor > NUM_PT_FILES) ||
       !pt_files[descriptor].isopen ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* Call file reader function */
  if ( !pt_files[descriptor].read(buff, &len) ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* Return number of bytes actually read */
  return(len);
}

static
int32 RIPCALL ptdev_write_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  uint8*           buff,
  int32            len)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  RESET_ERROR();

  /* Invalid descriptor or file is not open */
  if ( (descriptor < 0) || (descriptor > NUM_PT_FILES) ||
       !pt_files[descriptor].isopen ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* Call the file writer function */
  if ( !pt_files[descriptor].write(buff, &len) ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* Return number of bytes written */
  return(len);
}

static
int32 close_file(
  DEVICE_FILEDESCRIPTOR descriptor,
  int32            abort)
{
  /* Invalid descriptor or file is not open */
  if ( (descriptor < 0) || (descriptor > NUM_PT_FILES) ||
       !pt_files[descriptor].isopen ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  /* File is no longer open */
  pt_files[descriptor].isopen = FALSE;

  /* Close the file */
  if ( !pt_files[descriptor].close(descriptor, abort) ) {
    return(ptdev_set_error(DeviceIOError, -1));
  }

  return(0);
}

static
int32 RIPCALL ptdev_close_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  RESET_ERROR();

  return(close_file(descriptor, FALSE));
}

static
int32 RIPCALL ptdev_abort_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  RESET_ERROR();

  return(close_file(descriptor, TRUE));
}

static
int32 RIPCALL ptdev_seek_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  Hq32x2*          destination,
  int32            flags)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(Hq32x2*, destination);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(int32, flags);

  /* File seek is not supported on a PT device. */
  RESET_ERROR();
  return(ptdev_set_error(DeviceIOError, FALSE));
}

static
int32 RIPCALL ptdev_bytes_file(
  DEVICELIST*      dev,
  DEVICE_FILEDESCRIPTOR descriptor,
  Hq32x2*          bytes,
  int32            reason)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  RESET_ERROR();

  /* Invalid descriptor, file is not open, file not open for read, or asking for
   * file extent is a device error */
  if ( (descriptor < 0) || (descriptor > NUM_PT_FILES) ||
       !pt_files[descriptor].isopen ||
       (reason == SW_BYTES_TOTAL_ABS) ||
       ((pt_files[descriptor].openflags & (SW_RDONLY|SW_RDWR)) == 0) ) {
    return(ptdev_set_error(DeviceIOError, FALSE));
  }

  /* Return false if file has reached EOF, but don't set last_error */
  if ( pt_files[descriptor].iseof(descriptor) ) {
    Hq32x2FromInt32(bytes, -1);
    return(ptdev_set_error(DeviceIOError, FALSE));
  }

  /* Just indicate that there are some bytes available */
  Hq32x2FromUint32(bytes, 0);
  return(TRUE);
}

static
int32 RIPCALL ptdev_status_file(
  DEVICELIST* dev,
  uint8*      filename,
  STAT*       statbuff)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(uint8*, filename);
  UNUSED_PARAM(STAT*, statbuff);

  /* Named file querying is not supported on a PT device. */
  RESET_ERROR();
  return(ptdev_set_error(DeviceIOError, -1));
}

static
void* RIPCALL ptdev_start_file_list(
  DEVICELIST* dev,
  uint8*      pattern)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(uint8*, pattern);

  /* Returning NULL with no device error makes the RIP skip to the next device */
  RESET_ERROR();
  return(NULL);
}

static
int32 RIPCALL ptdev_next_file(
  DEVICELIST* dev,
  void**      handle,
  uint8*      pattern,
  FILEENTRY*  entry)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(void*, handle);
  UNUSED_PARAM(uint8*, pattern);
  UNUSED_PARAM(FILEENTRY*, entry);

  /* Should not be called */
  return(FileNameNoMatch);
}

static
int32 RIPCALL ptdev_end_file_list(
  DEVICELIST* dev,
  void*       handle)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(void*, handle);

  /* Should not be called */
  return(0);
}

static
int32 RIPCALL ptdev_rename_file(
  DEVICELIST* dev,
  uint8*      file1,
  uint8*      file2)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(uint8*, file1);
  UNUSED_PARAM(uint8*, file2);

  RESET_ERROR();

  /* File renaming is not supported on a PT device */
  return(ptdev_set_error(DeviceInvalidAccess, -1));
}

static
int32 RIPCALL ptdev_delete_file(
  DEVICELIST* dev,
  uint8*      filename)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(uint8*, filename);

  RESET_ERROR();

  /* File delete is not supported on a PT device. */
  return(ptdev_set_error(DeviceInvalidAccess, -1));
}

/**
 * @brief Extract a single page range group string from the \c PageRange parameter.
 *
 * For example, the 2nd group in "1,3-4,5-7" is "3-4".
 *
 * @param[in,out] strDest  Buffer to hold the requested page range group.
 * @param[in] nBytes  The number of available in \c strDest.
 * @param[in] nPageGroupIndex  A zero-based index specifying the required group.
 * @return \c TRUE on success, \c FALSE otherwise.
 *
 * @see getNextPageNumber()
 */
static
int32 getPageRangeGroup (uint8* strDest, size_t nBytes, int32 nPageGroupIndex)
{
  DEVICEPARAM* pParam = &(pt_device_params[PT_PARAM_PAGERANGE].param);
  int32 nPageRangeBytes = theIDevParamStringLen(pParam);
  int32 nStartCharIndex, nCharsInGroup;
  int32 nCurrentGroupIndex = 0;

  /* Find start of required page range group */
  nStartCharIndex = 0;
  while (nStartCharIndex < nPageRangeBytes && nCurrentGroupIndex < nPageGroupIndex)
  {
  	if (theIDevParamString(pParam)[nStartCharIndex ++] == ',')
      nCurrentGroupIndex ++;
  }

  if (nStartCharIndex == nPageRangeBytes)
  {
    /* Page group index not found */
    return FALSE;
  }

  /* Copy page range group to destination buffer */
  nCharsInGroup = 0;
  while (nStartCharIndex + nCharsInGroup < nPageRangeBytes &&
         theIDevParamString(pParam)[nStartCharIndex + nCharsInGroup] != ',')
  {
    if (nCharsInGroup + 1 > CAST_SIZET_TO_INT32(nBytes))
    {
      /* Destination buffer not big enough */
      return FALSE;
    }

    strDest[nCharsInGroup] = theIDevParamString(pParam)[nStartCharIndex + nCharsInGroup];
    nCharsInGroup ++;
  }
  strDest[nCharsInGroup] = '\0';

  return TRUE;
}

/**
 * @brief Get the start and end page numbers for a specific page range group.
 *
 * @param[in] nPageGroupIndex
 * @param[out] pStartPage
 * @param[out] pEndPage
 * @return \c TRUE on success, \c FALSE otherwise.
 *
 * @see getNextPageNumber()
 */
static
int32 getPageRangeStartAndEnd (int32 nPageGroupIndex,
                               int32* pStartPage, int32* pEndPage)
{
  uint8 strPageGroup[32];
  uint8* strPageStart;
  uint8* strPageEnd;
  long lVal;

  /* Get page range group from the PageRange parameter */
  if (! getPageRangeGroup (strPageGroup, sizeof (strPageGroup), nPageGroupIndex))
  {
    return FALSE;
  }

  /* Find start and end page ranges */
  strPageStart = strPageGroup;
  strPageEnd = (uint8*) strchr ((char*) strPageGroup, '-');
  if (strPageEnd)
  {
    *strPageEnd = '\0';
    strPageEnd ++;
  }
  else
  {
    strPageEnd = strPageStart;
  }

  /* Convert page range strings to integers */
  lVal = strtol ((const char*) strPageStart, NULL, 10);
  if (lVal < 1 || lVal == LONG_MAX)
    return FALSE;
  *pStartPage = CAST_SIGNED_TO_INT32 (lVal);

  lVal = strtol ((const char*) strPageEnd, NULL, 10);
  if (lVal < 1 || lVal == LONG_MAX)
    return FALSE;
  *pEndPage = CAST_SIGNED_TO_INT32 (lVal);

  return TRUE;
}

/**
 * @brief Calculate the next page number to print, taking into account the
 * \c PageRange device parameter (if set).
 *
 * If no \c PageRange is set then all pages will be printed in order.
 * \c PageRange can be set according to the XPS PrintSchema. Examples could be
 * "1-5", "1-2,4-5", "1,2,5", "5,3,1", "5-1" (in this latter case pages
 * 5, 4, 3, 2, and 1 are printed in that order).
 *
 * The \c PageRangeGroupIndex and \c PageRangeGroupOffset device parameters are
 * automatically updated to keep track of state. \c PageRangeGroupIndex is a
 * zero-based index into a range 'group'. In the case of "1,4-5" the first
 * group is "1" and the second is "4-5". \c PageRangeGroupOffset is a zero-based
 * page offset into the group currently being printed.
 *
 * It is possible to obtain the full sequence of pages specified in \c PageRange
 * without actually having do any RIPping. First set the \c PageRange (which will
 * automatically reset \c PageRangeGroupIndex and \c PageRangeGroupOffset), and then
 * examine the \c NextPage parameter. Then increment \c PageRangeGroupOffset and 
 * examine \c NextPage, repeating until \c NextPage returns \c XPSPT_PAGES_NOMORE (-2).
 * Note that \c PageRangeGroupOffset could be reset by quizzing \c NextPage, so
 * increment whatever value it is currently set as.
 */
static
int32 getNextPageNumber ()
{
  DEVICEPARAM* pParam;
  int32 nPageRangeStart, nPageRangeEnd;
  int32 nPageGroupIndex;
  int32 nNextPage;
  int32 fIsEndOfGroup;

  if (theDevParamInteger(pt_device_params[PT_PARAM_PAGEREPEATCOUNT].param) == 1)
  {
    if (theDevParamStringLen(pt_device_params[PT_PARAM_PAGERANGE].param) == 0)
    {
      /* No page range set, so print all pages in order */
      return XPSPT_PAGES_ALL;
    }
  }
  else
  {
    /* To print each page multiple times we need the total page count. */
    if (theDevParamBoolean(pt_device_params[PT_PARAM_NEEDPAGECOUNT].param))
    {
      /* Returning XPSPT_COUNT_PAGES causes the RIP to call us for each page
         in the job, allowing us to increment a page count. After this we
         can explicitly tell the RIP which page we require processing,
         allowing us to print each page multiple times. */
      theDevParamInteger(pt_device_params[PT_PARAM_PAGECOUNT].param) ++;
      return XPSPT_COUNT_PAGES;
    }
  }

  /* Get start and end page numbers for the current page group */
  nPageGroupIndex = theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPINDEX].param);
  if (! getPageRangeStartAndEnd (nPageGroupIndex,
                                 &nPageRangeStart, &nPageRangeEnd))
  {
    /* No more page ranges to process */
    return XPSPT_PAGES_NOMORE;
  }

  /* Calculate the next page number to print */
  pParam = &(pt_device_params[PT_PARAM_PAGERANGE_GROUPOFFSET].param);
  if (nPageRangeStart <= nPageRangeEnd)
  {
    nNextPage = nPageRangeStart + theIDevParamInteger(pParam);
    fIsEndOfGroup = (nNextPage > nPageRangeEnd);
  }
  else
  {
    nNextPage = nPageRangeStart - theIDevParamInteger(pParam);
    fIsEndOfGroup = (nNextPage < nPageRangeEnd);
  }

  if (fIsEndOfGroup)
  {
    /* Finished processing pages in current group */
    theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPINDEX].param) ++;
    theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPOFFSET].param) = 0;
    return getNextPageNumber ();
  }

  return nNextPage;
}

static
int32 RIPCALL ptdev_set_param(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  int32 i;

  UNUSED_PARAM(DEVICELIST*, dev);

  /* Find the parameter in list of supported parameters */
  for ( i = 0; i < NUM_PT_PARAMS; i++ ) {
    if ( (theIDevParamNameLen(param) == theDevParamNameLen(pt_device_params[i].param)) &&
         (strncmp((char*)theIDevParamName(param),
                  (char*)theDevParamName(pt_device_params[i].param),
                  CAST_SIGNED_TO_SIZET(theDevParamNameLen(pt_device_params[i].param))) == 0) ) {
      break;
    }
  }
  if ( i >= NUM_PT_PARAMS ) {
    return(ParamIgnored);
  }

  /* Check the parameter is writable and the passed in value has the right type */
  if ( (pt_device_params[i].flags&PARAM_WRITEABLE) == 0 ) {
    return(ParamConfigError);
  }
  if ( theDevParamType(pt_device_params[i].param) != theIDevParamType(param) ) {
    return(ParamConfigError);
  }

  /* Update devices parameter value according to type */
  switch ( theIDevParamType(param) ) {
  case ParamBoolean:
    theDevParamBoolean(pt_device_params[i].param) = theIDevParamBoolean(param);
    if ( i == PT_PARAM_ABORTJOB ) {
      /* Add callback to handle change in abortjob value */
    }
    break;

  case ParamString:
    if ( i == PT_PARAM_DIGSIG_STATUS )
    {
      /* Special case when setting /DigitalSignatureStatus to "CheckNow".
         Call over to the configuration layer's checking agent (if supplied).
         The string "CheckNow" is not actually installed as the new param
         value. Instead, the result of cfgGetDigitalSignatureStatus() is
         used. This idiom gives PostScript code the opportunity to
         effectively issue commands to the environment beyond the RIP, so
         it is quite common for skin devices to intercept certain parameters
         and take action in this way. */
      if ( strncmp
             (
               (char*) theIDevParamString( param ),
               "CheckNow",
               CAST_SIGNED_TO_SIZET( theIDevParamStringLen( param ) )
             ) == 0 )
      {
        const char *pszStatus = cfgCheckDigitalSignatures();
        theDevParamString(pt_device_params[i].param) = (uint8*) pszStatus;
        theDevParamStringLen(pt_device_params[i].param) = strlen_int32( pszStatus );
      }
      else
      {
        /* Ignore any attempt to set /DigitalSignatureStatus unless the value
           is "CheckNow". */
        return(ParamIgnored);
      }
    }
    else
    {
      if (i == PT_PARAM_PAGERANGE)
      {
        /* Copy new string into the page range buffer. */
        theDevParamStringLen(pt_device_params[i].param) = theIDevParamStringLen(param);
        memcpy (theDevParamString(pt_device_params[i].param),
                theIDevParamString(param),
                theIDevParamStringLen(param));

        /* Resetting PageRange should also reset page index and offset */
        theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPINDEX].param) = 0;
        theDevParamInteger(pt_device_params[PT_PARAM_PAGERANGE_GROUPOFFSET].param) = 0;
      }
      else
      {
        /* Just store the supplied string. */
        theDevParamString(pt_device_params[i].param) = theIDevParamString(param);
        theDevParamStringLen(pt_device_params[i].param) = theIDevParamStringLen(param);
      }
    }
    break;

  case ParamInteger:
    theDevParamInteger(pt_device_params[i].param) = theIDevParamInteger(param);
    break;

  default:
    return(ParamIgnored);
  }

  /* Note that parameter is now set */
  pt_device_params[i].flags |= PARAM_SET;

  return(ParamAccepted);
}

/* Index of next device parameter to return */
static uint32 next_param = 0;

static
int32 RIPCALL ptdev_start_param(
  DEVICELIST* dev)
{
  int32 param;
  int32 count;

  UNUSED_PARAM(DEVICELIST*, dev);

  /* Reset index of next parameter to return */
  next_param = 0;

  /* Count number of parameters with values that have been set */
  count = 0;
  for ( param = 0; param < NUM_PT_PARAMS; param++ ) {
    if ( PT_PARAM_SET(param) ) {
      count++;
    }
  }

  return(count);
}

static
int32 RIPCALL ptdev_get_param(
  DEVICELIST*   dev,
  DEVICEPARAM*  param)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  if ( theIDevParamName(param) == NULL ) {
    /* Get next set parameter */
    for ( ; next_param < NUM_PT_PARAMS; next_param++ ) {
      if ( PT_PARAM_SET(next_param) ) {
        break;
      }
    }
    if ( next_param >= NUM_PT_PARAMS ) {
      return(ParamIgnored);
    }

  } else { /* Look for named parameter */
    for ( next_param = 0; next_param < NUM_PT_PARAMS; next_param++ ) {
      if ( (theIDevParamNameLen(param) == theDevParamNameLen(pt_device_params[next_param].param)) &&
           (strncmp((char*)theIDevParamName(param),
                   (char*)theDevParamName(pt_device_params[next_param].param),
                   CAST_SIGNED_TO_SIZET(theDevParamNameLen(pt_device_params[next_param].param))) == 0) ) {
        break;
      }
    }
    if ( (next_param >= NUM_PT_PARAMS) || !PT_PARAM_SET(next_param) ) {
      return(ParamIgnored);
    }
  }

  if (next_param == PT_PARAM_NEXTPAGE)
  {
    /* Generate NextPage param dynamically */
    theDevParamInteger(pt_device_params[PT_PARAM_NEXTPAGE].param) = getNextPageNumber ();
  }

  /* Return parameter */
  memcpy(param, &pt_device_params[next_param].param, sizeof(DEVICEPARAM));

  /* Continue next tie with next parameter in list */
  next_param++;
  return(ParamAccepted);
}

static
int32 RIPCALL ptdev_status_device(
  DEVICELIST* dev,
  DEVSTAT*    devstat)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(DEVSTAT*, devstat);

  /* Not meaningful for a PrintTicket device. */
  RESET_ERROR();
  return(ptdev_set_error(DeviceIOError, -1));
}

static
int32 RIPCALL ptdev_device_dismount(
  DEVICELIST* dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  (void)cfg_end();

  return(0);
}

static
int32 RIPCALL ptdev_buffer_size(
  DEVICELIST* dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);

  return(FILE_BUFFER_SIZE);
}

static
int32 RIPCALL ptdev_ioctl(
  DEVICELIST* dev,
  DEVICE_FILEDESCRIPTOR fd,
  int32       opcode,
  intptr_t    arg)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(int32, opcode);
  UNUSED_PARAM(intptr_t, arg);

  /* Should not be called */
  return(-1);
}

/*=============================================================================*/
/**
 * @brief The device type structure for PrintTicket devices.
 */
DEVICETYPE XpsPrintTicket_Device_Type = {
  XPSPT_DEVICE_TYPE,              /* the device type number */
  DEVICERELATIVE | DEVICEWRITABLE ,
                                  /* device characteristics flags */
  sizeof (DeviceState),           /* the size of the private data */
  0,                              /* tickle function control: n/a */
  NULL,                           /* procedure to service the device */
  skindevices_last_error,         /* return last error for this device */
  ptdev_device_init,              /* call to initialise device */
  ptdev_open_file,                /* call to open file on device */
  ptdev_read_file,                /* call to read data from file on device */
  ptdev_write_file,               /* call to write data to file on device */
  ptdev_close_file,               /* call to close file on device */
  ptdev_abort_file,               /* call to abort file on device*/
  ptdev_seek_file,                /* call to seek file on device */
  ptdev_bytes_file,               /* call to get bytes avail on an open file */
  ptdev_status_file,              /* call to check status of file */
  ptdev_start_file_list,          /* call to start listing files */
  ptdev_next_file,                /* call to get next file in list */
  ptdev_end_file_list,            /* call to end listing */
  ptdev_rename_file,              /* call to rename file on the device */
  ptdev_delete_file,              /* call to remove file from device */
  ptdev_set_param,                /* call to set device parameter */
  ptdev_start_param,              /* call to start getting device parameters */
  ptdev_get_param,                /* call to get the next device parameter */
  ptdev_status_device,            /* call to get the status of the device */
  ptdev_device_dismount,          /* call to dismount the device (dummy) */
  ptdev_buffer_size,              /* call to determine buffer size (dummy) */
  ptdev_ioctl,                    /* ioctl slot */
  NULL                            /* unused slot */
};

