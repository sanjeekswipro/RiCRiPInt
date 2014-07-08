/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpjl!src:pjlparser.c(EBDSDK_P.1) $
 */

#include "pjlparser.h"

#include <assert.h>
#include <string.h>


#define MAX_FILENAME_LEN 255

/**
 * \brief Holds context for the parser.
 */
struct PjlParserContext
{
  int32 eDefaultPersonality;

  PjlMemAllocFn  * pMemAllocFn;
  PjlMemFreeFn   * pMemFreeFn;

  PjlCommandCallback * pCommandCallbackFn;
  PjlErrorCallback   * pErrorCallbackFn;

  PjlEnvVar * pEnvironmentVariables;
  int32       nEnvironmentVariables;

  uint32 nJobDepth;

  /* For FSAPPEND, FSDOWNLOAD */
  int32 cbBinaryDataToRead;
  uint8 aFilename[ MAX_FILENAME_LEN + 1 ];

  HqBool fReadToUEL;
  int32  cbSkipBeforeUEL;
};


/**
 * \brief Command syntax
 */
typedef struct PjlCommandSyntax
{
  uint8 * pzName;
  size_t cbNameLen;

  int32  eFormat;

  int32       eOptionKind;
  PjlOption * pRequiredOptions;
  PjlOption * pOptionalOptions;
  PjlOption * pRequiredModifier;

} PjlCommandSyntax;


/**
 * \brief Enumeration of command formats
 */
enum {
  eFormat1 = 1,
  eFormat3 = 3,
  eFormat4
};

static PjlOption gFormatBinaryModifier = { (uint8 *) "FORMAT", { eValueAlphanumeric, (uint8 *) "BINARY" }, NULL };

static PjlOption gAsciiHexOption = { (uint8 *) "ASCIIHEX", { eValueString, NULL },       NULL };
static PjlOption gLanguageOption = { (uint8 *) "LANGUAGE", { eValueAlphanumeric, NULL }, NULL };
static PjlOption gJobNameOption  = { (uint8 *) "NAME",     { eValueString, NULL },       NULL };
static PjlOption gStartOption    = { (uint8 *) "START",    { eValueInt, NULL },          &gJobNameOption };
static PjlOption gEndOption      = { (uint8 *) "END",      { eValueInt, NULL },          &gStartOption };
static PjlOption gPasswordOption = { (uint8 *) "PASSWORD", { eValueInt, NULL },          &gEndOption };
static PjlOption gDisplayOption  = { (uint8 *) "DISPLAY",  { eValueString, NULL },       NULL };
static PjlOption gWordsOption    = { (uint8 *) "WORDS",    { eValueString, NULL },       NULL };

static PjlOption gFileNameOption   = { (uint8 *) "NAME",   { eValueString, NULL },       NULL };
static PjlOption gFileSizeOption   = { (uint8 *) "SIZE",   { eValueInt, NULL },          &gFileNameOption };
static PjlOption gFileOffsetOption = { (uint8 *) "OFFSET", { eValueInt, NULL },          &gFileSizeOption };
static PjlOption gDirCountOption   = { (uint8 *) "COUNT",  { eValueInt, NULL },          &gFileNameOption };
static PjlOption gDirEntryOption   = { (uint8 *) "ENTRY",  { eValueInt, NULL },          &gDirCountOption };
static PjlOption gVolumeOption     = { (uint8 *) "VOLUME", { eValueString, NULL },       NULL };


/**
 * \brief Enumeration of option kinds
 */
enum {
  eNoOptions,
  eSingleOption,
  eSingleOptionWithValue,
  eEnvVar,
  eEnvVarWithValue,
  eRequiredOptions,
  eOptionalOptions
};

#define STRING_AND_LEN( _s_ ) (uint8 *) _s_, sizeof( _s_ ) - 1

static PjlCommandSyntax gStandardCommands[] =
{
  { STRING_AND_LEN( "\033%-12345X" ), eFormat1, eNoOptions,             NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "COMMENT" ),      eFormat3, eOptionalOptions,       NULL,               &gWordsOption,    NULL                   },
  { STRING_AND_LEN( "DEFAULT" ),      eFormat4, eEnvVarWithValue,       NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "DINQUIRE" ),     eFormat4, eEnvVar,                NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "DMCMD" ),        eFormat4, eRequiredOptions,       &gAsciiHexOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "DMINFO" ),       eFormat4, eRequiredOptions,       &gAsciiHexOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "ECHO" ),         eFormat3, eOptionalOptions,       NULL,               &gWordsOption,    NULL                   },
  { STRING_AND_LEN( "ENTER" ),        eFormat4, eRequiredOptions,       &gLanguageOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "EOJ" ),          eFormat4, eOptionalOptions,       NULL,               &gJobNameOption,  NULL                   },
  { STRING_AND_LEN( "FSAPPEND" ),     eFormat4, eRequiredOptions,       &gFileSizeOption,   NULL,             &gFormatBinaryModifier },
  { STRING_AND_LEN( "FSDELETE" ),     eFormat4, eRequiredOptions,       &gFileNameOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "FSDIRLIST" ),    eFormat4, eRequiredOptions,       &gDirEntryOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "FSDOWNLOAD" ),   eFormat4, eRequiredOptions,       &gFileSizeOption,   NULL,             &gFormatBinaryModifier },
  { STRING_AND_LEN( "FSINIT" ),       eFormat4, eRequiredOptions,       &gVolumeOption,     NULL,             NULL                   },
  { STRING_AND_LEN( "FSMKDIR" ),      eFormat4, eRequiredOptions,       &gFileNameOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "FSQUERY" ),      eFormat4, eRequiredOptions,       &gFileNameOption,   NULL,             NULL                   },
  { STRING_AND_LEN( "FSUPLOAD" ),     eFormat4, eRequiredOptions,       &gFileOffsetOption, NULL,             NULL                   },
  { STRING_AND_LEN( "INFO" ),         eFormat4, eSingleOption,          NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "INITIALIZE" ),   eFormat4, eNoOptions,             NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "INQUIRE" ),      eFormat4, eEnvVar,                NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "JOB" ),          eFormat4, eOptionalOptions,       NULL,               &gPasswordOption, NULL                   },
  { STRING_AND_LEN( "OPMSG" ),        eFormat4, eRequiredOptions,       &gDisplayOption,    NULL,             NULL                   },
  { STRING_AND_LEN( "RDYMSG" ),       eFormat4, eRequiredOptions,       &gDisplayOption,    NULL,             NULL                   },
  { STRING_AND_LEN( "RESET" ),        eFormat4, eNoOptions,             NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "SET" ),          eFormat4, eEnvVarWithValue,       NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "STMSG" ),        eFormat4, eRequiredOptions,       &gDisplayOption,    NULL,             NULL                   },
  { STRING_AND_LEN( "USTATUS" ),      eFormat4, eSingleOptionWithValue, NULL,               NULL,             NULL                   },
  { STRING_AND_LEN( "USTATUSOFF" ),   eFormat4, eNoOptions,             NULL,               NULL,             NULL                   }
};
#define N_STANDARD_COMMANDS ( sizeof( gStandardCommands ) / sizeof( gStandardCommands[ 0 ] ) )

static uint8 UEL[] = { '\033', '%', '-', '1', '2', '3', '4', '5', 'X' };
#define UEL_LENGTH sizeof( UEL )

static uint8 ATPJL[] = { '@', 'P', 'J', 'L' };
#define ATPJL_LENGTH sizeof( ATPJL )

static uint8 COMMENT_CMD[] = { 'C', 'O', 'M', 'M', 'E', 'N', 'T' };
#define COMMENT_LENGTH sizeof( COMMENT_CMD )

static uint8 ECHO_CMD[] = { 'E', 'C', 'H', 'O' };
#define ECHO_LENGTH sizeof( ECHO_CMD )

static uint8 WORDS_OPTION[] = { 'W', 'O', 'R', 'D', 'S' };
#define WORDS_LENGTH sizeof( WORDS_OPTION )

static uint8 ENTER_CMD[] = { 'E', 'N', 'T', 'E', 'R' };
#define ENTER_LENGTH sizeof( ENTER_CMD )

static uint8 LANGUAGE_OPTION[] = { 'L', 'A', 'N', 'G', 'U', 'A', 'G', 'E' };
#define LANGUAGE_LENGTH sizeof( LANGUAGE_OPTION )

static uint8 PCLXL_LANGUAGE[] = { 'P', 'C', 'L', 'X', 'L' };
#define PCLXL_LANG_LENGTH sizeof( PCLXL_LANGUAGE )

static uint8 PCL5_LANGUAGE[] = { 'P', 'C', 'L' };
#define PCL5_LANG_LENGTH sizeof( PCL5_LANGUAGE )

static uint8 PDF_LANGUAGE[] = { 'P', 'D', 'F' };
#define PDF_LANG_LENGTH sizeof( PDF_LANGUAGE )

static uint8 PS_LANGUAGE[] = { 'P', 'O', 'S', 'T', 'S', 'C', 'R', 'I', 'P', 'T' };
#define PS_LANG_LENGTH sizeof( PS_LANGUAGE )

static uint8 IMAGE_LANGUAGE[] = { 'I', 'M', 'A', 'G', 'E' };
#define IMAGE_LANG_LENGTH sizeof( IMAGE_LANGUAGE )

static uint8 XPS_LANGUAGE[] = { 'X', 'P', 'S' };
#define XPS_LANG_LENGTH sizeof( XPS_LANGUAGE )

static uint8 PCLXL_HEADER_ASCII[]     = { '\'', ' ', 'H', 'P', '-', 'P', 'C', 'L', ' ', 'X', 'L', ';' };
static uint8 PCLXL_HEADER_BE_BINARY[] = { '(',  ' ', 'H', 'P', '-', 'P', 'C', 'L', ' ', 'X', 'L', ';' };
static uint8 PCLXL_HEADER_LE_BINARY[] = { ')',  ' ', 'H', 'P', '-', 'P', 'C', 'L', ' ', 'X', 'L', ';' };
#define PCLXL_HEADER_LENGTH sizeof( PCLXL_HEADER_ASCII )

static uint8 PCL5_HEADER[] = { '\033', 'E' };
#define PCL5_HEADER_LENGTH sizeof( PCL5_HEADER )

static uint8 PDF_HEADER[] = { '%', 'P', 'D', 'F' };
#define PDF_HEADER_LENGTH sizeof( PDF_HEADER )

static uint8 PS_HEADER[] = { '%', '!' };
#define PS_HEADER_LENGTH sizeof( PS_HEADER )

