/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:export:rcbmapping.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine API
 */

#ifndef __RCBMAPPING_H__
#define __RCBMAPPING_H__

/** Build a pseudo-colorant to actual colorant mapping.

    \param[out] mapping  A pointer where the mapping is stored.
    \param[out] map_length  The length of the mapping.
    \param[in] ciCompositeGrayActual The actual colorant index that should
                         be used for objects that were marked as recombined,
                         when a composite page has been detected.

    \retval TRUE if the mapping was created successfully. In this case \c
                 rcbn_mapping_free() must be called to dispose of the mapping.
    \retval FALSE if the mapping was not created, and an error was signalled.
 */
Bool rcbn_mapping_create(COLORANTINDEX **mapping, int32 *map_length,
                         COLORANTINDEX ciCompositeGrayActual) ;

/** Free the pseudo-colorant to actual colorant mapping created by \c
    rcbn_mapping_create.

    \param[out] mapping  A pointer where the mapping created by
                         \c rcbn_mapping_create() is stored. This will be
                         NULL on exit.
    \param[in] map_length  The length of the mapping, as returned by
                         \c rcbn_mapping_create().
*/
void rcbn_mapping_free(COLORANTINDEX **mapping, int32 map_length) ;

#endif /* !__RCBMAPPING_H__ */

/* Log stripped */
