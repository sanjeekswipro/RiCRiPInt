/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tifcntxt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Creation and usage of Tiff execution contexts
 */


#ifndef __TIFCNTXT_H__
#define __TIFCNTXT_H__

#include "lists.h"      /* DLL_* */
#include "tifreadr.h"   /* tiff_reader_t */

struct cmyk_spot_data;

/*
 * Context ids are non-zero unsigned integers.
 */
typedef uint32  tiff_contextid_t;


/*
 * Yer actual tiff context structure - maintained in a double linked
 * list.
 */

typedef struct tiff_context_t {
  dll_link_t        dll;            /* List links */
  tiff_contextid_t  id;             /* Unique context id */
  tiff_reader_t*    p_reader;       /* The tiff reader */
  OBJECT            ofile_tiff;     /* The tiff source file */
  int32             f_ignore_orientation; /* Default is TRUE */
  int32             f_adjust_ctm;   /* Default is TRUE */
  int32             f_do_setcolorspace; /* Default is TRUE */
  int32             f_install_iccprofile; /* Default is TRUE */
  int32             f_invert_image; /* Default is FALSE */
  int32             f_do_imagemask; /* Default is FALSE */
  int32             f_do_pagesize;  /* Default is TRUE */
  int32             f_ignore_ES0;       /* ignore ES0 data */
  int32             f_ES0_as_ES2;       /* treat ES0 as ES2 data */
  int32             f_no_units_same_as_inch;  /* if a file has no specified units use inch */
  uint32            number_images;  /* Number of images (& IFDs) in the file */
  mm_pool_t         mm_pool;
  USERVALUE         defaultresolution[2];   /* default resolution */
  struct cmyk_spot_data *sp_data;
} tiff_context_t;


/*
 * tiff_init_contexts() initialises any tiff global structures.
 */
Bool tiff_init_contexts(void);

/* tiff_finish_contexts - deinitialization for tiff global structures. */
void tiff_finish_contexts(void);


/*
 * tiff_new_context() creates a new tiff context, associates the given
 * tiff reader with it, and adds it to the global context list.  It sets
 * VMERROR if it is unable to allocate the context.
 * tiff_free_context() first removes the context from the global list,
 * then frees off the tiff reader before freeing off the context.
 */
Bool tiff_new_context(
  corecontext_t       *context,
  tiff_context_t**    pp_new_context);  /* O */

void tiff_free_context(
  tiff_context_t**    pp_context);      /* I */

/*
 * tiff_first_context() returns the first context in the global list, or
 * NULL if the list is empty.
 * tiff_next_context() returns the next context in the global list, or
 * NULL if there are no more in the list.
 */
tiff_context_t* tiff_first_context(void);

tiff_context_t* tiff_next_context(
  tiff_context_t*     p_context);       /* I */


#endif /* !__TIFCNTXT_H__ */


/* Log stripped */
