/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:file.h(EBDSDK_P.1) $
 * File related utility functions
 */

#ifndef __FILE_H__
#define __FILE_H__

/* Platform specific parts */
#include "pfile.h"


#define LONGESTFILENAME   2048    /**< The longest file name */


/**
 * \file
 *
 * \ingroup skinkit
 *
 * \brief Platform file abstraction.  Each platform can have its own
 * implementation of these functions.
 */

/**
 * \brief This set of error returns should cover most cases -- at least
 * enough for the RIP to extract the information it needs from
 * this platform abstraction layer.
 */
enum PKError
{
  PKErrorNone,             /**< No error */
  PKErrorUnknown,          /**< Generic error failure */

  /* PARAMETER REJECTED / ILLEGAL VALUE ERRORS */
  PKErrorParameter,        /**< Generic problem with a supplied parameter */
  PKErrorPointerNull,      /**< A pointer was null when it should not have been */
  PKErrorNumericRange,     /**< A value was out of range */
  PKErrorNumericValue,     /**< A value was illegal */
  PKErrorStringEmpty,      /**< A string was empty when it should not have been */
  PKErrorStringLength,     /**< The length of a string was illegal (usually: the string was too long) */
  PKErrorStringSyntax,     /**< A string was syntactically incorrect */
  PKErrorStringValue,      /**< A string had an incorrect value */

  /* Operation denied errors */
  PKErrorOperationDenied,  /**< Generic denial of operation */
  PKErrorNonExistent,      /**< A file mentioned in an operation did not exist */
  PKErrorAlreadyExists,    /**< A file already existed when attempting to create it */
  PKErrorAccessDenied,     /**< Access to a file system resource was denied */
  PKErrorInUse,            /**< A filesystem resource is already is use */

  /* Operation failed errors */
  PKErrorOperationFailed,  /**< Generic failure */
  PKErrorAbort,            /**< Operation was externally aborted */
  PKErrorNoMemory,         /**< Operation failed due to lack of memory */
  PKErrorDiskFull,         /**< Operation failed due to disk being full or a disk quota being exceed */
  PKErrorSoftwareLimit,    /**< Operation failed due to a software limitation */
  PKErrorHardware,         /**< Operation failed due to hardware error, e.g., an I/O error */
  PKErrorUnimplemented,    /**< Operation failed because it is unimplemented */
  PKErrorFatal             /**< Operation failed, fatally */
};

/**
 * \brief  Opaque file descriptor structure which hides platform-specific content.
 */
typedef struct FileDesc FileDesc;

/**
 * \brief Copy, into the buffer provided, the directory in which the application
 * executable lives. This directory will be terminated with a directory
 * separator.
 *
 * \param pAppDir Buffer to hold application directory name.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKAppDir( uint8 * pAppDir );

/**
 * \brief Copy into the buffer provided the directory in which the application
 * is running. This directory will be terminated with a directory
 * separator. On the Classic Mac, which has no concept of current dir,
 * this is the same as PKAppDir().
 *
 * \param pCurrDir Buffer to hold current directory name.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKCurrDir(uint8 * pCurrDir);

/**
 * \brief Copy into the buffer provided the SW directory name.
 * This directory will be terminated with a directory
 * separator.
 *
 * \param pSWDir Buffer to hold SW directory name.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKSWDir(uint8 * pSWDir);

/**
 * \brief Record an explicit path to the SW folder, overriding any default
 * search rules.
 *
 * \param pSWDir Pointer to a null-terminated path, which may not exceed
 * \c LONGESTFILENAME in length (inclusive of the terminator), and which must end
 * with a directory separator.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 KSetSWDir( uint8 *pSWDir );

/**
 * \brief Get an explicit path to the SW folder, taking into account any path
 * set using KSetSWDir().
 *
 * \param pSWDir Pointer to memory at least \c LONGESTFILENAME bytes in length,
 * which on output contains the SW folder path (terminated with a directory
 * separator).
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 KGetSWDir( uint8* pSWDir );

/**
 * \brief Parse the root part of an absolute filename \c *ppInput.
 *
 * If pOutput is non-NULL the variable part of the root, excluding any
 * platform specific root prefixes and suffices is sent to it.
 * \c *ppInput is updated to point to first unconsumed character of input.
 *
 * For example:
 * <pre>
 *      Input                   Output                  Remaining input
 * PC   "C:\..."                "C"                     "..."
 *      "\\machine\drive\..."   "machine\drive"         "..."
 *
 * Mac  "volume:..."            "volume"                "..."
 *
 * Unix "/..."                  ""                      "..."
 * </pre>
 *
 * \param pOutput The root fragment (if non-NULL)
 * \param ppInput An absolute filename
 *
 * \return \c FALSE if the filename is relative; \c TRUE otherwise
 */
