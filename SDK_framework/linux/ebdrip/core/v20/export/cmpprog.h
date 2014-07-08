/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:cmpprog.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine progress bar interface
 */

#ifndef __RCBPROG_H__
#define __RCBPROG_H__

#include "swtimelines.h"

typedef enum {
  RECOMBINE_PROGRESS,
  PRECONVERT_PROGRESS,
  N_PROGRESS_DIALS
} dl_progress_t ;

struct DL_STATE;

void updateDLProgress(dl_progress_t prog_type);
void updateDLProgressTotal(double val, dl_progress_t prog_type);
void openDLProgress(struct DL_STATE *page, dl_progress_t prog_type);
void closeDLProgress(struct DL_STATE *page, dl_progress_t prog_type);

#endif /* _RCBPROG_H__ */


/* Log stripped */
