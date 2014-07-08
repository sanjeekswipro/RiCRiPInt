/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:packdata.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to pack/unpack/planarize utility functions.
 */

#ifndef __PACKDATA_H__
#define __PACKDATA_H__

/* packdata.h */

#include "mm.h" /* required since mm_pool_t is an opaque struct */

#define PD_ALLPLANES (-1)

#define PD_PACK     0x01
#define PD_UNPACK   0x02
#define PD_PLANAR   0x04
#define PD_ALIGN    0x08
#define PD_CLEAR    0x10
#define PD_NATIVE   0x20
  /* Normally the unpacked results of successive calls to pd_unpack
     are appended to a single buffer until the buffer is full.  If
     PD_NOAPPEND is set then each call of pd_unpack starts the buffer
     from the beginning.
  */
#define PD_NOAPPEND 0x40

typedef struct PACKDATA PACKDATA ;

PACKDATA *pd_packdataopen( mm_pool_t *pools,
                           int32 ncomps , int32 nconv , int32 bpp ,
                           int32 interleaved , int32 pad ,
                           int32 pack12into16 , int32 use ) ;

int32 pd_packdatasize( PACKDATA *pd ) ;

void pd_packdatafree( PACKDATA *pd ) ;

/**
 * Use pd_unpack() or pd_unpack_icomp() wrappers instead of calling
 * pd_unpackdata() directly.
 */
void pd_unpackdata( PACKDATA *pd ,
                    uint8 *pbuffs[] , int32 nbuffs , int32 icomp ,
                    int32 **rbuf , int32 tconv ) ;

/**
 * pd_unpack unpacks data into int32 containers.  It can read a buffer
 * containing interleaved data, a buffer containing planar data, or separate
 * buffers for each plane of data.  Individual buf ptrs may be null but if so it
 * is expected PD_CLEAR will have been specified in pd_packdataopen.
 */
#define pd_unpack( pd , pbuffs , nbuffs , rbuf , tconv ) \
  pd_unpackdata( pd , pbuffs , nbuffs , -1 , rbuf , tconv )

/**
 * pd_unpack_icomp is like pd_unpack but allows each buf of planar data to be
 * unpacked separately.  The function assumes icomp goes in order from 0 to
 * ncomp.  If the data is already interleaved then use pd_unpack instead.
 */
#define pd_unpack_icomp( pd , buf , icomp , rbuf , tconv ) \
  pd_unpackdata( pd , &(buf) , 1 , icomp , rbuf , tconv )


void pd_pack( PACKDATA *pd ,
              int32 *ubuf ,
              uint8 **rbuf,
              int32 tconv ) ;

void pd_planarize( PACKDATA *pd ,
                   int32 *ubuf ,
                   int32 **rbuf ) ;

/* These are used by HDLT to invert the packed data */
void pd_invert( PACKDATA *pd ,
                uint8 *buf , int32 *maxlevels ,
                uint8 **rbuf ) ;

void pd_set( PACKDATA *pd  , int32 val , int32 plane ) ;
#define pd_clear( pd ) pd_set( pd , 0 , PD_ALLPLANES )

void pd_copy( PACKDATA *pd_src , PACKDATA *pd_dst ,
              int32 offset_src , int32 offset_dst , int32 offset_adj ,
              int32 plane ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
