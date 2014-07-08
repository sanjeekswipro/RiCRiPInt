/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!export:backdrop.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop public interface
 */

#ifndef __BACKDROP_H__
#define __BACKDROP_H__

#include "displayt.h" /* LISTOBJECT, DL_STATE */
#include "mm.h" /* mm_pool_t */

struct CV_COLCVT;
struct COLORINFO;
struct FN_INTERNAL;
struct Group;
struct blit_color_t; /* from CORErender */

typedef struct Backdrop Backdrop;
typedef struct BackdropShared BackdropShared;

/**
 * BackdropShared contains all the information that is common to all backdrops.
 */
Bool bd_sharedNew(DL_STATE *page, uint32 width, uint32 height,
                  uint32 regionHeight, uint32 regionRowsPerBand,
                  Bool multiThreaded, BackdropShared **newShared);

/**
 * Provision resources in preparation for compositing.  bd_resourceUpdate can be
 * called whenever new information is known regarding the resource requirements
 * for compositing, such as when adding a transparent object to the DL.  When
 * all the backdrops have been created bd_resourceUpdate should be called to
 * finalise resource provisioning.  In this case inCompsMax and
 * nonisolatedPresent arguments are unused and the function uses the more
 * accurate information gathered when creating the backdrops.
 */
Bool bd_resourceUpdate(BackdropShared *shared, uint32 inCompsMax,
                       Bool nonisolatedPresent, uint32 maxBackdropDepth);

/**
 * Free all shared info after a partial paint.
 */
void bd_sharedFree(BackdropShared **freeShared);

/**
 * Make a new backdrop.  Converter is used to color convert between blend spaces
 * or to the final device.
 */
Bool bd_backdropNew(BackdropShared *shared, uint32 depth,
                    Backdrop *initialBackdrop, Backdrop *parentBackdrop,
                    struct Group *group, Bool out16,
                    uint32 inComps, uint32 inProcessComps,
                    COLORANTINDEX *inColorants,
                    uint32 outComps, COLORANTINDEX *outColorants,
                    struct CV_COLCVT *converter, LateColorAttrib *pageGroupLCA,
                    Backdrop **newBackdrop);

/**
 * The final composited backdrop is composited against a completely
 * opaque color made from the erase color.
 */
Bool bd_setPageColor(Backdrop *backdrop, uint32 inComps, COLORVALUE *pageColor);

/**
 * bd_backdropFree unlinks the backdrop from the list of backdrops and frees off
 * all bits inside.
 */
void bd_backdropFree(Backdrop **freeBackdrop);

/**
 * Set the initial color and alpha for a softmask from luminosity backdrop.
 * Softmask from luminosity backdrops are defined to be initialised with a fully
 * opaque color.  Also set the transfer function for a softmask (either from
 * luminosity or alpha).
 */
Bool bd_setSoftMask(Backdrop *backdrop, COLORVALUE *initialColor,
                    struct FN_INTERNAL *softMaskTransfer);

/**
 * bd_backdropPrepare ensures there are enough resources to composite this
 * backdrop, at least for the minimum region area.  Specifies backdrop retention
 * required for band or frame/separated output.  For overprinted objects the
 * resulting screen is a combination of foreground and background spots.  This
 * spot merging only applies if we are doing screened output.
 */
enum {
  BD_RETAINNOTHING,
  BD_RETAINBAND,
  BD_RETAINPAGE
};
Bool bd_backdropPrepare(BackdropShared *shared, uint32 retention,
                        Bool mergeSpots);

/**
 * CompositeContext encompasses all the workspace and state required to
 * composite the backdrops.  It is passed into the backdrop compositing
 * functions and one CompositeContext is used per compositing thread.
 */
typedef struct CompositeContext CompositeContext;

/**
 * There may be multiple page backdrops if doing imposition. It's useful to know
 * which of the page backdrops we're currently compositing. This function should
 * be called before doing bd_requestRegions() and then updated when switching
 * page backdrops.
 */
void bd_setCurrentPagebackdrop(CompositeContext *context,
                               Backdrop *pageBackdrop);

/**
 * bd_requestRegions is called to determine if we have enough resources to
 * composite the region area given by bounds.  A large region area may have to
 * be split into separate regions to ensure compositing can be completed.
 */
Bool bd_requestRegions(const BackdropShared *shared, CompositeContext *context,
                       uint32 backdrops, const dbbox_t *bounds);

/**
 * bd_regionRequiresCompositing determines whether the region has already been
 * composited in a previous band, frame or separation.  If it has been
 * composited then obviously we don't need to do it again.  Check for
 * consistency if the arg has been supplied, otherwise assume consistency is
 * required and assert it.
 */
Bool bd_regionRequiresCompositing(const Backdrop *backdrop,
                                  const dbbox_t *bounds,
                                  Bool *consistent);

/**
 * bd_regionInit creates backdrop blocks from resources to cover the region area
 * given by bounds.  The blocks are initialised from the init color (for
 * isolated backdrops) or from the initial backdrop (for non-isolated
 * backdrops).
 */