extern int32 PKParseRoot( uint8 * pOutput, uint8 ** ppInput );

/**
 * \brief Performs the inverse of PKParseRoot(), and builds a complete
 * root part of a platform dependent filename from the variable part
 * as previously output by PKParseRoot().
 *
 * \return \c FALSE if the input is badly formed, and no output is generated.
 */
extern int32 PKBuildRoot( uint8 * pOutput, uint8 * pInput );

/**
 * \brief Converts an absolute or relative filename to an absolute
 * PostScript filename.  Relative filenames are made absolute by prefixing the
 * current working directory as returned by PKCurrDir() above.
 */
extern int32 PKMakePSFilename(uint8* filename, uint8* psFilename);

/**
 * \brief Open a file, with the given flags.  (Flags are from
 * swdevice.h.)
 * \param filename Full path of the file to be opened
 * \param openflags Flags to control the way in which the file is opened. See DEVICELIST_OPEN()
 * \param pError A value which the function can set to one of the \c PKError values.
 *
 * \return The file descriptor if open was successful; \c NULL otherwise.
 */
extern FileDesc* PKOpenFile(uint8 *filename, int32 openflags, int32 * pError );

/**
 * \brief Read a specified number of bytes from a file into a buffer.
 * \param pDescriptor The file descriptor, as returned from PKOpenFile().
 * \param buff Buffer into which the data read is stored.
 * \param len The maximum number of bytes to read.
 * \param pError A value which the function can set to one of the \c PKError values.
 * \return The number of bytes read, if successful; -1 otherwise.
 */
extern int32 PKReadFile(FileDesc* pDescriptor, uint8 *buff, int32 len, int32 * pError );

/**
 * \brief Write a specified number of bytes from a buffer into a file.
 * \param pDescriptor The file descriptor, as returned from PKOpenFile().
 * \param buff Buffer into which the data read is stored.
 * \param len The maximum number of bytes to write. 
 * \param pError A value which the function can set to one of the \c PKError values.
 * \return The number of bytes written, if successful; -1 otherwise.
 */
extern int32 PKWriteFile(FileDesc* pDescriptor, uint8 *buff, int32 len, int32 * pError );

/**
 * \brief Close an open file.
 * \param pDescriptor The file descriptor, as returned from PKOpenFile().
 * \param pError A value which the function can set to one of the \c PKError values.
 */
extern int32 PKCloseFile(FileDesc* pDescriptor, int32 * pError );

/**
 * \brief Seek to a byte position within a file, with the given flags.
 * \param pDescriptor The file descriptor, as returned from PKOpenFile().
 * \param destination The location to seek to.
 * \param flags One of SW_SET, SW_INCR, SW_XTND. See DEVICELIST_SEEK().
 * \param pError A value which the function can set to one of the \c PKError values.
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKSeekFile(FileDesc* pDescriptor,  Hq32x2 * destination, int32 flags, int32 * pError );

/**
 * \brief Return the number of bytes in a file in the \c bytes parameter.
 * \param pDescriptor The file descriptor, as returned from PKOpenFile().
 * \param bytes The number of bytes is written into this.
 * \param reason One of \c SW_BYTES_AVAIL_REL or \c SW_BYTES_TOTAL_ABS from swdevice.h.
 * \param pError A value which the function can set to one of the \c PKError values.
 */
