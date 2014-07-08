/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdffs.c(EBDSDK_P.1) $
 * $Id: src:pdffs.c,v 1.26.4.1.1.1 2013/12/19 11:25:15 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF filespec handling code.
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "objecth.h"
#include "fileio.h"
#include "devices.h"
#include "hqmemcpy.h"
#include "dictscan.h"
#include "namedef_.h"
#include "swcopyf.h"
#include "miscops.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfmem.h"

#include "pdfexec.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdffs.h"
#include "pdfx.h"


#define PS_FILENAME_BUFFER_LEN 1024

/* ----------------------------------------------------------------------------
 * pdf_get_embedded_file()
 * Given the value of the /EF (embedded file) key from a file specification
 * dictionary, this function goes and gets the stream associated with it.
 * However, the EF may specify different streams for different platforms.
 * Here, we only accept the stream for the specific platform we're running on
 * except that we may default to the generic /F stream.
 */
static Bool pdf_get_embedded_file( PDFCONTEXT *pdfc,
                                   OBJECT *EFobj,
                                   OBJECT *pStreamObj /* returned */ )
{
  static NAMETYPEMATCH ef_dict[] = {
  /* 0 */ { NAME_Mac  | OOPTIONAL, 2, { OFILE, OINDIRECT }},
  /* 1 */ { NAME_DOS  | OOPTIONAL, 2, { OFILE, OINDIRECT }},
  /* 2 */ { NAME_Unix | OOPTIONAL, 2, { OFILE, OINDIRECT }},
  /* 3 */ { NAME_F    | OOPTIONAL, 2, { OFILE, OINDIRECT }},
           DUMMY_END_MATCH
  } ;

  theTags(*pStreamObj) = ONULL;   /* assume failure */

  if (!pdf_dictmatch( pdfc, EFobj, ef_dict ))
    return FALSE;

  /* Go for the platform dependent one first, then default to the
     generic /F stream. */
#ifdef MACINTOSH
    if (ef_dict[0].result)
      Copy( pStreamObj, ef_dict[0].result );
#endif
#ifdef WIN32
    if (ef_dict[1].result)
      Copy( pStreamObj, ef_dict[1].result );
#endif
#ifdef UNIX
    if (ef_dict[2].result)
      Copy( pStreamObj, ef_dict[2].result );
#endif

  if (oType(*pStreamObj) == ONULL) {
    if (ef_dict[3].result) {
      Copy( pStreamObj, ef_dict[3].result );         /* F */
    } else {
      HQFAIL( "No embedded file specified for this platform." );
      /* NB: caller handles this condition */
    }
  }

  return TRUE;
}


/* ----------------------------------------------------------------------------
 * pdf_filedict_parse()
 * Parse the file specification dictionary (i.e. when the value of the /F key
 * is a dictionary and not just a string).  The resulting file name is returned
 * via the 'filespec' parameter, but if an embedded file was obtained instead,
 * its resolution is returned via the 'pStreamObj' parameter.  The values of
 * the optional /ID (file ID) and /V (volatile flag) are returned via the
 * ID and isVolatile parameters respectively.
 */