static uint8 ZIP_HEADER[] = { 'P', 'K', '\003', '\004' };
#define ZIP_HEADER_LENGTH sizeof( ZIP_HEADER )

static uint8 JOB_CMD[] = { 'J', 'O', 'B' };
#define JOB_CMD_LENGTH sizeof( JOB_CMD )

static uint8 EOJ_CMD[] = { 'E', 'O', 'J' };
#define EOJ_CMD_LENGTH sizeof( EOJ_CMD )

static uint8 FSAPPEND_CMD[] = { 'F', 'S', 'A', 'P', 'P', 'E', 'N', 'D' };
#define FSAPPEND_CMD_LENGTH sizeof( FSAPPEND_CMD )

static uint8 FSDOWNLOAD_CMD[] = { 'F', 'S', 'D', 'O', 'W', 'N', 'L', 'O', 'A', 'D' };
#define FSDOWNLOAD_CMD_LENGTH sizeof( FSDOWNLOAD_CMD )

static uint8 BINARY_DATA_OPTION[] = { 'B', 'I', 'N', 'A', 'R', 'Y', 'D', 'A', 'T', 'A' };
#define BINARY_DATA_LENGTH sizeof( BINARY_DATA_OPTION )

#define MAX_WORDS_LEN 80


/**
 * \brief Enumeration of error severities
 */
enum {
  eErrorSeverityNoError = 0,
  eErrorSeverityWarning,
  eErrorSeverityError
};


static void * PjlMemAlloc( PjlParserContext * pContext, size_t cbSize, int32 fZero );
static void PjlMemFree( PjlParserContext * pContext, void * pMem );

static uint8 * CopyToUpperCase( PjlParserContext * pContext, uint8 * pName, size_t cbNameLen );
static HqBool StringMatches( uint8 * pz, uint8 * p, size_t cbLen );
static HqBool CommandMatches( PjlCommand * pCommand, uint8 * pName, size_t cbNameLen );
static HqBool OptionMatches( PjlOption * pOption, uint8 * pName, size_t cbNameLen );
static HqBool OptionIsType( PjlOption * pOption, int32 eType );

static int32 DetectPersonality( PjlParserContext * pContext, uint8 * pData, size_t cbDataLen );
static HqBool GetLine( uint8 * pData, size_t cbDataLen, size_t * pcbLineLen );
static int32 GetBinaryData( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed );
static int32 GetMoreBinaryData( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed );
static HqBool ReadToUEL( PjlParserContext * pContext, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed );
static int32 ProcessLine( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pLine, size_t cbLineLen );
static int32 NoteCommand( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pName, size_t cbNameLen );
static PjlCommandSyntax * LookupCommand( PjlParserContext * pContext, PjlCommand * pCommand );
static PjlEnvVar * LookupEnvironmentVariable( PjlParserContext * pContext, uint8 * pzName );
static int32 ValidateCommand( PjlParserContext * pContext, PjlCommand * pCommand, int32 eParseError );
static int32 ProcessCommand( PjlParserContext * pContext, PjlCommand * pCommand, int32 eParseError, int32 * pePersonality );
static int32 ParseWords( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pData, size_t cbDataLen );
static int32 ParseOptions( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pLine, size_t cbLineLen );
static int32 NoteModifier( PjlCommand * pCommand, PjlOption * pModifier );

static PjlOption * NewOption( PjlParserContext * pContext, uint8 * pName, size_t cbNameLen );
static PjlOption * FindOption( PjlCommand * pCommand, uint8 * pName, size_t cbNameLen );
static void AddOption( PjlCommand * pCommand, PjlOption * pNewOption );
static int32 NoteOption( PjlCommand * pCommand, PjlOption * pOption );
static void FreeOption( PjlParserContext * pContext, PjlOption * pOption );
static void FreeOneOption( PjlParserContext * pContext, PjlCommand * pCommand, PjlOption * pFreeOption );
static void FreeAllOptions( PjlParserContext * pContext, PjlCommand * pCommand );
static int32 SetOptionValue( PjlParserContext * pContext, PjlOption * pOption, uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext );
static int32 SetOptionStringValue( PjlParserContext * pContext, PjlOption * pOption, uint8 * pValue, size_t cbValueLen, int32 eType );
static void SetOptionIntValue( PjlOption * pOption, int32 iValue );
static void SetOptionDoubleValue( PjlOption * pOption, double dValue );

static HqBool SkipWhiteSpace( uint8 ** ppData, uint8 * pDataEnd );
static HqBool ReadAlphaNum( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext );
static int32 ReadQuotedString( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext );
static int32 ReadWords( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext );
static int32 ReadAndSetNumeric( PjlOption * pOption, uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext );

static int32 GetErrorSeverity( int32 eError );
static HqBool IsError( int32 eError );
static void ReportError( PjlParserContext * pContext, int32 eError );

static void FreeCommand( PjlParserContext * pContext, PjlCommand * pCommand );


/**
 * @file
 * @ingroup pjlparser
 * @brief String-related utility functions.
 */

PjlParserContext * PjlParserInit (
  PjlMemAllocFn * pMemAllocFn,
  PjlMemFreeFn * pMemFreeFn,
  PjlCommandCallback * pCommandCallbackFn,
  PjlErrorCallback * pErrorCallbackFn,
  int32 eDefaultPersonality,
  PjlEnvVar * pEnvironmentVariables,
  int32 nEnvironmentVariables )
{
  PjlParserContext * pContext;

  assert( pMemAllocFn );
  assert( pMemFreeFn );
  assert( pCommandCallbackFn );
  assert( pErrorCallbackFn );

  pContext = (PjlParserContext *) (pMemAllocFn)( sizeof( PjlParserContext ), TRUE );

  if( pContext )
  {
    pContext->eDefaultPersonality = eDefaultPersonality;

    pContext->pMemAllocFn = pMemAllocFn;
    pContext->pMemFreeFn = pMemFreeFn;
    pContext->pCommandCallbackFn = pCommandCallbackFn;
    pContext->pErrorCallbackFn = pErrorCallbackFn;

    pContext->pEnvironmentVariables = pEnvironmentVariables;
    pContext->nEnvironmentVariables = nEnvironmentVariables;

    pContext->nJobDepth = 0;
    pContext->cbBinaryDataToRead = 0;
    pContext->fReadToUEL = FALSE;
    pContext->cbSkipBeforeUEL = 0;
  }

  return pContext;
}


void PjlParserExit ( PjlParserContext * pContext )
{
  if( pContext )
  {
    (pContext->pMemFreeFn)( pContext );
  }
}


int32 PjlParserParseData (
  PjlParserContext * pContext,
  uint8            * pData,
  size_t             cbDataLen,
  size_t           * pcbDataConsumed )
{
  int32 ePersonality;
  uint8 * pLine = pData;
  size_t cbDataRemaining = cbDataLen;
  size_t cbLineLen;
  size_t cbConsumed;
  int32 eError;

  assert( pContext );
  assert( pData != NULL );
  assert( pcbDataConsumed );

  ePersonality = ePJL;

  while( ePersonality == ePJL && cbDataRemaining > 0 )
  {
    assert( pContext->fReadToUEL == FALSE || pContext->cbBinaryDataToRead == 0 );

    if( pContext->fReadToUEL )
    {
      /* Looking for UEL to conclude FSAPPEND, FSDOWNLOAD */
      if( ReadToUEL( pContext, pLine, cbDataRemaining, &cbConsumed ) )
      {
        pContext->fReadToUEL = FALSE;
      }
      else if( cbConsumed == 0 )
      {
        ePersonality = eNeedMoreData;
      }

      pLine += cbConsumed;
      cbDataRemaining -= cbConsumed;
    }
    else if( pContext->cbBinaryDataToRead > 0 )
    {
      /* Read more binary data for FSAPPEND, FSDOWNLOAD */
      PjlCommand command = { 0 };

      eError = GetMoreBinaryData( pContext, &command, pLine, cbDataRemaining, &cbConsumed );

      if( eError == eNoError )
      {
        eError = (pContext->pCommandCallbackFn)( &command );
      }

      if( eError != eNoError )
      {
        ReportError( pContext, eError );
      }

      pLine += cbConsumed;
      cbDataRemaining -= cbConsumed;

      FreeCommand( pContext, &command );
    }
    else if( GetLine( pLine, cbDataRemaining, &cbLineLen ) )
    {
      /* Got a complete line, process it */
      PjlCommand command = { 0 };

      ePersonality = ProcessLine( pContext, &command, pLine, cbLineLen );

      pLine += cbLineLen;
      cbDataRemaining -= cbLineLen;

      if( ePersonality == eNeedMoreData )
      {
        /* FSAPPEND or FSDOWNLOAD command which needs us to read additional binary data */
        if( cbDataRemaining > 0 )
        {
          eError = GetBinaryData( pContext, &command, pLine, cbDataRemaining, &cbConsumed );
          if( eError == eNoError )
          {
            eError = (pContext->pCommandCallbackFn)( &command );

            if( eError != eNoError )
            {
              pContext->fReadToUEL = TRUE;
              pContext->cbSkipBeforeUEL = pContext->cbBinaryDataToRead;
              pContext->cbBinaryDataToRead = 0;
            }
          }

          if( eError != eNoError )
          {
            ReportError( pContext, eError );
          }

          ePersonality = ePJL;
          pLine += cbConsumed;
          cbDataRemaining -= cbConsumed;
        }
        else
        {
          /* Back up to start of command - we'll re-read it again next time */
          pLine -= cbLineLen;
          cbDataRemaining += cbLineLen;
        }
      }

      FreeCommand( pContext, &command );
    }
    else if( cbDataRemaining > 0 )
    {
      ePersonality = DetectPersonality( pContext, pLine, cbDataRemaining );
    }
  }

  *pcbDataConsumed = (pLine - pData);

  return ePersonality;
}


uint32 PjlGetJobDepth ( PjlParserContext * pContext )
{
  assert( pContext );

  return pContext->nJobDepth;
}


int32 PjlGetDefaultPersonality ( PjlParserContext * pContext )
{
  assert( pContext );

  return pContext->eDefaultPersonality;
}


void PjlSetDefaultPersonality ( PjlParserContext * pContext, int32 eDefaultPersonality )
{
  assert( pContext );

  pContext->eDefaultPersonality = eDefaultPersonality;
}


int32 PjlCombineErrors( PjlParserContext * pContext, int32 eExistingError, int32 eNewError )
{
  int32 eCombinedError;

  int32 eExistingSeverity = GetErrorSeverity( eExistingError );
  int32 eNewSeverity = GetErrorSeverity( eNewError );

  if( eNewSeverity > eExistingSeverity )
  {
    if( eExistingError != eNoError )
    {
      ReportError( pContext, eExistingError );
    }

    eCombinedError = eNewError;
  }
  else
  {
    eCombinedError = eExistingError;
  }

  return eCombinedError;
}


