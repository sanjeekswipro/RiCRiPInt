/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdffs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF File System API
 */

#ifndef __PDFFS_H__
#define __PDFFS_H__


/* pdffs.h */

/* ----- Functions ----- */

int32 pdf_filespec( PDFCONTEXT * pdfc, OBJECT * fspecobj ,
		    OBJECT * psfilename , OBJECT ** ID ,
		    int32 * isVolatile);

/**
 * Locate a file and optionally open it. If the file cannot be found using its
 * full name, the OPI proceset is invoked to search for it.
 *
 * \param filename The name of the file to locate.
 * \param foundFilename Set to the name of the file located, or ONULL if no
 *        matching file could be found.
 * \param file If not NULL, the located file will be opened, and this parameter
 *        will be set to the opened file.
 * \param onlySearchOpi If true, the file will only be searched for via the OPI
 *        procset; no attempt will be made to find 'filename' directly using its
 *        full path.
 * \return FALSE on error.
 */
Bool pdf_locate_file(PDFCONTEXT *pdfc,
                     OBJECT *filename,
                     OBJECT *foundFilename,
                     OBJECT *file,
                     Bool onlySearchOpi);

#endif /* protection for multiple inclusion */

/* Log stripped */
