/* Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_config.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief PMS configuration file support.
 *
 * These routines read a PMS configuration file and act on its contents
 */

#include "pms_config.h"
#include "pms.h"
#include "pms_malloc.h"
#include <stdio.h>
#include <string.h>

/* Types */
enum
{
  eNone = 0,
  eBad,
  eIncomplete,

  eArrayStart,
  eArrayEnd,
  eDictStart,
  eDictEnd,

  eName,
  eNumeric,
  eString,
};

typedef struct
{
  FILE * pFile;

  char   acBuffer[ 256 ];
  int    nBytesInBuffer;

  char * pBuffer;

  char * pToken;
  char * pTokenEnd;
  int    eToken;

  int    fInArray;
  int    fInDict;

  char   acName[ 32 ];
  char   acEntryName[ 32 ];

  void * pData;

} ParseState;

typedef struct
{
  int    value;
  char * pValue;
} EnumAndString;

static EnumAndString *gMediaSourceEnum;
static EnumAndString *gPaperSizeEnum;
static EnumAndString *gMediaTypeEnum;
static EnumAndString *gMediaColorEnum;

static char DontKnow[] = "DONT_KNOW";

/* Forward Declarations */
static int ProcessFileContents( ParseState * pState );

static int ReadFile( ParseState * pState );

static int GetToken( ParseState * pState );
static int ProcessToken( ParseState * pState );

static int ProcessArrayStart( ParseState * pState );
static int ProcessArrayEnd( ParseState * pState );
static int ProcessDictStart( ParseState * pState );
static int ProcessDictEnd( ParseState * pState );
static int ProcessNumericValue( ParseState * pState, long value );
static int ProcessStringValue( ParseState * pState, char * pValue );

static char * SkipWhiteSpace( char * pBuffer, char * pBufferEnd );
static char * FindWhiteSpace( char * pBuffer, char * pBufferEnd );

static int GetEnumValue( EnumAndString * pTable, char * pValue, int * pfKnown );

/* external prototype declarations */
int PMS_GetPaperInfo(PMS_ePaperSize ePaperSize, PMS_TyPaperInfo** ppPaperInfo);
int PMS_GetMediaType(PMS_eMediaType eMediaType, PMS_TyMediaType** ppMediaType);
int PMS_GetMediaSource(PMS_eMediaSource eMediaSource, PMS_TyMediaSource** ppMediaSource);
int PMS_GetMediaColor(PMS_eMediaColor eMediaColor, PMS_TyMediaColor** ppMediaColor);

/**
 * \brief Create a table of media source enums and strings for input conversion.
 */
static EnumAndString *CreateMediaSourceEnumStringTable(int NumFixedMedia)
{
EnumAndString *pTableEntry;
EnumAndString *pStartTable;
PMS_TyMediaSource *ThisMediaSource;
int i;

  pStartTable = (EnumAndString *)OSMalloc(NumFixedMedia*sizeof(EnumAndString), PMS_MemoryPoolPMS);
  pTableEntry = pStartTable;
/* we need to ignore the first PMS enum for AUTO selection */
  for(i = 1; i < NumFixedMedia; i++)
  {
      PMS_GetMediaSource(i, &ThisMediaSource);
      pTableEntry->value = i;
      pTableEntry->pValue = (char *)(ThisMediaSource->szPJLMediaSource);
      pTableEntry++;
  }
  pTableEntry->value = 0;
  pTableEntry->pValue = NULL;
  return pStartTable;
}
/**
 * \brief Create a table of media type enums and strings for input conversion.
 */
static EnumAndString *CreateMediaTypeEnumStringTable(int NumFixedMedia)
{
EnumAndString *pTableEntry;
EnumAndString *pStartTable;
PMS_TyMediaType *ThisMediaType;
int i;
/* needs the number of fixed media + 2 (for the DONT_KNOW + NULL pointer) */
  pStartTable = (EnumAndString *)OSMalloc((NumFixedMedia+2)*sizeof(EnumAndString), PMS_MemoryPoolPMS);
  pTableEntry = pStartTable;

  for(i = 0; i < NumFixedMedia; i++)
  {
      PMS_GetMediaType(i, &ThisMediaType);
      pTableEntry->value = i;
      pTableEntry->pValue = (char *)(ThisMediaType->szPJLType);
      pTableEntry++;
  }
  pTableEntry->value = PMS_TYPE_DONT_KNOW;
  pTableEntry->pValue = DontKnow;
  pTableEntry++;
  pTableEntry->value = 0;
  pTableEntry->pValue = NULL;
  return pStartTable;
}
/**
 * \brief Create a table of media color enums and strings for input conversion.
 */
