/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:rsdblist.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * blist data storage abstraction for Reusable Stream Decode filter
 */

#ifndef __RSDBLIST_H__
#define __RSDBLIST_H__


#include "fileioh.h"  /* FILELIST */
#include "rsdstore.h" /* RSD_BLOCKLIST */
#include "mps.h" /* mps_res_t */
#include "mm.h" /* mm_pool_t */
#include "swdevice.h" /* DEVICELIST */

/* -------------------------------------------------------------------------- */
/* Flags to control store purging and block data memory recycling. */
#define RSD_NO_WRITE        0x1 /* Just find, don't write to disk */
#define RSD_FROMFILLBLOCK   0x2 /* sequential access only. */
#define RSD_EXACTBYTES      0x4 /* for when trying to recycle the data. */
#define RSD_ALLOWDISKWRITE  0x8 /* seqn/rand access. */

#define RSD_LENGTH_UNKNOWN -1

enum {
  RSD_ACCESS_SEQN = 0 ,
  RSD_ACCESS_RAND = 1
};


/** The pool for RSD data. */
extern mm_pool_t mm_pool_rsd;


/* -------------------------------------------------------------------------- */
RSD_BLOCKLIST *rsd_blistnew( FILELIST *source, Bool origsource, Bool seekable,
                             Bool encoded, int accesshint );
void  rsd_blistfree( RSD_BLOCKLIST **pblocklist ) ;

mps_res_t rsd_blistscan( mps_ss_t scan_state, RSD_BLOCKLIST *blocklist ) ;

Bool rsd_blistread( RSD_BLOCKLIST *blocklist, Bool saverestorefilepos,
                    uint8 **rbuf, int32 *rbytes );
Bool rsd_blistseek( RSD_BLOCKLIST *blocklist, int32 offset, int32 *roffset );
int32 rsd_blistlength( RSD_BLOCKLIST *blocklist ) ;
void  rsd_blistdolength( RSD_BLOCKLIST *blocklist ) ;

void  rsd_blistsetsource( RSD_BLOCKLIST *blocklist , FILELIST *flptr ) ;
FILELIST *rsd_blistgetsource( RSD_BLOCKLIST *blocklist ) ;
void  rsd_blistclearlock( RSD_BLOCKLIST *blocklist ) ;
void  rsd_blistreset( RSD_BLOCKLIST *blocklist ) ;
void  rsd_blistforcetodisk( RSD_BLOCKLIST *blocklist ) ;
Bool rsd_blistcomplete( RSD_BLOCKLIST *blocklist );

Bool rsd_blistfindreclaim( corecontext_t *context,
                           RSD_BLOCKLIST *blocklist, int32 tbytes,
                           int accesstype, int action, Bool allow_open,
                           int32 *gotone, size_t *purge_size );

void  rsd_blistrecycle( RSD_BLOCKLIST *blocklist ,
                        uint8 **rdata , int32 *rbytes ) ;
void  rsd_blistpurge( RSD_BLOCKLIST *blocklist ) ;

Bool rsd_datapools_create( void );
void  rsd_datapools_destroy( void ) ;
#ifdef RSD_STORE_STATS
int32 rsd_bliststats( RSD_BLOCKLIST *blocklist, Bool dumpstore,
                      int *purge0, int *purge1, int *purge2, int *purgen );
#endif

typedef int32 rsd_device_iterator_t, *rsd_device_iterator_h ;

DEVICELIST *rsd_device_first(rsd_device_iterator_h iter) ;
DEVICELIST *rsd_device_next(rsd_device_iterator_h iter) ;

/* -------------------------------------------------------------------------- */
#ifdef METRICS_BUILD
extern struct rsd_metrics {
  int32 rsd_pool_max_size ;
  int32 rsd_pool_max_objects ;
  int32 rsd_pool_max_frag;
} rsd_metrics ;
#endif

#endif /* protection for multiple inclusion */


/* Log stripped */
