/** \file
 * \ingroup hdphoto
 *
 * $HopeName: COREwmphoto!export:wmpfilter.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Template and signature interface for WMP image filter.
 */

#ifndef __WMPFILTER_H__
#define __WMPFILTER_H__

struct core_init_fns ; /* from SWcore */
struct FILELIST ; /* from COREfileio */

/** \defgroup hdphoto Microsoft HD Photo image format
    \ingroup images */
/** \{ */

void wmp_C_globals(struct core_init_fns *fns) ;

/** \brief Fill in template for HD Photo image decode filter. */
void wmp_decode_filter(/*@notnull@*/ /*@out@*/ struct FILELIST* filter);

/** \brief Test a filestream for a HD Photo image signature, without consuming
    bytes from the filestream. */
Bool wmp_signature_test(/*@notnull@*/ /*@in@*/ struct FILELIST *filter);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