static EnumAndString *CreateMediaColorEnumStringTable(int NumFixedMedia)
{
EnumAndString *pTableEntry;
EnumAndString *pStartTable;
PMS_TyMediaColor *ThisMediaColor;
int i;
/* needs the number of fixed media + 2 (for the DONT_KNOW + NULL pointer) */
  pStartTable = (EnumAndString *)OSMalloc((NumFixedMedia+2)*sizeof(EnumAndString), PMS_MemoryPoolPMS);
  pTableEntry = pStartTable;

  for(i = 0; i < NumFixedMedia; i++)
  {
      PMS_GetMediaColor(i, &ThisMediaColor);
      pTableEntry->value = i;
      pTableEntry->pValue = (char *)(ThisMediaColor->szPJLColor);
      pTableEntry++;
  }
  pTableEntry->value = PMS_COLOR_DONT_KNOW;
  pTableEntry->pValue = DontKnow;
  pTableEntry++;
  pTableEntry->value = 0;
  pTableEntry->pValue = NULL;
  return pStartTable;
}
/**
 * \brief Create a table of paper size enums and strings for input conversion.
 */
static EnumAndString *CreatePaperSizeEnumStringTable(int NumFixedMedia)
{
EnumAndString *pTableEntry;
EnumAndString *pStartTable;
PMS_TyPaperInfo *ThisPaperInfo;
int i;
/* needs the number of fixed media * 2 + 2 (for the rotated sizes plus DONT_KNOW and NULL pointer) */
  pStartTable = (EnumAndString *)OSMalloc(((NumFixedMedia * 2) +2)*sizeof(EnumAndString), PMS_MemoryPoolPMS);
  pTableEntry = pStartTable;
/* we need to double the loop to pick up the _R selections */
  for(i = 0; i < NumFixedMedia*2; i++)
  {
      PMS_GetPaperInfo(i, &ThisPaperInfo);
      pTableEntry->value = i;
      pTableEntry->pValue = (char *)(ThisPaperInfo->szPJLName);
      pTableEntry++;
  }
  pTableEntry->value = PMS_SIZE_DONT_KNOW;
  pTableEntry->pValue = DontKnow;
  pTableEntry++;
  pTableEntry->value = 0;
  pTableEntry->pValue = NULL;
  return pStartTable;
}
/**
 * \brief Read the config file.
 */
int ReadConfigFile( char * pFilename )
{
  int fSuccess = FALSE;

  ParseState parseState = { 0 };

  parseState.pFile = fopen( pFilename, "rb" );

  if ( parseState.pFile != NULL )
  {
    /* create temporary media lookup tables from the common PMS media data */
    gMediaSourceEnum = CreateMediaSourceEnumStringTable(NUMFIXEDMEDIASOURCES);
    gMediaTypeEnum = CreateMediaTypeEnumStringTable(NUMFIXEDMEDIATYPES);
    gMediaColorEnum = CreateMediaColorEnumStringTable(NUMFIXEDMEDIACOLORS);
    gPaperSizeEnum = CreatePaperSizeEnumStringTable(NUMFIXEDMEDIASIZES);
    /* create the global printer tray definition g_pstTrayInfo from the config file */ 
    fSuccess = ProcessFileContents( &parseState );
    fclose( parseState.pFile );
    /* delete the temporary media lookup tables */
    OSFree(gMediaSourceEnum, PMS_MemoryPoolPMS);
    OSFree(gMediaTypeEnum, PMS_MemoryPoolPMS);
    OSFree(gMediaColorEnum, PMS_MemoryPoolPMS);
    OSFree(gPaperSizeEnum, PMS_MemoryPoolPMS);
  }

  return fSuccess;
}


/**
 * \brief Proces the config file contents.
 */
static int ProcessFileContents( ParseState * pState )
{
  while( ReadFile( pState ) > 0 )
  {
    while( GetToken( pState ) )
    {
      if( ! ProcessToken( pState ) || pState->eToken == eBad )
      {
        return FALSE;
      }
    }
  }

  /* Check that we have processed all of buffer and are not in an array and/or dict */
  return pState->nBytesInBuffer == 0 && ! pState->fInArray && ! pState->fInDict;
}


/**
 * \brief Read next chunk of file into buffer.
 */
