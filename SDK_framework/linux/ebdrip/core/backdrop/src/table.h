/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:table.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop table API
 */

#ifndef __TABLE_H__
#define __TABLE_H__

#include "dl_color.h"

struct CV_COLCVT;
struct FN_INTERNAL;

typedef struct BackdropTable BackdropTable;

enum {
  BDT_ISOLATED,    /**< color, alpha, info */
  BDT_ISOLATED_SHAPE, /**< color, alpha, shape, info */
  BDT_NONISOLATED, /**< color, alpha, group alpha, info */
  BDT_NONISOLATED_SHAPE, /**< color, alpha, group alpha, shape, info */
  BDT_ALPHA,       /**< alpha */
  BDT_OUTPUT8,     /**< color (8 bpp), info */
  BDT_OUTPUT16     /**< color (16 bpp), info */
};

/**
 * Provides the number of bytes required for a table with the given
 * attributes. If the table already exists use bdtGetNBytes instead.
 */
size_t bdt_size(int32 type, uint32 nComps, uint16 nSlots);

/**
 * The memory for this table is preallocated. The table may part of a larger
 * block of memory allowing the entire block to be written to disk in one
 * go. This routine is used to initialise a new table. The amount of memory
 * required is given by the routine bdtSize.
 */
void bdt_init(int32 type, uint32 nComps, uint16 nMaxSlots, BackdropTable *table);

/**
 * This routine is used to restore a table (update the ptrs inside the tables)
 * which has just been allocate or read from disk.
 */
void bdt_reset(BackdropTable *table, uint32 nComps);

/**
 * Insert the entry into the given slot overwriting any previous entry.
 */
void bdt_initEntry(BackdropTable *table, uint32 nComps, uint8 iSlot,
                   COLORVALUE *color, COLORVALUE alpha, COLORVALUE groupAlpha,
                   const COLORINFO *info);

/**
 * Copy the entry into the given slot overwriting any previous entry.
 */
void bdt_copyEntry(BackdropTable *tableFrom, BackdropTable *tableTo,
                   uint32 nComps, uint8 iSlotFrom, uint8 iSlotTo);

/**
 * Tests if two entries from the same or different tables are equal.  Assumes
 * the given slots are valid for their respective tables.
 */
Bool bdt_equalEntry(BackdropTable *table1, BackdropTable *table2,
                    uint32 nComps, uint8 iSlot1, uint8 iSlot2);

#define HASH_BIT              (11)
#define HASH_SIZE  (1 << HASH_BIT)
#define HASH_UNSET            (-1)
int16 bdt_hashVal(BackdropTable *table, uint32 nComps, uint8 iSlot);

/**
 * Non-isloated groups store two alphas: table->alpha includes the alpha from
 * the group backdrop and table->groupAlpha excludes it.  Group alpha is used as
 * part of removing the contribution of the group backdrop from the computed
 * results to ensure when a non-isolated group is itself composited into another
 * group, the backdrop's contribution is included only once.  This routine is
 * create a new backdrop table which inherits values from another table, but
 * which adds groupAlpha.
 */
void bdt_copyToNonIsolated(BackdropTable *tableFrom, uint32 nCompsFrom,
                           BackdropTable *tableTo, uint32 nCompsTo,
                           uint8 iSlot);

/**
 * For each entry in the table, composite with the page color using the 'Normal'
 * blend mode.  The pixel label for each entry is left unchanged; their setting
 * cannot not be changed by the page composite, since opaque colors will not be
 * changed (and thus keep their pixel label), and semi-transparent colors will
 * already have been labelled as being composited.
 */
void bdt_compositeToPage(BackdropTable *table, uint32 nComps, COLORVALUE *pageColor);

/**
 * For each entry in the table, divide out the associated alpha.
 */
void bdt_divideAlpha(BackdropTable *table, uint32 nComps);

/**
 * Color converts inputTable into outputTable.  Either the table is reused
 * (inputTable and outputTable are the same) or the outputTable has been
 * allocated with precisely the required number of slots (when the table is
 * being retained across a band/frame/separation boundary).  Assumes the color
 * chain is set as required.
 */
Bool bdt_colorConvert(BackdropTable *inputTable, int32 tableType,
                      uint32 outComps, struct CV_COLCVT *converter,
                      BackdropTable *outputTable);

/**
 * When compositing, the rendering intent applied to color conversions is normally
 * inherited from the containing group as per PDFRM. An exception is made for the
 * color management of the page group to the output, for which we use the intent
 * the objects and groups had when they were painted into the page group.
 * NB. This doesn't conform to the PDFRM, but we have chosen to ignore the rule
 * that says we should use the default intent for the page in this case.
 *
 * This function transfers the rendering intent for all backdrops painted directly
 * into the page group, irrespective of whether compositing or color conversion
 * was applied to them, so the correct intent can be used in the output stage.
 *
 * We have chosen to apply the same rule to the color model.
 */
void bdt_updatePageGroupLateColor(BackdropTable *table, LateColorAttrib *lca);

/**
 * bdt_setAlphaFromGroupAlpha is used when completing a non-isolated backdrop
 * with the same blend space as its parent.  In this case all we need do is
 * replace alpha with groupAlpha.
 */
void bdt_setAlphaFromGroupAlpha(BackdropTable *table);

/**
 * Apply a transfer function to the alpha channel for soft masks.
 */
void bdt_applySoftMaskTransfer(BackdropTable *table,
                               struct FN_INTERNAL *transfer);

void bdt_get(BackdropTable *table, uint32 nComps, uint8 iSlot,
             COLORVALUE **color, COLORVALUE *alpha, COLORVALUE *shape,
             COLORINFO **info);

void bdt_getAlpha(BackdropTable *table, uint8 iSlot, COLORVALUE *alpha);

void bdt_getGroupAlpha(BackdropTable *table, uint8 iSlot,
                       COLORVALUE *groupAlpha);

struct CompositeResult;
void bdt_setResultPtrs(BackdropTable *table, uint32 nComps, uint8 iSlot,
                       struct CompositeResult *result);

void bdt_getOutput(BackdropTable *table, uint32 nComps, uint8 iSlot,
                   COLORVALUE **color, uint8 **color8, COLORINFO **info);

uint16 bdt_getNUsedSlots(BackdropTable *table);
void bdt_setNUsedSlots(BackdropTable *table, uint16 nUsedSlots);

/**
 * A more convenient form of bdtSize (when the table already exists).
 */
size_t bdt_getNBytesAlloc(BackdropTable *table, uint32 nComps);
size_t bdt_getNBytesUsed(BackdropTable *table, uint32 nComps);

/**
 * If the final backdrop blocks need to be retained across bands or separations
 * then we may need to write the tables to disk in low memory.
 */
struct IM_FILES;
Bool bdt_tableToDisk(BackdropTable *table, uint32 nComps, struct IM_FILES *file);

Bool bdt_tableFromDisk(BackdropTable *table, uint32 nComps, struct IM_FILES *file);

#endif /* __BACKDROPTABLE_H__ */

/* Log stripped */
