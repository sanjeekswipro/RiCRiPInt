/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:rectops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS rectangle operations
 */

#ifndef __RECTOPS_H__
#define __RECTOPS_H__

#include "objecth.h"  /* OBJECT */
#include "graphict.h" /* RECTANGLE */
#include "matrix.h"   /* OMATRIX */

/* ----- Exported functions ----- */
Bool decode_number_string( OBJECT *stro ,
                           SYSTEMVALUE  **ret_array ,
                           int32 *ret_number ,
                           int32 vector_size ) ;
/* Flags for dorectfill */
typedef enum {
  RECT_NORMAL = 0,       /* Normal rect, with all bells & whistles */
  RECT_NOT_VIGNETTE = 1, /* Don't do vignette detection */
  RECT_NO_SETG = 2,      /* Don't do DEVICE_SETG() */
  RECT_NOT_ERASE = 4,    /* This is not a pseudo-erase */
  RECT_NO_HDLT = 8,      /* Don't do HDLT */
  RECT_NO_PDFOUT = 16    /* Don't do PDF output */
} RECT_OPTIONS ;

Bool dorectfill( int32 number , RECTANGLE *rects , int32 colorType,
                 RECT_OPTIONS options) ;

#define STACK_RECTS     1

/** \brief Get rectangle arguments off the stack for rectclip, rectfill, etc.

  \param stack The stack from which arguments will be removed.
  \param ret_array On entry, contains a pointer to an array of rectangles. On
    exit, contains a pointer to this array or an array allocated in
    mm_pool_temp if there were more rectangles than the input array had space
    for.
  \param ret_number On entry, contains the maximum number of rectangles that
    can be stored in \c ret_array. On exit, contains the number of
    rectangles specified in the arguments.
  \param ret_matrix If non-null, this points to a matrix which will be filled
    in.
  \param got_matrix If non-null, indicates if a matrix was supplied in the
    arguments. NULL indicates that a matrix is not allowed.
  \return TRUE for success, FALSE on failure.

  \note On success, if the \c ret_array pointer is not the same as the
    pointer passed in, the caller MUST free the memory allocated for the
    rectangles. This memory is allocated out of mm_pool_temp, and should be
    freed with mm_free_with_header. */
Bool get_rect_op_args(STACK *stack,
                      RECTANGLE   **ret_array ,
                      int32        *ret_number ,
                      OMATRIX      *ret_matrix  ,
                      Bool         *got_matrix);

#endif /* protection for multiple inclusion */

/* Log stripped */
