/** \file
 * \ingroup png
 *
 * $HopeName: COREpng!export:pngfilter.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Template and signature interface for PNGimage filter.
 */

#ifndef __PNGFILTER_H__
#define __PNGFILTER_H__

struct core_init_fns ; /* from SWcore */
struct FILELIST ;

/** \defgroup png PNG images
    \ingroup images */
/** \{ */

/** \brief Initialise global variables to a known state. */
void png_C_globals(struct core_init_fns *fns) ;

/** \brief Test a filestream for a PNG image signature, without consuming
    bytes from the filestream. */
Bool png_signature_test(/*@notnull@*/ /*@in@*/ struct FILELIST *filter);

/** \} */

/* =============================================================================
* Log stripped */

#endif /* protection for multiple inclusion */
