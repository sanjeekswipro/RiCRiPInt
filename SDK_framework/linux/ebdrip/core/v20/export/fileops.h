/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:fileops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * File operations at PostScript level; opening, closing, reading, writing.
 */

#ifndef __FILEOPS_H__
#define __FILEOPS_H__

extern OBJECT *currfileCache;

Bool currfile_cache( void );

void free_flist(SLIST *flist);

Bool execute_filenameforall(OBJECT *scratch, SLIST *head_flist, OBJECT *proc);

OBJECT *get1filestring( uint8 **thec, uint16 *thelen, int8 *glmode, uint8 c1, uint8 c2 );
OBJECT *get1filelongstring( uint8 **thec, int32 *thelen, uint8 c1, uint8 c2 );

Bool ps_file_standard(uint8 *name, int32 len, int32 flags, OBJECT *file) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
