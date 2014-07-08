/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * $HopeName: SWtools!src:imptool.c(EBDSDK_P.1) $
 */

#include <stdio.h>
#include <stdarg.h>

/* Standard */
#include "std.h"
#include "hqcstass.h"

#define FW_ALLOW_DEPRECATED

/* Framework */
#include "fwstring.h"
#include "fwfile.h"
#include "fwboot.h"
#include "fwvector.h"

/* ZLIB */
#include "zlib.h"

typedef struct _ImportContext
{
  uint32         fileTicker;
  uint32         dirTicker;
  uint32         fMultiple;
  FwTextString   pszOutputFilename;
  FwFile         mainOutputFile;
  FwFile         rawDataOutputFile;
  uint32         nDataLines;
  uint32         maxDataLines;
  uint32         nDataFiles;
} ImportContext;

static char szHeader[] =
  "/* THIS FILE IS AUTOMATICALLY-GENERATED. DO NOT EDIT. */\n"
  "\n"
  "#include \"memfs.h\"\n\n";

static void exitError( FwTextString ptbzFormat, ... )
{
  if( ptbzFormat != NULL )
  {
    char    acMessage[256];
    va_list vaList;

    va_start( vaList, ptbzFormat );
    vsprintf( acMessage, ptbzFormat, vaList );
    va_end( vaList );

    printf( "Error: %s\n", acMessage );
  }

  exit( 1 );
}

/**
 * @brief The framework doesn't have an equivalent of <code>fprintf()</code>.
 * But if it did, it would look like this.
 *
 * <p>Works by building a string record first, and then just blatting the
 * formatted record data to the file.
 */
static uint32 FwFilePrintf
  ( FwErrorState *errstate, FwFile f, FwTextString ptbzFormat, ... )
{
  FwStrRecord  record;
  va_list      vaList;
  uint32       sz, result;
  FwStrRecordOpenSize( &record, 2 * FwStrCountBytes( ptbzFormat ) );

  va_start( vaList, ptbzFormat );
  sz = FwStrVPrintf( &record, FALSE, ptbzFormat, vaList );
  va_end( vaList );

  result = FwFileWrite( errstate, f, (void*) FwStrRecordGetBuffer( &record ), sz );
  FwStrRecordAbandon( &record );

  return result;
}

static void ensureRawDataFile( FwErrorState *errState, ImportContext *pContext )
{
  if ( !pContext->fMultiple )
  {
     /* When not in multiple mode, the raw data file is always the main
        output file. */
     pContext->rawDataOutputFile = pContext->mainOutputFile;
  }
  else
  {
    /* We are in multiple mode. If we haven't yet created the first raw data
       file, or if the current raw data file has grown too large, then we
       need to allocate a new raw data file. */
    if ( pContext->rawDataOutputFile == FWFILE_ROGUE
         || pContext->nDataLines >= pContext->maxDataLines )
    {
       FwTextString tmp = FwStrDuplicate( pContext->pszOutputFilename );
       FwStrRecord  rec;
       FwStrRecord  leaf;

       /* If there was an existing file, close it now. */
       if ( pContext->rawDataOutputFile != FWFILE_ROGUE )
         FwFileClose( errState, pContext->rawDataOutputFile );

       /* Derive the name of a new raw data file in the same directory as the
          main output file. */
       FwStrRecordOpen( &rec );
       FwStrRecordOpen( &leaf );
       tmp = FwFileRemoveLeafname( tmp );
       FwStrPutTextString( &rec, tmp );
       FwStrPrintf( &leaf, FALSE, FWSTR_TEXTSTRING( "mfs%d.c" ), pContext->nDataFiles++ );
       FwFilenameConcatenate( &rec, FwStrRecordGetBuffer( &leaf ) );

       /* And try to open it. Leave pContext->rawDataOutputFile as
          FWFILE_ROGUE if this fails. */
       pContext->rawDataOutputFile =
         FwFileOpen( errState, FwStrRecordGetBuffer( &rec ),
                     FW_OPEN_WRONLY | FW_OPEN_CREAT | FW_OPEN_TRUNC );

       if ( pContext->rawDataOutputFile != FWFILE_ROGUE )
       {
         pContext->nDataLines = 0; /* Reset line count for fresh file. */

         FwFileWrite
           (
             errState,
             pContext->rawDataOutputFile,
             (void*) szHeader,
             sizeof( szHeader ) - 1
           );
       }

       /* Clean up stringy bits. */
       FwStrRecordAbandon( &rec );
       FwStrRecordAbandon( &leaf );
       FwMemFree( tmp );
    }
  }
}

