/** \file
 * \ingroup fontcache
 *
 * $HopeName: SWv20!src:formOps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * FORM operations.
 */

#include "core.h"
#include "formOps.h"

#include "bitbltt.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "display.h"
#include "rlecache.h"
#include "swerrors.h"
#include "render.h"

/**
 * Returns true if 'wordCount' words of 'data' are zero.
 */
static Bool rowEmpty(blit_t* data, int32 wordCount)
{
  int32 i;
  for (i = 0; i < wordCount; i ++) {
    if (data[i] != 0)
      return FALSE;
  }
  return TRUE;
}

/**
 * Returns true if the specified column of bits within 'data' contains only
 * zeros.
 * \param rows Number of rows of data to check.
 * \param stride Number of words between consecutive rows.
 */
static Bool columnEmpty(blit_t* data, int32 column, int32 rows, int32 stride)
{
  blit_t* columnWord;
  int32 y;

  columnWord = BLIT_ADDRESS(data, BLIT_OFFSET(column));
  column = column & BLIT_MASK_BITS;
  for (y = 0; y < rows; y ++) {
    if (((AONE >> column) & *columnWord) != 0) {
      return FALSE;
    }
    columnWord += stride;
  }
  return TRUE;
}

/**
 * Determine the horizontal extent of the black pixels in the line provided by
 * 'reader'. The low and high X values are stored in the passed extent array.
 * \param rows The number of rows the block covers; this is valid even for empty
 *        blocks.
 * \return TRUE if the line was completely empty.
 */
static Bool rleRowBlockExtent(RLECACHE_LINE_READ_STATE* reader,
                              int32* xExtent, int32* rows)
{
  int32 span_lengths[2];
  int32 x = 0;

  *rows = reader->row_height;
  if (reader->line_finished)
    return TRUE;

  xExtent[0] = MAXINT32;
  xExtent[1] = 0;
  while (! reader->line_finished) {
    rlecache_get_span_pair(reader, span_lengths);
    x += span_lengths[0];
    /* We're now at the start of the next black span. */
    if (x < xExtent[0])
      xExtent[0] = x;

    x += span_lengths[1];
    /* We're now at the end of the black span. */
    if (x > xExtent[1])
      xExtent[1] = x;
  }

  return FALSE;
}

/**
 * Bounding box method for FORMTYPE_CACHEBITMAP forms.
 */
static ibbox_t bitmapFormGetBounds(FORM* form)
{
  int32 width, height;
  int32 x, y;
  int32 stride;
  ibbox_t bounds;
  Bool empty;
  blit_t* data;

  bbox_clear(&bounds);

  width = form->w;
  height = form->h;
  stride = form->l >> BLIT_SHIFT_BYTES;

  /* Find bottom (i.e. lowest y) boundary. Note that we assume that the
   * right-edge padding has been zero-initialised. */
  empty = TRUE;
  data = form->addr;
  for (y = 0; empty && y < height; y ++) {
    empty = rowEmpty(data, stride);
    data += stride;
  }
  if (empty) {
    /* The form is empty. */
    return bounds;
  }
  bounds.y1 = y - 1;

  /* Find the top. */
  empty = TRUE;
  data = form->addr + stride * (height - 1);
  for (y = height - 1; empty && y >= 0; y --) {
    empty = rowEmpty(data, stride);
    data -= stride;
  }
  HQASSERT(! empty, "Form should have been detected as being empty.");
  bounds.y2 = y + 1;

  /* Find the left. */
  empty = TRUE;
  data = form->addr;
  for (x = 0; empty && x < width; x ++) {
    empty = columnEmpty(data, x, height, stride);
  }
  HQASSERT(! empty, "Form should have been detected as being empty.");
  bounds.x1 = x - 1;

  /* Find the right. */
  empty = TRUE;
  for (x = width - 1; empty && x >= 0; x --) {
    empty = columnEmpty(data, x, height, stride);
  }
  HQASSERT(! empty, "Form should have been detected as being empty.");
  bounds.x2 = x + 1;

  return bounds;
}

/**
 * Bounding box method for FORMTYPE_CACHERLE[x] forms.
 */
