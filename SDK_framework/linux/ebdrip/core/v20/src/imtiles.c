/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:imtiles.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to create image tiles for rotated image rendering.
 */

#include "core.h"
#include "imtiles.h"

#include "monitor.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "formOps.h"
#include "control.h"
#include "dlstate.h"
#include "render.h"
#include "gstate.h"
#include "rlecache.h"
#include "devops.h"
#include "params.h"
#include "images.h"
#include "display.h"  /* dlBackdropRender */
#include "routedev.h"
#include "bitblts.h" /* blit_chain_t */
#include "bitblth.h"
#include "blttables.h"
#include "diamondfill.h"
#include "metrics.h"
#include "jobmetrics.h"


static TILE *tileCache[TILECACHESIZE] = { NULL } ;

/* -------------------------------------------------------------------------- */

void init_C_globals_imtiles(void)
{
  HqMemSetPtr(tileCache, NULL, TILECACHESIZE) ;
}

static void imt_free_tile(TILE *tile)
{
  FORM *form = theITileForm(tile) ;

  destroy_Form(form) ;
  mm_free(mm_pool_temp, (mm_addr_t) tile, sizeof( TILE ) ) ;
}

/* -------------------------------------------------------------------------- */
/** Make a tile for a rotated image and fill it with a diamond pattern.
 * Arguments are the matrix values defining the tile rounded to integers.
 * Tiles come out of display list memory so that they are reclaimed after
 * rendering the page.
 */