/**
 * \brief Allocate memory using the provided allocator.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param cbSize Number of bytes to allocate.
 *
 * \param fZero Whether to set allocated memory to 0.
 *
 * \return Allocated memory, or NULL if memory cannot be allocated and fExitOnFail is FALSE.
 */
static void * PjlMemAlloc( PjlParserContext * pContext, size_t cbSize, int32 fZero )
{
  assert( pContext );

  return (pContext->pMemAllocFn)( cbSize, fZero );
}


/**
 * \brief Deallocate memory using the provided deallocator.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pMem Pointer to memory allocated by PjlMemAlloc.
 */
static void PjlMemFree( PjlParserContext * pContext, void * pMem )
{
  assert( pContext );

  (pContext->pMemFreeFn)( pMem );
}


/**
 * \brief Return a newly allocated copy of a string, converted to upper case.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pName Pointer to name to copy.
 *
 * \param cbNameLen Length of name to copy.
 *
 * \return Copy of pName, converted to upper case.
 */
static uint8 * CopyToUpperCase( PjlParserContext * pContext, uint8 * pName, size_t cbNameLen )
{
  uint8 * pzCopy = (uint8 *) PjlMemAlloc( pContext, cbNameLen + 1, FALSE );

  if( pzCopy != NULL )
  {
    size_t i;

    for( i = 0; i < cbNameLen; i++ )
    {
      if( pName[ i ] >= 'a' && pName[ i ] <= 'z' )
      {
        pzCopy[ i ] = pName[ i ] + 'A' - 'a';
      }
      else
      {
        pzCopy[ i ] = pName[ i ];
      }
    }

    pzCopy[ cbNameLen ] = '\0';
  }

  return pzCopy;
}


/**
 * \brief Compare two strings.
 *
 * \param pz Pointer to terminated string.
 *
 * \param p Pointer to possibly unterminated string.
 *
 * \param cbLen Length of string p.
 *
 * \return TRUE if strings are same length and contents match.
 */
static HqBool StringMatches( uint8 * pz, uint8 * p, size_t cbLen )
{
  return( strlen( (const char *) pz ) == cbLen
    && memcmp( (const char * ) pz, (const char * ) p, cbLen ) == 0 );
}


/**
 * \brief Compare command name to given string.
 *
 * \param pCommand Pointer to command.
 *
 * \param pName Pointer to possibly unterminated string.
 *
 * \param cbNameLen Length of string pName.
 *
 * \return TRUE if command name matches pName.
 */
static HqBool CommandMatches( PjlCommand * pCommand, uint8 * pName, size_t cbNameLen )
{
  assert( pCommand );
  assert( pCommand->pzName );
  assert( pName );

  return StringMatches( pCommand->pzName, pName, cbNameLen );
}


/**
 * \brief Compare command name to given string.
 *
 * \param pOption Pointer to option.
 *
 * \param pName Pointer to possibly unterminated string.
 *
 * \param cbNameLen Length of string pName.
 *
 * \return TRUE if command name matches pName.
 */
static HqBool OptionMatches( PjlOption * pOption, uint8 * pName, size_t cbNameLen )
{
  assert( pOption );

  if( pOption->pzName == NULL )
  {
    return ( pName == NULL ) ? TRUE : FALSE;
  }

  return StringMatches( pOption->pzName, pName, cbNameLen );
}


/**
 * \brief Check option has a value of the given type.
 *
 * \param pOption Pointer to option.
 *
 * \param eType Expected type of value.
 *
 * \return TRUE if command name matches pName.
 */
static HqBool OptionIsType( PjlOption * pOption, int32 eType )
{
  assert( pOption );

  return ( pOption->value.eType == eType ) ? TRUE : FALSE;
}


/**
 * \brief Detect personality (i.e. PDL) from data stream.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pData Pointer to data stream.
 *
 * \param cbDataLen Length of data stream.
 *
 * \return One of the personality enumeration values.
 */
static int32 DetectPersonality( PjlParserContext * pContext, uint8 * pData, size_t cbDataLen )
{
  int32 ePersonality = eUnknown;

  assert( pContext );
  assert( pData );
  assert( cbDataLen > 0 );

  if( cbDataLen >= ATPJL_LENGTH && memcmp( (const void *) pData, (const void *) ATPJL, ATPJL_LENGTH ) == 0 )
  {
    /* Have @PJL, but presumably not a whole line.
     * Check that there isn't just a CR line ending.
     */
    uint8 * p = pData + ATPJL_LENGTH;
    uint8 * pDataEnd = pData + cbDataLen;

    ePersonality = eNeedMoreData;

    while( p < pDataEnd - 1 )
    {
      if( *p++ == '\r' )
      {
        /* Previous call of GetLine() would have found \r\n so this \r can't be followed by \n */
        assert( *p != '\n' );
        ePersonality = eUnknown;
        break;
      }
    }
  }
  else if( cbDataLen < ATPJL_LENGTH && memcmp( (const void *) pData, (const void *) ATPJL, cbDataLen ) == 0 )
  {
    /* Might be PJL */
    ePersonality = eNeedMoreData;
  }
  else if( cbDataLen < UEL_LENGTH && memcmp( (const void *) pData, (const void *) UEL, cbDataLen ) == 0 )
  {
    /* Might be UEL */
    ePersonality = eNeedMoreData;
  }
  else if( pContext->eDefaultPersonality == eAutoPDL )
  {
    if( cbDataLen >= PCLXL_HEADER_LENGTH &&
      ( memcmp( (const void *) pData, (const void *) PCLXL_HEADER_ASCII, PCLXL_HEADER_LENGTH ) == 0
        || memcmp( (const void *) pData, (const void *) PCLXL_HEADER_BE_BINARY, PCLXL_HEADER_LENGTH ) == 0
        || memcmp( (const void *) pData, (const void *) PCLXL_HEADER_LE_BINARY, PCLXL_HEADER_LENGTH ) == 0 ) )
    {
      ePersonality = ePCLXL;
    }
    else if( cbDataLen >= PCL5_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PCL5_HEADER, PCL5_HEADER_LENGTH ) == 0 )
    {
      ePersonality = ePCL5c;
    }
    else if( cbDataLen >= PS_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PS_HEADER, PS_HEADER_LENGTH ) == 0 )
    {
      ePersonality = ePostScript;
    }
    else if( cbDataLen >= PDF_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PDF_HEADER, PDF_HEADER_LENGTH ) == 0 )
    {
      ePersonality = ePDF;
    }
    else if( cbDataLen >= ZIP_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) ZIP_HEADER, ZIP_HEADER_LENGTH ) == 0 )
    {
      ePersonality = eZIP;
    }
    else if( cbDataLen < PCLXL_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PCL5_HEADER, cbDataLen ) == 0 )
    {
      /* Might be PCL5 */
      ePersonality = eNeedMoreData;
    }
    else if( cbDataLen < PCL5_HEADER_LENGTH &&
      ( memcmp( (const void *) pData, (const void *) PCLXL_HEADER_ASCII, cbDataLen ) == 0
        || memcmp( (const void *) pData, (const void *) PCLXL_HEADER_BE_BINARY, cbDataLen ) == 0
        || memcmp( (const void *) pData, (const void *) PCLXL_HEADER_LE_BINARY, cbDataLen ) == 0 ) )
    {
      /* Might be PCL XL */
      ePersonality = eNeedMoreData;
    }
    else if( cbDataLen < PS_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PS_HEADER, cbDataLen ) == 0 )
    {
      /* Might be PostScript */
      ePersonality = eNeedMoreData;
    }
    else if( cbDataLen < PDF_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) PDF_HEADER, cbDataLen ) == 0 )
    {
      /* Might be PDF */
      ePersonality = eNeedMoreData;
    }
    else if( cbDataLen < ZIP_HEADER_LENGTH && memcmp( (const void *) pData, (const void *) ZIP_HEADER, cbDataLen ) == 0 )
    {
      /* Might be ZIP */
      ePersonality = eNeedMoreData;
    }
    else
    {
      /* Don't know */
      ePersonality = eUnknown;
    }
  }
  else if( pContext->eDefaultPersonality != eNone )
  {
    /* Assume that we have the default personality */
    ePersonality = pContext->eDefaultPersonality;
  }

  return ePersonality;
}


/**
 * \brief Find the next line of data to process.
 *
 * \param pData Pointer to data stream.
 *
 * \param cbDataLen Length of data stream.
 *
 * \param pcbLineLen If function returns TRUE then set to the length of
 * the next line to process.
 *
 * \return TRUE if a complete line is found, i.e. a line starting with
 * @PJL and ending with <LF>, or a UEL.
 */
static HqBool GetLine( uint8 * pData, size_t cbDataLen, size_t * pcbLineLen )
{
  HqBool fGotLine = FALSE;

  if( cbDataLen >= UEL_LENGTH && memcmp( (const void *) pData, (const void *) UEL, UEL_LENGTH ) == 0 )
  {
    fGotLine = TRUE;
    *pcbLineLen = UEL_LENGTH;
  }
  else if( cbDataLen >= ATPJL_LENGTH && memcmp( (const void *) pData, (const void *) ATPJL, ATPJL_LENGTH ) == 0 )
  {
    /* Look for next LF */
    uint8 * pDataEnd = pData + cbDataLen;
    uint8 * pLineEnd = pData;

#if 0
    while( pLineEnd < pDataEnd )
    {
      if( (*pLineEnd == '\r') || (*pLineEnd == '\n') )
      {
        fGotLine = TRUE;
        /* increment for cr or lf found*/
        pLineEnd++;
        /* if '\n' is the next character we assume that it as CRLF and need to increment again */
        if(*pLineEnd == '\n')
          pLineEnd++;
        /* line lenth includes the cr and/or lf charactewrs */
        *pcbLineLen = (pLineEnd - pData);
        break;
      }
      pLineEnd++;
    }
#else
    while( pLineEnd < pDataEnd )
    {
      if( *pLineEnd++ == '\n' )
      {
        fGotLine = TRUE;
        *pcbLineLen = (pLineEnd - pData);
        break;
      }
    }
#endif
  }

  return fGotLine;
}


/**
 * \brief read in the binary data for a command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Command to read data for.
 *
 * \param pData Pointer to data stream.
 *
 * \param cbDataLen Length of data stream.
 *
 * \param pcbConsumed Set to amount of data consumed.
 *
 * \return One of the error enumeration values.
 */