static Bool pdf_filedict_parse( PDFCONTEXT *pdfc,
                                OBJECT *fspecobj,
                                PDF_FILESPEC *filespec,
                                OBJECT *pStreamObj,
                                OBJECT **ID,
                                Bool *isVolatile )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *pFN;

  static NAMETYPEMATCH file_dict[] = {
  /* 0 */ { NAME_FS   | OOPTIONAL, 2, { ONAME , OINDIRECT}},
  /* 1 */ { NAME_F    | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
  /* 2 */ { NAME_Mac  | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
  /* 3 */ { NAME_DOS  | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
  /* 4 */ { NAME_Unix | OOPTIONAL, 2, { OSTRING, OINDIRECT }},
  /* 5 */ { NAME_ID   | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  /* 6 */ { NAME_V    | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
  /* 7 */ { NAME_Type | OOPTIONAL, 2, { ONAME, OINDIRECT }},
  /* 8 */ { NAME_EF   | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
           DUMMY_END_MATCH
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( filespec , "pdf_filedict_parse: flptr NULL" ) ;
  HQASSERT( fspecobj , "pdf_filedict_parse: fspecobj NULL" ) ;
  HQASSERT( isVolatile , "pdf_filedict_parse: isVolatile NULL." );
  HQASSERT( ID , "pdf_filedict_parse: ID NULL." );

  if (!pdf_dictmatch( pdfc, fspecobj, file_dict ))
    return FALSE;

  /* Initialize Flags */
  filespec->flags = PDF_FILESPEC_None;

  /* Specify a filesystem */
  if (file_dict[0].result) {
    filespec->filesystem = theICList( oName( *(file_dict[0].result) ) );
    filespec->flags = PDF_FILESPEC_FS;
  } else
    filespec->filesystem = NULL;

  /* Volatile? */
  if (file_dict[6].result)
    *isVolatile = oBool( *(file_dict[6].result) );
  else
    *isVolatile = FALSE;

  /* ID */
  *ID = file_dict[5].result;

  /* If the file is embedded (EF), locate the referenced object and return its
     stream. As far as I can tell from the spec (PDF 1.4 manual), if the
     EF key is specified it's stream "ought" to be used even though the other
     keys in the file spec dictionary might/should still name a file. However,
     since the EF key may fail to yield an embedded file for the current host
     platform, I think that if that happens then we may as well try to follow
     through with the rest of the file specification.  Otherwise, with an
     embedded file succesfully found, we can return straight away. */
  if (file_dict[8].result) {
    if (!pdf_get_embedded_file( pdfc, file_dict[8].result, pStreamObj ))
      return FALSE;

    /* If an embedded file stream has been found, exit now.  (Otherwise,
        continue with the rest of the file specification.) */
    if (oType(*pStreamObj) == OFILE) {
      return TRUE;
    }
  }

  /* Go for the file name.  Note that these are platform dependent but if all
     we've got, say, is a MAC name when runing on a PC, then we can still try
     to use the MAC name since the file itself may have been placed in one of
     the "OPI" folders (and hence found through a search to be conducted by
     pdf_locate_file()). */
  pFN = NULL;

#ifdef MACINTOSH
  if (file_dict[2].result) {                           /* Mac */
    pFN = file_dict[2].result;
    filespec->flags |= PDF_FILESPEC_Mac;
  } else
#endif
#ifdef WIN32
  if (file_dict[3].result) {                           /* DOS */
    pFN = file_dict[3].result;
    filespec->flags |= PDF_FILESPEC_DOS;
  } else
#endif
#ifdef UNIX
  if (file_dict[4].result) {                          /* Unix */
    pFN = file_dict[4].result;
    filespec->flags |= PDF_FILESPEC_Unix;
  } else
#endif
  if (file_dict[1].result) {                             /* F */
    pFN = file_dict[1].result;
    filespec->flags |= PDF_FILESPEC_PDF;
  } else {
    /* Neither the host platform-specific nor the generic (/F) entry is
       present, so go for any. */
    if (file_dict[2].result) {                           /* Mac */
      pFN = file_dict[2].result;
      filespec->flags |= PDF_FILESPEC_Mac;
    } else if (file_dict[3].result) {                    /* DOS */
      pFN = file_dict[3].result;
      filespec->flags |= PDF_FILESPEC_DOS;
    } else if (file_dict[4].result) {                    /* Unix */
      pFN = file_dict[4].result;
      filespec->flags |= PDF_FILESPEC_Unix;
    }
  }

  if (pFN != NULL) {
    filespec->filename.clist = oString( *pFN );
    filespec->filename.len = theILen( pFN );
  } else {
    return error_handler( UNDEFINED );
  }

  return TRUE;
}


/* ----------------------------------------------------------------------------
**  pdf_filespec()
**  This function is given the value of a /F key from a PDF stream (e.g. Form
**  XObject) dictionary.  The /F value is, in the first place, either a string
**  (giving the file specification directly) or else a dictionary.  The
**  dictionary form can yield either a file specification string, or else an
**  embedded file stream.
**  Besides returning the value of the ID and the V (is volatile) keys, this
**  function returns (via 'psfilename') either the string containing the
**  resolved file specification or else a file stream for the embedded file
**  (if present).
*/
Bool pdf_filespec( PDFCONTEXT *pdfc,
                   OBJECT *fspecobj,   /* /F key value */
                   OBJECT *psfilename, /* Returned file spec or embedded file stream */
                   OBJECT **ID,
                   Bool *isVolatile )
{
  FILELIST *flptr;
  uint8 stringbuf[PS_FILENAME_BUFFER_LEN];
  PDF_FILESPEC filespec;
  DEVICELIST *dev;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc ) ;

  HQASSERT( isVolatile , "pdf_filespec: isVolatile NULL." );
  HQASSERT( ID , "pdf_filespec: ID NULL." );
  HQASSERT( fspecobj , "pdf_filespec: fspecobj NULL." );

  switch (oType( *fspecobj ))
  {
  case ODICTIONARY:
    {
      OBJECT streamObj = OBJECT_NOTVM_NOTHING;
      if (!pdf_filedict_parse( pdfc, fspecobj, &filespec, &streamObj, ID, isVolatile ))
        return FALSE;
      if (oType(streamObj) == OFILE) {
        if (! pdfxFileSpecificationDetected(pdfc, TRUE))
          return FALSE;
        Copy( psfilename, &streamObj );
        return TRUE;
      }
    }
    break;

  case OSTRING:
    filespec.filename.clist = oString( *fspecobj );
    filespec.filename.len = theILen( fspecobj );
    filespec.flags = PDF_FILESPEC_PDF;
    *isVolatile = FALSE;
    *ID = NULL;

    break;

  default:
    return error_handler( TYPECHECK );
  }

  if (! pdfxFileSpecificationDetected(pdfc, FALSE))
    return FALSE;

  /* Translate the given file name to PostScript format
     using the ioctl function. */
  flptr = pdfxc->flptr;
  HQASSERT( flptr , "pdf_filespec: flptr NULL." );
  dev = theIDeviceList( flptr );

  filespec.current_filename.clist = theICList( flptr );
  filespec.current_filename.len = theINLen( flptr );
  filespec.current_device = theIDevName(dev);
  filespec.buffer.clist = stringbuf;
  filespec.buffer.len   = PS_FILENAME_BUFFER_LEN;

  if (-1 == (*theIIoctl(dev))( dev,
                               theIDescriptor(flptr),
                               DeviceIOCtl_PDFFilenameToPS,
                               (intptr_t)&filespec ))
    return device_error_handler( dev );

  /* Allocate a string to return the file name. */
  if (!pdf_create_string( pdfc, filespec.buffer.len, psfilename ))
    return FALSE;

  HqMemCpy( oString(*psfilename), stringbuf, filespec.buffer.len );

  return TRUE;
}

/* See header for doc. */
Bool pdf_locate_file(PDFCONTEXT *pdfc,
                     OBJECT *filename,
                     OBJECT *foundFilename,
                     OBJECT *file,
                     Bool onlySearchOpi)
{
  int32 openflags = SW_RDONLY | SW_FROMPS;
  int32 psflags = READ_FLAG;

  OBJECT BuffStr = OBJECT_NOTVM_NOTHING;
  uint8 *pCmd = (uint8*) "/HqnExternalFile /ProcSet resourcestatus {\n"
                         "  pop pop\n"
                         "  (%.*!s) << >> /HqnExternalFile /ProcSet findresource\n"
                         "  /FindExternalFile get exec\n"
                         "} {\n"
                         "  false\n"
                         "} ifelse";
  int32 len ;
  int32 result ;

  int32 ssize;
  OBJECT *pObj;
  int32 FoundFile;
  OBJECT lFileObj = OBJECT_NOTVM_NOTHING;

  HQASSERT( filename != NULL, "filename null" );
  HQASSERT( foundFilename != NULL, "foundFilename null" );

  /* Assume unable to find the file. */
  theTags(*foundFilename) = ONULL;
  if (file != NULL)
    theTags(*file)  = ONULL;

  /* Try to open the file. */
  HQASSERT(!error_signalled(), "File finding while in error condition");
  if (onlySearchOpi ||
      ! file_open( filename, openflags, psflags, FALSE, 0, &lFileObj )) {

    if (! onlySearchOpi && newerror != UNDEFINEDFILENAME)
      return FALSE;   /* i.e. some other ioerror occurred */

    error_clear_context(pdfc->corecontext->error);

    /* The file has not yet been found. Use the same search mechanism
       as for OPI files (via the HqnOPI procset). */
    len = swncopyf(NULL, 0, pCmd, theLen(*filename), oString(*filename)) + 1;

    /* Create string with space for a C string terminating NUL since it will be
     * passed to run_ps_string() which expects a C string, not a PS string.
     */
    if (!pdf_create_string( pdfc, len, &BuffStr ))
      return FALSE;

    (void)swncopyf(oString(BuffStr), len, pCmd, theLen(*filename), oString(*filename));

    result = run_ps_string(oString(BuffStr));
    pdf_destroy_string(pdfc, theLen(BuffStr), &BuffStr);
    if ( !result ) {
      return FALSE;
    }

    /* Pop the boolean off the operand stack indicating success or failure
       to find the file. */
    ssize = theStackSize( operandstack );
    if ( ssize < 0 )
      return error_handler( STACKUNDERFLOW );

    pObj = TopStack( operandstack, ssize );
    if (oType(*pObj) != OBOOLEAN)
      return error_handler( TYPECHECK );

    FoundFile = oBool(*pObj);
    pop( &operandstack );

    if (!FoundFile)   /* Inablility to find the file is not an error. */
      return TRUE;    /* Condition returned to the caller by the fact
                         *foundFilename is ONULL. */

    /* The file has been found.  Obtain the returned full file spec. */
    pObj = theTop(operandstack);
    if (oType(*pObj) != OSTRING)
      return error_handler( TYPECHECK );

    if (!pdf_create_string( pdfc, theILen(pObj), foundFilename ))
      return FALSE;

    HqMemCpy( oString(*foundFilename), oString(*pObj), theILen(pObj) );

    pop( &operandstack );

    /* And try to open the file again (but only if the caller wants us to). */
    if (file != NULL) {
      if (!file_open( foundFilename, openflags, psflags, FALSE, 0, file )) {
        theTags(*file) = ONULL;
        if (newerror == UNDEFINEDFILENAME) {
          /* The file still has not been found, but this is not to be treated
             as an error. The condition is returned to the caller by the fact
             *file is ONULL. */
          error_clear_context(pdfc->corecontext->error);
          return TRUE;
        }
        return FALSE;     /* i.e. some other ioerror */
      }
    }

  } else {
    /* The initial attempt to open the file succeeded. */
    Copy( foundFilename, filename );      /* Return file name string. */
    if (file != NULL)           /* Caller did indeed want the file open */
      Copy( file, &lFileObj );  /* so leave it open, */
    else if (!file_close( &lFileObj )) /* else close it. */
      return FALSE;
  }

  return TRUE;
}

/* Log stripped */