static int ReadFile( ParseState * pState )
{
  int nBytesRead;

  if( pState->pBuffer != NULL )
  {
    int nBytesProcessed = (int) ( pState->pBuffer - pState->acBuffer );

    pState->nBytesInBuffer -= nBytesProcessed;

    if( pState->nBytesInBuffer > 0 )
    {
      memcpy( pState->acBuffer, pState->acBuffer + nBytesProcessed, pState->nBytesInBuffer );
    }
  }

  nBytesRead = (int) fread( pState->acBuffer + pState->nBytesInBuffer, 1, sizeof( pState->acBuffer ) - pState->nBytesInBuffer, pState->pFile );

  pState->nBytesInBuffer += nBytesRead;
  pState->pBuffer = pState->acBuffer;

  if( pState->eToken == eIncomplete )
  {
    pState->pToken = pState->pBuffer;

    if( nBytesRead == 0 )
    {
      /* Incomplete token, but no more to read from file.
       * Append a CR to the buffer to cope with a config file with no trailing whitespace.
       */
      pState->acBuffer[ pState->nBytesInBuffer++ ] = '\n';
      nBytesRead = 1;
    }
  }

  return nBytesRead;
}


/**
 * \brief Get the next token from the buffer.
 */
static int GetToken( ParseState * pState )
{
  char * pBuffer = pState->pBuffer;
  char * pBufferEnd = pState->acBuffer + pState->nBytesInBuffer;

  char * pToken;
  char * pTokenEnd;

  if( pState->pToken == NULL )
  {
    pToken = SkipWhiteSpace( pBuffer, pBufferEnd );

    if( pToken == pBufferEnd )
    {
      pState->eToken = eNone;
      pState->pBuffer = pBufferEnd;

      return FALSE;
    }

    pState->pToken = pToken;
  }
  else
  {
    /* Incomplete token read last time */
    pToken = pState->pToken;
  }

  pTokenEnd = FindWhiteSpace( pToken, pBufferEnd );

  if( pTokenEnd != NULL )
  {
    pState->pTokenEnd = pTokenEnd;

    switch( *pToken )
    {
    case '[':
      if( pTokenEnd == pToken + 1 )
      {
        pState->eToken = eArrayStart;
      }
      else
      {
        PMS_SHOW_ERROR("Bad token: %.*s\n", pTokenEnd - pToken, pToken);
        pState->eToken = eBad;
      }
      break;

    case ']':
      if( pTokenEnd == pToken + 1 )
      {
        pState->eToken = eArrayEnd;
      }
      else
      {
        PMS_SHOW_ERROR("Bad token: %.*s\n", pTokenEnd - pToken, pToken);
        pState->eToken = eBad;
      }
      break;

    case '{':
      if( pTokenEnd == pToken + 1 )
      {
        pState->eToken = eDictStart;
      }
      else
      {
        PMS_SHOW_ERROR("Bad token: %.*s\n", pTokenEnd - pToken, pToken);
        pState->eToken = eBad;
      }
      break;

    case '}':
      if( pTokenEnd == pToken + 1 )
      {
        pState->eToken = eDictEnd;
      }
      else
      {
        PMS_SHOW_ERROR("Bad token: %.*s\n", pTokenEnd - pToken, pToken);
        pState->eToken = eBad;
      }
      break;

    case '$':
      if( pTokenEnd >= pToken + 1 )
      {
        pState->eToken = eName;
      }
      else
      {
        PMS_SHOW_ERROR("Bad token: %.*s\n", pTokenEnd - pToken, pToken);
        pState->eToken = eBad;
      }
      break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      pState->eToken = eNumeric;
      break;

    default:
      /* Assume that anything else is a string value */
      pState->eToken = eString;
      break;
    }

    pState->pBuffer = pState->pTokenEnd;

    return TRUE;
  }

  /* Need more data to complete token */
  pState->eToken = eIncomplete;
  pState->pBuffer = pState->pToken;

  return FALSE;
}


/**
 * \brief Process the token obtained by GetToken().
 */