static int32 GetBinaryData( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed )
{
  int32 eError = eNoError;

  PjlOption * pOption;
  PjlValue  * pSizeValue;
  int32       cbDataSize;

  assert( pContext );
  assert( pCommand );
  assert( pData );
  assert( cbDataLen > 0 );
  assert( pcbConsumed );

  *pcbConsumed = 0;

  /* Find SIZE option, which must exist as command has been validated */
  pOption = FindOption( pCommand, gFileSizeOption.pzName, strlen( (const char *) gFileSizeOption.pzName ) );
  assert( pOption );

  pSizeValue = &pOption->value;
  assert( pSizeValue->eType == eValueInt );

  cbDataSize = pSizeValue->u.iValue;

  if( cbDataSize < 0 )
  {
    /* Bad SIZE */
    eError = PjlCombineErrors( pContext, eError, eOptionValueOutOfRange );
    eError = PjlCombineErrors( pContext, eError, eOptionMissing );

    pContext->fReadToUEL = TRUE;
    pContext->cbSkipBeforeUEL = 0;
  }
  else
  {
    int32       cbBinaryDataRead = cbDataSize;
    PjlOption * pOption;
    uint8     * pValue;

    if( cbBinaryDataRead > (int) cbDataLen )
    {
      cbBinaryDataRead = (int) cbDataLen;
    }

    /* Add binary data to command */
    pOption = NewOption( pContext, BINARY_DATA_OPTION, BINARY_DATA_LENGTH );

    if( pOption != NULL )
    {
      pValue = PjlMemAlloc( pContext, cbBinaryDataRead, FALSE );

      if( pValue != NULL )
      {
        memcpy( pValue, pData, cbBinaryDataRead );

        pOption->value.eType = eValueBinaryData;
        pOption->value.u.pzValue = pValue;

        AddOption( pCommand, pOption );

        /* Adjust SIZE value to match amount of data actually read */
        pSizeValue->u.iValue = cbBinaryDataRead;
      }
      else
      {
        /* No memory */
        eError = PjlCombineErrors( pContext, eError, eOutOfMemory );
        FreeOption( pContext, pOption );
      }
    }
    else
    {
      /* No memory */
      eError = PjlCombineErrors( pContext, eError, eOutOfMemory );
    }

    *pcbConsumed = cbBinaryDataRead;

    if( cbDataSize > cbBinaryDataRead && eError == eNoError )
    {
      /* Note how much more binary data to read */
      pContext->cbBinaryDataToRead = cbDataSize - cbBinaryDataRead;

      pOption = FindOption( pCommand, gFileNameOption.pzName, strlen( (const char *) gFileNameOption.pzName ) );
      assert( pOption );

      strcpy( (char *) pContext->aFilename, (char *) pOption->value.u.pzValue );
    }
    else
    {
      pContext->fReadToUEL = TRUE;
      pContext->cbSkipBeforeUEL = cbDataSize - cbBinaryDataRead;
    }
  }

  return eError;
}


/**
 * \brief read in more binary data for an already handled FSAPPEND, FSDOWNLOAD command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Command to read data for.
 *
 * \param pData Pointer to data stream.
 *
 * \param cbDataLen Length of data stream.
 *
 * \param pcbConsumed Set to amount of data consumed.
 *
 * \return One of the error enumeration values.
 */
static int32 GetMoreBinaryData( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed )
{
  int32 eError = eNoError;

  PjlOption * pModifier;
  PjlOption * pNameOption = NULL;
  PjlOption * pSizeOption = NULL;
  uint8     * pValue;

  assert( pContext );
  assert( pCommand );
  assert( pData );
  assert( cbDataLen > 0 );
  assert( pcbConsumed );

  *pcbConsumed = 0;

  pCommand->pzName = CopyToUpperCase( pContext, FSAPPEND_CMD, FSAPPEND_CMD_LENGTH );

  /* Add FORMAT:BINARY modifier to command */
  pModifier = NewOption( pContext, gFormatBinaryModifier.pzName, strlen( (const char *) gFormatBinaryModifier.pzName ) );
  if( pModifier != NULL )
  {
    pValue = PjlMemAlloc( pContext, strlen( "BINARY" ) + 1, FALSE );

    if( pValue != NULL )
    {
      strcpy( (char *) pValue, "BINARY" );

      pModifier->value.eType = eValueAlphanumeric;
      pModifier->value.u.pzValue = pValue;

      NoteModifier( pCommand, pModifier );
    }
    else
    {
      /* No memory */
      eError = eOutOfMemory;
      FreeOption( pContext, pModifier );
    }
  }
  else
  {
    /* No memory */
    eError = eOutOfMemory;
  }

  /* Add NAME option to command */
  if( eError == eNoError )
  {
    pNameOption = NewOption( pContext, gFileNameOption.pzName, strlen( (const char *) gFileNameOption.pzName ) );
    if( pNameOption != NULL )
    {
      eError = SetOptionStringValue( pContext, pNameOption, pContext->aFilename, MAX_FILENAME_LEN, eValueString );

      if( eError == eNoError )
      {
        AddOption( pCommand, pNameOption );
      }
      else
      {
        FreeOption( pContext, pNameOption );
      }
    }
    else
    {
      /* No memory */
      eError = eOutOfMemory;
    }
  }

  /* Add SIZE option to command - value is set below */
  if( eError == eNoError )
  {
    pSizeOption = NewOption( pContext, gFileSizeOption.pzName, strlen( (const char *) gFileSizeOption.pzName ) );
    if( pSizeOption != NULL )
    {
      pSizeOption->value.eType = eValueInt;

      AddOption( pCommand, pSizeOption );
    }
    else
    {
      /* No memory */
      eError = eOutOfMemory;
    }
  }

  if( eError == eNoError )
  {
    int32       cbBinaryDataRead = pContext->cbBinaryDataToRead;
    PjlOption * pOption;
    uint8     * pValue;

    if( cbBinaryDataRead > (int) cbDataLen )
    {
      cbBinaryDataRead = (int) cbDataLen;
    }

    /* Add binary data to command */
    pOption = NewOption( pContext, BINARY_DATA_OPTION, BINARY_DATA_LENGTH );

    if( pOption != NULL )
    {
      pValue = PjlMemAlloc( pContext, cbBinaryDataRead, FALSE );

      if( pValue != NULL )
      {
        memcpy( pValue, pData, cbBinaryDataRead );

        pOption->value.eType = eValueBinaryData;
        pOption->value.u.pzValue = pValue;

        AddOption( pCommand, pOption );

        /* Set SIZE option to match amount of data actually read */
        pSizeOption->value.u.iValue = cbBinaryDataRead;

        *pcbConsumed = cbBinaryDataRead;
        pContext->cbBinaryDataToRead -= cbBinaryDataRead;

        if( pContext->cbBinaryDataToRead == 0 )
        {
          pContext->fReadToUEL = TRUE;
          pContext->cbSkipBeforeUEL = 0;
        }
      }
      else
      {
        /* No memory */
        eError = PjlCombineErrors( pContext, eError, eOutOfMemory );
        FreeOption( pContext, pOption );
      }
    }
    else
    {
      /* No memory */
      eError = PjlCombineErrors( pContext, eError, eOutOfMemory );
    }
  }

  if( eError != eNoError )
  {
    /* Error - look for UEL next, skipping remainder of binary data first */
    pContext->fReadToUEL = TRUE;
    pContext->cbSkipBeforeUEL = pContext->cbBinaryDataToRead;
    pContext->cbBinaryDataToRead = 0;
  }

  return eError;
}


/**
 * \brief Look for a UEL in the data stream.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pData Pointer to data stream.
 *
 * \param cbDataLen Length of data stream.
 *
 * \param pcbConsumed Set to how much data was consumed (including any UEL).
 *
 * \return TRUE if a UEL was found.
 */
static HqBool ReadToUEL( PjlParserContext * pContext, uint8 * pData, size_t cbDataLen, size_t * pcbConsumed )
{
  HqBool fUELFound = FALSE;

  size_t cbConsumed = 0;

  if( pContext->cbSkipBeforeUEL > 0 )
  {
    if( pContext->cbSkipBeforeUEL > (int) cbDataLen )
    {
      pContext->cbSkipBeforeUEL -= (int) cbDataLen;
      cbConsumed = cbDataLen;
    }
    else
    {
      cbConsumed = pContext->cbSkipBeforeUEL;
      pContext->cbSkipBeforeUEL = 0;
      pData += cbConsumed;
      cbDataLen -= cbConsumed;
    }
  }

  if( pContext->cbSkipBeforeUEL == 0 && cbDataLen >= UEL_LENGTH )
  {
    size_t iUEL;

    for( iUEL = 0; iUEL < cbDataLen - UEL_LENGTH; iUEL++ )
    {
      if( pData[ iUEL ] == UEL[ 0 ]
        && memcmp( (const void *) &pData[ iUEL ], (const void *) UEL, UEL_LENGTH ) == 0 )
      {
        /* Found UEL  */
        fUELFound = TRUE;
        cbConsumed += iUEL + UEL_LENGTH;
        break;
      }
    }

    if( !fUELFound )
    {
      cbConsumed += iUEL;
    }
  }

  *pcbConsumed = cbConsumed;

  return fUELFound;
}


/**
 * \brief Process a line of data.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 *
 * \param pLine Pointer to line.
 *
 * \param cbLineLen Length of line.
 *
 * \return One of the personality enumeration values, reporting the expected
 * personality of the following data.
 */
