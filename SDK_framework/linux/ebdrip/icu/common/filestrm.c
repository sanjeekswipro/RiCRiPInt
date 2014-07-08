/* $HopeName: HQNlibicu_3_4!common:filestrm.c(EBDSDK_P.1) $ */
/*
******************************************************************************
*
*   Copyright (C) 1997-2005, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
******************************************************************************
*
* File FILESTRM.C
*
* @author       Glenn Marcy
*
* Modification History:
*
*   Date        Name        Description
*   5/8/98      gm          Created
*  03/02/99     stephen     Reordered params in ungetc to match stdio
*                           Added wopen
*   3/29/99     helena      Merged Stephen and Bertrand's changes.
*
******************************************************************************
*/

#include "filestrm.h"
#include "cmemory.h"
#include "unicode/uclean.h"

#include <stdio.h>

static const void *pContext = NULL ;
static UFileOpenFn *pOpen = NULL ;
static UFileCloseFn *pClose = NULL ;
static UFileReadFn *pRead = NULL ;
static UFileWriteFn *pWrite = NULL ;
static UFileRewindFn *pRewind = NULL ;
static UFileSizeFn *pSize = NULL ;
static UFileEofFn *pEof = NULL ;
static UFileRemoveFn *pRemove = NULL ;
static UFileErrorFn *pError = NULL ;

static UBool gFileInUse = FALSE ;

U_DRAFT void U_EXPORT2
u_setFileFunctions(const void *context,
                   UFileOpenFn *o,
                   UFileCloseFn *c,
                   UFileReadFn *r,
                   UFileWriteFn *w,
                   UFileRewindFn *rew,
                   UFileSizeFn *s,
                   UFileEofFn *e,
                   UFileRemoveFn *rm,
                   UFileErrorFn *err,
                   UErrorCode *status)
{
    if (U_FAILURE(*status)) {
        return;
    }
    if (o==NULL || c==NULL || r==NULL || w==NULL || rew==NULL ||
        s==NULL || e==NULL || rm==NULL || err==NULL) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    if (gFileInUse) {
        *status = U_INVALID_STATE_ERROR;
        return;
    }
    pContext  = context;
    pOpen     = o;
    pClose    = c;
    pRead     = r;
    pWrite    = w;
    pRewind   = rew;
    pSize     = s;
    pEof      = e;
    pRemove   = rm;
    pError    = err;
}

U_CAPI FileStream* U_EXPORT2
T_FileStream_open(const char* filename, const char* mode)
{
    if(filename != NULL && *filename != 0 && mode != NULL && *mode != 0) {
        FILE *file ;
        gFileInUse = TRUE ;
        if ( pOpen != NULL ) {
          return (*pOpen)(pContext, filename, mode) ;
        }
        file = fopen(filename, mode);
        return (FileStream*)file;
    } else {
        return NULL;
    }
}

/*
U_CAPI FileStream* U_EXPORT2
T_FileStream_wopen(const wchar_t* filename, const wchar_t* mode)
{
   // TBD: _wfopen is believed to be MS-specific?
#if defined(U_WINDOWS)
    FILE* result = _wfopen(filename, mode);
    return (FileStream*)result;
#else
    size_t fnMbsSize, mdMbsSize;
    char *fn, *md;
    FILE *result;

    // convert from wchar_t to char
    fnMbsSize = wcstombs(NULL, filename, ((size_t)-1) >> 1);
    fn = (char*)uprv_malloc(fnMbsSize+2);
    wcstombs(fn, filename, fnMbsSize);
    fn[fnMbsSize] = 0;

    mdMbsSize = wcstombs(NULL, mode, ((size_t)-1) >> 1);
    md = (char*)uprv_malloc(mdMbsSize+2);
    wcstombs(md, mode, mdMbsSize);
    md[mdMbsSize] = 0;

    result = fopen(fn, md);
    uprv_free(fn);
    uprv_free(md);
    return (FileStream*)result;
#endif
}
*/
U_CAPI void U_EXPORT2
T_FileStream_close(FileStream* fileStream)
{
    if (fileStream != 0) {
        if ( pClose != NULL ) {
          (*pClose)(pContext, fileStream) ;
          return ;
        }
        fclose((FILE*)fileStream);
    }
}

U_CAPI UBool U_EXPORT2
T_FileStream_file_exists(const char* filename)
{
    FileStream* temp = T_FileStream_open(filename, "r");
    if (temp) {
        T_FileStream_close(temp);
        return TRUE;
    } else
        return FALSE;
}

/*static const int32_t kEOF;
const int32_t FileStream::kEOF = EOF;*/

/*
U_CAPI FileStream*
T_FileStream_tmpfile()
{
    FILE* file = tmpfile();
    return (FileStream*)file;
}
*/

