/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:bitGrid.h(EBDSDK_P.1) $
 * $Id: export:bitGrid.h,v 1.14.2.1.1.1 2013/12/19 11:25:18 anon Exp $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public interface to BitGrid object.
 */

#ifndef __BITGRID_H__
#define __BITGRID_H__

typedef struct BitGrid BitGrid;

/* Multi-cell state value. */
typedef uint32 BGMultiState;

/* Possible values for BGMultiState. */
enum {
  BGAllSet = 1,
  BGAllClear,
  BGMixed
};

Bool bitGridNew(Size2d size, BitGrid** instance);
Bool bitGridNewMapped(dbbox_t *frame, Size2d cellSize, BitGrid** instance);
void bitGridDestroy(BitGrid** self);
void bitGridClearAll(BitGrid* self);

void bitGridSet(BitGrid* self, uint32 x, uint32 y, Bool value);
Bool bitGridGet(BitGrid* self, uint32 x, uint32 y);

void bitGridSetMapped(BitGrid* self, int32 x, int32 y);
Bool bitGridGetMapped(BitGrid* self, int32 x, int32 y);

void bitGridSetBoxMapped(BitGrid* self, dbbox_t* box, Bool value);
void bitGridSetIntersectionMapped(BitGrid* self, BitGrid* sourceGrid,
                                  dbbox_t* sourceBox, Bool value);
BGMultiState bitGridGetBoxMapped(BitGrid* self, dbbox_t* box);
BGMultiState bitGridGetAll(BitGrid* self);

/* Query methods. */

Size2d bitGridSize(BitGrid* self);
Size2d bitGridCellSize(BitGrid* self);

/* Returns the proportion of set regions. */
double bitGridRegionsSetProportion(BitGrid* self);

#endif

/* --Description--

BitGrid maintains a grid of bit values (which are all initially set to false).
Methods are provided to set and query each value.

Coordinate translation is provided for grids created using the
bitGridNewMapped() constructor; the source coordinate system is that defined
by the 'frame' parameter, and the dimensions for the grid are calculated using
the 'cellSize' parameter, which specifies the size of each cell in 'frame'.
Each cell will be mapped to a single bit value.

Once so constructed, bits in a mapped grid can be accessed via the
set/getMapped() methods, which take coordinates in the source coordinate
system.
*/

/* Log stripped */