static int32 ProcessLine( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pLine, size_t cbLineLen )
{
  int32      ePersonality = ePJL;

  int32      eError = eNoError;

  assert( pContext );

  if( cbLineLen == UEL_LENGTH && memcmp( (const void *) pLine, (const void *) UEL, UEL_LENGTH ) == 0 )
  {
    /* UEL */
    eError = NoteCommand( pContext, pCommand, UEL, UEL_LENGTH );

    if( eError == eNoError )
    {
      eError = ProcessCommand( pContext, pCommand, eNoError, &ePersonality );
    }
  }
  else if( cbLineLen > ATPJL_LENGTH )
  {
    /* @PJL */
    assert( memcmp( (const void *) pLine, (const void *) ATPJL, ATPJL_LENGTH ) == 0 );

    /* Decrement cbLineLen for [CR] LF */
    cbLineLen--;
    if( pLine[ cbLineLen - 1 ] == '\r' )
    {
      cbLineLen--;
    }

    if( cbLineLen == ATPJL_LENGTH )
    {
      /* Just have @PJL - nothing more to do */
      ePersonality = ePJL;
    }
    else
    {
      uint8 * pCmdName = pLine + ATPJL_LENGTH;
      uint8 * pLineEnd;

      pLineEnd = pLine + cbLineLen;

      /* Check for and skip whitespace before command */
      if( SkipWhiteSpace( &pCmdName, pLineEnd ) )
      {
        if( pCmdName < pLineEnd )
        {
          uint8 * pCmdNameEnd;

          if( ReadAlphaNum( pCmdName, pLineEnd, &pCmdNameEnd ) )
          {
            eError = NoteCommand( pContext, pCommand, pCmdName, pCmdNameEnd - pCmdName );

            if( eError == eNoError )
            {
              uint8 * pOptions = pCmdNameEnd;

              /* Check for and skip whitespace after command */
              HqBool fSkipped = SkipWhiteSpace( &pOptions, pLineEnd );

              if( pOptions == pLineEnd )
              {
                /* Command with no modifer or options */
                assert( eError == eNoError );
                eError = ProcessCommand( pContext, pCommand, eNoError, &ePersonality );
              }
              else if( fSkipped )
              {
                /* Look for words / options for command */
                if( CommandMatches( pCommand, COMMENT_CMD, COMMENT_LENGTH )
                  || CommandMatches( pCommand, ECHO_CMD, ECHO_LENGTH ) )
                {
                  eError = ParseWords( pContext, pCommand, pOptions, cbLineLen - ( pOptions - pLine ) );
                }
                else
                {
                  eError = ParseOptions( pContext, pCommand, pOptions, cbLineLen - ( pOptions - pLine ) );
                }

                if( ! IsError( eError ) )
                {
                  /* Process command even if there have been warnings */
                  eError = ProcessCommand( pContext, pCommand, eError, &ePersonality );
                }
              }
              else
              {
                /* Something other than whitespace after command */
                eError = eNonAlphanumericCommand;
              }
            }
          }
          else
          {
            /* Non-alphabetic command */
            eError = eNonAlphanumericCommand;
          }
        }
        else
        {
          /* Just whitespace after @PJL, which is OK */
        }
      }
      else
      {
        /* Missing required whitespace after @PJL */
        eError = eNoPJLPrefix;
      }
    }
  }

  if( eError != eNoError )
  {
    ReportError( pContext, eError );
  }

  return ePersonality;
}


/**
 * \brief Set name of command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command to set name of.
 *
 * \param pName Pointer to name.
 *
 * \param cbNameLen Length of name.
 *
 * \return One of the error enumeration values.
 */
static int32 NoteCommand( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pName, size_t cbNameLen )
{
  int32 eError = eNoError;

  assert( pContext );
  assert( pCommand );
  assert( pCommand->pzName == NULL );
  assert( pName );
  assert( cbNameLen > 0 );

  pCommand->pzName = CopyToUpperCase( pContext, pName, cbNameLen );

  if( pCommand->pzName == NULL )
  {
    eError = eOutOfMemory;
  }

  return eError;
}


/**
 * \brief Lookup command syntax.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command to process.
 *
 * \return Pointer to command syntax, or NULL if command is unknown.
 */
static PjlCommandSyntax * LookupCommand( PjlParserContext * pContext, PjlCommand * pCommand )
{
  PjlCommandSyntax * pCommandSyntax = NULL;

  int32 i;

  for( i = 0; i < N_STANDARD_COMMANDS; i++  )
  {
    assert( strlen( (const char *) gStandardCommands[ i ].pzName ) == gStandardCommands[ i ].cbNameLen );

    if( CommandMatches( pCommand, gStandardCommands[ i ].pzName, gStandardCommands[ i ].cbNameLen ) )
    {
      pCommandSyntax = &gStandardCommands[ i ];
      break;
    }
  }

/* At some point we might have non-standard commands noted in the context
 * but we've not got them yet.  Set pContext to NULL just to squash a
 * compiler warning.
 */
pContext = NULL;

  return pCommandSyntax;
}


/**
 * \brief Lookup environment variable.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pzName Name of environment variable.
 *
 * \return Pointer to environment variable, or NULL if unknown.
 */
static PjlEnvVar * LookupEnvironmentVariable( PjlParserContext * pContext, uint8 * pzName )
{
  PjlEnvVar * pEnvVar = NULL;

  int32 i;

  for( i = 0; i < pContext->nEnvironmentVariables; i++ )
  {
    if( strcmp( (const char *) pzName, (const char *) pContext->pEnvironmentVariables[ i ].pzName ) == 0 )
    {
      pEnvVar = &pContext->pEnvironmentVariables[ i ];
      break;
    }
  }

  return pEnvVar;
}


/**
 * \brief Validate command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command to process.
 *
 * \param eParseError Any error from parsing the command - one of the error
 * enumeration values.
 *
 * \return One of the error enumeration values.
 */
static int32 ValidateCommand( PjlParserContext * pContext, PjlCommand * pCommand, int32 eParseError )
{
  int32 eError = eParseError;

  PjlCommandSyntax * pCommandSyntax;

  PjlOption * pRequiredOption;
  PjlOption * pOptionalOption;
  PjlOption * pOption;
  PjlOption * pNextOption;

  pCommandSyntax = LookupCommand( pContext, pCommand );

  if( pCommandSyntax != NULL )
  {
    /* Generic validation */
    switch( pCommandSyntax->eFormat )
    {
    case eFormat1:
      /* Nothing to do for UEL */
      break;

    case eFormat3:
    case eFormat4:
      /* Check modifier */
      if( pCommandSyntax->pRequiredModifier != NULL )
      {
        if( pCommand->pModifier != NULL )
        {
          if( ! OptionIsType( pCommand->pModifier, eValueAlphanumeric ) )
          {
            /* Modifier value is of wrong type */
            eError = PjlCombineErrors( pContext, eError, eNoAlphanumericAfterModifier );
          }
          else if( ! OptionMatches( pCommand->pModifier, pCommandSyntax->pRequiredModifier->pzName, strlen( (const char *) pCommandSyntax->pRequiredModifier->pzName ) ) )
          {
            /* Modifier has wrong name */
            eError = PjlCombineErrors( pContext, eError, eUnsupportedModifer );
          }
          else if( strcmp( (const char *) pCommandSyntax->pRequiredModifier->value.u.pzValue, (const char *) pCommand->pModifier->value.u.pzValue ) != 0 )
          {
            /* Wrong modifer value */
            eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
            eError = PjlCombineErrors( pContext, eError, eOptionMissing );
          }
        }
        else
        {
          /* Required modifier is missing */
          eError = PjlCombineErrors( pContext, eError, eModifierMissing );
        }
      }
      else if( pCommandSyntax->eOptionKind == eEnvVar || pCommandSyntax->eOptionKind == eEnvVarWithValue )
      {
        /* Optional modifiers are only for commands taking an environment variable and are checked below */
      }
      else if( pCommand->pModifier != NULL )
      {
        /* Should not have a modifier for this command */
        eError = PjlCombineErrors( pContext, eError, eUnsupportedModifer );
      }

      /* Check options */
      switch( pCommandSyntax->eOptionKind )
      {
      case eNoOptions:
        assert( pCommandSyntax->pRequiredOptions == NULL );
        assert( pCommandSyntax->pOptionalOptions == NULL );

        if( pCommand->pOptions )
        {
          eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
        }
        break;

      case eSingleOption:
      case eSingleOptionWithValue:
      case eEnvVar:
      case eEnvVarWithValue:
        assert( pCommandSyntax->pRequiredOptions == NULL );
        assert( pCommandSyntax->pOptionalOptions == NULL );

        pOption = pCommand->pOptions;

        if( pOption != NULL )
        {
          if( pCommandSyntax->eOptionKind == eSingleOption
            || pCommandSyntax->eOptionKind == eEnvVar )
          {
            if( ! OptionIsType( pOption, eValueNone ) )
            {
              /* Got a value when we shouldn't have */
              eError = PjlCombineErrors( pContext, eError, eOptionValueUnsupported );
              eError = PjlCombineErrors( pContext, eError, eOptionMissing );
            }
          }
          else
          {
            if( OptionIsType( pOption, eValueNone ) )
            {
              /* No value when we should have one */
              eError = PjlCombineErrors( pContext, eError, eOptionMissingValue );
              eError = PjlCombineErrors( pContext, eError, eOptionMissing );
            }
            /* Value might be wrong type, but we can't check that (yet) */
          }

          if( pCommandSyntax->eOptionKind == eEnvVar
            || pCommandSyntax->eOptionKind == eEnvVarWithValue )
          {
            PjlEnvVar * pEnvVar = LookupEnvironmentVariable( pContext, pOption->pzName );

            if( pEnvVar != NULL )
            {
              if( pCommandSyntax->eOptionKind == eEnvVarWithValue )
              {
                if( pEnvVar->pModifier == NULL )
                {
                  if( pCommand->pModifier != NULL )
                  {
                    /* Modifier provided when it shouldn't be */
                    eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
                    eError = PjlCombineErrors( pContext, eError, eOptionMissing );
                  }
                }
                else
                {
                  if( pCommand->pModifier != NULL )
                  {
                    if( ! OptionIsType( pCommand->pModifier, eValueAlphanumeric ) )
                    {
                      /* Modifier value is of wrong type */
                      eError = PjlCombineErrors( pContext, eError, eNoAlphanumericAfterModifier );
                    }
                    else if( strcmp( (const char *) pEnvVar->pModifier->pzName, (const char *) pCommand->pModifier->pzName ) != 0 )
                    {
                      /* Wrong modifier */
                      eError = PjlCombineErrors( pContext, eError, eUnsupportedModifer );
                    }
                    else if( strcmp( (const char *) pEnvVar->pModifier->value.u.pzValue, (const char *) pCommand->pModifier->value.u.pzValue ) != 0 )
                    {
                      /* Wrong modifer value */
                      if( strcmp( (const char *) pEnvVar->pModifier->pzName, "LPARM" ) == 0 )
                      {
                        eError = PjlCombineErrors( pContext, eError, eUnsupportedPersonality );
                      }
                      else
                      {
                        eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
                        eError = PjlCombineErrors( pContext, eError, eOptionMissing );
                      }
                    }
                  }
                  else
                  {
                    /* No modifier provided when it should be */
                    eError = PjlCombineErrors( pContext, eError, eModifierMissing );
                  }
                }

                if( pEnvVar->eType == eValueDouble && OptionIsType( pOption, eValueInt )  )
                {
                  /* Convert provided int value to double */
                  double dValue = (double) pOption->value.u.iValue;

                  pOption->value.eType = eValueDouble;
                  pOption->value.u.dValue = dValue;
                }
                else if( ! OptionIsType( pOption, pEnvVar->eType ) )
                {
                  eError = PjlCombineErrors( pContext, eError, eOptionWrongValueType );
                  eError = PjlCombineErrors( pContext, eError, eOptionMissing );
                }
              }
            }
            else
            {
              /* Unknown environment variable */
              eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
              eError = PjlCombineErrors( pContext, eError, eOptionMissing );
            }
          }

          /* Error if any more options */
          if( pOption->pNext != NULL )
          {
            eError = PjlCombineErrors( pContext, eError, eExtraDataAfterOption );
          }
        }
        else
        {
          eError = PjlCombineErrors( pContext, eError, eOptionMissing );
        }
        break;

      case eRequiredOptions:
        assert( pCommandSyntax->pRequiredOptions != NULL );
        assert( pCommandSyntax->pOptionalOptions == NULL );

        /* Check that we have all the required options */
        for( pRequiredOption = pCommandSyntax->pRequiredOptions; pRequiredOption != NULL; pRequiredOption = pRequiredOption->pNext )
        {
          pOption = FindOption( pCommand, pRequiredOption->pzName, strlen( (const char *) pRequiredOption->pzName ) );

          if( pOption != NULL )
          {
            if( ! OptionIsType( pOption, pRequiredOption->value.eType ) )
            {
              if( OptionIsType( pOption, eValueNone ) )
              {
                /* Required option has no value */
                eError = PjlCombineErrors( pContext, eError, eOptionMissingValue );
                eError = PjlCombineErrors( pContext, eError, eOptionMissing );
              }
              else
              {
                /* Required option has value of wrong type */
                eError = PjlCombineErrors( pContext, eError, eOptionWrongValueType );
                eError = PjlCombineErrors( pContext, eError, eOptionMissing );
              }
            }
          }
          else
          {
            /* Required option is missing */
            eError = PjlCombineErrors( pContext, eError, eOptionMissing );
          }
        }

        /* Check that there aren't any other options */
        pOption = pCommand->pOptions;

        while( pOption != NULL )
        {
          HqBool fRequiredOption = FALSE;

          pNextOption = pOption->pNext;

          for( pRequiredOption = pCommandSyntax->pRequiredOptions; pRequiredOption != NULL; pRequiredOption = pRequiredOption->pNext )
          {
            if( OptionMatches( pOption, pRequiredOption->pzName, strlen( (const char *) pRequiredOption->pzName ) ) )
            {
              fRequiredOption = TRUE;
              break;
            }
          }

          if( ! fRequiredOption )
          {
            /* Ignore bad option */
            eError = PjlCombineErrors( pContext, eError, eUnsupportedOption);
            FreeOneOption( pContext, pCommand, pOption );
          }

          pOption = pNextOption;
        }
        break;

      case eOptionalOptions:
        assert( pCommandSyntax->pRequiredOptions == NULL );
        assert( pCommandSyntax->pOptionalOptions != NULL );

        pOption = pCommand->pOptions;

        /* Check that any options we have are OK */
        while( pOption != NULL )
        {
          HqBool fOptionalOption = FALSE;

          pNextOption = pOption->pNext;

          for( pOptionalOption = pCommandSyntax->pOptionalOptions; pOptionalOption != NULL; pOptionalOption = pOptionalOption->pNext )
          {
            if( OptionMatches( pOption, pOptionalOption->pzName, strlen( (const char *) pOptionalOption->pzName ) ) )
            {
              fOptionalOption = TRUE;

              if( ! OptionIsType( pOption, pOptionalOption->value.eType ) )
              {
                if( OptionIsType( pOption, eValueNone ) )
                {
                  /* Optional option has no value, ignore it */
                  eError = PjlCombineErrors( pContext, eError, eOptionMissingValue );
                  FreeOneOption( pContext, pCommand, pOption );
                }
                else
                {
                  /* Optional option has value of wrong type, ignore it */
                  eError = PjlCombineErrors( pContext, eError, eOptionWrongValueType );
                  FreeOneOption( pContext, pCommand, pOption );
                }
              }

              break;
            }
          }

          if( ! fOptionalOption )
          {
            /* Ignore bad option */
            eError = PjlCombineErrors( pContext, eError, eUnsupportedOption );
            FreeOneOption( pContext, pCommand, pOption );
          }

          pOption = pNextOption;
        }
        break;

      default:
        /* Unknown option type */
        assert( 0 );
        break;
      }
      break;

    default:
      /* Unknown command format */
      assert( 0 );
      break;
    }

    /* Additional command-specific validation */

  }
  else
  {
    eError = PjlCombineErrors( pContext, eError, eUnsupportedCommand );
  }

  return eError;
}


