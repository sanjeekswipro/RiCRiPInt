/** \file
 * \ingroup fontcache
 *
 * $HopeName: SWv20!export:rlecache.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * RLE character and userpath caching. Note that the RLE used here is NOT the
 * same as the output RLE format.
 */

#ifndef __RLECACHE_H__
#define __RLECACHE_H__

#include "bitbltt.h" /* FORM */
#include "imtiles.h" /* TILE (FORM from bitbltt.h) */

struct render_blit_t ;  /* from SWv20 */
struct render_state_t ; /* from SWv20 */
struct CHARCACHE ; /* from SWv20 */

#define RLE_CHAR_SPAN_GUESS(w) ((uint32)(w) / 6)
#define RLE_UPATH_SPAN_GUESS(w) ((uint32)(w) / 4)
#define RLE_TILE_SPAN_GUESS(w) ((uint32)(w))

/**
 * The structure is used to track state when reading RLE compressed forms.
 * See rlecache_line_read_init().
 */
typedef struct {
  /* Pointer to the start of the next line. */
  uint8* next_line;

  /* Each row of RLE has a vertical extent (the RLE is 2D); this member gives
   * the height of this row. */
  uint8 row_height;

  /* TRUE when all spans in the current line have been read; this may be TRUE
   * immediately for blank lines. */
  Bool line_finished;

  /* Private members. */
  uint8* memory;
  uint8 current;
  Bool first;
  Bool first_pair_on_line;
  int32 nibbles;
} RLECACHE_LINE_READ_STATE;

/**
 * Initialise the passed RLE form reading state. This supports reading of forms
 * with the FORMTYPE_CACHERLE[x] type. Spans are read in pairs using the
 * rlecache_get_span_pair() function.
 *
 * When this is first called for a form, 'memory' should be 'form->addr'.
 * For the next line use 'state->next_line'. It is up to the client to detect
 * when all lines have been read.
 */
void rlecache_line_read_init(RLECACHE_LINE_READ_STATE* state,
                             FORM* form,
                             uint8* memory);

/**
 * Read a pair of span lengths using the passed 'state', storing them in the
 * passed 'pair'. The first span is white, the second is black.
 * 'state->line_finished' will be true when all spans for this line have been
 * read; note that a line may be empty and contain no spans.
 */
void rlecache_get_span_pair(RLECACHE_LINE_READ_STATE* state, int32* pair);


/** \brief Convert a character's form from bitmap to RLE encoded. RLE encoded
    characters may be smaller than bitmap characters.

    \param thechar Character cache entry containing the form to convert to
      RLE encoding.
    \param forcerlesize If this size is non-zero, then character form will be
      encoded to RLE even if it is larger than the previous size.
    \return A pointer to the new form is returned, or NULL if there was either
      an error, or the encoded size was larger than the existing size. If the
      RLE form couldn't be allocated, or the encoded size was larger than the
      existing size, the form's type is set to \c FORMTYPE_CACHEBITMAP.
 */
FORM *form_to_rle(struct CHARCACHE *thechar , int32 forcerlesize);

/** \brief Render an RLE-encoded character at the specified position.
    \param rb Rendering context.
    \param theform RLE encoded form to render.
    \param sx X coordinate at which to render the form.
    \param sy Y coordinate at which to render the form.
 */
void rlechar(struct render_blit_t *rb,
             FORM *theform , dcoord sx , dcoord sy );

/** \brief Prepares a render state to receive spans for a direct
    scan-conversion to RLE characters.

    \param rs The render state to use.
    \param nthreads The number of threads in the NBRESS structure which will
      be scan-converted.
    \param width The width of the FORM in which to render.
    \param height The height of the FORM in which to render.
    \param span An estimate at the average filled span length that will be
      produced by the scan conversion. This is used to set the coding method;
      an incorrect estimate will result in poorer encoding, but should not
      normally cause the encoding to fail.
    \return TRUE on success (in which case \c finish_rle_render() MUST be
      called), FALSE on error.
 */
Bool setup_rle_render(struct render_state_t *rs,
                      int32 nthreads, int32 width, int32 height,
                      uint32 span) ;

/** Finishes an RLE character scan conversion. This MUST be called if \c
    setup_rle_render() was called, regardless of any errors in between.

    \return An RLE or blank form representing the character converted. NULL
    if the RLE character form could not be allocated, or there was an error
    in RLE generation. */
FORM *finish_rle_render(void) ;

/* --- Exported Variables --- */
/* RLE shifts. */
extern int32 grleshift[BLIT_WIDTH_BYTES] ;

#endif /* protection for multiple inclusion */


/* Log stripped */
