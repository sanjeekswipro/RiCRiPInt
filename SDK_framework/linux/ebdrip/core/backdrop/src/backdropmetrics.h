/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:backdropmetrics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop metrics structure
 */
#ifndef __BACKDROPMETRICS_H__
#define __BACKDROPMETRICS_H__

typedef struct backdrop_metrics_t {
  uint32 nBackdrops;
  uint32 nBlocks;
  uint32 nBlocksToDisk;
  uint32 nBytesToDisk;
  uint32 nDuplicateEntries;
  uint32 compressedSizePercent;
} backdrop_metrics_t ;

extern backdrop_metrics_t backdrop_metrics ;

/* Log stripped */
#endif /* !__BACKDROPMETRICS_H__ */