/**
 * \brief Process command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command to process.
 *
 * \param eParseError Any error from parsing the command - one of the error
 * enumeration values.
 *
 * \param pePersonality Set to personality expected of following data.
 *
 * \return One of the error enumeration values.
 */
static int32 ProcessCommand( PjlParserContext * pContext, PjlCommand * pCommand, int32 eParseError, int32 * pePersonality )
{
  int32 eValidationError;
  int32 eProcessingError = eNoError;

  assert( pContext );
  assert( pCommand );
  assert( pePersonality );

  eValidationError = ValidateCommand( pContext, pCommand, eParseError );

  if( ! IsError( eValidationError ) )
  {
    if( CommandMatches( pCommand, ENTER_CMD, ENTER_LENGTH ) )
    {
      /* Process ENTER command to determine PDL which follows */
      PjlOption * pLanguageOption = FindOption( pCommand, LANGUAGE_OPTION, LANGUAGE_LENGTH );

      assert( pLanguageOption );       /* Validation has checked for presence of option */
      assert( pCommand->pOptions == pLanguageOption );    /* and that it is only option */
      assert( pLanguageOption->pNext == NULL );
      assert( OptionIsType( pLanguageOption, eValueAlphanumeric ) );  /* and the correct type */

      if( StringMatches( pLanguageOption->value.u.pzValue, PCLXL_LANGUAGE, PCLXL_LANG_LENGTH ) )
      {
        *pePersonality = ePCLXL;
      }
      else if( StringMatches( pLanguageOption->value.u.pzValue, PCL5_LANGUAGE, PCL5_LANG_LENGTH ) )
      {
        *pePersonality = ePCL5c;
      }
      else if( StringMatches( pLanguageOption->value.u.pzValue, PS_LANGUAGE, PS_LANG_LENGTH ) )
      {
        *pePersonality = ePostScript;
      }
      else if( StringMatches( pLanguageOption->value.u.pzValue, PDF_LANGUAGE, PDF_LANG_LENGTH ) )
      {
        *pePersonality = ePDF;
      }
      else if( StringMatches( pLanguageOption->value.u.pzValue, XPS_LANGUAGE, XPS_LANG_LENGTH ) )
      {
        *pePersonality = eZIP;
      }
      else if( StringMatches( pLanguageOption->value.u.pzValue, IMAGE_LANGUAGE, IMAGE_LANG_LENGTH ) )
      {
        *pePersonality = eImage;
      }
      else
      {
        *pePersonality = eUnknown;
        eProcessingError = eUnsupportedPersonality;
      }
    }
    else if( CommandMatches( pCommand, JOB_CMD, JOB_CMD_LENGTH ) )
    {
      eProcessingError = (pContext->pCommandCallbackFn)( pCommand );

      if( ! IsError( eProcessingError ) )
      {
        pContext->nJobDepth++;
      }
    }
    else if( CommandMatches( pCommand, EOJ_CMD, EOJ_CMD_LENGTH ) )
    {
      if( pContext->nJobDepth == 0 )
      {
        eProcessingError = eEOJwithoutJOB;
      }
      else
      {
        eProcessingError = (pContext->pCommandCallbackFn)( pCommand );

        if( ! IsError( eProcessingError ) )
        {
          pContext->nJobDepth--;
        }
      }
    }
    else if( CommandMatches( pCommand, FSAPPEND_CMD, FSAPPEND_CMD_LENGTH )
      || CommandMatches( pCommand, FSDOWNLOAD_CMD, FSDOWNLOAD_CMD_LENGTH ) )
    {
      /* Need to read the next line to get the binary data.
       * Command will be passed to callback from PjlParserParseData().
       */
      *pePersonality = eNeedMoreData;
    }
    else
    {
      /* Pass other commands to callback for handling */
      eProcessingError = (pContext->pCommandCallbackFn)( pCommand );
    }
  }

  return PjlCombineErrors( pContext, eProcessingError, eValidationError );
}


/**
 * \brief Parse data stream for words for format #3 commands.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 *
 * \param pLine Pointer to data stream.
 *
 * \param cbLineLen Length of data stream.
 *
 * \return One of the error enumeration values.
 */
static int32 ParseWords( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pLine, size_t cbLineLen )
{
  int32 eError = eNoError;

  uint8 * pLineEnd;
  uint8 * pWordsEnd;

  assert( pContext );
  assert( pCommand );
  assert( pLine );

  pLineEnd = pLine + cbLineLen;

  eError = ReadWords( pLine, pLineEnd, &pWordsEnd );

  if( eError == eNoError )
  {
    PjlOption * pOption;

    pOption = NewOption( pContext, WORDS_OPTION, WORDS_LENGTH );

    if( pOption != NULL )
    {
      size_t cbWordsLen = pWordsEnd - pLine;

      if( cbWordsLen > MAX_WORDS_LEN )
      {
        /* Silently limit words to max length - no warning */
        cbWordsLen = MAX_WORDS_LEN;
      }

      eError = SetOptionStringValue( pContext, pOption, pLine, cbWordsLen, eValueString );

      if( eError == eNoError )
      {
        AddOption( pCommand, pOption );

        /* Check that there's nothing else */
        if( pWordsEnd != pLineEnd )
        {
          /* Extraneous data after value */
          eError = eGenericWarningError;
        }
      }
      else
      {
        /* No memory */
        FreeOption( pContext, pOption );
      }
    }
    else
    {
      /* No memory */
      eError = eOutOfMemory;
      FreeOption( pContext, pOption );
    }
  }

  return eError;
}


/**
 * \brief Parse data stream for command options for format #4 commands.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 *
 * \param pLine Pointer to data stream.
 *
 * \param cbLineLen Length of data stream.
 *
 * \return One of the error enumeration values.
 */