static ibbox_t rleFormGetBounds(FORM* form)
{
  ibbox_t bounds;
  Bool empty = TRUE;
  Bool allEmpty = TRUE;
  int32 y = 0;
  int32 rows = 0;
  int32 xExtent[2] = {0};
  RLECACHE_LINE_READ_STATE line_reader;

  bbox_clear(&bounds);

  line_reader.next_line = (uint8*)form->addr;
  while (y < form->h) {
    rlecache_line_read_init(&line_reader, form, line_reader.next_line);
    empty = rleRowBlockExtent(&line_reader, xExtent, &rows);
    if (y == 0) {
      if (empty)
        bounds.y1 = rows;
      else
        bounds.y1 = 0;
    }

    if (! empty) {
      allEmpty = FALSE;
      if (xExtent[0] < bounds.x1)
        bounds.x1 = xExtent[0];
      if (xExtent[1] > bounds.x2)
        bounds.x2 = xExtent[1];
    }

    y += rows;
  }

  if (empty)
    bounds.y2 = form->h - rows;
  else
    bounds.y2 = form->h;

  if (allEmpty)
    bbox_clear(&bounds);
  else {
    /* Bounds are inclusive. */
    bounds.x2 --;
    bounds.y2 --;
  }

  return bounds;
}

/**
 * Copy and invert 'width' bits of data from 'sourceLine' to 'targetLine'.
 * The first 'sourceOffset' bits within 'sourceLine' will be skipped.
 */
static void bitmapCopyAndInvertRow(blit_t* sourceLine, int32 sourceOffset,
                                   blit_t* targetLine, int32 width)
{
  int32 sourceBitIndex = BLIT_MASK_BITS - (sourceOffset & BLIT_MASK_BITS);
  int32 targetBitIndex = BLIT_MASK_BITS;
  int32 i;

  sourceLine = BLIT_ADDRESS(sourceLine, BLIT_OFFSET(sourceOffset));
  *targetLine = 0;
  for (i = 0; i < width; i ++) {
    if (sourceBitIndex < 0) {
      sourceLine ++;
      sourceBitIndex = BLIT_MASK_BITS;
    }
    if (targetBitIndex < 0) {
      targetLine ++;
      *targetLine = 0;
      targetBitIndex = BLIT_MASK_BITS;
    }
    *targetLine |= ((~(*sourceLine) >> sourceBitIndex) & 1) << targetBitIndex;
    sourceBitIndex --;
    targetBitIndex --;
  }
}

/**
 * Invert method for FORMTYPE_CACHEBITMAP forms.
 */
static OFFSETFORM* bitmapFormInvert(FORM* source, OFFSETFORM* target)
{
  int32 y;
  int32 sourceStride = source->l >> BLIT_SHIFT_BYTES;
  int32 targetStride = target->form->l >> BLIT_SHIFT_BYTES;

  for (y = 0; y < target->form->h; y ++) {
    blit_t* sourceLine = source->addr + sourceStride * (y + target->y);
    blit_t* targetLine = target->form->addr + targetStride * y;
    bitmapCopyAndInvertRow(sourceLine, target->x, targetLine, target->form->w);
  }
  return target;
}

/**
 * Invert method for FORMTYPE_CACHERLE[x] forms.
 */
static OFFSETFORM* rleFormInvert(FORM* source, OFFSETFORM* target)
{
  int32 y;
  int32 spanLengths[2];
  Bool finished = FALSE;
  RLECACHE_LINE_READ_STATE reader;
  render_forms_t forms ;
  blit_chain_t blits;
  render_state_t renderState;
  int32 span = target->form->l;

  /* Note that the clip form passed here is initialised by render_state_mask()
   * and is effectively a dummy which is not actually used, so no form memory
   * need be allocated. */
  render_state_mask(&renderState, &blits, &forms, &invalid_surface,
                    target->form);

  /* Render in white, on a black surface. */
  RESET_BLITS(&blits, &blitslice0[BLT_CLP_NONE], &blitslice0[BLT_CLP_RECT],
              &blitslice0[BLT_CLP_COMPLEX]);

  /* Fill the target with black; this allows us to simply render the black runs
   * as white. */
  area1fill(target->form);

  reader.next_line = (uint8*)source->addr;
  for (y = 0; ! finished && y < source->h; ) {
    rlecache_line_read_init(&reader, source, reader.next_line);
    if (y + reader.row_height > target->y) {
      int32 x = 0;
      int32 left, right;
      int32 bottom = y - target->y;
      int32 top = bottom + reader.row_height;

      /* Vertical clipping. */
      if (bottom < 0)
        bottom = 0;

      if (top > target->form->h) {
        top = target->form->h;
        finished = TRUE;
      }

      if (top <= bottom)
        continue;

      renderState.ri.rb.ylineaddr = target->form->addr + (bottom * span);
      renderState.ri.rb.ylineaddr = BLIT_ADDRESS(target->form->addr,
                                                 span * bottom);
      while (! reader.line_finished) {
        rlecache_get_span_pair(&reader, spanLengths);
        /* Skip the white block (forms are white-initialised). */
        x += spanLengths[0];

        left = x - target->x;
        right = left + spanLengths[1];

        /* Horizontal clipping. */
        if (left < 0)
          left = 0;

        if (right > target->form->w)
          right = target->form->w;

        if (right <= left)
          continue;

        /* Render the black block as white. */
        DO_BLOCK(&renderState.ri.rb, bottom, top - 1, left, right - 1);
        x += spanLengths[1];
      }
    }
    y += reader.row_height;
  }
  return target;
}


