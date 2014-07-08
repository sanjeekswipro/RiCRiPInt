/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:rsdstore.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Reusable Stream Decode (RSD) filter storage routines
 */

#ifndef __RSDSTORE_H__
#define __RSDSTORE_H__


#include "fileioh.h"  /* FILELIST */
#include "rsd.h"  /* RSD_ACCESS_* */

/* -------------------------------------------------------------------------- */

/** \addtogroup filters */
/** \{ */

typedef struct RSD_STORE RSD_STORE ;
typedef struct RSD_BLOCKLIST RSD_BLOCKLIST ;

/* -------------------------------------------------------------------------- */
Bool rsd_storeinit(void);
Bool rsd_storepostboot(void);
void rsd_storefinish(void);

RSD_STORE *rsd_storeopen( FILELIST *dsource , int32 seekable , int32 accesshint ) ;
void rsd_storeclose( RSD_STORE *rsds ) ;
Bool rsd_storeread( RSD_STORE *rsds , uint8 **rbuf , int32 *rbytes ) ;
Bool rsd_storeseek( RSD_STORE *rsds , int32 offset ,
                    int32 *roffset ) ;
int32 rsd_storelength( RSD_STORE *rsds ) ;

Bool rsd_recycle( RSD_BLOCKLIST *ablocklist, int32 tbytes, int32 accesstype,
                  uint8 **rdata, int32 *rbytes );

void rsd_store_filter(FILELIST *flptr) ;

/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
