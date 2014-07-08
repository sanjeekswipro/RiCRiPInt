/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:imtiles.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rotated image pixel tiles API
 */

#ifndef __IMTILES_H__
#define __IMTILES_H__

#include "dlstate.h"  /* DL_STATE */
#include "matrix.h"
#include "bitbltt.h" /* FORM */


#define TILES_IN_ROW            4       /* caused by rounding of edge deltas */
#define TILES_IN_COL            4
#define MAX_IMAGE_TILES         ((TILES_IN_ROW)*(TILES_IN_COL))

#define IMAGE_ROWTILES(tiles, c, d) ( (tiles)[(((d) & 1) << 1) + ((c) & 1)] )
#define ROWTILES_TILE(row, a, b)    ( (row)[(((b) & 1) << 1) + ((a) & 1)] )
#define IMAGE_TILE(tiles, a, b, c, d) \
        ( ROWTILES_TILE(IMAGE_ROWTILES((tiles), (c), (d)), (a), (b)) )
#define INDEXED_TILE(tiles, i) \
        ( (tiles)[(i) / TILES_IN_ROW][(i) % TILES_IN_ROW] )

#define TILECACHESIZE 64
#define TILE_HASH(a,b,c,d) \
        ( (uint32)((a) * 29 + (b) * 17 + (c) * 7 + (d)) % TILECACHESIZE )
#define theITileDxMin(val) ((val)->d00)
#define theITileDyMin(val) ((val)->d01)
#define theITileDxMaj(val) ((val)->d10)
#define theITileDyMaj(val) ((val)->d11)


/** Structure to hold image tiles. */
typedef struct tile {
  struct tile *next ;
  int32 d00, d01, d10, d11 ;    /**< Tile shape. */
  int32 dx ;            /**< Horizontal offset of tile origin in form. */
  int32 dy ;            /**< Vertical offset of tile origin in form. */
  int32 pageno ;        /**< Last page number tile is used on. */
  int8  area_sign ;     /**< The cross-product sign of the image. */
  FORM *tileform ; /**< Tile form pointer. */
#ifdef METRICS_BUILD
#define theITileUsage(val) ((val)->usedcount)
  int32 usedcount ;     /**< Times used. */
#endif
} TILE ;

typedef TILE *ROWTILES[TILES_IN_ROW] ;          /* tiles on one row */
typedef ROWTILES IMAGETILES[TILES_IN_COL] ;     /* tiles in whole image */

#define theITileForm(val) ((val)->tileform)
#define theITileDx(val) ((val)->dx)
#define theITileDy(val) ((val)->dy)

IMAGETILES *im_generatetiles(DL_STATE *page, const im_transform_t *geometry) ;
void purge_tcache(int32 erasenumber);

#endif /* protection for multiple inclusion */

/* Log stripped */