U_CAPI int32_t U_EXPORT2
T_FileStream_read(FileStream* fileStream, void* addr, int32_t len)
{
    if ( pRead != NULL ) {
      return (*pRead)(pContext, addr, len, fileStream) ;
    }
    return fread(addr, 1, len, (FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_write(FileStream* fileStream, const void* addr, int32_t len)
{
    if ( pWrite != NULL ) {
      return (*pWrite)(pContext, addr, len, fileStream) ;
    }
    return fwrite(addr, 1, len, (FILE*)fileStream);
}

U_CAPI void U_EXPORT2
T_FileStream_rewind(FileStream* fileStream)
{
    if ( pRewind != NULL ) {
      (*pRewind)(pContext, fileStream) ;
      return ;
    }
    rewind((FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_putc(FileStream* fileStream, int32_t ch)
{
    int32_t c ;
    if ( pWrite != NULL )
      return EOF ; /* Unsupported functionality; not used by library itself */
    c = fputc(ch, (FILE*)fileStream);
    return c;
}

U_CAPI int U_EXPORT2
T_FileStream_getc(FileStream* fileStream)
{
    int c ;
    if ( pRead != NULL )
      return EOF ; /* Unsupported functionality; not used by library itself */
    c = fgetc((FILE*)fileStream);
    return c;
}

U_CAPI int32_t U_EXPORT2
T_FileStream_ungetc(int32_t ch, FileStream* fileStream)
{

    int32_t c ;
    if ( pRead != NULL )
      return EOF ; /* Unsupported functionality; not used by library itself */
    c = ungetc(ch, (FILE*)fileStream);
    return c;
}

U_CAPI int32_t U_EXPORT2
T_FileStream_peek(FileStream* fileStream)
{
    int32_t c = T_FileStream_getc(fileStream);
    return T_FileStream_ungetc(c, fileStream);
}

U_CAPI char* U_EXPORT2
T_FileStream_readLine(FileStream* fileStream, char* buffer, int32_t length)
{
    if ( pRead != NULL )
      return NULL ; /* Unsupported functionality; only used by tools */
    return fgets(buffer, length, (FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_writeLine(FileStream* fileStream, const char* buffer)
{
    if ( pWrite != NULL )
      return 0 ; /* Unsupported functionality; only used by tools */
    return fputs(buffer, (FILE*)fileStream);
}

U_CAPI int32_t U_EXPORT2
T_FileStream_size(FileStream* fileStream)
{
    int32_t savedPos;
    int32_t size = 0;

    if ( pSize != NULL ) {
      return (*pSize)(pContext, fileStream) ;
    }

    /*Changes by Bertrand A. D. doesn't affect the current position
    goes to the end of the file before ftell*/
    savedPos = ftell((FILE*)fileStream);
    fseek((FILE*)fileStream, 0, SEEK_END);
    size = (int32_t)ftell((FILE*)fileStream);
    fseek((FILE*)fileStream, savedPos, SEEK_SET);
    return size;
}

U_CAPI int U_EXPORT2
T_FileStream_eof(FileStream* fileStream)
{
    if ( pEof != NULL ) {
      return (*pEof)(pContext, fileStream) ;
    }
    return feof((FILE*)fileStream);
}

/*
 Warning
 This function may not work consistently on all platforms
 (e.g. HP-UX, FreeBSD and MacOSX don't return an error when
 putc is used on a file opened as readonly)
*/
U_CAPI int U_EXPORT2
T_FileStream_error(FileStream* fileStream)
{
    if ( fileStream == 0 )
      return 1 ;
    if ( pError != NULL ) {
      return (*pError)(pContext, fileStream) ;
    }
    return ferror((FILE*)fileStream);
}

/* This function doesn't work. */
/* force the stream to set its error flag*/
/*U_CAPI void U_EXPORT2
T_FileStream_setError(FileStream* fileStream)
{
    fseek((FILE*)fileStream, 99999, SEEK_SET);
}
*/

U_CAPI FileStream* U_EXPORT2
T_FileStream_stdin(void)
{
    if ( pOpen != NULL )
      return NULL ; /* Unsupported functionality; only used by tools */
    return (FileStream*)stdin;
}

U_CAPI FileStream* U_EXPORT2
T_FileStream_stdout(void)
{
    if ( pOpen != NULL )
      return NULL ; /* Unsupported functionality; only used by tools */
    return (FileStream*)stdout;
}


U_CAPI FileStream* U_EXPORT2
T_FileStream_stderr(void)
{
    if ( pOpen != NULL )
      return NULL ; /* Unsupported functionality; only used by tools */
    return (FileStream*)stderr;
}

U_CAPI UBool U_EXPORT2
T_FileStream_remove(const char* fileName)
{
    gFileInUse = TRUE ;
    if ( pRemove != NULL ) {
      return ((*pRemove)(pContext, fileName) == 0) ;
    }
    return (remove(fileName) == 0);
}

void init_C_globals_icu_filestrm(void)
{
  gFileInUse = FALSE ;
  pContext = NULL ;
  pOpen = NULL ;
  pClose = NULL ;
  pRead = NULL ;
  pWrite = NULL ;
  pRewind = NULL ;
  pSize = NULL ;
  pEof = NULL ;
  pRemove = NULL ;
  pError = NULL ;
}

/* Log stripped */