/* See header for doc. */
FORM *make_Form( int32 w , int32 h )
{                               /* allocate form and bitmap */
  int32 lbytes , tbytes ;
  FORM *this_form ;

  HQASSERT(w > 0, "make_Form: width must be greater than 0");
  HQASSERT(h > 0, "make_Form: height must be greater than 0");

/* Round up to nearest multiple of blit_t. */
  lbytes = FORM_LINE_BYTES(w) ;
  tbytes = lbytes * h ;

  if ( NULL == ( this_form =
          ( FORM * )mm_alloc(mm_pool_temp,
                             (mm_size_t)(tbytes + BLIT_ALIGN_SIZE(sizeof(FORM))),
                             MM_ALLOC_CLASS_FORM))) {
    (void)error_handler(VMERROR);
    return NULL ;
  }
  this_form->addr = BLIT_ALIGN_UP(this_form + 1) ;

/* Set up FORM's values, */
  this_form->type = FORMTYPE_CACHEBITMAP ;
  this_form->w = w ;
  this_form->h = h ;
  this_form->l = lbytes ;
  this_form->size = tbytes ;
  this_form->rh = h ;
  this_form->hoff = 0 ;

  area0fill( this_form ) ;

  return (this_form ) ;
}

/* See header for doc. */
FORM *make_RLEForm( int32 w , int32 h , int32 tbytes, int32 ftype )
{                               /* allocate form and bitmap */
  int32 lbytes ;
  FORM *this_form ;

/* Round up to nearest multiple of blit_t. */
  lbytes = FORM_LINE_BYTES(w) ;

  if ( NULL == (this_form =
         mm_alloc(mm_pool_temp,
                  (mm_size_t)(tbytes + BLIT_ALIGN_SIZE(sizeof(FORM))),
                  MM_ALLOC_CLASS_RLEFORM))) {
    (void)error_handler(VMERROR);
    return NULL ;
  }

  /* Do the right thing with the address pointer for blank forms too. */
  /* Putting asserts within the if doesn't change the code, but makes each
     assert less convoluted */
  if ( ftype != FORMTYPE_BLANK ) {
    this_form->addr = BLIT_ALIGN_UP(this_form + 1) ;

    HQASSERT( w > 0, "make_RLEForm: non blank form needs +ve width");
    HQASSERT( h > 0, "make_RLEForm: non blank form needs +ve height");
    HQASSERT( tbytes > 0, "make_RLEForm: non blank form needs +ve size");
    HQASSERT( lbytes > 0, "make_RLEForm: non blank got bogus lbyte value");
  } else {
    this_form->addr = NULL;

    HQASSERT( w == 0, "make_RLEForm: blank form needs 0 width");
    HQASSERT( h == 0, "make_RLEForm: blank form needs 0 height");
    HQASSERT( tbytes == 0, "make_RLEForm: blank form needs 0 size");
    HQASSERT( lbytes == 0, "make_RLEForm: blank form needs 0 lbyte");
  }

  /* Set up FORM's values, */
  this_form->type = ftype ;
  this_form->w = w ;
  this_form->h = h ;
  this_form->l = lbytes ;
  this_form->size = tbytes ;
  this_form->rh = h ;
  this_form->hoff = 0 ;

  return this_form ;
}

/* See header for doc. */
void destroy_Form( FORM *this_form )
{
  mm_free(mm_pool_temp,
          (mm_addr_t)this_form,
          (mm_size_t)(this_form->size + BLIT_ALIGN_SIZE(sizeof(FORM))));
}