#define MAX_BYTES_ON_LINE 16
#define BUFFER_LINE_COUNT 256

static void emitByteArray
  (
    FwErrorState  *errState,
    ImportContext *pContext,
    uint8         *pData,
    uint32         cbData
  )
{
  static const char hexdigits[] = "0123456789ABCDEF";
  uint32       i;
  uint32       bytesOnLine = 0;
  uint32       lineCount = 0;
  char         buf[ MAX_BYTES_ON_LINE * 6 * BUFFER_LINE_COUNT + BUFFER_LINE_COUNT ];
  uint32       pos = 0;


  for ( i = 0; i < cbData; i++ )
  {
    buf[ pos++ ] = '0'; buf[ pos++ ] = 'x';
    buf[ pos++ ] = hexdigits[ pData[ i ] / 16 ];
    buf[ pos++ ] = hexdigits[ pData[ i ] % 16 ];
    buf[ pos++ ] = ',';
    buf[ pos++ ] = ' ';

    bytesOnLine++;
    if ( bytesOnLine == MAX_BYTES_ON_LINE )
    {
      buf[ pos++ ] = '\n';
      bytesOnLine = 0;
      lineCount ++;
      if ( lineCount == BUFFER_LINE_COUNT )
      {
        FwFileWrite( errState, pContext->rawDataOutputFile, (void*) buf, pos );
        lineCount = 0;
        pos = 0;
        pContext->nDataLines += BUFFER_LINE_COUNT;
      }
    }
  }

  /* Flush any remainder. */
  if ( pos > 0 )
    FwFileWrite( errState, pContext->rawDataOutputFile, (void*) buf, pos );
}

