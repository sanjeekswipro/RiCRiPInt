/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:imfile.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image file storage implementation
 */

#ifndef __IMFILE_H__
#define __IMFILE_H__

struct core_init_fns; /* from SWcore */

void im_file_C_globals(struct core_init_fns *fns);

typedef struct IM_FILES IM_FILES;
typedef struct IM_FILE_CTXT IM_FILE_CTXT;

Bool im_filecreatectxt(IM_FILE_CTXT **imfile_ctxt, Bool parallel);
void im_filedestroyctxt(IM_FILE_CTXT **imfile_ctxt);

Bool im_fileoffset(IM_FILE_CTXT *imfile_ctxt, int16 falign,
                   int32 tbytes, IM_FILES **ffile, int32 *offset);
Bool im_fileseek(IM_FILES *ffile, int32 foffset);
Bool im_fileread(IM_FILES *ffile, uint8 *fdata, int32 fbytes);
Bool im_filewrite(IM_FILES *ffile, uint8 *fdata, int32 fbytes);
Bool im_filecloseall(IM_FILE_CTXT *imfile_ctxt);

#endif /* __IMFILE_H__ protection for multiple inclusion */

/* Log stripped */