extern int32 PKBytesFile(FileDesc* pDescriptor, Hq32x2 * bytes, int32 reason, int32 * pError );

/**
 * \brief Populate a structure with information about a file.
 *
 * \param filename The file to check status. 
 * \param statbuff Pointer to a structure in which to put the file status.
 * \param pError A value which the function can set to one of the \c
 * PKError values.
 *
 * \return 0 on success; -1 otherwise.
 */
extern int32 PKStatusFile(uint8 *filename, STAT *statbuff, int32 * pError );

/**
 * \brief Delete a file.
 *
 * \param filename Full path to the file to be deleted.
 *
 * \param pError A value which the function can set to one of the \c
 * PKError values.
 *
 * \return Zero on success, non-zero otherwise.
 */
extern int32 PKDeleteFile(uint8 *filename, int32 * pError );

/**
 * \brief Find the first file matching a pattern. 
 *
 * \param pszPattern platform file name pattern to be matched
 *
 * \param pszEntryName returns the first matched path Caller must ensure
 *        that its size is at least \c LONGESTFILENAME.
 *
 * \param pError A value which the function can set to one of the
 * PKError values.
 *
 * \return A FindFile handle (\c FindFileState pointer) cast to \c void*,
 * or \c NULL if the operation failed.
 */
extern void * PKFindFirstFile( uint8 * pszPattern, uint8 * pszEntryName, int32 * pError ) ;

/**
 * \brief Enumerate file entries matching a pattern.  It returns, in
 * \c pszEntryName, the path of an entry in the directory.  Calls on
 * the same handle will enumerate the matched entries until all entries 
 * have been enumerated.
 *
 * \param handle A FindFile handle, as returned from PKFindFirstFile().
 *
 * \param pszEntryName [out] Set to the path name of the entry enumerated.
 *        Caller must ensure that its size is at least \c LONGESTFILENAME.
 *
 * \param pError A value which the function can set to one of the
 * \c PKError values.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
 
extern int32 PKFindNextFile(void * handle, uint8 * pszEntryName, int32 * pError) ;

/**
 * \brief Close the file matching specified by the handle.
 *
 * \param handle A FindFile handle, as returned from PKFindFirstFile().
 *
 * \param pError A value which the function can set to one of the
 * \c PKError values.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKCloseFindFile(void * handle, int32 * pError);

/**
 * \brief Open a directory.
 *
 * \param pszDirName Full path to the directory to be opened.
 * \param pError A value which the function can set to one of the
 * PKError values.
 *
 * \return A directory handle (\c FindFileState pointer) cast to \c void*,
 * or \c NULL if the operation failed.
 */
extern void * PKDirOpen( uint8 * pszDirName, int32 * pError);


/**
 * \brief Enumerate entries in a directory.  It returns, in
 * \c pszEntryName, the leaf name of an entry in the directory.  Calls on
 * the same handle will enumerate the entries in the directory until
 * all entries have been enumerated.
 *
 * \param handle A directory handle, as returned from PKDirOpen().
 *
 * \param pszEntryName [out] Set to the leaf name of the entry enumerated.
 *
 * \param fIsFolder [out] Set to \c TRUE if the entry enumerated is a
 * directory, and \c FALSE if it is a file.
 *
 * \param pError A value which the function can set to one of the
 * \c PKError values.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKDirNext(void * handle,
                       uint8 * pszEntryName,
                       int32 * fIsFolder,
                       int32 * pError);

/**
 * \brief Close the directory specified by the handle.
 *
 * \param handle A directory handle, as returned from PKDirOpen().
 *
 * \param pError A value which the function can set to one of the
 * \c PKError values.
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKDirClose(void * handle, int32 * pError);

/**
 * \brief Get an explicit path to the OS font folder. (E.g. %C%/WINDOWS/fonts/)
 *
 * \param pszFontDir Pointer to memory at least \c LONGESTFILENAME bytes in length,
 * which on output contains the OS font folder path (terminated with a directory
 * separator).
 *
 * \return \c TRUE on success; \c FALSE otherwise.
 */
extern int32 PKOSFontDir( uint8* pszFontDir );

#endif