static FwTextString emitFile
  (
    FwErrorState *errState,
    ImportContext *pContext,
    FwTextString ptbzInputFile
  )
{
  Hq32x2          fileSize64;
  uint32          fileSize;
  char           *fileData = NULL;
  char           *compressedData = NULL;
  uint32          compressedSize;
  int32           fCompressed = FALSE;
  uint32          fSuccess = FALSE;
  char           *data;
  uint32          size;
  FwStrRecord     nodeName;

  FwFile inputFile = FwFileOpen
    (
      errState,
      ptbzInputFile,
      FW_OPEN_RDONLY
    );

  fprintf( stderr, "MFS Importing %s ... ", (char*) ptbzInputFile );

  FwStrRecordOpen( &nodeName );
  FwStrPrintf( &nodeName, FALSE, FWSTR_TEXTSTRING( "file%d_N" ), pContext->fileTicker );

  if ( inputFile == FWFILE_ROGUE )
    goto cleanup;

  ensureRawDataFile( errState, pContext );

  if ( pContext->rawDataOutputFile == FWFILE_ROGUE )
    goto cleanup;

  if ( FwFileExtent( errState, inputFile, &fileSize64 ) == -1 )
    goto cleanup;

  if ( !Hq32x2ToUint32( &fileSize64, &fileSize ) )
    goto cleanup;

  fileData =
    (char*) FwMemAlloc( fileSize, FWMEM_ALLOC_FAIL_NULL | FWMEM_INIT_UNSPECIFIED );

  fprintf( stderr, "%d bytes ... ", fileSize );

  if ( fileData == NULL )
    goto cleanup;

  /* Read entire input file into memory. */
  if ( FwFileRead( errState, inputFile, (void*) fileData, fileSize ) < fileSize )
    goto cleanup;

  size = fileSize;
  data = fileData;

  /* Allocate a secondary buffer of equal size. We will attempt a ZLIB compression
     into this buffer. If the allocation fails, or decompression is not
     possible, then just import the uncompressed data. */

  compressedData =
    (char*) FwMemAlloc( fileSize, FWMEM_ALLOC_FAIL_NULL | FWMEM_INIT_UNSPECIFIED );
  compressedSize = fileSize;

  if ( compressedData != NULL )
  {
    int zresult = compress
      (
        (uint8 *) compressedData,
        (uLongf*) &compressedSize,
        (uint8 *) fileData,
        fileSize
      );

    if ( zresult == Z_OK )
    {
      size = compressedSize;
      data = compressedData;
      fCompressed = TRUE;
      fprintf( stderr,"(Compressed to %d bytes) ... ", compressedSize );
    }
  }

  /* So, start outputting. If the raw data file and the main file are not
     the same, then emit an extern declaration for the raw data into the
     main file. */

  if ( pContext->rawDataOutputFile != pContext->mainOutputFile )
  {
    FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "extern uint8 file%d_DAT[];\n\n" ),
                  pContext->fileTicker );
  }

  /* Emit the raw data. */

  fprintf( stderr, "data ... " );

  if ( pContext->fMultiple )
  {
    /* If we are emitting raw data into separate files from the descriptors,
       emit an extra comment to label the raw data with the file it was
       imported from. */
    FwFilePrintf( errState, pContext->rawDataOutputFile, FWSTR_TEXTSTRING( "/* %s */\n" ),
                  ptbzInputFile );
  }

  FwFilePrintf( errState, pContext->rawDataOutputFile, FWSTR_TEXTSTRING( "uint8 file%d_DAT[] = {\n" ),
                pContext->fileTicker );

  emitByteArray( errState, pContext, (uint8 *) data, size );

  FwFilePrintf( errState, pContext->rawDataOutputFile, FWSTR_TEXTSTRING( "0x0\n};\n\n" ) );

  /* Emit the file descriptor, labelled with a comment that gives the
     absolute pathname. */

  fprintf( stderr, "descriptor ..." );

  FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "/* %s */\n" ), ptbzInputFile );

  if ( fCompressed )
  {
    FwFilePrintf
      (
        errState,
        pContext->mainOutputFile,
        FWSTR_TEXTSTRING( "MFSFILE file%d_F =\n"
        "{\n"
        "  %d,\n"           /* cbSize */
        "  NULL,\n"         /* pData */
        "  0,\n"            /* nReaders */
        "  0,\n"            /* nWriters */
        "  FALSE,\n"        /* fDynamicBuffer */
        "  0,\n"            /* cbCapacity */
        "  TRUE,\n"         /* fCompressed */
        "  %d,\n"           /* cbCompressedSize */
        "  FALSE,\n"        /* fDynamicCompressedBuffer */
        "  file%d_DAT,\n"   /* pCompressedData */
        "  FALSE\n"         /* fModified */
        "};\n\n" ),
        pContext->fileTicker,
        fileSize,
        compressedSize,
        pContext->fileTicker
      );
  }
  else
  {
    FwFilePrintf
      (
        errState,
        pContext->mainOutputFile,
        FWSTR_TEXTSTRING( "MFSFILE file%d_F =\n"
        "{\n"
        "  %d,\n"           /* cbSize */
        "  file%d_DAT,\n"   /* pData */
        "  0,\n"            /* nReaders */
        "  0,\n"            /* nWriters */
        "  FALSE,\n"        /* fDynamicBuffer */
        "  %d,\n"           /* cbCapacity */
        "  FALSE,\n"        /* fCompressed */
        "  0,\n"            /* cbCompressedSize */
        "  FALSE,\n"        /* fDynamicCompressedBuffer */
        "  NULL,\n"         /* pCompressedData */
        "  FALSE\n"         /* fModified */
        "};\n\n" ),
        pContext->fileTicker,
        fileSize,
        pContext->fileTicker,
        fileSize
      );
  }

  /* Emit the node descriptor, which looks the same whether the file is compressed
     or not. */

  FwFilePrintf
    (
      errState,
      pContext->mainOutputFile,
      FWSTR_TEXTSTRING( "MFSNODE file%d_N =\n"
      "{\n"
      "  MFS_File,\n"       /* type */
      "  FALSE,\n"          /* fReadOnly */
      "  FALSE,\n"          /* fDynamic */
      "  \"%s\",\n"         /* pszName */
      "  &file%d_F,\n"      /* pFile */
      "  NULL\n"            /* pDir == NULL, because this is a file */
      "};\n\n" ),
      pContext->fileTicker,
      FwFileSkipToLeafname( ptbzInputFile ),
      pContext->fileTicker
    );

  fSuccess = TRUE;
  pContext->fileTicker++;