static int ProcessToken( ParseState * pState )
{
  int fSuccess = FALSE;

  char acValue[ 32 ];
  int  len;

  switch( pState->eToken )
  {
  case eNone:
  case eBad:
  case eIncomplete:
    break;

  case eArrayStart:
    if( ! pState->fInArray )
    {
      pState->fInArray = TRUE;
      fSuccess = ProcessArrayStart( pState );
    }
    else
    {
      PMS_SHOW_ERROR("\nNested array\n");
    }
    break;

  case eArrayEnd:
    if( pState->fInArray )
    {
      pState->fInArray = FALSE;
      fSuccess = ProcessArrayEnd( pState );
    }
    else
    {
      PMS_SHOW_ERROR("\nUnexpected array end\n");
    }
    break;

  case eDictStart:
    if( ! pState->fInDict )
    {
      pState->fInDict = TRUE;
      fSuccess = ProcessDictStart( pState );
    }
    else
    {
      PMS_SHOW_ERROR("\nNested dict\n");
    }
    break;

  case eDictEnd:
    if( pState->fInDict )
    {
      pState->fInDict = FALSE;
      fSuccess = ProcessDictEnd( pState );
    }
    else
    {
      PMS_SHOW_ERROR("\nUnexpected dict end\n");
    }
    break;

  case eName:
    if( pState->fInDict )
    {
      /* Name in dictionary - expect to find a value next */
      len = (int) ( pState->pTokenEnd - pState->pToken );
      strncpy( pState->acEntryName, pState->pToken, len );
      pState->acEntryName[ len ] = '\0';
    }
    else
    {
      /* Top level name */
      len = (int) ( pState->pTokenEnd - pState->pToken );
      strncpy( pState->acName, pState->pToken, len );
      pState->acName[ len ] = '\0';
    }
    fSuccess = TRUE;
    break;

  case eNumeric:
    {
      long   value;
      char * pEnd;

      len = (int) ( pState->pTokenEnd - pState->pToken );
      strncpy( acValue, pState->pToken, len );
      acValue[ len ] = '\0';
      value = strtol( acValue, &pEnd, 10 );
      if( pEnd - acValue == len )
      {
        fSuccess = ProcessNumericValue( pState, value );
      }
      else
      {
        PMS_SHOW_ERROR("\nBad numeric value for %s: %s\n", pState->acEntryName, acValue);
      }
    }
    break;

  case eString:
    len = (int) ( pState->pTokenEnd - pState->pToken );
    strncpy( acValue, pState->pToken, len );
    acValue[ len ] = '\0';
    fSuccess = ProcessStringValue( pState, acValue );
    break;
  }

  pState->pToken = NULL;
  pState->pTokenEnd = NULL;

  return fSuccess;
}


/**
 * \brief Process an eArrayStart token
 */
static int ProcessArrayStart( ParseState * pState )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    /* Allocate a local PMS_TyTrayInfo to hold data read from file */
    pState->pData = OSMalloc( sizeof(PMS_TyTrayInfo), PMS_MemoryPoolPMS );

    if( pState->pData == NULL )
    {
      fSuccess = FALSE;
    }
    else
    {
      /* Free any existing tray info */
      if( g_pstTrayInfo != NULL )
      {
        OSFree( g_pstTrayInfo, PMS_MemoryPoolPMS );
        g_pstTrayInfo = NULL;
        g_nInputTrays = 0;
      }
    }
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessArrayStart: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Process an eArrayEnd token
 */
static int ProcessArrayEnd( ParseState * pState )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    /* Free the local PMS_TyTrayInfo */
    OSFree( pState->pData, PMS_MemoryPoolPMS );
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessArrayEnd: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Process an eDictStart token
 */
static int ProcessDictStart( ParseState * pState )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    /* Set tray to default values */
    PMS_TyTrayInfo * pTrayInfo = (PMS_TyTrayInfo *) pState->pData;

    pTrayInfo->eMediaSource = PMS_TRAY_MANUALFEED;
    pTrayInfo->ePaperSize = PMS_SIZE_DONT_KNOW;
    pTrayInfo->eMediaType = PMS_TYPE_DONT_KNOW;
    pTrayInfo->eMediaColor = PMS_COLOR_DONT_KNOW;
    pTrayInfo->uMediaWeight = 0;
    pTrayInfo->nPriority = 0;
    pTrayInfo->bTrayEmptyFlag = FALSE;
    pTrayInfo->nNoOfSheets = 200;
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessDictStart: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Process an eDictEnd token
 */