static int32 ParseOptions( PjlParserContext * pContext, PjlCommand * pCommand, uint8 * pLine, size_t cbLineLen )
{
  int32 eError = eNoError;

  uint8 * pLineEnd;
  uint8 * pName;
  uint8 * pNameEnd;
  uint8 * pValue;
  uint8 * pValueEnd;
  HqBool fIsModifier;

  assert( pContext );
  assert( pCommand );
  assert( pLine );

  pLineEnd = pLine + cbLineLen;

  while( pLine < pLineEnd )
  {
    pName = pLine;

    if( ReadAlphaNum( pName, pLineEnd, &pNameEnd ) )
    {
      HqBool fSkipped;
      size_t cbNameLen = pNameEnd - pName;

      pValue = pNameEnd;

      /* Skip optional whitespace after name */
      fSkipped = SkipWhiteSpace( &pValue, pLineEnd );

      /* Check for : (modifier) or = (option) before value */
      if( *pValue == '=' || *pValue == ':' )
      {
        fIsModifier = ( *pValue == ':' ) ? TRUE : FALSE;
        pValue++;

        /* Skip optional whitespace before value */
        SkipWhiteSpace( &pValue, pLineEnd );

        if( pValue < pLineEnd )
        {
          PjlOption * pOption;

          pOption = NewOption( pContext, pName, cbNameLen );

          if( pOption != NULL )
          {
            eError = SetOptionValue( pContext, pOption, pValue, pLineEnd, &pValueEnd );

            if( eError == eNoError )
            {
              if( fIsModifier )
              {
                eError = NoteModifier( pCommand, pOption );
              }
              else
              {
                eError = NoteOption( pCommand, pOption );
              }
            }

            if( eError == eNoError )
            {
              /* Skip optional whitespace after value */
              SkipWhiteSpace( &pValueEnd, pLineEnd );
              pLine = pValueEnd;
            }
            else
            {
              FreeOption( pContext, pOption );
            }
          }
          else
          {
            /* No memory */
            eError = eOutOfMemory;
          }
        }
        else
        {
          /* No value */
          eError = eNoOptionValue;
        }
      }
      else
      {
        /* Option without value.
         * Check that we skipped whitespace or are at EOL.
         */
        if( fSkipped || pValue == pLineEnd )
        {
          PjlOption * pOption;

          pOption = NewOption( pContext, pName, cbNameLen );

          if( pOption != NULL )
          {
            eError = NoteOption( pCommand, pOption );

            if( eError != eNoError )
            {
              FreeOption( pContext, pOption );
            }
          }
          else
          {
            /* No memory */
            eError = eOutOfMemory;
          }

          pLine = pValue;
        }
        else
        {
          /* Bad char in option name */
          eError = eInvalidAlphanumericChar;
        }
      }
    }
    else
    {
      /* Option or modifier name not alphanumeric */
      eError = eInvalidAlphanumericChar;
    }

    if( eError != eNoError )
    {
      /* Don't read any more options if there has been an error or warning */
      break;
    }
  }

  return eError;
}


/**
 * \brief Set modifier into command.
 *
 * \param pCommand Pointer to command.
 *
 * \param pModifier Modifer.
 *
 * \return One of the error enumeration values.
 */
static int32 NoteModifier( PjlCommand * pCommand, PjlOption * pModifier )
{
  int32 eError = eNoError;

  assert( pCommand );
  assert( pModifier );

  if( pCommand->pModifier != NULL )
  {
    /* Command already has a modifier */
    eError = eTooManyModifiers;
  }
  else if( pCommand->pOptions != NULL )
  {
    /* Command already has options (any modifier must precede options) */
    eError = eModifierAfterOption;
  }
  else
  {
    pCommand->pModifier = pModifier;
  }

  return eError;
}


/**
 * \brief Allocate memory for a new option with the given name.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pName Pointer to option name.
 *
 * \param cbNameLen Length of option name.
 *
 * \return Allocated option, or NULL if memory cannot be allocated.
 */
static PjlOption * NewOption( PjlParserContext * pContext, uint8 * pName, size_t cbNameLen )
{
  PjlOption * pOption;

  assert( pContext );
  assert( pName );
  assert( cbNameLen > 0 );

  pOption = (PjlOption *) PjlMemAlloc( pContext, sizeof( PjlOption ), TRUE );

  if( pOption != NULL )
  {
    pOption->pzName = CopyToUpperCase( pContext, pName, cbNameLen );

    if( pOption->pzName == NULL )
    {
      PjlMemFree( pContext, pOption );
      pOption = NULL;
    }
  }

  return pOption;
}


/**
 * \brief Check command's options for an option with the given name.
 *
 * \param pCommand Pointer to command.
 *
 * \param pName Pointer to option name.
 *
 * \param cbNameLen Length of option name.
 *
 * \return Option if found, or NULL if command does not have option.
 */
static PjlOption * FindOption( PjlCommand * pCommand, uint8 * pName, size_t cbNameLen )
{
  PjlOption * pOption;

  assert( pCommand );
  assert( pName );
  assert( cbNameLen > 0 );

  pOption = pCommand->pOptions;

  while( pOption != NULL )
  {
    if( StringMatches( pOption->pzName, pName, cbNameLen ) )
    {
      return pOption;
    }

    pOption = pOption->pNext;
  }

  return NULL;
}


/**
 * \brief Add an option to a command.
 *
 * \param pCommand Pointer to command.
 *
 * \param pNewOption Option to add.
 */
static void AddOption( PjlCommand * pCommand, PjlOption * pNewOption )
{
  assert( pCommand );
  assert( pNewOption );
  assert( pNewOption->pNext == NULL );

  if( pCommand->pOptions == NULL )
  {
    pCommand->pOptions = pNewOption;
  }
  else
  {
    PjlOption * pOption = pCommand->pOptions;

    while( pOption->pNext != NULL )
    {
      pOption = pOption->pNext;
    }

    pOption->pNext = pNewOption;
  }
}


/**
 * \brief Add an option to a command, checking that it is OK to do so.
 *
 * \param pCommand Pointer to command.
 *
 * \param pOption Option to add.
 *
 * \return One of the error enumeration values.
 */
static int32 NoteOption( PjlCommand * pCommand, PjlOption * pOption )
{
  int32 eError = eNoError;

  assert( pCommand );
  assert( pOption );
  assert( pOption->pzName );

  if( FindOption( pCommand, pOption->pzName, strlen( (const char *) pOption->pzName ) ) != NULL )
  {
    /* Already have this option */
    eError = eRepeatedOption;
  }
  else
  {
    AddOption( pCommand, pOption );
  }

  return eError;
}


/**
 * \brief Free memory for an option and its value.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pOption Pointer to option to free.
 */
static void FreeOption( PjlParserContext * pContext, PjlOption * pOption )
{
  assert( pContext );
  assert( pOption );

  if( pOption->pzName != NULL )
  {
    PjlMemFree( pContext, pOption->pzName );
  }

  if( pOption->value.eType == eValueString
    || pOption->value.eType == eValueAlphanumeric
    || pOption->value.eType == eValueBinaryData )
  {
    if( pOption->value.u.pzValue != NULL )
    {
      PjlMemFree( pContext, pOption->value.u.pzValue );
    }
  }

  PjlMemFree( pContext, pOption );
}


/**
 * \brief Free memory for one option of a command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 *
 * \param pCommand Pointer to option to free.
 */
static void FreeOneOption( PjlParserContext * pContext, PjlCommand * pCommand, PjlOption * pFreeOption )
{
  assert( pContext );
  assert( pCommand );
  assert( pFreeOption );

  if( pFreeOption == pCommand->pOptions )
  {
    /* Freeing first option */
    pCommand->pOptions = pFreeOption->pNext;
    FreeOption( pContext, pFreeOption );
  }
  else
  {
    PjlOption * pOption = pCommand->pOptions;

    while( pOption->pNext != NULL )
    {
      if( pOption->pNext == pFreeOption )
      {
        pOption->pNext = pFreeOption->pNext;
        FreeOption( pContext, pFreeOption );
        break;
      }

      pOption = pOption->pNext;
    }
  }
}


/**
 * \brief Free memory for all options of a command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 */
static void FreeAllOptions( PjlParserContext * pContext, PjlCommand * pCommand )
{
  PjlOption * pOption;
  PjlOption * pNextOption;

  assert( pContext );
  assert( pCommand );

  pOption = pCommand->pOptions;

  while( pOption != NULL )
  {
    pNextOption = pOption->pNext;

    FreeOption( pContext, pOption );

    pOption = pNextOption;
  }

  pCommand->pOptions = NULL;
}


/**
 * \brief Set option value from data stream.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pOption Option.
 *
 * \param pData Pointer to data stream.
 *
 * \param pData Pointer to end of data stream.
 *
 * \param ppNext Set to next byte to read from data stream.
 *
 * \return One of the error enumeration values.
 */
static int32 SetOptionValue( PjlParserContext * pContext, PjlOption * pOption, uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext )
{
  int32 eError = eNoError;

  assert( pContext );
  assert( pOption );
  assert( pData );
  assert( *pData != '\0' );
  assert( ppNext );
  assert( pDataEnd );
  assert( pData < pDataEnd );

  if( ( *pData >= 'A' && *pData <= 'Z' )
    || ( *pData >= 'a' && *pData <= 'z' ) )
  {
    /* Alphanumeric variable */
    HqBool fFound = ReadAlphaNum( pData, pDataEnd, ppNext );
    assert( fFound );
    if ( fFound )
      ; /* quiet compiler warning */

    eError = SetOptionStringValue( pContext, pOption, pData, (*ppNext) - pData, eValueAlphanumeric );
  }
  else if( ( *pData >= '0' && *pData <= '9' ) || *pData == '+' || *pData == '-' )
  {
    /* Numeric variable */
    eError = ReadAndSetNumeric( pOption, pData, pDataEnd, ppNext );
  }
  else if( *pData == '"' )
  {
    /* Quoted string value.  Set value to be string without the quotes. */
    eError = ReadQuotedString( pData, pDataEnd, ppNext );

    if( eError == eNoError )
    {
      size_t cbStringLen = ((*ppNext) - pData) - 2;

      eError = SetOptionStringValue( pContext, pOption, pData + 1, cbStringLen, eValueString );
    }
  }
  else if( *pData == '.' )
  {
    /* No digits before decimal point */
    eError = eNumericStartsWithDP;
  }
  else
  {
    /* Bad first char */
    eError = eInvalidFirstChar;
  }

  return eError;
}


/**
 * \brief Set option value to given string.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pOption Option.
 *
 * \param pValue Pointer to value.
 *
 * \param cbValueLen Length of value.
 *
 * \param eType Type of data: eValueString or eValueAlphanumeric.
 *
 * \return One of the error enumeration values.
 */