cleanup:

  if ( inputFile != FWFILE_ROGUE )
    FwFileClose( errState, inputFile );

  if ( fileData != NULL )
    FwMemFree( fileData );

  if ( compressedData != NULL )
    FwMemFree( compressedData );

  if ( fSuccess )
    fprintf( stderr, "OK\n" );
  else
    fprintf( stderr, "FAILED\n" );

  return FwStrRecordClose( &nodeName );
}

static FwTextString emitDirectory
  (
    FwErrorState  *errState,
    ImportContext *pContext,
    FwTextString   ptbzDirectoryName
  )
{
  uint32        fSuccess = FALSE;
  FwVector      items = { FW_VECTOR_NOT_INIT };
  FwStrRecord   nodeName;
  FwDir        *pDir = FWDIR_ROGUE;
  FwTextString  ptbzDirTrailingSep = FwStrDuplicate( ptbzDirectoryName );
  uint32        dirno = pContext->dirTicker++;
  FwStrRecord   leafname;
  FwFileInfo    info;
  int32         i;

  FwStrRecordOpen( &nodeName );
  FwStrPrintf( &nodeName, FALSE, FWSTR_TEXTSTRING( "dir%d_N" ), dirno );

  FwVectorOpen( &items, 16 );

  FwFileEnsureTrailingSeparatorRealloc( &ptbzDirTrailingSep );

  pDir = FwDirOpen( errState, ptbzDirTrailingSep, FWDIR_OPEN_SNAPSHOT );

  if ( pDir == FWDIR_ROGUE )
    goto cleanup;

  /* Emit each item in the directory, and collect up the node names
     into a vector. Each node name is a freshly-allocated string, but
     the cleanup code will abandon the vector and free the elements. */

  FwStrRecordOpen( &leafname );
  while ( FwDirNext( errState, pDir, &leafname, &info, FWFILE_INFO_MASK ) )
  {
    FwTextString ptbzNodeName = NULL;
    FwStrRecord absolutePath;

    FwStrRecordOpen( &absolutePath );
    FwStrPutTextString( &absolutePath, ptbzDirTrailingSep );
    FwStrPutTextString( &absolutePath, FwStrRecordGetBuffer( &leafname ) );

    switch( info.type )
    {
      case FWFILE_TYPE_DIR:
        ptbzNodeName = emitDirectory
          ( errState, pContext, FwStrRecordGetBuffer( &absolutePath ) );
        break;

      case FWFILE_TYPE_FILE:
        ptbzNodeName = emitFile
          ( errState, pContext, FwStrRecordGetBuffer( &absolutePath ) );
        break;

      default:
        /* Only directories and files are supported. Skip this entry. */ ;
    }

    FwStrRecordAbandon( &absolutePath );
    FwStrRecordShorten( &leafname, 0 );

    if ( ptbzNodeName != NULL )
    {
      FwVectorAddElement( &items, (void*) ptbzNodeName );
    }
  }

  FwStrRecordAbandon( &leafname );

  /* Now emit the item list into the main file. */

  FwFilePrintf
    ( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "MFSNODE *dir%d_E[] =\n" ), dirno );
  FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "{\n" ) );

  for ( i = 0; i < items.nCurrentSize; i++ )
  {
    FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "  &%s,\n" ),
                  (FwTextString) FwVectorGetElement( &items, i ) );
  }

  /* Include a NULL terminator. The semantics of MFS don't require it, but
     it avoids emitting an empty array (illegal in C) if the directory is
     empty. */
  FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "  NULL\n" ) );
  FwFilePrintf( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "};\n\n" ) );

  /* Emit the node descriptor. */

  FwFilePrintf
    ( errState, pContext->mainOutputFile, FWSTR_TEXTSTRING( "/* %s */\n" ), ptbzDirectoryName );

  FwFilePrintf
    (
      errState,
      pContext->mainOutputFile,
      FWSTR_TEXTSTRING( "MFSDIR dir%d_D =\n"
      "{\n"
      "  %d,\n"             /* nEntries */
      "  dir%d_E,\n"        /* entries */
      "  FALSE\n"           /* fDynamicList */
      "};\n\n" ),
     dirno,
     items.nCurrentSize,
     dirno
    );

  FwFilePrintf
    (
      errState,
      pContext->mainOutputFile,
      FWSTR_TEXTSTRING( "MFSNODE dir%d_N =\n"
      "{\n"
      "  MFS_Directory,\n"  /* type */
      "  FALSE,\n"          /* fReadOnly */
      "  FALSE,\n"          /* fDynamic */
      "  \"%s\",\n"         /* pszName */
      "  NULL,\n"           /* pFile == NULL, because this is a directory. */
      "  &dir%d_D\n"        /* pDir */
      "};\n\n" ),
      dirno,
      FwFileSkipToLeafname( ptbzDirectoryName ),
      dirno
    );

  fSuccess = TRUE;

