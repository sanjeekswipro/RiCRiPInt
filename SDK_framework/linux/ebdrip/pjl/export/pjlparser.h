/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpjl!export:pjlparser.h(EBDSDK_P.1) $
 * Interface to PJL parser
 */

#ifndef __PJLPARSER_H__
#define __PJLPARSER_H__

#include "std.h"


/**
 * \file
 *
 * \brief This header file defines the interface to the PJL parser.
 */

/**
 * \defgroup pjlparser PJL parser: Parser for PJL
 * \ingroup leskin_group
 * \{
 */


/**
 * \brief Enumeration of PJL errors and warnings
 */
enum {
  eNoError = 0,

  eGenericSyntaxError            = 20001,
  eUnsupportedCommand            = 20002,
  eUnsupportedPersonality        = 20004,
  eCommandBufferOverflow         = 20005,
  eIllegalCharOrUEL              = 20006,
  eWSorLFmissingAfterQuotes      = 20007,
  eInvalidAlphanumericChar       = 20008,
  eInvalidNumericChar            = 20009,
  eInvalidFirstChar              = 20010,
  eMissingClosingQuote           = 20011,
  eNumericStartsWithDP           = 20012,
  eNumericNoDigits               = 20013,
  eNoAlphanumericAfterModifier   = 20014,
  eNoOptionValue                 = 20015,
  eTooManyModifiers              = 20016,
  eModifierAfterOption           = 20017,
  eNonAlphanumericCommand        = 20018,
  eNumericForAlphanumeric        = 20019,
  eStringForAlphanumeric         = 20020,
  eUnsupportedModifer            = 20021,
  eModifierMissing               = 20022,
  eOptionMissing                 = 20023,
  eExtraDataAfterOption          = 20024,
  eTwoDPs                        = 20025,
  eOutOfMemory                   = 20026,
  eWhiteSpaceBeforeDataStashed   = 20027,
  eWhiteSpaceAndUELPrecedeData   = 20028,

  eGenericWarningError           = 25001,
  eNoPJLPrefix                   = 25002,
  eAlphanumericTooLong           = 25003,
  eStringTooLong                 = 25004,
  eNumericTooLong                = 25005,
  eUnsupportedOption             = 25006,
  eOptionMissingValue            = 25007,
  eOptionWrongValueType          = 25008,
  eOptionUnexpectedValue         = 25009,
  eRepeatedOption                = 25010,
  eOptionIgnoredValueOutOfRange  = 25011,
  eOptionValueDataLossConversion = 25012,
  eOptionValueDataLossRange      = 25013,
  eOptionValueOutOfRange         = 25014,
  eOptionValueUnsupported        = 25016,
  eOptionIgnoredEmptyString      = 25017,
  eMissingUEL                    = 25018,

  eGenericSemanticError          = 27001,
  eEOJwithoutJOB                 = 27002,
  ePasswordProtected             = 27003,
  eReadOnlyVariable              = 27004,
  eUnsettableVariable            = 27005,
  eNullString                    = 27006,
  eUndefaultableVariable         = 27007
};


/**
 * \brief Enumeration of file system errors
 */
enum {
  eGeneralFileSystemError        = 32000,
  eVolumeNotAvailable            = 32001,
  eDiskFull                      = 32002,
  eFileNotFound                  = 32003,
  eNoFreeFileDescriptors         = 32004,
  eInvalidNoOfBytes              = 32005,
  eFileAlreadyExists             = 32006,
  eIllegalName                   = 32007,
  eCannotDeleteRoot              = 32008,
  eFileOpOnDirectory             = 32009,
  eDirectoryOpOnFile             = 32010,
  eNotSameVolume                 = 32011,
  eReadOnly                      = 32012,
  eDirectoryFull                 = 32013,
  eDirectoryNotEmpty             = 32014,
  eBadDisk                       = 32015,
  eNoLable                       = 32016,
  eInvalidParameter              = 32017,
  eNoContiguousSpace             = 32018,
  eCannotChangeRoot              = 32019,
  eFileDescriptorObselete        = 32020,
  eDeleted                       = 32021,
  eNoBlockDevice                 = 32022,
  eBadSeek                       = 32023,
  eInternalError                 = 32024,
  eWriteOnly                     = 32025,
  eWriteProtected                = 32026,
  eNoFilename                    = 32027,

  eEndOfDirectory                = 32051,
  eNoFileSystem                  = 32052,
  eNoMemory                      = 32053,
  eVolumeNameOutOfRange          = 32054,
  eBadFS                         = 32055,
  eHardwareFailure               = 32056,
  eAccessDenied                  = 32064
};


/**
 * \brief Enumeration of personalities
 */
enum {
  ePJL = -3,
  eNeedMoreData = -2,
  eUnknown = -1,
  eNone = 0,
  eAutoPDL,
  ePostScript,
  ePDF,
  eZIP,
  ePCL5c,
  ePCL5e,
  ePCLXL,
  eImage
};


/**
 * \brief Enumeration of value types
 */
