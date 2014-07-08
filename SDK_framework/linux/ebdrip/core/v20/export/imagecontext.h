/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:imagecontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image context interface, for generic access to image decoding filters.
 */

#ifndef __IMAGECONTEXT_H__
#define __IMAGECONTEXT_H__

#include "objecth.h"
#include "fileioh.h" /* imagefilter_match_t */

struct IMAGEARGS ; /* from SWv20 */
struct core_init_fns ; /* from SWcore */

/* Utility functions. */

/** \brief Opaque type for image contexts. */
typedef struct ImageContext ImageContext ;

/** \brief C runtime initialisation. */
void imagecontext_C_globals(struct core_init_fns *fns) ;

/** \brief Called to scan imagecontexts when restoring. */
Bool imagecontext_restore(int32 savelevel) ;

/* ==========================================================================
   C interface
   ========================================================================== */
/** \brief Open an image context, creating an image filter.

    \param stream File object with input stream.
    \param formats Name or array of names giving possible types for image.
           NULL or a null object can be used to mean any supported format.
    \param subimage The sub-image index for formats that support multiple
           images per file.
    \param p_context Location to store a reference to the new context.
    \return TRUE on successful completion, FALSE on error. \c p_context can be
            NULL on successful completion if the stream was not an image.
*/
Bool imagecontext_open(
  /*@notnull@*/ /*@in@*/  OBJECT *stream,
  /*@notnull@*/ /*@in@*/  OBJECT *formats,
                          int32 subimage,
  /*@notnull@*/ /*@out@*/ ImageContext **p_context);

/** \brief Get tag data from an open image context through callbacks.

    \param context An open image context.
    \param match A list of tag names and callback functions to call if the
           tags are matched.
    \return TRUE on successful completion, FALSE on error.
 */
Bool imagecontext_info(
  /*@notnull@*/ /*@in@*/ ImageContext *context,
  /*@notnull@*/ /*@in@*/ imagefilter_match_t *match);

/** \brief Fill in an \c IMAGEARGS structure from an open image context.

    \param context An open image context.
    \param args Location to store a reference to a completed image argument
                structure.
    \return TRUE on successful completion, FALSE on error.
*/
Bool imagecontext_args(
  /*@notnull@*/ /*@in@*/  ImageContext *context,
  /*@notnull@*/ /*@out@*/ struct IMAGEARGS **args);

/** \brief Close an open image context.

    \param p_context A reference to an open image context, which will be closed.
    \return TRUE on successful completion, FALSE on error.
 */
Bool imagecontext_close(
  /*@notnull@*/ /*@in@*/ ImageContext** p_context);

/* ==========================================================================
   PostScript interface

   <file> <dict> imagecontextopen <id> <type> true
   <file> <dict> imagecontextopen false

   <id> {proc} [keys] imagecontextinfo

   <id> closeimagecontext

   ========================================================================== */

/*
* Log stripped */

#endif