cleanup:

  if ( pDir != FWDIR_ROGUE )
    FwDirClose( errState, pDir );

  FwVectorAbandon( &items, TRUE );
  FwMemFree( ptbzDirTrailingSep );
  return FwStrRecordClose( &nodeName );
}

static int import
  ( FwTextString ptbzFSRoot, FwTextString ptbzOutput,
    FwTextString ptbzRootVariable, int fMultiple, int maxline )
{
  FwErrorState  errState = FW_ERROR_STATE_INIT( FALSE );
  FwFile        outputFile;

  outputFile = FwFileOpen( &errState, ptbzOutput,
                           FW_OPEN_WRONLY | FW_OPEN_CREAT | FW_OPEN_TRUNC );

  if ( outputFile != FWFILE_ROGUE )
  {
    ImportContext context;
    FwTextString  ptbzRootNode;

    context.fileTicker = 1;
    context.dirTicker = 1;
    context.fMultiple = fMultiple;
    context.pszOutputFilename = ptbzOutput;
    context.mainOutputFile = outputFile;
    context.rawDataOutputFile = FWFILE_ROGUE;
    context.nDataLines = 0;
    context.maxDataLines = maxline;
    context.nDataFiles = 0;

    FwFileWrite( &errState, outputFile, (void*) szHeader, sizeof( szHeader ) - 1 );

    ptbzRootNode = emitDirectory( &errState, &context, ptbzFSRoot );

    FwFilePrintf( &errState, outputFile, FWSTR_TEXTSTRING( "MFSNODE *%s = &%s;\n" ),
                  ptbzRootVariable, ptbzRootNode );

    FwMemFree( ptbzRootNode );

    FwFileClose( &errState, outputFile );
  }
  else
  {
    fprintf( stderr, "Failed to open output file %s.\n", ptbzOutput );
  }

  return 0;
}

/* Process the command line.
 */

#define COMMAND_ARG_TYPE_NONE     0
#define COMMAND_ARG_TYPE_STRING   1
#define COMMAND_ARG_TYPE_INT      2

#define IS_ARG_PREFIX(_c_) (_c_ == '-' || _c_ == '/')
/* Command line args and their defaults */
typedef struct _CommandArg {
  char * arg_name ;       /* name */
  int    arg_type ;       /* type */
  void * arg_value ;      /* value */
  char * arg_help;        /* help */
} CommandArg ;

static int fDoHelp  = FALSE, fMultiple = FALSE;
static FwTextString rootDir = NULL, outFile = NULL , outVar = NULL;
static int maxLine = 10000;

static CommandArg allArguments[] = {
  { "root" ,     COMMAND_ARG_TYPE_STRING , (void *)&rootDir ,
                  "source:\tsource file or directory to import. Req."},
  { "output" ,   COMMAND_ARG_TYPE_STRING , (void *)&outFile ,
                  "file:\ttarget C file to output. Req."},
  { "var" ,      COMMAND_ARG_TYPE_STRING , (void *)&outVar ,
                  "varname:\tvariable name in target C file. Req."},
  { "multiple" , COMMAND_ARG_TYPE_NONE ,   (void *)&fMultiple ,
                  ":\tmultiple mode. Opt."},
  { "line" ,     COMMAND_ARG_TYPE_INT ,    (void *)&maxLine ,
                  "max:\t(multiple mode) max number of lines in a file. Opt."},
  { "help" ,     COMMAND_ARG_TYPE_NONE ,   (void *)&fDoHelp ,
                  ":\t\tthis message."}
};

