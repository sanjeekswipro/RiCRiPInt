/* impl.h.mpsio: RAVENBROOK MEMORY POOL SYSTEM I/O INTERFACE
 *
 * $Id: export:mpsio.h,v 1.5.25.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mpsio.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * .readership: For MPS client application developers, MPS developers.
 * .sources: design.mps.io
 */

#ifndef mpsio_h
#define mpsio_h

#include "mps.h"	/* for mps_res_t */


typedef struct mps_io_s *mps_io_t;

extern mps_res_t MPS_CALL mps_io_create(mps_io_t *);
extern void MPS_CALL mps_io_destroy(mps_io_t);

extern mps_res_t MPS_CALL mps_io_write(mps_io_t, void *, size_t);
extern mps_res_t MPS_CALL mps_io_flush(mps_io_t);


#endif /* mpsio_h */
