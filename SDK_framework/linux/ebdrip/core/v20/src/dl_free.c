/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_free.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Free display list objects.
 */

#include "core.h"
#include "swoften.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "dl_free.h"

#include "bitblts.h"
#include "display.h"
#include "dlstate.h"

#include "often.h"
#include "stacks.h"
#include "matrix.h"
#include "dl_cell.h"
#include "dl_image.h"
#include "shading.h"  /* SHADINGinfo */
#include "shadex.h" /* GOURAUDOBJECT */
#include "ndisplay.h"
#include "graphics.h"
#include "gstate.h"
#include "pathops.h"
#include "idlom.h"
#include "render.h"
#include "vnobj.h"    /* VIGNETTEOBJECT */
#include "rcbshfil.h"
#include "dl_bres.h"
#include "rcbtrap.h"

#include "dl_store.h"
#include "hdlPrivate.h"
#include "groupPrivate.h"
#include "trap.h"


/* LISTOBJECT destructor routines. */

static void free_dl_char(LISTOBJECT *killMe, DL_STATE *dl);
static void free_dl_fill (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_image (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_vignette (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_gouraud (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_shfill (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_shfill_patch (LISTOBJECT* killMe, DL_STATE *dl);
static void free_dl_hdl(LISTOBJECT* killMe, DL_STATE *page);
static void free_dl_group(LISTOBJECT* killMe, DL_STATE *page);
static void free_dl_backdrop(LISTOBJECT* killMe, DL_STATE *page);
static void free_dl_cell(LISTOBJECT* killMe, DL_STATE *page);

typedef void (*DL_DESTRUCTOR) (LISTOBJECT* killMe, DL_STATE *dl);

DL_DESTRUCTOR destructorFuncs[N_RENDER_OPCODES] = {
  free_listobject,      /* RENDER_void */
  free_listobject,      /* RENDER_erase */
  free_dl_char,         /* RENDER_char */
  free_listobject,      /* RENDER_rect */
  free_listobject,      /* RENDER_quad */
  free_dl_fill,         /* RENDER_fill */
  free_dl_image,        /* RENDER_mask */
  free_dl_image,        /* RENDER_image */
  free_dl_vignette,     /* RENDER_vignette */
  free_dl_gouraud,      /* RENDER_gouraud */
  free_dl_shfill,       /* RENDER_shfill */
  free_dl_shfill_patch, /* RENDER_shfill_patch */
  free_dl_hdl,          /* RENDER_hdl */
  free_dl_group,        /* RENDER_group */
  free_dl_backdrop,     /* RENDER_backdrop */
  free_dl_cell          /* RENDER_cell */
};

/************ Destructors for object on the displaylist... ***************/

void free_dl_object( LISTOBJECT* killMe, DL_STATE *page)
{
  uint8 basefunc;
  basefunc = killMe->opcode;

  /*
   * If DL object has been serialised to disk, then its data has already
   * been freed and replaced with a disk offset. So just need to free
   * listobject itself in that case.
   */
  if ( (killMe->marker & MARKER_ONDISK) )
    free_listobject(killMe , page);
  else
    destructorFuncs[ basefunc ]( killMe , page ) ;
}

static void free_dl_char(LISTOBJECT *killMe, DL_STATE *page)
{
  DL_CHARS *text = killMe->dldata.text;

  if ( text ) {
    int32 size = sizeof(DL_CHARS) + (text->nalloc-1)*sizeof(DL_GLYPH);

    dl_free(page->dlpools, text, size, MM_ALLOC_CLASS_CHAR_OBJECT);
  }
  free_listobject(killMe, page);
}

/* free_fill frees a NFILLOBJECT and all the subobjects which dangle
 * from it.
 */
void free_fill( NFILLOBJECT *nfill , DL_STATE *page )
{
  free_nfill(nfill, page->dlpools);
}

/* free_dl_fill frees a fill LISTOBJECT, and the NFILLOBJECT and subobjects
 * which dangle from it.
 */
static void free_dl_fill( LISTOBJECT *killMe, DL_STATE *page )
{
  /* fills and clips (which are just modified fills) have NFILLOBJECT*
   * stored in the dl object, which is a complex structure allocated as a
   * contiguous chunk:
   * (1) the NFILLOBJECT,
   * (2) oodles of NBRESS threads, each of
   *     (a) NBRESS object
   *     (b) FLWORD FLWORD FLWORD... ((FLWORD)0)
   * (3) and an array of nothreads NBRESS*'s
   */
  NFILLOBJECT *nfill = killMe->dldata.nfill;
  HQASSERT( nfill, "no NFILLOBJECT in LISTOBJECT?" ) ;
  free_fill( nfill , page ) ;
  free_listobject( killMe, page ) ;
}

/* free_dl_image frees an image LISTOBJECT, and the image structures which
 * dangles from it.
 */
static void free_dl_image( LISTOBJECT* killMe , DL_STATE *page )
{
  IMAGEOBJECT *imageobj = killMe->dldata.image;

  HQASSERT( imageobj , "no image to free, but image DL object exists?!" ) ;
  im_freeobject( imageobj , page ) ;
  free_listobject( killMe, page ) ;
}

/* free_dl_vignette destroys a vignette LISTOBJECT and it's associated chain
 * of sub-objects.
 */
static void free_dl_vignette(LISTOBJECT *killMe, DL_STATE *page)
{
  VIGNETTEOBJECT *vigobj;
  vn_outlines_t *outlines;

  vigobj = killMe->dldata.vignette;
  outlines = &vigobj->outlines;
  hdlDestroy(&killMe->dldata.vignette->vhdl);

  if ( outlines->nfillb && outlines->freenfillb )
    free_fill(outlines->nfillb, page);

  if ( outlines->nfillr && outlines->freenfillr )
    free_fill(outlines->nfillr, page);

  if ( outlines->nfills )
    free_fill(outlines->nfills, page);

  if ( outlines->nfillh && outlines->freenfillh )
    free_fill(outlines->nfillh, page);

  if ( outlines->nfillt && outlines->freenfillt )
    free_fill(outlines->nfillt, page);

  if ( vigobj->partialcolors )
    dl_release(page->dlc_context, &vigobj->partialcolors);

  if ( vigobj->white.lobj )
    free_dl_object(vigobj->white.lobj, page);

#if defined( DEBUG_BUILD )
  if ( outlines->outline_lobj )
    free_dl_object(outlines->outline_lobj, page);
#endif

  free_listobject(killMe, page);

  dl_free(page->dlpools, vigobj , sizeof(VIGNETTEOBJECT),
          MM_ALLOC_CLASS_VIGNETTEOBJECT);
}

/* free_dl_shfill destroys a shfill LISTOBJECT and it's associated chain
 * of sub-objects.
 */
static void free_dl_shfill(LISTOBJECT* killMe, DL_STATE *page)
{
  SHADINGOBJECT *sobj = killMe->dldata.shade;
  SHADINGinfo *sinfo = sobj->info;

  hdlDestroy(&killMe->dldata.shade->hdl);

  if ( sinfo ) {
    if ( sinfo->rfuncs ) {
      int32 i ;

      for ( i = 0 ; i < sinfo->nfuncs ; ++i )
        rcbs_fn_free(&sinfo->rfuncs[i], page);

      dl_free(page->dlpools, sinfo->rfuncs,
              RCBS_FUNC_HARRAY_SPACE(sinfo->nfuncs), MM_ALLOC_CLASS_SHADING);
    }
    dl_free(page->dlpools, sinfo->base_addr, sizeof(SHADINGinfo) +
            sizeof(OMATRIX) + sizeof(dbbox_t) + 4,  MM_ALLOC_CLASS_SHADING);
  }
  if ( sobj ) {
    size_t workSize = 0;

    if ( sobj->colorWorkspace_base ) {
      workSize = gouraud_dda_size(sobj->nchannels) + 7;
      dl_free(page->dlpools, sobj->colorWorkspace_base, workSize,
              MM_ALLOC_CLASS_SHADING);
    }
    dl_free(page->dlpools, sobj, sizeof(SHADINGOBJECT), MM_ALLOC_CLASS_SHADING);
  }
  free_listobject( killMe, page ) ;
}

/* free_dl_gouraud destroys a gouraud-shaded triangle LISTOBJECT and its
   associated GOURAUDOBJECT.
 */
static void free_dl_gouraud(LISTOBJECT* killMe, DL_STATE *page)
{
  GOURAUDOBJECT *gour = killMe->dldata.gouraud;

  if ( gour )
    dl_free(page->dlpools, gour->base, gour->gsize, MM_ALLOC_CLASS_GOURAUD);

  free_listobject(killMe, page);
}

/* free_dl_shfill_patch destroys a recombine shaded fill patch and its
   associated sub-objects.
 */
static void free_dl_shfill_patch(LISTOBJECT* killMe, DL_STATE *page)
{
  rcbs_free_patch(&(killMe->dldata.patch), page);
  free_listobject(killMe, page);
}

/* Destroy an HDL */
static void free_dl_hdl(LISTOBJECT* killMe, DL_STATE *page)
{
  HQASSERT(killMe != NULL, "free_dl_hdl - 'killMe' cannot be NULL");

  /* Destroy HDL if it exists */
  hdlDestroy(&killMe->dldata.hdl);

  free_listobject(killMe, page);
}

/* Destroy a Group */
static void free_dl_group(LISTOBJECT* killMe, DL_STATE *page)
{
  HQASSERT(killMe != NULL, "free_dl_group - 'killMe' cannot be NULL");

  /* Destroy Group if it exists */
  groupDestroy(&killMe->dldata.group);

  free_listobject(killMe, page);
}

/* free_dl_cell frees the a compressed cell */
static void free_dl_cell( LISTOBJECT* killMe , DL_STATE *page )
{
  HQASSERT_LPTR( killMe ) ;

  cellfree( killMe->dldata.cell ) ;

  free_listobject( killMe , page ) ;
}

/**
 * Backdrop DL objects are a special case in that they are not added to the DL.
 * After a group has been composited a backdrop DL object wrapper is allocated
 * off the C stack and then rendered immediately.
 */
static void free_dl_backdrop( LISTOBJECT* killMe , DL_STATE *page )
{
  UNUSED_PARAM(LISTOBJECT*, killMe);
  UNUSED_PARAM(DL_STATE*, page);
  HQFAIL("Should not be any backdrop DL objects on the DL");
}

/* end of file dl_free.c */

/* Log stripped */
