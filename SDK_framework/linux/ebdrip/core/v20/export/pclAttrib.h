/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:pclAttrib.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display List state structures in support of PCL rendering.
 */
#ifndef _pclAttrib_h_
#define _pclAttrib_h_

#include "pclAttribTypes.h"
#include "displayt.h"
#include "dlstate.h"
#include "displayt.h"
#include "dl_color.h"

#ifdef DEBUG_BUILD
enum {
  DEBUG_PCL_IDIOM_PRINT_BD = 1,       /**< Print why PCL needs backdrop. */
  DEBUG_PCL_IDIOM_EXTENDED_PRINT = 2, /**< Print details of multi-part PCL idioms */
  DEBUG_PCL_SKIP_ROP_BD = 4,          /**< Skip objects with ROPs that require backdrop render. */
  DEBUG_PCL_SKIP_ROP_DIRECT = 8,      /**< Skip objects with ROPs that require direct render. */
  DEBUG_PCL_FORCE_BACKDROP = 16       /**< Disable surface rop support. */
} ;
extern int32 debug_pcl_idiom ;
extern int32 debug_pcl_idiom_control ;
#endif

/**
 * Set the current pattern to a PCLXL pattern.
 * \param angle The angle of rotation for the pattern. Must be multiple of 90.
 * \param scale The scaling aspect of the XL ctm for the pattern.
 * \param origin The origin of the pattern. If null, (0, 0) will be used.
 * \param targetSize The size of the pattern; this may be null or (0,0) if
 *        the default size should be used.
 * \param palette Palette for indexed patterns. The passed palette should be
 *        valid until the patterned object has been added to the display list
 *        (at which point a copy is made if required). This may be NULL, in
 *        which case color patterns are treated as black and white.
 */
void setPclXLPattern(PclXLPattern* pattern, int32 angle, FPOINT *scale,
                     FPOINT* origin, IPOINT* targetSize, PclXLCachedPalette* palette);

/**
 * Obtain a PclAttrib object from the PCL state store.
 */
Bool getPclAttrib(DL_STATE* page, Bool is_image, Bool first_try,
                  PclAttrib** result);

/* DLStore interface methods. */
DlSSEntry* pclAttribCopy(DlSSEntry* entry, mm_pool_t *pools);
void pclAttribDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t pclAttribHash(DlSSEntry* entry);
Bool pclAttribSame(DlSSEntry* entryA, DlSSEntry* entryB);
void pclAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set);

struct BitGrid;

/**
 * Depending on the object's source, destination, texture and ROP, the object
 * may be direct or backdrop rendered. PCL does not update the region map
 * immediately an object is added to the DL. It queues up a number of objects
 * and tries to recognise common idioms that can be direct rendered. At the
 * end of the page the display list is checked to decide how each object is
 * handled.
 */
void pclUpdateRegionMap(DL_STATE *page, DLREF *dlref,
                        struct BitGrid *destinationRegions,
                        struct BitGrid *backdropRegions);


/** Add an object to the PCL idiom queue. */
void pclIdiomAdd(DL_STATE *page, DLREF *dlref) ;

/** Flush the PCL idiom queue. */
void pclIdiomFlush(DL_STATE *page) ;

/**
 * Return true if the ROP requires the destination
 * color.
 */
Bool pclROPRequiresDestination(uint8 rop) ;

/**
 * Return true if the ROP requires the source color.
 */
Bool pclROPRequiresSource(uint8 rop) ;

/**
 * Return true if the ROP requires the pattern color.
 */
Bool pclROPRequiresTexture(uint8 rop) ;

Bool foregroundIsBlack(const struct DL_STATE *page, const PclAttrib* self) ;
Bool foregroundIsWhite(const struct DL_STATE *page, const PclAttrib* self) ;

/**
 * Read a row of pattern data from the specified pattern. Pattern data is
 * addressed in terms of device coordinates; the relevant area of the source
 * pattern will be obtained and tiled as appropriate.
 *
 * \param x The x coordinate (in device pixels) of the start of the row.
 * \param y The y coordinate (in device pixels) of the start of the row.
 * \param runLength The length of the row, in pixels. This should not exceed the
 *        maximum size passed to pclDLPatternIteratorNew().
 */
void pclDLPatternIteratorStart(PclDLPatternIterator* self,
                               PclDLPattern* pattern,
                               int32 x, int32 y, int32 runLength);

void pclDLPatternIteratorNext(PclDLPatternIterator *iter, int32 runLength);

/**
 * Don't allow partial painting if we're in the middle of a potential
 * PCL idiom (a sequence of ropped objects that can be manipulated to
 * avoid compositing).
 */
Bool pclPartialPaintSafe(DL_STATE *page);

/** Replace a template DL color with the values from a packed PCL color. */
Bool dlc_from_packed_color(dlc_context_t *dlc_context, dl_color_t *dlc,
                           PclPackedColor packedColor) ;

#endif

/* Log stripped */

