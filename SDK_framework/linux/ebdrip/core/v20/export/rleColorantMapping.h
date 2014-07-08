/** \file
 * \ingroup rleblt
 *
 * $HopeName: SWv20!export:rleColorantMapping.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interfaces for RLE colorant mapping. Colorants on the display list need to
 * be mapped to the set of colorants output in the PGB (in color RLE we output
 * composite data).
 */

#ifndef __RLECOLORANTMAPPING_H__
#define __RLECOLORANTMAPPING_H__

#include "displayt.h" /* DL_STATE */
#include "gu_chan.h"
#include "mm.h"
#include "rleColorantMappingTypes.h"


/** \brief Page colorant list iterator: this is public only to allow stack
    variables to be passed into the iterator functions - its contents should be
    considered private and not used or changed by clients. */
typedef struct {
  ColorantList *list;
  ColorantEntry **next;
  int32 index;
} ColorantListIterator;

/** \brief Create a new RLE colorant list using a specified memory pool.

    \param[in] pool  The memory pool in which colorant list data is stored.

    \retval A new colorant list reference, or \c NULL if an error was
            encountered. This colorant list must be destroyed using
            \c colorantListDestroy().
*/
ColorantList *colorantListNew(mm_pool_t pool);

/** \brief Add any unique colorants from a raster style to a colorant list.

    \param list  The colorant list to add to.
    \param hf    The initial channel of a sheet.
    \param sheet_only  If \c TRUE, only add colorants for the same sheet. If
                 \c FALSE, add colorants for all sheets in the raster style.

    \retval TRUE All of the colorants were added successfully.
    \retval FALSE An error prevented all colorants from being added.

    \note The colorant list stores references into the rasterstyle. It is the
    caller's responsibility to ensure that the colorant list is
    destroyed before the rasterstyle goes out of scope or is deallocated. */
Bool colorantListAddFromRasterStyle(ColorantList *list, GUCR_CHANNEL *hf,
                                    Bool sheet_only);

/** \brief Note that all device colorants have been added to a colorant list:
    all subsequent colorants added will be group colorants.

    \param list  The colorant list that has no more device colorants.
*/
void colorantListTerminateDeviceColorants(ColorantList* list);

/** \brief Deallocate a colorant list.

    \param selfPointer  A location where a colorant list is stored.

    On exit, the colorant list pointer will be set to NULL. */
void colorantListDestroy(ColorantList** selfPointer);

/** \brief Start an iteration over a colorant list.

    \param list      The colorant list to iterate over.
    \param iterator  An iterator used to track progress. The contents of this
                     structure should be treated as opaque by the caller.
    \param groupColorant  If not \c NULL, this points to a boolean variable that
                     will be set to \c TRUE if the colorant is only used in
                     a transparency group, \c FALSE if the colorant is a device
                     colorant.

    \returns A pointer to the colorant information for the first colorant in
             the list, NULL if there are no colorants in the list.

    The colorant iterator will return NULL when at the end of the colorant
    list. If any colorants are subsequently added to the list,
    \c colorantListGetNext() will return those colorants until exhausted, and
    then NULL. */
const GUCR_COLORANT_INFO* colorantListGetFirst(ColorantList* list,
                                               ColorantListIterator* iterator,
                                               Bool* groupColorant);

/** \brief Continue an iteration over a colorant list.

    \param iterator  An iterator initialised by \c colorantListGetFirst().
                     The contents of this structure should be treated as
                     opaque by the caller.
    \param groupColorant  If not \c NULL, this points to a boolean variable that
                     will be set to \c TRUE if the colorant is only used in
                     a transparency group, \c FALSE if the colorant is a device
                     colorant.

    \returns A pointer to the colorant information for the next colorant in
             the list, NULL if there are no more colorants in the list.

    The colorant iterator will return NULL when at the end of the colorant
    list. If any colorants are subsequently added to the list,
    \c colorantListGetNext() will return those colorants until exhausted, and
    then NULL. */
const GUCR_COLORANT_INFO* colorantListGetNext(ColorantListIterator* iterator,
                                              Bool* groupColorant);

/** \brief Create a new colorant map.

    \param pools  The display list memory pools used to allocate the RLE
                  colorant map.
    \param rs     A colorant map used to populate the map.
    \param colorants  The colorant list to use to populate the map.

    \retval A new RLE colorant map, or \c NULL if an error occurred. The new
            colorant map must be destroyed using \c rleColorantMapDestroy().

    RLE colorant maps store a single mapping from a display list colorant
    index to the ordinal position of the colorant in a colorant list. They
    map from internal colorant numbers to the external values presented in
    the RLE stream. */
RleColorantMap* rleColorantMapNew(mm_pool_t *pools,
                                  GUCR_RASTERSTYLE* rs,
                                  ColorantList* colorants);

/** \brief Destroy a colorant map.

    \param selfPointer  A location where a colorant map is stored.

    On exit, the colorant map pointer will be set to NULL. */
void rleColorantMapDestroy(RleColorantMap** selfPointer);

/** \brief Map a display list colorant index to an external RLE colorant index.

    \param self   An RLE colorant map.
    \param ci     The display list colorant index to map.
    \param result A location to store the index for the mapped colorant. If
                  the input colorant index is a special index, or is not
                  in the original colorant list used to create the map, the
                  index stored will be one higher than the highest external
                  colorant index.

    \retval TRUE  If the colorant was known.
    \retval FALSE If the colorant was unknown. */
Bool rleColorantMapGet(RleColorantMap* self, COLORANTINDEX ci, uint32* result);


#endif


/* Log stripped */