Bool formarray_new(mm_pool_t *pools, dbbox_t *bbox, int32 rh,
                   form_array_t **p_formarray)
{
  uint32 nforms ;
  form_array_t *formarray ;
  FORM *form ;
  int32 w ;

  HQASSERT(rh > 0, "height must be greater than 0");
  nforms = ( bbox->y2 / rh ) - ( bbox->y1 / rh ) + 1 ;

  *p_formarray = formarray = dl_alloc(pools, sizeof(form_array_t) +
                              sizeof(FORM) * nforms, MM_ALLOC_CLASS_FORMARRAY);
  if ( !formarray )
    return error_handler(VMERROR) ;

  formarray->bbox = *bbox ;
  formarray->rh = rh ;
  formarray->nforms = nforms ;
  formarray->forms = (FORM*)(formarray + 1) ;

  w = bbox->x2 - bbox->x1 + 1 ;

  for ( form = formarray->forms ; nforms > 0 ; ++form, --nforms ) {
    form->addr = NULL;
    form->type = FORMTYPE_BLANK;
    form->w = w;
    form->h = rh;
    form->l = 0;
    form->size = 0;
    form->rh = rh;
    form->hoff = 0;
  }

  /* The first and last forms may not be full height, so adjust their height
     values accordingly. */
  form = &formarray->forms[0] ;
  form->rh -= bbox->y1 % rh ;
  form->h = form->rh;

  form = &formarray->forms[formarray->nforms - 1] ;
  form->rh -= rh - (bbox->y2 % rh) - 1 ;
  form->h = form->rh;

#if defined( ASSERT_BUILD )
 {
   dcoord y = bbox->y1 ;
   for ( form = formarray->forms, nforms = formarray->nforms ; nforms > 0 ; ++form, --nforms ) {
     y += form->rh;
   }
   --y ;
   HQASSERT(y == bbox->y2, "form list not created properly") ;
 }
#endif

  return TRUE ;
}

void formarray_destroy(form_array_t **p_formarray, mm_pool_t *pools)
{
  FORM *form;
  uint32 nforms;

  for ( form = (*p_formarray)->forms, nforms = (*p_formarray)->nforms;
        nforms > 0 ; ++form, --nforms )
    formarray_destroyform(form, pools);

  dl_free(pools, *p_formarray, sizeof(form_array_t) +
          sizeof(FORM) * (*p_formarray)->nforms, MM_ALLOC_CLASS_FORMARRAY);

  *p_formarray = NULL;
}

Bool formarray_newform(FORM *form, mm_pool_t *pools, int32 type,
                       int32 linebytes)
{
  int32 totalbytes ;

  HQASSERT(form->type == FORMTYPE_BLANK && !form->addr,
           "Trying to create a form form a non-blank form") ;

  /* Round up to nearest multiple of blit_t. */
  linebytes = BLIT_ALIGN_SIZE(linebytes) ;
  totalbytes = linebytes * form->rh;

  if ( linebytes > 0 ) {
    form->addr = dl_alloc(pools, totalbytes, MM_ALLOC_CLASS_FORMARRAY);
    if ( !form->addr )
      return error_handler(VMERROR) ;
    HQASSERT(BLIT_ALIGN_UP(form->addr) == form->addr,
             "the form addr should be blit_t aligned already");
  }

  /* Set up FORM's values */
  form->type = type ;
  form->l = linebytes ;
  form->size = totalbytes ;

  return TRUE ;
}

void formarray_findform(form_array_t *formarray, dcoord y,
                        FORM **form, dcoord *yform)
{
  uint32 iform ;

  if ( y < formarray->bbox.y1 || y > formarray->bbox.y2 ) {
    HQFAIL("y is out of range in formarray_findform") ;
    *form = NULL ;
    return ;
  }

  iform = ( y / formarray->rh ) - ( formarray->bbox.y1 / formarray->rh ) ;
  HQASSERT(iform < formarray->nforms, "iform out of range in formarray_findform") ;

  *form = &formarray->forms[iform] ;

  /* The first and last forms may not be full height. */
  if ( iform == 0 )
    *yform = y - formarray->bbox.y1 ;
  else if ( iform == formarray->nforms - 1 )
    *yform = y - (formarray->bbox.y2 - (*form)->rh + 1) ;
  else
    *yform = y % formarray->rh ;

  if ( *yform < 0 || *yform >= (*form)->rh ) {
    HQFAIL("yform is out of range in formarray_findform") ;
    *form = NULL ;
  }
}