static int32 SetOptionStringValue( PjlParserContext * pContext, PjlOption * pOption, uint8 * pValue, size_t cbValueLen, int32 eType )
{
  int32 eError = eNoError;

  uint8 * pzValue;

  assert( pContext );
  assert( pOption );
  assert( pOption->value.eType == eValueNone );
  assert( pValue );
  assert( eType == eValueString || eType == eValueAlphanumeric );

  pzValue = PjlMemAlloc( pContext, cbValueLen + 1, FALSE );

  if( pzValue != NULL )
  {
    if( cbValueLen > 0 )
    {
      memcpy( pzValue, pValue, cbValueLen );
    }
    pzValue[ cbValueLen ] = '\0';

    pOption->value.eType = eType;
    pOption->value.u.pzValue = pzValue;
  }
  else
  {
    /* No memory */
    eError = eOutOfMemory;
  }

  return eError;
}


/**
 * \brief Set option value to given integer.
 *
 * \param pOption Option.
 *
 * \param iValue Value.
 */
static void SetOptionIntValue( PjlOption * pOption, int32 iValue )
{
  assert( pOption );
  assert( pOption->value.eType == eValueNone );

  pOption->value.eType = eValueInt;
  pOption->value.u.iValue = iValue;
}


/**
 * \brief Set option value to given double.
 *
 * \param pOption Option.
 *
 * \param dValue Value.
 */
static void SetOptionDoubleValue( PjlOption * pOption, double dValue )
{
  assert( pOption );
  assert( pOption->value.eType == eValueNone );

  pOption->value.eType = eValueDouble;
  pOption->value.u.dValue = dValue;
}


/**
 * \brief Skip whitespace in data stream.
 *
 * \param ppData Pointer to data stream.  Set to point to
 * first non-whitespace char.
 *
 * \param pDataEnd Pointer to end of data stream.
 *
 * \return TRUE if any whitespace was skipped.
 */
static HqBool SkipWhiteSpace( uint8 ** ppData, uint8 * pDataEnd )
{
  HqBool fSkipped = FALSE;

  assert( ppData );
  assert( *ppData );
  assert( **ppData != '\0' );
  assert( pDataEnd );
  assert( *ppData <= pDataEnd );

  while( *ppData < pDataEnd && ( **ppData == '\t' || **ppData == ' ' ) )
  {
    fSkipped = TRUE;
    (*ppData)++;
    assert( **ppData != '\0' );
  }

  return fSkipped;
}


/**
 * \brief Skip alphanumeric chars in data stream.
 *
 * \param pData Pointer to data stream.
 *
 * \param pDataEnd Pointer to end of data stream.
 *
 * \param ppNext Set to next byte to read from data stream,
 * if there is no error.
 *
 * \return TRUE if any alphanumeric chars were skipped.
 */
static HqBool ReadAlphaNum( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext )
{
  HqBool fFound = FALSE;

  assert( pData );
  assert( *pData != '\0' );
  assert( pDataEnd );
  assert( pData <= pDataEnd );
  assert( ppNext );

  if( pData < pDataEnd )
  {
    if( ( *pData >= 'A' && *pData <= 'Z' )
      || ( *pData >= 'a' && *pData <= 'z' ) )
    {
      /* Letters and numbers allowed from now on */
      fFound = TRUE;
      pData++;
      assert( *pData != '\0' );

      while( pData < pDataEnd
        && ( ( *pData >= 'A' && *pData <= 'Z' )
          || ( *pData >= 'a' && *pData <= 'z' )
          || ( *pData >= '0' && *pData <= '9' ) ) )
      {
        pData++;
        assert( *pData != '\0' );
      }

      *ppNext = pData;
    }
  }

  return fFound;
}


/**
 * \brief Skip quoted string in data stream.
 *
 * \param pData Pointer to data stream, which will start with ".
 *
 * \param pDataEnd Pointer to end of data stream.
 *
 * \param ppNext Set to next byte to read from data stream, i.e. character
 * after closing quote, if there is no error.
 *
 * \return One of the error enumeration values.
 */
static int32 ReadQuotedString( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext )
{
  int32 eError = eNoError;

  assert( pData );
  assert( *pData == '"' );
  assert( pDataEnd );
  assert( pData < pDataEnd );
  assert( ppNext );

  pData++;

  /* String can contain chars 32 - 255, except 34 (double quote), plus 9 (tab) */
  while( pData < pDataEnd
    && ( *pData >= '\043' || *pData == '\t' || *pData == ' ' || *pData == '\041' ) )
  {
    pData++;
    assert( *pData != '\0' );
  }

  if( *pData == '"' )
  {
    assert( pData < pDataEnd );

    /* Check that we are either at end, or that next char is whitespace */
    if( pData + 1 == pDataEnd || *(pData + 1) == ' ' || *(pData + 1) == '\t' )
    {
      *ppNext = pData + 1;
    }
    else
    {
      eError = eIllegalCharOrUEL;
    }
  }
  else if( pData < pDataEnd )
  {
    /* Bad char */
    eError = eIllegalCharOrUEL;
  }
  else
  {
    /* No closing double quote */
    eError = eMissingClosingQuote;
  }

  return eError;
}


/**
 * \brief Skip words in data stream.
 *
 * \param pData Pointer to data stream.
 *
 * \param pDataEnd Pointer to end of data stream.
 *
 * \param ppNext Set to next byte to read from data stream, i.e. pDataEnd,
 * if there is no error.
 *
 * \return One of the error enumeration values.
 */
static int32 ReadWords( uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext )
{
  int32 eError = eNoError;

  assert( pData );
  assert( pDataEnd );
  assert( pData < pDataEnd );
  assert( ppNext );

  /* String can contain chars 32 - 255 plus 9 (tab) */
  if( *pData >= ' ' || *pData == '\t' )
  {
    /* First char is OK */
    pData++;

    while( pData < pDataEnd
      && ( *pData >= ' ' || *pData == '\t' ) )
    {
      pData++;
      assert( *pData != '\0' );
    }

    if( pData == pDataEnd )
    {
      /* All chars OK */
      *ppNext = pData;
    }
    else
    {
      /* Bad char */
      eError = eIllegalCharOrUEL;
    }
  }
  else
  {
    /* Bad first char */
    eError = eInvalidFirstChar;
  }

  return eError;
}


/**
 * \brief Set option to numeric value read from data stream.
 *
 * \param pOption Pointer to option.
 *
 * \param pData Pointer to data stream.
 *
 * \param pDataEnd Pointer to end of data stream.
 *
 * \param ppNext Set to next byte to read from data stream,
 * if there is no error.
 *
 * \return One of the error enumeration values.
 */
static int32 ReadAndSetNumeric( PjlOption * pOption, uint8 * pData, uint8 * pDataEnd, uint8 ** ppNext )
{
  int32 eError = eNoError;

  HqBool fNegative = FALSE;
  HqBool fDpSeen = FALSE;
  double dValue = 0.0;
  double dMultiplier = 0.1;
  int32  nDigitsAfterDp = 0;

  assert( pOption );
  assert( pData );
  assert( *pData != '\0' );
  assert( pDataEnd );
  assert( pData < pDataEnd );
  assert( ppNext );

  if( *pData == '-' || *pData == '+' )
  {
    if( *pData == '-' )
    {
      fNegative = TRUE;
    }

    pData++;

    if( pData == pDataEnd )
    {
      eError = eNumericNoDigits;
    }
    else if( ! ( *pData >= '0' && *pData <= '9' ) )
    {
      eError = eInvalidNumericChar;
    }
  }
  else
  {
    assert( *pData >= '0' && *pData <= '9' );
  }

  if( eError == eNoError )
  {
    dValue = *pData++ - '0';

    while( pData < pDataEnd && eError == eNoError )
    {
      if( *pData >= '0' && *pData <= '9' )
      {
        if( fDpSeen )
        {
          if( nDigitsAfterDp < 2 )
          {
            /* Limit to 2 decimal places */
            dValue += ((*pData) - '0') * dMultiplier;
            dMultiplier /= 10.0;
          }
          nDigitsAfterDp++;
        }
        else
        {
          dValue *= 10.0;
          dValue += *pData - '0';
        }
        pData++;
      }
      else if( *pData == '.' )
      {
        if( fDpSeen )
        {
          eError = eTwoDPs;
          break;
        }
        else
        {
          fDpSeen = TRUE;
          pData++;
        }
      }
      else if( *pData == ' ' || *pData == '\t' )
      {
        break;
      }
      else
      {
        eError = eInvalidNumericChar;
        break;
      }
    }

    if( eError == eNoError )
    {
      if( fNegative )
      {
        dValue = -dValue;
      }

      if( fDpSeen )
      {
        SetOptionDoubleValue( pOption, dValue );
        *ppNext = pData;
      }
      else
      {
        if( dValue > (double)((int32)0x7FFFFFFF) || dValue < (double)((int32)0x80000000)  )
        {
          eError = eNumericTooLong;
        }
        else
        {
          int32 iValue = (int32) dValue;
          SetOptionIntValue( pOption, iValue );
          *ppNext = pData;
        }
      }
    }
  }

  return eError;
}


/**
 * \brief Get severity of error value.
 *
 * \param eError Error enumeration value.
 *
 * \return One of the error severity enumeration values.
 */
static int32 GetErrorSeverity( int32 eError )
{
  int32 eSeverity;

  if( eError >= eGenericSyntaxError && eError <= eWhiteSpaceAndUELPrecedeData )
  {
    eSeverity = eErrorSeverityError;
  }
  else if( eError != eNoError )
  {
    eSeverity = eErrorSeverityWarning;
  }
  else
  {
    eSeverity = eErrorSeverityNoError;
  }

  return eSeverity;
}


/**
 * \brief Report if error enumeration value is an error, as opposed to
 * a warning.
 *
 * \param eError Error enumeration value.
 *
 * \return TRUE if value is an error value.
 */
static HqBool IsError( int32 eError )
{
  return ( GetErrorSeverity( eError ) == eErrorSeverityError ) ? TRUE : FALSE ;
}


/**
 * \brief Report error via the context's error callback function.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param eError Error enumeration value.
 */
static void ReportError( PjlParserContext * pContext, int32 eError )
{
  assert( pContext );
  assert( eError != eNoError );

  (pContext->pErrorCallbackFn)( eError );
}


/**
 * \brief Free memory held by command.
 *
 * \param pContext Context returned by PjlParserInit.
 *
 * \param pCommand Pointer to command.
 */
static void FreeCommand( PjlParserContext * pContext, PjlCommand * pCommand )
{
  assert( pContext );
  assert( pCommand );

  if( pCommand->pzName != NULL )
  {
    PjlMemFree( pContext, pCommand->pzName );
    pCommand->pzName = NULL;
  }

  if( pCommand->pModifier != NULL )
  {
    FreeOption( pContext, pCommand->pModifier );
    pCommand->pModifier = NULL;
  }

  FreeAllOptions( pContext, pCommand );
}



/* eof */