enum {
  eValueNone = 0,
  eValueString,
  eValueAlphanumeric,
  eValueInt,
  eValueDouble,
  eValueBinaryData
};


/**
 * \brief Structure to hold the value of an option or modifier
 */
typedef struct PjlValue
{
  int32 eType;

  union
  {
    uint8 * pzValue;
    int32   iValue;
    double  dValue;
  } u;

} PjlValue;


/**
 * \brief Structure to hold an option
 */
typedef struct PjlOption
{
  uint8 * pzName;

  PjlValue value;

  struct PjlOption * pNext;

} PjlOption;
  

/**
 * \brief Structure to hold an environment variable
 */
typedef struct PjlEnvVar
{
  uint8 * pzName;

  PjlOption * pModifier;

  int32 eType;

  void * pPrivate;

} PjlEnvVar;


/**
 * \brief Structure to hold a PJL command
 */
typedef struct PjlCommand
{
  uint8 * pzName;

  PjlOption * pModifier;

  PjlOption * pOptions;

} PjlCommand;


/**
 * \brief Forward declaration of opaque type to hold parser context.
 */
struct PjlParserContext;
typedef struct PjlParserContext PjlParserContext;


/**
 * \brief Defines a memory allocator function which is passed to the
 * PJL parser via PjlParserInit.
 */
typedef void * (PjlMemAllocFn)
  (size_t cbSize, int32 fZero);


/**
 * \brief Defines a memory deallocator function which is passed to the
 * PJL parser via PjlParserInit.
 * 
 * Is passed a pointer to memory allocated by the allocatro function.
 */
typedef void (PjlMemFreeFn)
  (void * pMem);


/**
 * \brief Defines a callback function which is called for each PJL
 * command.
 * 
 * Is passed a pointer to a PjlCommand.  Returns one of the error
 * enumeration values.
 */
typedef int32 (PjlCommandCallback)
  (PjlCommand * pCommand);


/**
 * \brief Defines a callback function which is called when the
 * parser has detected a PJL error or warning.
 * 
 * Is passed one of the error enumeration values.
 */
typedef void (PjlErrorCallback)
  (int32 eError);


/* Functions */

/**
 * \brief Initializes the PJL parser.
 *
 * \param pMemAllocFn Memory allocating function.
 *
 * \param pMemFreeFn Memory deallocating function.
 *
 * \param pCommandCallbackFn Command callback function.
 *
 * \param pErrorCallbackFn Error callback function.
 *
 * \param eDefaultPersonality Default personality to assume.
 * One of the personality enumeration values.
 *
 * \param pEnvironmentVariables Pointer to array of
 * environment variables.
 *
 * \param nEnvironmentVariables Number of entries
 * in pEnvironmentVariables array.
 *
 * \return Context for PJL parser to be passed to other calls.
 */
extern PjlParserContext * PjlParserInit (
  PjlMemAllocFn * pMemAllocFn,
  PjlMemFreeFn * pMemFreeFn,
  PjlCommandCallback * pCommandCallbackFn,
  PjlErrorCallback * pErrorCallbackFn,
  int32 eDefaultPersonality,
  PjlEnvVar * pEnvironmentVariables,
  int32 nEnvironmentVariables );


/**
 * \brief Exits the PJL parser.
 *
 * \param pContext Context returned by PjlParserInit.
 */
extern void PjlParserExit ( PjlParserContext * pContext );


/**
 * \brief Pass input data to the PJL parser.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pData Input data to be parsed.
 *
 * \param cbData Length of input data.
 *
 * \param pcbDataConsumed Filled in with amount of data consumed by call.
 *
 * \return One of the personality enumeration values.  If eNeedMoreData is returned
 * then any data not consumed by this call must be provided to the next call.
 */
extern int32 PjlParserParseData (
  PjlParserContext * pContext,
  uint8            * pData,
  size_t             cbData,
  size_t           * pcbDataConsumed );


/**
 * \brief Reports the depth of JOB - EOJ nesting.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \return The number of currently unmatched JOB commands.
 */
extern uint32 PjlGetJobDepth ( PjlParserContext * pContext );


/**
 * \brief Reports the default personality to assume.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \return Default personality to assume.
 * One of the personality enumeration values.
 */
extern int32 PjlGetDefaultPersonality ( PjlParserContext * pContext );


/**
 * \brief Set the default personality to assume.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param eDefaultPersonality Default personality to assume.
 * One of the personality enumeration values.
 */
extern void PjlSetDefaultPersonality ( PjlParserContext * pContext, int32 eDefaultPersonality );


/**
 * \brief Returns the combined error value of eExistingError and eNewError.
 *
 * \param eExistingError Existing error enumeration value.
 *
 * \param eNewError New error enumeration value.
 *
 * \return combined value of provided error values.  The new error
 * overrides the existing error if it is more severe, and the existing
 * error is reported, so that one command can generate both a warning
 * and an error.
 */
extern int32 PjlCombineErrors( PjlParserContext * pContext, int32 eExistingError, int32 eNewError );


/** \} */  /* end Doxygen grouping */


#endif /* __PJLPARSER_H__ */
