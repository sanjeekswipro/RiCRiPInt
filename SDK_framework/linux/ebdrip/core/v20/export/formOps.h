/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!export:formOps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * FORM operations.
 */
#ifndef __formOps_h___
#define __formOps_h__

#include "bitbltt.h"
#include "mm.h"

/** \brief Create a blank form, with no data. */
#define MAKE_BLANK_FORM() make_RLEForm(0, 0, 0, FORMTYPE_BLANK)

/** \brief Create a bitmap form of the specified size.

    \param w The width of the form to create.
    \param h The height of the form to create.
    \return A pointer to the allocated form, or NULL if it could not be
      allocated.
*/
FORM *make_Form(int32 w, int32 h);

/** \brief Create an RLE form of the specified size.

    \param w The width of the form to create.
    \param h The height of the form to create.
    \param tbytes The number of bytes for the RLE data.
    \param ftype The form's sub-type. This is used to set the number of
      nibbles used to encode runs.
    \return A pointer to the allocated form, or NULL if it could not be
      allocated.
*/
FORM *make_RLEForm(int32 w, int32 h, int32 tbytes, int32 ftype);

/** \brief Free the memory for a bitmap or RLE form.
    \param this_form The form to free.
 */
void destroy_Form(FORM *this_form);

Bool formarray_new(mm_pool_t *pools, dbbox_t *bbox, int32 rh,
                   form_array_t **p_formarray) ;

void formarray_destroy(form_array_t **p_formarray, mm_pool_t *pools);

void formarray_findform(form_array_t *formarray, dcoord y,
                        FORM **form, dcoord *yform) ;

Bool formarray_newform(FORM *form, mm_pool_t *pools,
                       int32 type, int32 linebytes) ;

void formarray_destroyform(FORM *form, mm_pool_t *pools);

/**
 * Produce an inverted version of the passed form. Only the region within the
 * minimum bounding box of content in the source form will be inverted; thus an
 * OFFSETFORM is returned, the offset accounting for region trimmed from the
 * top-left of the source form.
 *
 * \param empty Set to TRUE if the 'source' form is empty.
 * \return The inverted form, or NULL on error or if the passed 'source' is
 *         empty.
 */
OFFSETFORM* formInvert(FORM* source, Bool* empty);

/**
 * Constructor.
 */
OFFSETFORM* offsetFormNew(int32 width, int32 height);

/**
 * Destructor.
 */
void offsetFormDelete(OFFSETFORM* self);

#endif

/* Log stripped */