static Bool imt_make_tile(DL_STATE *page, IMAGETILES *tiles, int32 area_sign,
                          int32 d00, int32 d01, int32 d10, int32 d11,
                          int32 maximagepixel)
{                               /* allocate form and bitmap */
  int32 w, h, tx, ty, cross ;
  TILE *this_tile ;
  FORM *tileform ;
  int32 index = (int32)TILE_HASH(d00, d01, d10, d11) ;
  render_state_t tile_rs ;

  for ( this_tile = tileCache[index] ;
        this_tile ;
        this_tile = this_tile->next )
    if ( d00 == theITileDxMin(this_tile) && d01 == theITileDyMin(this_tile) &&
         d10 == theITileDxMaj(this_tile) && d11 == theITileDyMaj(this_tile) &&
         area_sign == this_tile->area_sign )
      goto RETURN_THIS_TILE ;

  w = abs(d00) + abs(d10) + 1 ; /* width of tile */
  h = abs(d11) + abs(d01) + 1 ; /* height of tile */

  this_tile = ( TILE * )mm_alloc( mm_pool_temp,
                                  sizeof( TILE ),
                                  MM_ALLOC_CLASS_IMAGE_TILE ) ;
  if ( this_tile == NULL )
    return FALSE ;

  {
    dcoord ta, tb ;
    ta = min(d00, d10) ;
    tb = min(0, d00 + d10) ;
    tx = min(ta, tb) ;

    ta = min(d01, d11) ;
    tb = min(0, d01 + d11) ;
    ty = min(ta, tb) ;
  }

  theITileDx( this_tile ) = tx ;
  theITileDy( this_tile ) = ty ;
  this_tile->area_sign = (int8)area_sign ;
  theITileDxMin(this_tile) = d00 ;
  theITileDyMin(this_tile) = d01 ;
  theITileDxMaj(this_tile) = d10 ;
  theITileDyMaj(this_tile) = d11 ;
  this_tile->pageno = -1 ;  /* set so that it can be purged immediately */
#ifdef METRICS_BUILD
  theITileUsage(this_tile) = 0 ;
#endif

  /* If the signed area of the triangle formed by the major and minor axes is
     negated relative to the floating point values of the axes, then the tile
     should be left blank because it would overpaint the same pixels. If the
     signed area of the triangle is zero, then the tile is degenerate. */
  cross = d00 * d11 - d10 * d01 ;
  cross = SIGN32(cross) ;
  if ( cross != area_sign ) {
    tileform = MAKE_BLANK_FORM() ;
    if ( tileform == NULL ) {
      mm_free(mm_pool_temp, (mm_addr_t) this_tile, sizeof(TILE)) ;
      return FALSE ;
    }
  } else if ( w > BLIT_WIDTH_BITS || !rendering_prefers_bitmaps(page) ) {
    /* If we're outputting wide tiles or to a surface where RLE is more
       efficient, generate an RLE tile */

    /* MUST call finish_rle_render if this succeeds */
    if ( !setup_rle_render(&tile_rs, 4, w, h, w / 2) ) {
      mm_free(mm_pool_temp, (mm_addr_t) this_tile, sizeof( TILE )) ;
      return FALSE ;
    }

    diamond_fill(&tile_rs.ri.rb, -tx, -ty, d00, d01, d10, d11) ;

    if ( (tileform = finish_rle_render()) == NULL ) {
      mm_free(mm_pool_temp, (mm_addr_t) this_tile, sizeof( TILE )) ;
      return FALSE ;
    }
  } else {      /* Normal bitmap case */
    blit_t *data ;
    blit_chain_t tile_blits ;
    render_forms_t tile_forms ;

    tileform = make_Form(w, h) ;
    if ( tileform == NULL ) {
      mm_free(mm_pool_temp, (mm_addr_t) this_tile, sizeof( TILE )) ;
      return FALSE ;
    }

    render_state_mask(&tile_rs, &tile_blits, &tile_forms, &mask_bitmap_surface,
                      tileform) ;

    /* don't need to set block, because this is only used for generating
       rotated image tiles, which always use bressfills */

    diamond_fill(&tile_rs.ri.rb, -tx, -ty, d00, d01, d10, d11) ;

    /* Cut off blank rows at bottom; can do this with one test, because tiles
       larger than one word wide are stored as RLE. On the whole, we won't
       need to cut off more than one row at the top or bottom, which is
       why this is written as simple loops. */
    HQASSERT(sizeof(*data) == theFormL(*tileform),
             "Unexpected bitmap tile width in make_Tile") ;

    for ( data = theFormA(*tileform) + h ; h > 0 && *--data == 0 ; h-- ) ;

    /* Cut off blank rows at top; need to adjust offset and form pointer */
    for ( data = theFormA(*tileform) ; h > 0 && *data == 0 ; data++, h--) ;

    theITileDy(this_tile) += CAST_PTRDIFFT_TO_INT32(data - theFormA(*tileform)) ;
    theFormH(*tileform) = h ;
    theFormA(*tileform) = data ;
  }

  if ( theFormS(*tileform) > maximagepixel ) {
    destroy_Form(tileform) ;
    mm_free(mm_pool_temp, (mm_addr_t) this_tile, sizeof( TILE )) ;

    return FALSE ;
  }

  theITileForm(this_tile) = tileform ;

  this_tile->next = tileCache[index] ;
  tileCache[index] = this_tile ;

#ifdef METRICS_BUILD
  dl_metrics()->imagetiles.tiles_cached++ ;
  dl_metrics()->imagetiles.tile_memory += sizeof(TILE) + theFormS(*tileform) ;
#endif

 RETURN_THIS_TILE:
  IMAGE_TILE(*tiles, d00, d01, d10, d11) = this_tile ;

  return TRUE ;
}

