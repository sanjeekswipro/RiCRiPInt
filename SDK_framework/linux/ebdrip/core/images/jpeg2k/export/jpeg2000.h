/** \file
 * \ingroup jpeg2000
 *
 * $HopeName: HQNjpeg2k-kak6!export:jpeg2000.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Template and signature interfaces for JPEG 2000 image filter.
 */

#ifndef __JPEG2000_H__
#define __JPEG2000_H__

struct FILELIST ;
struct core_init_fns ; /* from SWcore */

/** \addtogroup jpeg2000 */
/** \{ */

void jpeg2000_C_globals(struct core_init_fns *fns) ;

enum {
  jpeg2000_flag_override_cs,
  jpeg2000_flag_scale_to_byte
};

/** \brief Override the colourspace of a JPEG 2000 image. */
void jpeg2000_setflags(/*@notnull@*/ /*@in@*/ struct FILELIST *filter,
                       int flagid,
                       Bool state);

/** \brief Test a filestream for a JPEG 2000 image signature, without
    consuming bytes from the filestream. */
Bool jpeg2000_signature_test(/*@notnull@*/ /*@in@*/ struct FILELIST *filter);

/** \} */

/*
* Log stripped */
#endif /* __JPEG2000_H__ */

