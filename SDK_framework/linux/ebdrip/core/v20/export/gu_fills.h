/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:gu_fills.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Parameters for the graphic-state file operators
 */

#ifndef __GU_FILLS_H__
#define __GU_FILLS_H__

/* Flags for dofill() */
typedef enum {
  FILL_NORMAL = 0,         /* Normal fill, with all knobs & whistles */
  FILL_NOT_VIGNETTE = 1,   /* Don't do vignette detection */
  FILL_NO_SETG = 2,        /* Don't do DEVICE_SETG() */
  FILL_NOT_ERASE = 4,      /* This is not a pseudo-erase */
  FILL_NO_HDLT = 8,        /* Don't do HDLT */
  FILL_NO_PDFOUT = 16,     /* Don't do PDF output */
  FILL_IS_TRAP = 32,       /* Fill is a trap. For future use. */
  FILL_VIA_VIG = 64,       /* Fill came from Vignette code. */
  FILL_COPYCHARPATH = 128, /* Fill needs to copy char path if necessary */
  FILL_POLYCACHE = 256     /* Worth trying to cache polygon path */
} FILL_OPTIONS;

extern Bool dofill(PATHINFO *path, int32 type, int32 colorType,
                   FILL_OPTIONS options);

void init_polycache_debug(void);
void purge_polygon_cache(int32 eraseno);

#endif /* protection for multiple inclusion */

/* Log stripped */
