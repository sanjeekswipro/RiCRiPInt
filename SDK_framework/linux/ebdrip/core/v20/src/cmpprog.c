/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:cmpprog.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Code to setup and control the recombine progress bar.
 */

#include "core.h"
#include "cmpprog.h"
#include "swdevice.h"

#include "display.h"
#include "dlstate.h"
#include "hdl.h"
#include "render.h"
#include "stacks.h"
#include "vndetect.h"
#include "imageo.h" /* IMAGEOBJECT */

/* Progress dial declarations */
#include "fileio.h"
#include "swevents.h"
#include "swtimelines.h"
#include "riptimeline.h"
#include "progupdt.h"
#include "progress.h"

/* Progress Bar Code */

typedef struct {
  sw_tl_type type;
  sw_tl_ref tl_ref;
  sw_tl_extent so_far;
} prog_data_t ;

static  prog_data_t prog_data[N_PROGRESS_DIALS];

void init_C_globals_cmpprog(void)
{
  prog_data[RECOMBINE_PROGRESS].type = SWTLT_RECOMBINE_PAGE;
  prog_data[RECOMBINE_PROGRESS].tl_ref = SW_TL_REF_INVALID;
  prog_data[RECOMBINE_PROGRESS].so_far = 0.0;

  prog_data[PRECONVERT_PROGRESS].type = SWTLT_RENDER_PREPARE;
  prog_data[PRECONVERT_PROGRESS].tl_ref = SW_TL_REF_INVALID;
  prog_data[PRECONVERT_PROGRESS].so_far = 0.0;
}

/**
 * Calculate the effort involved and then open and set up
 * the progress bar.
 */
void openDLProgress(DL_STATE *page, dl_progress_t prog_type)
{
  HDL_LIST* hlist;
  prog_data_t* data = &prog_data[prog_type];
  Bool countRecombine;
  sw_tl_extent total = 0.0 ;

  HQASSERT(prog_type == RECOMBINE_PROGRESS || prog_type == PRECONVERT_PROGRESS,
           "openProgress: unknown progress type");

  data->so_far = 0.0;

  /*
   * Iterate over all active HDLs on the page, and then for each element
   * within each HDL counting up the objects of the appropriate kind
   */
  countRecombine = (prog_type == RECOMBINE_PROGRESS);
  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    DLRANGE dlrange;

    hdlDlrange(hlist->hdl, &dlrange);

    for ( dlrange_start(&dlrange); !dlrange_done(&dlrange);
          dlrange_next(&dlrange) ) {
      LISTOBJECT *lobj = dlrange_lobj(&dlrange);

      if ( countRecombine ) {
        if ( (lobj->spflags & RENDER_RECOMBINE) != 0)
          total += 1.0;
      } else {
        /* count preconvert */
        total += 1.0;

        if ( lobj->opcode == RENDER_image ) {
          IMAGEOBJECT *imageobj = lobj->dldata.image;
          int32 rw = imageobj->imsbbox.x2 - imageobj->imsbbox.x1 + 1 ;
          int32 rh = imageobj->imsbbox.y2 - imageobj->imsbbox.y1 + 1 ;
          sw_tl_extent inc = (sw_tl_extent)rw * (sw_tl_extent)rh;

          total += inc;
        }
      }
    }
  }

  CHECK_TL_VALID((data->tl_ref =
                  timeline_push(&page->timeline, data->type,
                                total /*end*/, SW_TL_UNIT_NONE,
                                page, NULL, 0))) ;
}

/* Close the progress device */
void closeDLProgress(DL_STATE *page, dl_progress_t prog_type)
{
  prog_data_t* data = &prog_data[prog_type];

  data->so_far = 0.0;

  if ( data->tl_ref != SW_TL_REF_INVALID ) {
    /**
     * The progress architecture is not thread safe as it stores one set of
     * information in a static structure that can be shared my many threads.
     * This is generally not a problem as most issues are benign.
     * What can happen is rendering can be closing recombine or render-prepare
     * progress while interpretation is between pages reseting pagedevice.
     * The interpreter loop includes occasional calls to update progress. These
     * will intermingle with the rendering progress and you can get progress
     * calls on a closing or closed device. Testing the timeline ref for not
     * being invalid prevents most of the problems. But you can can an update
     * and a close happening together and triggering a CHECK_TL_VALID assert.
     * Any asserts and other problems can be made highly unlikely by setting
     * the Timeline ref invalid before actually popping the timeline. This
     * means we will be OK unless another thread is swapped in between the test
     * for invalid and the setting invalid. Real solution is to analyse the
     * whole progress architecture and make it totally thread-safe, either by
     * adding a mutex or having a progress structure per DL in flight. Don't
     * want to do that at this late stage, so content myself with reudcing the
     * likelihood of a problem, knowing the worst that seems possible is a
     * very occasional assert.
     *
     * \todo BMJ 21-Sep-11 : Make progress properly thread safe.
     */
    data->tl_ref = SW_TL_REF_INVALID;
    CHECK_TL_VALID(timeline_pop(&page->timeline, data->type, TRUE)) ;
  }
}

/* Update the progress bar, called when gui is tickled, devices.c */
void updateDLProgress(dl_progress_t prog_type)
{
  prog_data_t* data = &prog_data[prog_type];

  if ( data->tl_ref != SW_TL_REF_INVALID )
    CHECK_TL_SUCCESS(SwTimelineSetProgress(data->tl_ref, data->so_far)) ;
}

/* Updates the count of the work done */
void updateDLProgressTotal(double val, dl_progress_t prog_type)
{
  prog_data_t * data = &prog_data[prog_type];

  data->so_far += val;
}

/* Log stripped */
