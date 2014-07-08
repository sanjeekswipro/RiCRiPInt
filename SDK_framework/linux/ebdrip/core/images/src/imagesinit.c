/** \file
 * \ingroup images
 *
 * $HopeName: COREimages!src:imagesinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for COREimages compound.
 */

#include "core.h"
#include "swstart.h"
#include "coreinit.h"
#include "imagesinit.h"
#include "imcontext.h"
#include "ccittfax.h"     /* ccitt_* */
#include "fltrdimg.h"     /* filteredimage_* */
#include "jbig2.h"        /* jbig_* */
#include "dct.h"          /* jpeg_* */
#include "jpeg2000.h"     /* jpeg2000_* */
#include "pngfilter.h"    /* png_* */
#include "wmpfilter.h"    /* wmp_* */
#include "tiffexec.h"     /* tiffexec_* */
#include "imstore.h"
#include "imfile.h"       /* im_file_C_globals */
#include "imexpand.h"     /* im_expand_C_globals */

/** \todo ajcd 2009-11-28: Use the versions in the appropriate header file
    when image store et al are moved to COREimages. */
extern void im_block_C_globals(core_init_fns *fns) ;

/** Initialisation table for COREimages, recursively calls image compound
    init functions. */
static core_init_t images_init[] =
{
  CORE_INIT("image store", im_store_C_globals),
  CORE_INIT("image files", im_file_C_globals),
  CORE_INIT("image blocks", im_block_C_globals),
  CORE_INIT("image expand", im_expand_C_globals),
  CORE_INIT("ccitt", ccitt_C_globals),
  CORE_INIT("jbig2", jbig2_C_globals),
  CORE_INIT("jpeg", jpeg_C_globals),
  CORE_INIT("jpeg2000", jpeg2000_C_globals),
  CORE_INIT("png", png_C_globals),
  CORE_INIT("TIFF", tiffexec_C_globals),
  CORE_INIT("HDPhoto", wmp_C_globals),
  CORE_INIT("simple image filtering", filtering_C_globals),
} ;

static im_context_t interpreter_im_context ;

/** Context localiser for the images context. */
static void im_context_specialise(corecontext_t *context,
                                  context_specialise_private *data)
{
  im_context_t im_context = { 0 } ;

  context->im_context = &im_context;
  context_specialise_next(context, data);
}


/** Structure for registering the image context specialiser. */
static context_specialiser_t im_context_specialiser = {
  im_context_specialise, NULL
};

static Bool images_swinit(SWSTART *params)
{
  CoreContext.im_context = &interpreter_im_context ;
  context_specialise_add(&im_context_specialiser);

  return core_swinit_run(images_init, NUM_ARRAY_ITEMS(images_init), params) ;
}

static Bool images_swstart(SWSTART *params)
{
  return core_swstart_run(images_init, NUM_ARRAY_ITEMS(images_init), params) ;
}

static Bool images_postboot(void)
{
  return core_postboot_run(images_init, NUM_ARRAY_ITEMS(images_init)) ;
}

static void images_finish(void)
{
  core_finish_run(images_init, NUM_ARRAY_ITEMS(images_init)) ;
}

void init_C_globals_imagesinit(void)
{
  im_context_t im_context_init = { 0 } ;

  interpreter_im_context = im_context_init ;
  im_context_specialiser.next = NULL ;
}

/** \todo ajcd 2009-11-23: Approximate list of what will go in COREimages.
    Split these out into the right places. */
IMPORT_INIT_C_GLOBALS( imageadj )
IMPORT_INIT_C_GLOBALS( imb32 )
IMPORT_INIT_C_GLOBALS( imstore )

void images_C_globals(core_init_fns *fns)
{
  init_C_globals_imagesinit() ;
  init_C_globals_imageadj() ;
  init_C_globals_imb32() ;
  init_C_globals_imstore() ;

  fns->swinit = images_swinit ;
  fns->swstart = images_swstart ;
  fns->postboot = images_postboot ;
  fns->finish = images_finish ;

  core_C_globals_run(images_init, NUM_ARRAY_ITEMS(images_init)) ;
}

/* Log stripped */