static int ProcessDictEnd( ParseState * pState )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    /* Add newly defined tray to global array */
    PMS_TyTrayInfo * pTrayInfo = (PMS_TyTrayInfo *) pState->pData;
    PMS_TyTrayInfo * pExistingTrayInfo = (PMS_TyTrayInfo *) g_pstTrayInfo;

    pTrayInfo->bTrayEmptyFlag = ( pTrayInfo->nNoOfSheets == 0 ) ? TRUE : FALSE;

    g_pstTrayInfo = (PMS_TyTrayInfo *) OSMalloc( ( g_nInputTrays + 1 ) * sizeof(PMS_TyTrayInfo), PMS_MemoryPoolPMS );

    if( pExistingTrayInfo != NULL )
    {
      memcpy( g_pstTrayInfo, pExistingTrayInfo, g_nInputTrays * sizeof(PMS_TyTrayInfo) );
      OSFree( pExistingTrayInfo, PMS_MemoryPoolPMS );
    }

    memcpy( g_pstTrayInfo + g_nInputTrays, pTrayInfo, sizeof(PMS_TyTrayInfo) );
    g_nInputTrays++;
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessDictEnd: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Process an eNumeric token
 */
static int ProcessNumericValue( ParseState * pState, long value )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    PMS_TyTrayInfo * pTrayInfo = (PMS_TyTrayInfo *) pState->pData;

    if( strcmp( pState->acEntryName, "$MediaWeight" ) == 0 )
    {
      pTrayInfo->uMediaWeight = (unsigned int) value;
    }
    else if( strcmp( pState->acEntryName, "$Priority" ) == 0 )
    {
      pTrayInfo->nPriority = (int) value;
    }
    else if( strcmp( pState->acEntryName, "$Sheets" ) == 0 )
    {
      pTrayInfo->nNoOfSheets = (unsigned int) value;
    }
    else
    {
      PMS_SHOW_ERROR("\nUnknown entry name in ProcessNumericValue: %s\n", pState->acEntryName);
      fSuccess = FALSE;
    }
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessNumericValue: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Process an eString token
 */
static int ProcessStringValue( ParseState * pState, char * pValue )
{
  int fSuccess = TRUE;

  if( strcmp( pState->acName, "$InputTrays" ) == 0 )
  {
    PMS_TyTrayInfo * pTrayInfo = (PMS_TyTrayInfo *) pState->pData;

    if( strcmp( pState->acEntryName, "$MediaSource" ) == 0 )
    {
      pTrayInfo->eMediaSource = GetEnumValue( gMediaSourceEnum, pValue, &fSuccess );
    }
    else if( strcmp( pState->acEntryName, "$PaperSize" ) == 0 )
    {
      pTrayInfo->ePaperSize = GetEnumValue( gPaperSizeEnum, pValue, &fSuccess );
    }
    else if( strcmp( pState->acEntryName, "$MediaType" ) == 0 )
    {
      pTrayInfo->eMediaType = GetEnumValue( gMediaTypeEnum, pValue, &fSuccess );
    }
    else if( strcmp( pState->acEntryName, "$MediaColor" ) == 0 )
    {
      pTrayInfo->eMediaColor = GetEnumValue( gMediaColorEnum, pValue, &fSuccess );
    }
    else
    {
      PMS_SHOW_ERROR("\nUnknown entry name in ProcessStringValue: %s\n", pState->acEntryName);
      fSuccess = FALSE;
    }
  }
  else
  {
    PMS_SHOW_ERROR("\nUnknown name in ProcessStringValue: %s\n", pState->acName);
    fSuccess = FALSE;
  }

  return fSuccess;
}


/**
 * \brief Find next non-whitespace character in buffer
 */
static char * SkipWhiteSpace( char * pBuffer, char * pBufferEnd )
{
  char * p = pBuffer;

  while( p < pBufferEnd && ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) )
  {
    p++;
  }

  return p;
}


/**
 * \brief Find next whitespace character in buffer
 */
static char * FindWhiteSpace( char * pBuffer, char * pBufferEnd )
{
  char * p = pBuffer;

  while( p < pBufferEnd )
  {
    if( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' )
    {
      return p;
    }

    p++;
  }

  return NULL;
}


/**
 * \brief Get numeric value of enum corresponding to string
 */
static int GetEnumValue( EnumAndString * pTable, char * pValue, int * pfKnown )
{
  while( pTable->pValue != NULL )
  {
    if( strcmp( pValue, pTable->pValue ) == 0 )
    {
      *pfKnown = TRUE;

      return pTable->value;
    }

    pTable++;
  }

  PMS_SHOW_ERROR("\nUnknown enum value: %s\n", pValue);

  *pfKnown = FALSE;

  return 0;  
}


