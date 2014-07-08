/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:bitGrid.c(EBDSDK_P.1) $
 * $Id: src:bitGrid.c,v 1.28.2.1.1.1 2013/12/19 11:25:19 anon Exp $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * BitGrid implementation. The BitGrid object supports a two dimension array
 * of bit values (flags which can be set (true) or clear (false)), and provides
 * methods for accessing and querying each flag.
 *
 * BitGrid supports a 'mapped' grid - this is a convenience representation that
 * allows an arbitrarily-sized rectangular area (the 'source space') to be
 * associated with the bit array ('grid space'); a cell size must be provided
 * which specifies the mapping from source space to grid space (each cell in the
 * source space maps to a single bit in grid space).
 */

#include "core.h"
#include "hqmemset.h"
#include "bitGrid.h"

#include "swerrors.h"
#include "objnamer.h"
#include "mm.h"

#if defined( ASSERT_BUILD )
/* For providing stats on bitGrid usage. */
#include "group.h"
#endif

/* --Private macros-- */

#define BITGRID_NAME "Bit grid"

/* Return the minimum number of containers (of the specified size) required to
hold the specified value. e.g. minContainers(15, 8) would return 2. */
#define minContainers(_value, _containerSize) \
  (((_value) + ((_containerSize) - 1)) / (_containerSize))

/* Bit set/clear/query. Only the least significant 3 bits are used in the
'bitInByte' parameter, so the caller need not bound the value. */
#define setBitInByte(_byte, _bitInByte) \
  MACRO_START \
  (_byte) |= (1 << ((_bitInByte) & 7)); \
  MACRO_END

#define clearBitInByte(_byte, _bitInByte) \
  MACRO_START \
  (_byte) &= (~(1 << ((_bitInByte) & 7))); \
  MACRO_END

#define queryBitInByte(_byte, _bitInByte) \
  (((_byte) & (1 << ((_bitInByte) & 7))) != 0)

/* --Private datatypes-- */

struct BitGrid {
  Size2d size;
  uint32 lineSize;
  uint8* grid;
  uint32 gridAllocSize;

  Bool allClear;
  Bool mapped;
  dbbox_t sourceSpace;
  Size2d cellSize;

  OBJECT_NAME_MEMBER
};

/* --Private prototypes-- */

static void mapCoords(BitGrid* self, int32* x, int32* y);
static void mapBox(BitGrid *self, dbbox_t *box, Bool value);

/* --Public methods-- */

/* Constructor.
*/
Bool bitGridNew(Size2d size, BitGrid** instance)
{
  BitGrid* self;

  HQASSERT(size2dDegenerate(size) == FALSE,
           "bitGridNew - 'size' is degenerate.");
  HQASSERT(instance != NULL, "bitGridNew - 'instance' cannot be NULL");

  *instance = NULL;
  self = (BitGrid*) mm_alloc(mm_pool_temp, sizeof(BitGrid),
                             MM_ALLOC_CLASS_BITGRID);
  if (self == NULL)
    return error_handler(VMERROR);

  NAME_OBJECT(self, BITGRID_NAME);
  self->size = size;
  self->lineSize = minContainers(size.width, 8);
  self->gridAllocSize = self->lineSize * size.height;
  self->grid = mm_alloc(mm_pool_temp, self->gridAllocSize,
                        MM_ALLOC_CLASS_BITGRID);
  if (self->grid == NULL) {
    bitGridDestroy(&self);
    return error_handler(VMERROR);
  }

  /* Clear the grid. */
  HqMemZero((uint8 *)self->grid, self->gridAllocSize);
  self->allClear = TRUE;

  /* By default a grid has no mapping. */
  self->mapped = FALSE;

  *instance = self;
  return TRUE;
}

/* Create a grid with a coordinate transform. 'frame' defines the source
coordinate space, 'cellSize' defines what area within 'frame' maps to a single
bit value.
*/
Bool bitGridNewMapped(dbbox_t *frame, Size2d cellSize, BitGrid** instance)
{
  Size2d size;
  BitGrid* self;

  HQASSERT(! bbox_is_empty(frame), "bitGridNewMapped - 'frame' is empty.");
  HQASSERT(size2dDegenerate(cellSize) == FALSE,
           "bitGridNewMapped - 'cellSize' is degenerate.");

  size.width = minContainers((frame->x2 - frame->x1 + 1), cellSize.width);
  size.height = minContainers((frame->y2 - frame->y1 + 1), cellSize.height);

  if (! bitGridNew(size, instance))
    return FALSE;

  self = *instance;
  self->mapped = TRUE;
  self->sourceSpace = *frame;
  self->cellSize = cellSize;
  return TRUE;
}

