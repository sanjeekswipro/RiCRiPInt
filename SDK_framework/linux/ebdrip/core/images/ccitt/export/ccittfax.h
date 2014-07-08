/** \file
 * \ingroup ccitt
 *
 * $HopeName: COREccitt!export:ccittfax.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for decoding CCITT compressed fax images
 */

#ifndef __CCITTFAX_H__
#define __CCITTFAX_H__

struct core_init_fns ; /* from SWcore */
struct FILELIST ; /* from COREfileio */

/** \defgroup ccitt CCITT images
    \ingroup images */
/** \{ */

void ccitt_C_globals(struct core_init_fns *fns) ;

/*
 * Api for direct CCITT decoding, used for MMR embedded CCITT data inside
 * JBIG2 streams.
 */
struct FILELIST *ccitt_open(struct FILELIST *source,
                            int32 columns, int32 rows,
                            Bool endofblock);

void ccitt_close(struct FILELIST *filter);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