Bool bd_regionInit(CompositeContext *context,
                   const Backdrop *backdrop, const dbbox_t *bounds);

/**
 * bd_runInfo is called once per object per region and is used to set the
 * properties of the incoming spans/blocks/backdrops.
 */
void bd_runInfo(CompositeContext *context,
                const Backdrop *backdrop,
                const LISTOBJECT *lobj,
                struct blit_color_t *color,
                Bool forceProcessKOs,
                int8 overrideColorType);

/**
 * bd_compositeSpan is the backdrop function underlying the span blit.  It is
 * used to composite the spans from an object into the backdrop.  To make use of
 * the repeated line optimisations in the backdrop it is better to blit in
 * blocks where possible, but this function does attempt to coalese spans back
 * into blocks to get most of the benefit back.  Currently fills and quads
 * are always span blitted only.
 */
void bd_compositeSpan(CompositeContext *context,
                      const Backdrop *backdrop,
                      dcoord x, dcoord y, dcoord runlen,
                      struct blit_color_t *sourceColor);

/**
 * bd_compositeBlock is the backdrop function underlying the block blit.  It is
 * used to composite the blocks from an object into the backdrop.  Block
 * blitting gives a big performance benefit over repeatedly calling the span
 * function because the backdrop optimising repeated lines.
 */
void bd_compositeBlock(CompositeContext *context,
                       const Backdrop *backdrop,
                       dcoord x, dcoord y, dcoord runlen, dcoord rows,
                       struct blit_color_t *sourceColor);

/**
 * bd_compositeBackdrop is used to composite one backdrop into another and is
 * used for nested groups.  It shortcuts the most of the normal rendering
 * process.
 */
void bd_compositeBackdrop(CompositeContext *context,
                          const Backdrop *backdrop, const dbbox_t *bounds);

/**
 * bd_regionComplete converts an area of the backdrop from the blend space to
 * its parent backdrop's blend space or the final device color space.  In the
 * process the backdrop blocks covering the region are compressed and the tables
 * are merged together.  Once a backdrop block has been completed no further
 * compositing in that block may take place.  Block poaching is an optimisation
 * to save work when compositing one backdrop into another.  If the right
 * conditions are met the child block can be swapped with the equivalent block
 * in the parent.
 */
Bool bd_regionComplete(CompositeContext *context, Backdrop *backdrop,
                       Bool canPoach, const dbbox_t *bounds);

/**
 * bd_regionRelease is called on a single backdrop to release the resources.
 * The resources are returned to the global list ready to be picked up to
 * composite the next region for any backdrop.
 */
Bool bd_regionRelease(const Backdrop *backdrop, const dbbox_t *bounds);

/**
 * bd_regionReleaseAll returns all the resources to the global list that have
 * been used for this region, for all the backdrops.
 */
Bool bd_regionReleaseAll(CompositeContext *context, const BackdropShared *shared,
                         const dbbox_t *bounds, Bool result);

/**
 * Resets backdrop for the next band for this context.
 */
void bd_bandInit(CompositeContext *context);

/**
 * Release all the backdrop blocks for this band if band retaining.
 * Also, if result is false, the whole band is cleared out to avoid region set
 * inconsistencies if compositing is retried (and this applies to all output
 * styles).
 */
Bool bd_bandRelease(CompositeContext *context, const BackdropShared *shared,
                    Bool canKeep);

/**
 * Returns the output set of colorants in the backdrop.  nComps is optional.
 */
COLORANTINDEX *bd_backdropColorants(const Backdrop *backdrop, uint32 *nComps);

/**
 * Specifies an area for reading which must be a subset of the current region
 * set.
 */
void bd_readerInit(CompositeContext *context, const dbbox_t *readBounds);

/**
 * bd_readerNext iterates over backdrop blocks in the readBounds supplied to
 * bd_readerInit.
 */
Bool bd_readerNext(CompositeContext *context, const Backdrop *backdrop,
                   Bool *result);

/**
 * bd_blockReader is called repeatedly after bd_readerNext to read the contents
 * of a backdrop block.  It returns a bounded area with color and info, or null
 * indicating the block has been read.
 */
const dbbox_t *bd_blockReader(CompositeContext *context,
                              const Backdrop *backdrop,
                              uint8 **color8, COLORVALUE **color,
                              struct COLORINFO **info);

/**
 * This function is used to iterate over alpha data. See bd_readSoftmaskLine.
 */
typedef Bool (*AlphaSpanIteratorFunc)(void *private, uint16 value, uint32 length);

/**
 * Pass the soft mask data for the passed line, within the passed x-extent, to
 * the provided iterator function one span at a time.
 */
Bool bd_readSoftmaskLine(const Backdrop *backdrop, dcoord y, dcoord x1, dcoord x2,
                         AlphaSpanIteratorFunc callback,
                         void *callbackData);

#endif /* protection for multiple inclusion */

/* Log stripped */