/* Destructor.
*/
void bitGridDestroy(BitGrid** selfPointer)
{
  BitGrid* self;

  HQASSERT(selfPointer != NULL,
           "bitGridDestroy - 'selfPointer' cannot be NULL.");

  self = *selfPointer;
  *selfPointer = NULL;

  if (self == NULL)
    return;
  VERIFY_OBJECT(self, BITGRID_NAME);

  if (self->grid != NULL) {
#if defined( ASSERT_BUILD )
    /* Provide stats on bitGrid usage. */
    if ((backdrop_render_debug & BR_DEBUG_TRACE) != 0) {
      uint32 x, y;
      uint32 numSet = 0u, numClear = 0u;
      for (y = 0u; y < self->size.height; ++y) {
        for (x = 0u; x < self->size.width; ++x) {
          if (bitGridGet(self, x, y))
            ++numSet;
          else
            ++numClear;
        }
      }
      /* Just ignore bitGrids that don't have any bits set. */
      if (numSet > 0) {
        HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
                ("Region map summary: rows %d, cols %d, "
                 "bytes %d, number set %d, number clear %d",
                 self->size.height, self->size.width,
                 self->gridAllocSize, numSet, numClear));
      }
    }
#endif
    mm_free(mm_pool_temp, self->grid, self->gridAllocSize);
  }
  UNNAME_OBJECT(self);
  mm_free(mm_pool_temp, self, sizeof(BitGrid));
}

/* Set a bit in the grid to the passed value.
*/
void bitGridSet(BitGrid* self, uint32 x, uint32 y, Bool value)
{
  uint8* byte;

  VERIFY_OBJECT(self, BITGRID_NAME);
  HQASSERT((x < self->size.width) && (y < self->size.height),
           "bitGridSet - out of bounds access.");

  byte = &self->grid[(self->lineSize * y) + (x >> 3)];
  if (value) {
    setBitInByte(*byte, x);
    self->allClear = FALSE;
  }
  else {
    clearBitInByte(*byte, x);
  }
}

/* Set all values in the grid to false.
*/
void bitGridClearAll(BitGrid* self)
{
  VERIFY_OBJECT(self, BITGRID_NAME);

  HqMemZero((uint8 *)self->grid, self->gridAllocSize);
  self->allClear = TRUE;
}

/* Query the status of the specified bit. TRUE is returned if the bit is set,
FALSE if clear.
*/
Bool bitGridGet(BitGrid* self, uint32 x, uint32 y)
{
  VERIFY_OBJECT(self, BITGRID_NAME);
  HQASSERT((x < self->size.width) && (y < self->size.height),
           "bitGridGet - out of bounds access.");

  if ( self->allClear )
    return FALSE;
  else
    return queryBitInByte(self->grid[(self->lineSize * y) + (x >> 3)], x);
}

/* Set a bit in the grid, mapping the specified coordinates from the source
space to the grid space. The grid must have been created with the newMapped()
constructor (this is asserted).
*/
void bitGridSetMapped(BitGrid* self, int32 x, int32 y)
{
  VERIFY_OBJECT(self, BITGRID_NAME);
  HQASSERT(self->mapped, "bitGridSetMapped - grid does not support mapping.");

  mapCoords(self, &x, &y);
  bitGridSet(self, x, y, TRUE);
}

/* Query a bit in the grid, mapping the specified coordinates from the source
space to the grid space. The grid must have been created with the newMapped()
constructor (this is asserted).
TRUE is returned if the bit is set, FALSE if clear.
*/
Bool bitGridGetMapped(BitGrid* self, int32 x, int32 y)
{
  VERIFY_OBJECT(self, BITGRID_NAME);
  HQASSERT(self->mapped, "bitGridGetMapped - grid does not support mapping.");

  mapCoords(self, &x, &y);
  return bitGridGet(self, x, y);
}

/* Set all the cells covered by the passed, bounding box (which is first mapped
to the grid space).
*/
void bitGridSetBoxMapped(BitGrid* self, dbbox_t* sourceBox, Bool value)
{
  int32 x, y;
  dbbox_t box = *sourceBox;

  VERIFY_OBJECT(self, BITGRID_NAME);
  HQASSERT(! bbox_is_empty(sourceBox), "bitGridSetBoxMapped - 'sourceBox' "
                                       "cannot be empty.");

  mapBox(self, &box, value) ;

  for (y = box.y1; y <= box.y2; y ++) {
    for (x = box.x1; x <= box.x2; x ++) {
      bitGridSet(self, x, y, value);
    }
  }
}

/* Sets the cells in self covered by the intersection of sourceGrid and
   sourceBox to 'value'. */