/** Generate tile forms for fast image rotation. */
IMAGETILES *im_generatetiles(DL_STATE *page,
                             const im_transform_t *geometry)
{
  IMAGETILES *tiles ;

#ifdef METRICS_BUILD
  dl_metrics()->imagetiles.possible_shapes += 16 ;
#endif

  tiles = (IMAGETILES *)dl_alloc(page->dlpools, sizeof(IMAGETILES),
                                 MM_ALLOC_CLASS_IMAGE_TILETABLE);
  if ( tiles != NULL ) {
    TILE *tile ;
    dcoord d00, d01, d10, d11 ;  /* tile deltas */
    int32 c00, c01, c10, c11 ;  /* counters */
    int32 i, pageno ;
    int32 maximagepixel = get_core_context_interp()->systemparams->MaxImagePixel;

    /* Clear tile pointers so that we can unwind easily if we fail. It's
       easier this way, given that the tile indexes are based on matrix
       roundings */
    for ( i = 0 ; i < MAX_IMAGE_TILES ; i++ )
      INDEXED_TILE(*tiles, i) = NULL ;

    /** \todo ajcd 2014-03-24: We could make the tiles into a genuine cache,
        by allocating the minimum we need (those that will appear in a single
        row, i.e. 4), and re-computing the diamond_fill for the tile as
        needed. */
    d00 = (int32)(geometry->wx / geometry->w), c00 = 0 ;
    do {
      d01 = (int32)(geometry->wy / geometry->w), c01 = 0 ;
      do {
        d10 = (int32)(geometry->hx / geometry->h), c10 = 0 ;
        do {
          d11 = (int32)(geometry->hy / geometry->h), c11 = 0 ;
          do {
            if ( !imt_make_tile(page, tiles, geometry->cross_sign,
                                d00, d01, d10, d11, maximagepixel) ) {
              /* Use outputpage eraseno to ensure all tiles required for the
                 pipeline are preserved. */
              int32 erasenumber = outputpage_lock()->eraseno ;
              outputpage_unlock() ;

              /* Dispose of the partial set of tiles (they still have
                 tile->pageno = -1) */
              purge_tcache(erasenumber);

              dl_free(page->dlpools, (mm_addr_t)tiles,
                      sizeof(IMAGETILES), MM_ALLOC_CLASS_IMAGE_TILETABLE);

              tiles = NULL ;        /* Return failure */

              goto CLEANUP_AND_RETURN ;
            }
            d11 += SIGN32(geometry->hy), c11++ ;
          } while ( c11 < 2 && geometry->hy % geometry->h != 0 ) ;
          d10 += SIGN32(geometry->hx), c10++ ;
        } while ( c10 < 2 && geometry->hx % geometry->h != 0 ) ;
        d01 += SIGN32(geometry->wy), c01++ ;
      } while ( c01 < 2 && geometry->wy % geometry->w != 0 ) ;
      d00 += SIGN32(geometry->wx), c00++ ;
    } while ( c00 < 2 && geometry->wx % geometry->w != 0 ) ;

    /* Now we've got all of the tiles, mark them as used on this page,
       and do any statistics gathering desired  */
    pageno = page->eraseno ;
    for ( i = 0 ; i < MAX_IMAGE_TILES ; i++ )   /* set page numbers */
      if ( (tile = INDEXED_TILE(*tiles, i)) != NULL ) {
        tile->pageno = pageno ;
#ifdef METRICS_BUILD
        theITileUsage(tile)++ ;
        dl_metrics()->imagetiles.actual_shapes++ ;
#endif
      }

 CLEANUP_AND_RETURN:
    EMPTY_STATEMENT();
  }

  return tiles ;
}

/** Low memory handler to purge tiles from the tile cache. */
void purge_tcache(int32 erasenumber)
{
  int32 i ;

  /* Clear tile cache; this can be done in the front end, because the tile
     cache is only used to look up tiles during interpretation. During
     rendering the image structures contain pointers directly to the tiles
     used */
  for (i = 0 ; i < TILECACHESIZE; i++ ) {
    TILE **tprev = & tileCache[i] ;
    TILE *tile ;

    while ( (tile = *tprev) != NULL ) {
      if ( tile->pageno < erasenumber ) {
        *tprev = tile->next ;   /* remove tile from chain */
        sw_metric_histogram_count(&dl_metrics()->imagetiles.times_used.info,
                                  theITileUsage(tile), 1) ;
        imt_free_tile( tile ) ; /* dispose of tile memory */
      } else
        tprev = & tile->next ;
    }
  }
}


/* Log stripped */
