/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!export:tiffmem.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for Tiff memory control
 */


#ifndef __TIFFMEM_H__
#define __TIFFMEM_H__ (1)

#include "mm.h"         /* mm_pool_t */
#include "objecth.h"    /* OBJECT*/

Bool tiff_alloc_psarray(
  mm_pool_t   mm_pool,      /* I */
  uint32      size,         /* I */
  OBJECT*     oarray);      /* O */

void tiff_free_psarray(
  mm_pool_t   mm_pool,      /* I */
  OBJECT*     oarray);      /* O */

Bool tiff_alloc_psdict(
  mm_pool_t   mm_pool,      /* I */
  uint32      size,         /* I */
  OBJECT*     odict);       /* O */

void tiff_free_psdict(
  mm_pool_t   mm_pool,      /* I */
  OBJECT*     odict);       /* O */

Bool tiff_insert_hash(/*@notnull@*/ /*@in@*/ register OBJECT *thed,
                      int32 namenum,
                      /*@notnull@*/ /*@in@*/ OBJECT *theo) ;

/** Allocate storage for an ICC profile, for use during filter info
    callbacks. This is currently stored as an array of strings, but may
    change to use a blob. */
Bool tiff_alloc_icc_profile(mm_pool_t pool, OBJECT *icc_profile, size_t size) ;

/** Free storage for an ICC profile, if allocated. This is currently stored
    as an array of strings, but may change to use a blob. */
void tiff_free_icc_profile(mm_pool_t pool, OBJECT *icc_profile) ;

#endif /* !__TIFFMEM_H__ */


/* Log stripped */
