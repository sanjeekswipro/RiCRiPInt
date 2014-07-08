/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:imstore.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image storage API
 */

#ifndef __IMSTORE_H__
#define __IMSTORE_H__

#include "swdevice.h"
#include "imaget.h"   /* IMAGEOBJECT */
#include "objecth.h"  /* OBJECT */
#include "displayt.h" /* DL_STATE */
#include "dl_color.h" /* COLORANTINDEX */
#include "mm.h"       /* mm_pool_t */

/** Image store data is indexed by plane, or else can apply to all planes. */
enum {
  IMS_ALLPLANES = -1
} ;

typedef struct IM_STORE IM_STORE;

typedef struct IM_SHARED IM_SHARED;


/**
 * Start and finish the image store module per page.
 */
Bool im_shared_start(DL_STATE *page);
void im_shared_finish(DL_STATE *page);

/**
 * Close all the file handles opened during interpretation with read/write
 * access.  Renderer threads reopen files with read only access.
 */
Bool im_shared_filecloseall(IM_SHARED *im_shared);

/** Preallocate reserves for preconversion of this store. */
Bool im_preconvert_prealloc(IM_STORE *ims);

/** Release the reserves held for rendering in this store. */
void im_store_release_reserves(IM_STORE *ims);

/** Test if this store is holding any reserves. */
Bool im_store_have_reserves(IM_STORE *ims);


struct core_init_fns ; /* from SWcore */
void im_store_C_globals(struct core_init_fns *fns) ;

typedef struct IM_BLOCK IM_BLOCK;
typedef struct IM_BLIST IM_BLIST;

struct IM_COLCVT;

enum /* Bits for flags field of store open */ {
  IMS_XFLIP = 0x1,         /**< Image mirrored in x (left <--> right) */
  IMS_YFLIP = 0x2,         /**< Image mirrored in y (top <--> bottom) */
  IMS_XYSWAP = 0x4,        /**<  Image has x and y axes swapped */
  IMS_DESPERATE = 0x8,     /**< Go to desperate lengths to recycle blocks */
  IMS_RECYCLED = 0x40,     /**< Image has been recycled for image adjustment */
  IMS_DOWNSAMPLED = 0x80,  /**< Has the image been down-sampled */
  IMS_ROWREPEATS_NEAR = 0x100,  /**< Do row repeats on near row matches */
  IMS_ROWREPEATS_2ROWS = 0x200, /**< Max 2 rows for this image */
};

/**
 * Create a new image store object.
 * \param im_shared    per-page shared image data
 * \param ibbox        image-space bbox of store
 * \param nplanes      number of image planes, must be positive
 * \param bpp          number of image bits per pixel, must be positive
 * \param flags        Combination of IMS_ enum flags
 * \return             New image storage object, or NULL on error
 *
 * All data points written to and read from the image store will be inside
 * the image space bbox provided. The \a xflip and \a yflip booleans are
 * informative, for optimising block purging order, the image store will not
 * alter the data it supplies when these are set. The \a xyswap parameter
 * controls how data is written into the store. If set, the X and Y parameters
 * are swapped when writing data into the store, and data is written as
 * columns into the store rather than as rows. Reads from the store are always
 * image-space rows. The image-space bbox is in the final (swapped) coordinate
 * space.
 */
IM_STORE *im_storeopen(IM_SHARED *im_shared, const ibbox_t *imsbbox,
                       int32 nplanes, int32 bpp, uint32 flags);

/**
 * Dispose of the given image store object
 * \param[in,out] ims The image store object
 * \return        Success status
 */
Bool im_storeclose(IM_STORE *ims);

/**
 * Return the image storage nplanes for the give store object
 * \param[in] ims The image store object
 * \return        nplanes
 */
int32 im_storenplanes(const IM_STORE* ims);

/**
 * Pre-allocate the planes and blocks data structure for the given
 * image storeage object, plus the actual block of memory for the image data.
 * \param[in,out] ims     The image store object
 * \param[in]     planei  Plane to pre-allocate, or IMS_ALLPLANES
 * \param[in]     nconv   Number of bytes to pre-alloc, must be positive
 * \return                Success status
 */
Bool im_storeprealloc(IM_STORE *ims, int32 planei, int32 nconv);

/**
 * Copy image data into an an image store
 *
 * Note that the data is always (must be) planar (on input).
 * Normally we only write whole scan-lines into the store, but when writing
 * back colour-converted pixels from doing image adjustment we may start
 * part way into the store.  This is due to the store being trimmed.  The
 * x must always be at the start of a block.
 *
 * Write the given image data to the specified image storage object.
 * \param[in,out] ims     The image store object
 * \param[in]     col     Column to write
 * \param[in]     row     Row to write
 * \param[in]     planei  Plane to write to, or IMS_ALLPLANES
 * \param[in]     buf     Pointer to image data
 * \param[in]     wbytes  Number of bytes to write
 * \return                Success status
 *
 * \note This function is one of the few places that use pre-swapped image
 * coordinates.
 */
Bool im_storewrite(IM_STORE *ims, int32 col, int32 row, int32 planei,
                   uint8 *buf, int32 wbytes);

/**
 * Read the image data from the specified image storage object.
 * \param[in,out] ims     The image store object
 * \param[in]     x       x value, must be >=0
 * \param[in]     y       y value, must be >=0
 * \param[in]     plane   Plane to read from, can't be IMS_ALLPLANES
 * \param[out]    rbuf    Return pointer to read image data here
 * \param[out]    rpixels Return number of pixels read here
 * \return                Success status
 */
Bool im_storeread(IM_STORE *ims, int32 x, int32 y, int32 planei,
                  uint8 **rbuf, int32 *rpixels);

