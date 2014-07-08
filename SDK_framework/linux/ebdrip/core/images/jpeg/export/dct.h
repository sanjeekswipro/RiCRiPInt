/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!export:dct.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Template and signature interface for JPEG image filter.
 */

#ifndef __DCT_H__
#define __DCT_H__

struct core_init_fns ; /* from SWcore */
struct FILELIST ;

/** \defgroup jpeg JPEG images and DCTDecode filter
    \ingroup images */
/** \{ */

void jpeg_C_globals(struct core_init_fns *fns) ;

/** \brief Test a filestream for a baseline JPEG image signature, without
    consuming bytes from the filestream. */
Bool jpeg_signature_test(/*@notnull@*/ /*@in@*/ struct FILELIST *filter);

/** \brief Test a filestream for a JFIF image signature, without consuming
    bytes from the filestream. */
Bool jfif_signature_test(/*@notnull@*/ /*@in@*/ struct FILELIST *filter);

/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