static int usage(char * program_name)
{
  CommandArg * arg;
  int j;
  fprintf(stderr, "Usage: %s -root source -output file -var varname [-multiple [-line max]]\n", program_name);
  for (j = 0, arg = &allArguments[0]; j < NUM_ARRAY_ITEMS(allArguments); j++, arg = &allArguments[j])
  {
    fprintf(stderr, "\t-%s %s\n", arg->arg_name, arg->arg_help);
  }
  return FALSE ;
}

/* Process the command line. Returns TRUE if all required options are present;
 * FALSE otherwise.
 */
static int processCommandLine (int32 argc, char ** argv)
{
  char       ** q;
  int        * v;
  CommandArg * arg;
  int        i, j;

  for (i = 1; i < argc; i++)
  {
    for (j = 0, arg = &allArguments[0]; j < NUM_ARRAY_ITEMS(allArguments); j++, arg = &allArguments[j])
    {
      if (IS_ARG_PREFIX(argv[i][0]) && FwStrIEqual((FwTextString)&(argv[i][1]), (FwTextString)arg->arg_name))
      {
        /* Find a match */
        switch (arg->arg_type)
        {
          case COMMAND_ARG_TYPE_NONE :
            v = (int32 *)arg->arg_value ;
            *v = TRUE ;
            argv[i] = NULL ;
            break ;
          case COMMAND_ARG_TYPE_STRING :
            argv[i++] = NULL ;
            if ( i >= argc || IS_ARG_PREFIX(argv[i][0]))
              return usage(argv[0]) ;
            q = (char **)arg->arg_value ;
            *q = argv[i] ;
            argv[i] = NULL ;
            break ;
          case COMMAND_ARG_TYPE_INT :
            argv[i++] = NULL ;
            if ( i >= argc || IS_ARG_PREFIX(argv[i][0]))
              return usage(argv[0]) ;
            v = (int32 *)arg->arg_value ;
            *v = atoi( argv[i] ) ;
            argv[i] = NULL ;
            break ;
          default:
            return usage(argv[0]) ;
        }
        break ;
      }
    }
  }

  if (fDoHelp || !rootDir || !outFile || !outVar)
    return usage(argv[0]);

#ifdef MACINTOSH
  /* Convert partial POSIX paths in args to full HFS paths */
  {
    FwTextString ptbzCurrentDir = FwCurrentDir();
    FwStrRecord  rPath;

    FwStrRecordOpen( &rPath );
    FwStrPutRawString( &rPath, ptbzCurrentDir );
    if( ! FxFileAppendPartialPOSIXPath( &rPath, rootDir ) )
    {
      fprintf( stderr, "Error: cannot process root dir \"%s\".\n", (char*) rootDir );
      (void) usage(argv[0]) ;
      exit( 1 );
    }
    rootDir = FwStrRecordClose( &rPath );

    FwStrRecordOpen( &rPath );
    FwStrPutRawString( &rPath, ptbzCurrentDir );
    if( ! FxFileAppendPartialPOSIXPath( &rPath, outFile ) )
    {
      fprintf( stderr, "Error: cannot process output file \"%s\".\n", (char*) outFile );
      (void) usage(argv[0]) ;
      exit( 1 );
    }
    outFile = FwStrRecordClose( &rPath );
  }
#endif

  return TRUE ;
}

int main( int argc, char *argv[] )
{
  FwBootContext bootContext;
  FwErrorState  error = FW_ERROR_STATE_INIT(FALSE);
  int result = 0;

  FwBootGetDefaultContext( &bootContext );

  bootContext.control.ptbzAppName = FWSTR_TEXTSTRING( "MFS Import Tool" );
  bootContext.control.exiterrorf = (FwExitErrorfFn*) exitError;
  FwBoot( &bootContext );

  if ( !FwBootDone() )
  {
    printf( "Framework boot-up failed.\n" );
    exit(1) ;
  }

  /* parse the commandline to get the source, etc */
  if (! processCommandLine( argc, argv ))
    result = 1;
  else if (! FwFileGetInfo( &error, rootDir, NULL, 0 ))
  {
    fprintf( stderr, "Error: cannot find root dir \"%s\".\n", (char*) rootDir );
    (void) usage(argv[0]) ;
    result = 1 ;
  }
  else
    result = import( rootDir, outFile, outVar, fMultiple, maxLine ) ;

  FwShutdown();
  exit(result);
}

/*
* Log stripped */