void formarray_destroyform(FORM *form, mm_pool_t *pools)
{
  if ( form->addr )
    dl_free(pools, form->addr, form->size, MM_ALLOC_CLASS_FORMARRAY);

  /* Reset FORM's values */
  form->type = FORMTYPE_BLANK ;
  form->l = 0 ;
  form->size = 0 ;
}

#if defined( DEBUG_BUILD )
#include "monitor.h"

void debug_print_form(FORM *form)
{
  int32 h ;
  monitorf((uint8*)"Print form >>>\n") ;
  for ( h = 0 ; h < form->rh; ++h ) {
    blit_t *ptr = BLIT_ADDRESS(form->addr, h * form->l) ;
    int32 w ;
    for ( w = form->l >> BLIT_SHIFT_BYTES ; w > 0 ; --w ) {
      monitorf((uint8*)"%.8X", *ptr) ;
      ++ptr ;
    }
    monitorf((uint8*)"\n") ;
  }
  monitorf((uint8*)"<<< Print form\n") ;
}

#endif

/**
 * Return the inclusive bounds of the marked pixels within the passed form.
 * Note: This method only supports empty, FORMTYPE_CACHEBITMAP, and
 * FORMTYPE_CACHERLE[x] form types.
 */
ibbox_t formGetBounds(FORM* form)
{
  ibbox_t bounds;
  bbox_clear(&bounds);

  switch (form->type) {
    default:
      HQFAIL("Unsupported form type.");
      /* Fall-through. */
    case FORMTYPE_BLANK:
      return bounds;

    case FORMTYPE_CACHEBITMAP:
      return bitmapFormGetBounds(form);

    case FORMTYPE_CACHERLE1:
    case FORMTYPE_CACHERLE2:
    case FORMTYPE_CACHERLE3:
    case FORMTYPE_CACHERLE4:
    case FORMTYPE_CACHERLE5:
    case FORMTYPE_CACHERLE6:
    case FORMTYPE_CACHERLE7:
    case FORMTYPE_CACHERLE8:
      return rleFormGetBounds(form);
  }
}

/**
 * Constructor.
 */
OFFSETFORM* offsetFormNew(int32 width, int32 height)
{
  OFFSETFORM* self = NULL;

  self = (OFFSETFORM*)mm_alloc(mm_pool_temp, sizeof(OFFSETFORM),
                               MM_ALLOC_CLASS_FORM);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  self->x = self->y = 0;
  self->form = make_Form(width, height);
  if (self->form == NULL) {
    offsetFormDelete(self);
    (void)error_handler(VMERROR);
    return NULL;
  }
  return self;
}

/**
 * Destructor.
 */
void offsetFormDelete(OFFSETFORM* self)
{
  if (self->form != NULL)
    destroy_Form(self->form);
  mm_free(mm_pool_temp, self, sizeof(OFFSETFORM));
}

/* See header for doc. */
OFFSETFORM* formInvert(FORM* source, Bool* empty)
{
  int32 width, height;
  ibbox_t bounds;
  OFFSETFORM* target;

  *empty = FALSE;

  /* Determin the bounds of the source form. */
  bounds = formGetBounds(source);
  if (bbox_is_empty(&bounds)) {
    *empty = TRUE;
    return NULL;
  }

  /* The bounds returned are inclusive. */
  width = bounds.x2 - bounds.x1 + 1;
  height = bounds.y2 - bounds.y1 + 1;

  target = offsetFormNew(width, height);
  if (target == NULL)
    return NULL;

  target->x = bounds.x1;
  target->y = bounds.y1;

  switch (source->type) {
  default:
    HQFAIL("Unsupported form type.");
    offsetFormDelete(target);
    return NULL;

  case FORMTYPE_CACHEBITMAP:
    return bitmapFormInvert(source, target);

  case FORMTYPE_CACHERLE1:
  case FORMTYPE_CACHERLE2:
  case FORMTYPE_CACHERLE3:
  case FORMTYPE_CACHERLE4:
  case FORMTYPE_CACHERLE5:
  case FORMTYPE_CACHERLE6:
  case FORMTYPE_CACHERLE7:
  case FORMTYPE_CACHERLE8:
    return rleFormInvert(source, target);
  }
}

/* Log stripped */

