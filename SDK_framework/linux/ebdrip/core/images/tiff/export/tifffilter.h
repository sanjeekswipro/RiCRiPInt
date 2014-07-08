/** \file
 * \ingroup filters
 *
 * $HopeName: SWv20tiff!export:tifffilter.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Template and signature interface for TIFF image filter.
 */

#ifndef __TIFFFILTER_H__
#define __TIFFFILTER_H__


/** \addtogroup filters */
/** \{ */

/** \brief Fill in template for TIFF image decode filter. */
void tiff_decode_filter(/*@notnull@*/ /*@out@*/ FILELIST* filter);

/** \brief Test a filestream for a TIFF image signature, without consuming
    bytes from the filestream. */
Bool tiff_signature_test(/*@notnull@*/ /*@in@*/ FILELIST *filter);

/** \} */

/*
Log stripped */

#endif /* protection for multiple inclusion */