void bitGridSetIntersectionMapped(BitGrid* self, BitGrid* sourceGrid,
                                  dbbox_t* sourceBox, Bool value)
{
  int32 x, y;
  dbbox_t box = *sourceBox;

  VERIFY_OBJECT(self, BITGRID_NAME);
  VERIFY_OBJECT(sourceGrid, BITGRID_NAME);
  HQASSERT(self->size.width == sourceGrid->size.width &&
           self->size.height == sourceGrid->size.height,
           "bit grid self and source are incompatible");
  HQASSERT(! bbox_is_empty(sourceBox), "bitGridSetIntersectionMapped - "
                                       "'sourceBox' cannot be empty.");

  mapBox(self, &box, value) ;

  for (y = box.y1; y <= box.y2; y ++) {
    for (x = box.x1; x <= box.x2; x ++) {
      if (bitGridGet(sourceGrid, x, y))
        bitGridSet(self, x, y, value);
    }
  }
}

/* Query the state of all the cells in the specified bounding box (which is
first mapped to the grid space).
*/
BGMultiState bitGridGetBoxMapped(BitGrid* self, dbbox_t* sourceBox)
{
  int32 x, y;
  BGMultiState state;
  dbbox_t box = *sourceBox;

  VERIFY_OBJECT(self, BITGRID_NAME);

  /* If the grid is all clear or the passed bounding box is empty, just return
     BGAllClear. */
  if (self->allClear ||
      bbox_is_empty(sourceBox))
    return BGAllClear;

  mapBox(self, &box, TRUE);
  if ( bbox_is_empty(&box) )
    return BGAllClear;

  /* Initialise the state using the state of the first (top-left most) cell.
  We will check this cell again in the main loop because its not worth the
  effort to skip over it. */
  if (bitGridGet(self, box.x1, box.y1)) {
    state = BGAllSet;
  }
  else {
    state = BGAllClear;
  }

  for (y = box.y1; y <= box.y2; y ++) {
    for (x = box.x1; x <= box.x2; x ++) {
      Bool cellState = bitGridGet(self, x, y);
      switch (state) {
        case BGAllSet:
          /* If the current cell is clear, then we must have a mixture of
          states (since all up to this point have been set). */
          if (cellState == FALSE) {
            return BGMixed;
          }
          break;

        case BGAllClear:
          /* If the current cell is set, then we must have a mixture of
          states (since all up to this point have been clear). */
          if (cellState) {
            return BGMixed;
          }
          break;

        default:
          HQFAIL("bitGridGetBoxMapped - undefined state.");
          break;
      }
    }
  }
  return state;
}

/* Determine whether the whole grid is set, clear or mixed. */
BGMultiState bitGridGetAll(BitGrid* self)
{
  return bitGridGetBoxMapped(self, &self->sourceSpace);
}

/* Size query method.
*/
Size2d bitGridSize(BitGrid* self)
{
  VERIFY_OBJECT(self, BITGRID_NAME);

  return self->size;
}

/* Cell size query method.
*/
Size2d bitGridCellSize(BitGrid* self)
{
  VERIFY_OBJECT(self, BITGRID_NAME);

  return self->cellSize;
}

double bitGridRegionsSetProportion(BitGrid* self)
{
  uint32 numSet = 0u;

  if ( !self->allClear ) {
    uint32 x, y;

    for (y = 0u; y < self->size.height; ++y) {
      for (x = 0u; x < self->size.width; ++x) {
        if (bitGridGet(self, x, y))
          ++numSet;
      }
    }
  }
  return (double)numSet / (double)(self->size.height * self->size.width);
}

/* --Private methods-- */

/* Map the passed coordinates from the source space to the grid space.
*/
static void mapCoords(BitGrid* self, int32* x, int32* y)
{
  uint32 mappedX = (*x - self->sourceSpace.x1) / self->cellSize.width;
  uint32 mappedY = (*y - self->sourceSpace.y1) / self->cellSize.height;

  if (mappedX >= self->size.width) {
    HQFAIL("x is out of range");
    mappedX = self->size.width - 1;
  }
  if (mappedY >= self->size.height) {
    HQFAIL("y is out of range");
    mappedY = self->size.height - 1;
  }
  *x = mappedX;
  *y = mappedY;
}

static void mapBox(BitGrid *self, dbbox_t *box, Bool value)
{
  if ( !value ) {
    /* Make sure the bbox covers the whole region before clearing a bit. */
    box->x1 += self->cellSize.width - 1;
    box->y1 += self->cellSize.height - 1;
    box->x2 -= self->cellSize.width - 1;
    box->y2 -= self->cellSize.height - 1;
    if ( bbox_is_empty(box) )
      return ; /* nothing to clear */
  }

  mapCoords(self, &box->x1, &box->y1);
  mapCoords(self, &box->x2, &box->y2);
}

/* Log stripped */