/**
 * Given the row y, return the number of rows which are the same.
 * \param[in,out] ims     The image store object
 * \param[in]     y       y value, must be >= 0
 * \return                Integer in the range [ 1 ysize ]
 */
int32 im_storeread_nrows(IM_STORE *ims, int32 y);

/**
 * Given the row y, return TRUE if the row is the same as y-1
 * \param[in,out] ims     The image store object
 * \param[in]     y       y value, must be >= 0
 * \return                Bool, repeat or not
 */
Bool im_is_row_repeat(IM_STORE *ims, int32 y);

/**
 * Is the given plane contained in the image store object
 * \param[in] ims     The image store object
 * \param[in] plane   The plane being requested
 * \return            TRUE if the plane is contained in the image object
 */
Bool im_storeplaneexists(const IM_STORE *ims, int32 plane);

/**
 * How many bits/pixel are in the given image store object
 * \param[in] ims     The image store object
 * \return            The number of bits/pixel (1,2,4,8,16)
 */
int32 im_storebpp(const IM_STORE *ims);

/**
 * Re-order the planes within the given image object so that they match
 * the specified order.
 * \param[in] ims     The image store object
 * \param[in] current The existing plane order
 * \param[in] order   The required new order
 * \param[in] nplanes The number of entries in the order array
 * \return            Success status
 */
Bool im_storereorder(IM_STORE *ims, const COLORANTINDEX current[],
                     const COLORANTINDEX order[], int32 nplanes);

/**
 * Free the image store memory associated with the given store object
 * \param[in] ims     The image store object
 */
void im_storefree(IM_STORE *ims);

/**
 * Can recycle an image store when image adjusting instead of creating a
 * new store.  When converting an RGB image to CMYK an additional plane
 * is added.
 * \param[in] ims       The image store object
 * \param[in] nplanes   The new number of planes for the image store
 * \param[in] recycled  Is the ims able to be re-used for image adjustment?
 */
Bool im_storerecycle(IM_STORE *ims, int32 nplanes, Bool *recycled);

/**
 * Get image store area, excluding trimmed blocks.
 * \param[in] ims           The image store object
 * \return                  The image space bounding box of the trimmed store.
 */
const ibbox_t *im_storebbox_trimmed(IM_STORE *ims);

/**
 * Get entire image store area.
 * \param[in] ims           The image store object
 * \return                  The total image space bounding box of the store.
 */
const ibbox_t *im_storebbox_original(IM_STORE *ims);

/** Reset flags for image flipping and swapping.
 *
 * The flip/swap flags must be disabled during image adjustment and then
 * put back to their original values before rendering; otherwise
 * their effect will be applied twice with unexpected results.
 *
 * \param[in] ims       The image store to set the flags on.
 * \param[in] flags     Value to set flags to.
 */
void im_storesetflags(IM_STORE *ims, uint32 flags);

/** Retrieve imstore flags
 *
 * The flip/swap flags must be disabled during image adjustment and then
 * put back to their original values before rendering; otherwise
 * their effect will be applied twice with unexpected results.
 *
 * \param[in] ims       The image store to get the flags for.
 * \return              Current imstore flag values.
 */
uint32 im_storegetflags(IM_STORE *ims);

/**
 * Free the image store memory associated with the specified plane
 * \param[in] ims     The image store object
 * \param[in] iplane  The plane to free
 */
void im_planefree(IM_STORE *ims, int32 iplane);

/**
 * Sanity check blist minimum requirements before trying to render.
 * \param[in] im_shared
 */
void im_store_pre_render_assertions(IM_SHARED *im_shared);

/**
 * Work out how many global blists are needed for rendering all images in
 * any band with the addition of the given image.
 * \param[in] lobj The image being added to the display list
 */
void im_addextent(LISTOBJECT *lobj, int32 sizefactdisplaylist,
                  int32 sizefactdisplayband);

/**
 * Merge all the planes from the source image store to list of planes
 * on the destination image store. The two image stores must be compatible.
 * \param[in]  ims_src Input image store
 * \param[out] ims_dst Output image store
 * \return             Success status
 */
Bool im_storemerge(IM_STORE *ims_src, IM_STORE *ims_dst);

/**
 * Merge all the planes from the source image store to list of planes
 * on the destination image store. The two image stores must be compatible.
 * \param[in] ims       The image store object
 * \param[in] band      Current band
 * \param[in] nullblock Boolean telling us if the current block is uninit'd
 */
void im_storerelease(IM_STORE *ims, int32 band, int32 nullblock);

/**
 * Throw away any bits of the image store outside of the bounding box, if
 * possible. Parts of the image may be retained for alignment with block
 * boundaries.
 * \param[in] ims       The image store object.
 * \param[in] ibbox     The bounding box to retain.
 */
void im_storetrim(IM_STORE *ims, const ibbox_t *ibbox);

/**
 * For multi-process image processing, decrement reference count
 * on image block and allow other process to claim block if now unused
 */
void im_storereadrelease(corecontext_t *context);

/**
 * Determines if the store is uniform in color (each pixel in a plane is the
 * same).
 *
 * \return TRUE iff the store is completely uniform
 */
Bool im_store_is_uniform(IM_STORE *ims, int32 iplane, uint16 *uniformcolor);

/**
 * Iterates over the store returning a bbox of an area and whether the area
 * is uniform or not.
 */
Bool im_store_uniformbox_iterator(IM_STORE *ims, int *icount, ibbox_t *ibox,
                                  Bool *uniform, uint16 *color) ;

/* ---------------------------------------------------------------------- */
#endif /* protection for multiple inclusion */


/* Log stripped */
