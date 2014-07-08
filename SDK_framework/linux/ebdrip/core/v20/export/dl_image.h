/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_image.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Images in the Display-list API
 */

#ifndef __DL_IMAGE_H__
#define __DL_IMAGE_H__

#include "displayt.h"       /* DL_STATE */
#include "graphict.h"       /* GS_COLORinfo */
#include "imaget.h"         /* IMAGEARGS */
#include "imageo.h"         /* IMAGEOBJECT */

struct STACK ; /* from COREobjects */
struct OMATRIX; /* from COREobjects */

Bool dl_image_start(DL_STATE *page);
void dl_image_finish(DL_STATE *page);

void im_freeobject(IMAGEOBJECT *imageobj, DL_STATE *page) ;

void im_addimagelist(IMAGEOBJECT **im_list, IMAGEOBJECT *imageobj) ;
void im_removeimagelist(IMAGEOBJECT **im_list, IMAGEOBJECT *imageobj) ;

Bool im_calculatematrix(IMAGEARGS *imageargs, struct OMATRIX *matrix ,
                        Bool islowlevel) ;

Bool dodisplayimage(DL_STATE *page, struct STACK *stack, IMAGEARGS *imageargs) ;
Bool donullimage(DL_STATE *page, struct STACK *stack, IMAGEARGS *imageargs) ;

Bool im_common(DL_STATE *page, struct STACK *stack, IMAGEARGS *imageargs, void *data,
               Bool (*im_begin)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                void *data),
               Bool (*im_end)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                              void *data, int32 abort),
               Bool (*im_data)(int32 data_type,
                               IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                               uint32 lines, void *data),
               void (*im_early_end)(IMAGEARGS *imageargs,
                                    IMAGEDATA *imagedata, void *data),
               void (*im_stripe)(IMAGEARGS *imageargs,
                                 IMAGEDATA *imagedata, void *data),
               void (*im_clip_lines)(IMAGEARGS *imageargs,
                                     IMAGEDATA *imagedata,
                                     void *data, int32 *iy1, int32 *iy2,
                                     int32 *my1, int32 *my2));

/* the following are for use by imageadj.c only, and are not intended to
   be public interfaces. */
Bool im_prepareimageoutput(IMAGEARGS *imageargs ,
                           IMAGEDATA *imagedata,
                           Bool adjusting, Bool filtering);
int32 im_determine_method(int32 imagetype, int32 ncomps, int32 bits_per_comp,
                          GS_COLORinfo *colorInfo, int32 colorType);
Bool im_bitmapsize( IMAGEARGS *imageargs,
                    IMAGEDATA *imagedata );
Bool im_alloc_buffers( IMAGEARGS *imageargs , IMAGEDATA *imagedata );
Bool im_alloc_bufptrs( IMAGEARGS *imageargs , IMAGEDATA *imagedata );
void im_free_bufptrs( IMAGEARGS *imageargs , IMAGEDATA *imagedata ) ;
void im_free_data( IMAGEARGS *imageargs , IMAGEDATA *imagedata ) ;

/** \brief Find the device space bbox containing a given image space bbox.

    \param[in] imageobj  The image object.
    \param[in] ibbox     The image space bbox to convert to device space.
    \param[out] dbbox    The device space bbox output.

    \retval TRUE  The bbox was converted to device space.
    \retval FALSE \a ibbox does not intersect the image.
*/
Bool image_dbbox_covering_ibbox(const IMAGEOBJECT *imageobj,
                                const ibbox_t *ibbox,
                                dbbox_t *dbbox) ;

/** \brief Find the image space bbox covering a given device space bbox.

    \param[in] imageobj  The image object.
    \param[in] dbbox     The device space bbox to convert to image space.
    \param[out] ibbox    The image space bbox output.

    \retval TRUE  The bbox was converted to image space.
    \retval FALSE \a dbbox does not intersect the image.
*/
Bool image_ibbox_covering_dbbox(const IMAGEOBJECT *imageobj,
                                const dbbox_t *dbbox,
                                ibbox_t *ibbox) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
