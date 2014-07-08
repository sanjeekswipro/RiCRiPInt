/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:table.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop table code
 */

#include "core.h"
#include "backdroppriv.h"
#include "table.h"
#include "composite.h"
#include "compositecolor.h"
#include "mm.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "objecth.h"
#include "compositers.h"
#include "pixelLabels.h"
#include "functns.h"
#include "packdata.h"
#include "cvcolcvt.h"
#include "hsiehhash32.h"
#include "imfile.h"
#include "display.h"
#if defined( DEBUG_BUILD )
#include "monitor.h"
#endif

struct BackdropTable {
  int32         type;       /**< one of BDT_ISOLATED et al */
  uint16        nMaxSlots;  /**< number of slots allocated for this table */
  uint16        nUsedSlots; /**< number of slots currently in use */
  union {
    COLORVALUE *color;      /**< interleaved values */
    uint8      *color8;     /**< 8 bit interleaved values (BDT_OUTPUT8 only) */
  } u;
  COLORVALUE   *alpha;      /**< stored separately for alpha only tables */
  COLORVALUE   *groupAlpha; /**< excludes initial backdrop's alpha */
  COLORVALUE   *shape;      /**< For knockout groups with fractional shape */
  COLORINFO    *info;       /**< spotno etc */
};

/**
 * Provides the number of bytes required for a table with the given
 * attributes. If the table already exists use bdt_getNBytes instead.
 */
size_t bdt_size(int32 type, uint32 nComps, uint16 nSlots)
{
  size_t nBytes;

  HQASSERT(nComps >= 0, "nComps < 0");
  HQASSERT(nSlots <= BACKDROPTABLE_MAXSLOTS, "nSlots out of range");

  nBytes = sizeof(BackdropTable);
  switch ( type ) {
  case BDT_NONISOLATED:
    nBytes += nSlots * sizeof(COLORVALUE); /* group alpha */
    /* fall through */
  case BDT_ISOLATED:
    nBytes += ((nComps * nSlots * sizeof(COLORVALUE)) + /* color */
               (nSlots * sizeof(COLORVALUE)) + /* alpha */
               (nSlots * sizeof(COLORINFO))); /* info */
    break;
  case BDT_NONISOLATED_SHAPE:
    nBytes += nSlots * sizeof(COLORVALUE); /* group alpha */
    /* fall through */
  case BDT_ISOLATED_SHAPE:
    nBytes += ((nComps * nSlots * sizeof(COLORVALUE)) + /* color */
               (nSlots * sizeof(COLORVALUE)) + /* alpha */
               (nSlots * sizeof(COLORVALUE)) + /* shape */
               (nSlots * sizeof(COLORINFO))); /* info */
    break;
  case BDT_ALPHA:
    nBytes += nSlots * sizeof(COLORVALUE); /* alpha */
    break;
  case BDT_OUTPUT8:
    nBytes += ((nComps * nSlots * sizeof(uint8)) + /* color 8 */
               (nSlots * sizeof(COLORINFO))); /* info */
    break;
  case BDT_OUTPUT16:
    nBytes += ((nComps * nSlots * sizeof(COLORVALUE)) + /* color 16 */
               (nSlots * sizeof(COLORINFO))); /* info */
    break;
  default:
    HQFAIL("invalid table type");
  }

  nBytes = DWORD_ALIGN_UP(size_t, nBytes);
  return nBytes;
}

/**
 * The memory for this table is preallocated. The table may part of a larger
 * block of memory allowing the entire block to be written to disk in one
 * go. This routine is used to initialise a new table. The amount of memory
 * required is given by the routine bdt_size.
 */
void bdt_init(int32 type, uint32 nComps, uint16 nMaxSlots, BackdropTable *table)
{
  HQASSERT(nComps >= 0, "nComps < 0");
  HQASSERT(nMaxSlots <= BACKDROPTABLE_MAXSLOTS, "nMaxSlots out of range");
  HQASSERT(table, "table is null");
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, table), "table unaligned");

#ifdef DEBUG_BUILD
  if ( (backdropDebug & BD_DEBUG_SCRIBBLE) != 0 ) {
    size_t nBytesInTable = bdt_size(type, nComps, nMaxSlots);
    HqMemSet32((uint32*)table, 0xDEADBEEF, nBytesInTable / sizeof(int32));
  }
#endif

  /* initialise table fields */
  table->type = type;
  table->nMaxSlots = nMaxSlots;
  table->nUsedSlots = nMaxSlots;

  /* reset ptrs for table */
  bdt_reset(table, nComps);
}

/**
 * This routine is used to restore a table (update the ptrs inside the tables)
 * which has just been allocate or read from disk.
 */
void bdt_reset(BackdropTable *table, uint32 nComps)
{
  void *next = NULL;

  HQASSERT(table, "table is null");
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, table), "table unaligned");
  HQASSERT(nComps >= 0, "nComps < 0");
  HQASSERT(table->nMaxSlots <= BACKDROPTABLE_MAXSLOTS,
           "nMaxSlots out of range");
  HQASSERT(table->nUsedSlots <= table->nMaxSlots,
           "nUsedSlots is out of range");

  /* WARNING! The order that info, alpha, groupAlpha, shape and color are
     assigned in memory is significant.  It allows bdt_colorConvert to avoid
     memory copies when reusing tables.  Do not change the order without
     considering bdt_colorConvert at the same time!  For alignment, largest
     first. */
  switch ( table->type ) {
  case BDT_NONISOLATED:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = (COLORVALUE*)(table->info + table->nMaxSlots);
    table->alpha = table->groupAlpha + table->nMaxSlots;
    table->shape = NULL;
    table->u.color = table->alpha + table->nMaxSlots;
    next = table->u.color + nComps * table->nMaxSlots;
    break;
  case BDT_ISOLATED:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = NULL;
    table->alpha = (COLORVALUE*)(table->info + table->nMaxSlots);
    table->shape = NULL;
    table->u.color = table->alpha + table->nMaxSlots;
    next = table->u.color + nComps * table->nMaxSlots;
    break;
  case BDT_NONISOLATED_SHAPE:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = (COLORVALUE*)(table->info + table->nMaxSlots);
    table->alpha = table->groupAlpha + table->nMaxSlots;
    table->shape = table->alpha + table->nMaxSlots;
    table->u.color = table->shape + table->nMaxSlots;
    next = table->u.color + nComps * table->nMaxSlots;
    break;
  case BDT_ISOLATED_SHAPE:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = NULL;
    table->alpha = (COLORVALUE*)(table->info + table->nMaxSlots);
    table->shape = table->alpha + table->nMaxSlots;
    table->u.color = table->shape + table->nMaxSlots;
    next = table->u.color + nComps * table->nMaxSlots;
    break;
  case BDT_ALPHA:
    table->info = NULL;
    table->groupAlpha = NULL;
    table->alpha = (COLORVALUE*)(table + 1);
    table->shape = NULL;
    table->u.color = NULL;
    next = table->alpha + table->nMaxSlots;
    break;
  case BDT_OUTPUT8:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = table->alpha = table->shape = NULL;
    table->u.color8 = (uint8*)(table->info + table->nMaxSlots);
    next = table->u.color8 + nComps * table->nMaxSlots;
    break;
  case BDT_OUTPUT16:
    table->info = (COLORINFO*)(table + 1);
    table->groupAlpha = table->alpha = table->shape = NULL;
    table->u.color = (COLORVALUE*)(table->info + table->nMaxSlots);
    next = table->u.color + nComps * table->nMaxSlots;
    break;
  default:
    HQFAIL("invalid table type");
  }

  HQASSERT((void*)((uint8*)table + bdt_getNBytesAlloc(table, nComps))
           == (void*)(DWORD_ALIGN_UP(uintptr_t, next)),
           "Unexpected end to the table");
}

/**
 * Insert the entry into the given slot overwriting any previous entry.
 */
void bdt_initEntry(BackdropTable *table, uint32 nComps, uint8 iSlot,
                   COLORVALUE *color, COLORVALUE alpha, COLORVALUE groupAlpha,
                   const COLORINFO *info)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ISOLATED || table->type == BDT_NONISOLATED ||
           table->type == BDT_ISOLATED_SHAPE || table->type == BDT_NONISOLATED_SHAPE ||
           table->type == BDT_ALPHA,
           "backdrop table is not of the input variety");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");
  HQASSERT(table->type == BDT_ALPHA || color != NULL,
           "color is required for given table");
  HQASSERT(table->type == BDT_ALPHA || info != NULL,
           "info is required for given table");

  table->alpha[iSlot] = alpha;

  if ( table->type != BDT_ALPHA ) {
    COLORVALUE *colorDst = table->u.color + nComps * iSlot;
    bd_copyColor(colorDst, color, nComps);
    if ( table->groupAlpha != NULL )
      table->groupAlpha[iSlot] = groupAlpha;
    if ( table->shape != NULL )
      table->shape[iSlot] = COLORVALUE_ZERO;
    table->info[iSlot] = *info;
  }
}

/**
 * Copy the entry into the given slot overwriting any previous entry.
 */
void bdt_copyEntry(BackdropTable *tableFrom, BackdropTable *tableTo,
                   uint32 nComps, uint8 iSlotFrom, uint8 iSlotTo)
{
  HQASSERT(tableFrom != NULL, "table is null");
  HQASSERT(tableFrom->type == BDT_ISOLATED || tableFrom->type == BDT_NONISOLATED ||
           tableFrom->type == BDT_ISOLATED_SHAPE || tableFrom->type == BDT_NONISOLATED_SHAPE ||
           tableFrom->type == BDT_ALPHA,
           "backdrop table is not of the input variety");
  HQASSERT(tableTo != NULL, "table is null");
  HQASSERT(tableTo->type == BDT_ISOLATED || tableTo->type == BDT_NONISOLATED ||
           tableTo->type == BDT_ISOLATED_SHAPE || tableTo->type == BDT_NONISOLATED_SHAPE ||
           tableTo->type == BDT_ALPHA,
           "backdrop table is not of the input variety");
  HQASSERT(iSlotFrom < tableFrom->nUsedSlots, "iSlotFrom is out of range");
  HQASSERT(iSlotTo < tableTo->nUsedSlots, "iSlotTo is out of range");

  tableTo->alpha[iSlotTo] = tableFrom->alpha[iSlotFrom];

  if ( tableTo->type != BDT_ALPHA ) {
    COLORVALUE *colorSrc = tableFrom->u.color + nComps * iSlotFrom;
    COLORVALUE *colorDst = tableTo->u.color + nComps * iSlotTo;
    bd_copyColor(colorDst, colorSrc, nComps);
    if ( tableTo->groupAlpha != NULL )
      tableTo->groupAlpha[iSlotTo] = tableFrom->groupAlpha[iSlotFrom];
    if ( tableTo->shape != NULL )
      tableTo->shape[iSlotTo] = tableFrom->shape[iSlotFrom];
    tableTo->info[iSlotTo] = tableFrom->info[iSlotFrom];
  }
}

/**
 * Tests if two entries from the same or different tables are equal.
 * Assumes the given slots are valid for their respective tables.
 */
Bool bdt_equalEntry(BackdropTable *table1, BackdropTable *table2,
                    uint32 nComps, uint8 iSlot1, uint8 iSlot2)
{
  HQASSERT(table1 != NULL, "table1 is null");
  HQASSERT(table2 != NULL, "table2 is null");
  HQASSERT(table1->type == table2->type, "table type mismatch");
  HQASSERT(iSlot1 < table1->nUsedSlots, "iSlot1 out of range");
  HQASSERT(iSlot2 < table2->nUsedSlots, "iSlot2 out of range");

  /* If one or both have no labels, the answer is quick. */
  if ( table1->info[iSlot1].label == 0 )
    return table2->info[iSlot2].label == 0;
  else if ( table2->info[iSlot2].label == 0 )
    return table1->info[iSlot1].label == 0;

  switch ( table1->type ) {
  case BDT_NONISOLATED:
  case BDT_NONISOLATED_SHAPE:
    if ( table1->groupAlpha[iSlot1] != table2->groupAlpha[iSlot2] )
      return FALSE;
    /* FALLTHRU */
  case BDT_ISOLATED:
  case BDT_ISOLATED_SHAPE: {
    COLORVALUE *color1 = table1->u.color + nComps * iSlot1;
    COLORVALUE *color2 = table2->u.color + nComps * iSlot2;

    /* The function call overhead with HqMemCmp appears to be significant. */
    switch ( nComps ) {
    default:
      if ( HqMemCmp(color1, nComps * sizeof(COLORVALUE),
                    color2, nComps * sizeof(COLORVALUE)) != 0 )
        return FALSE;
      break;
    case 4:
      if ( color1[0] != color2[0] || color1[1] != color2[1] ||
           color1[2] != color2[2] || color1[3] != color2[3] )
        return FALSE;
      break;
    case 3:
      if ( color1[0] != color2[0] || color1[1] != color2[1] ||
           color1[2] != color2[2] )
        return FALSE;
      break;
    case 2:
      if ( color1[0] != color2[0] || color1[1] != color2[1] )
        return FALSE;
      break;
    case 1:
      if ( color1[0] != color2[0] )
        return FALSE;
      break;
    }

    return ( table1->alpha[iSlot1] == table2->alpha[iSlot2] &&
             (table1->shape == NULL ||
              table1->shape[iSlot1] == table2->shape[iSlot2]) &&
             COLORINFO_EQ(table1->info[iSlot1], table2->info[iSlot2]) );
   }
  case BDT_ALPHA:
    return table1->alpha[iSlot1] == table2->alpha[iSlot2];
  default:
    HQFAIL("Unexpected backdrop table type");
    return FALSE; /* pretend entries are unequal */
  }
}

int16 bdt_hashVal(BackdropTable *table, uint32 nComps, uint8 iSlot)
{
  COLORVALUE *color = table->u.color + nComps * iSlot;
  uint32 *info = (uint32*)&table->info[iSlot];
  const uint32 infoLen = sizeof(COLORINFO) >> 2;
  uint32 key, i;

  HQASSERT(table->type == BDT_ISOLATED || table->type == BDT_NONISOLATED ||
           table->type == BDT_ISOLATED_SHAPE || table->type == BDT_NONISOLATED_SHAPE,
           "Unexpected table type");

  hsieh_hash32_init(&key, nComps + infoLen);

  for ( i = 0; i < nComps; ++i ) {
    hsieh_hash32_step(&key, (uint32)color[i]);
  }
  for ( i = 0; i < infoLen; ++i ) {
    hsieh_hash32_step(&key, info[i]);
  }
  hsieh_hash32_step(&key, (uint32)table->alpha[iSlot]);
  if ( table->groupAlpha != NULL )
    hsieh_hash32_step(&key, (uint32)table->groupAlpha[iSlot]);
  if ( table->shape != NULL )
    hsieh_hash32_step(&key, (uint32)table->shape[iSlot]);
  hsieh_hash32_finish(&key);

  key &= HASH_SIZE - 1;
  HQASSERT(0 <= key && key < HASH_SIZE, "key out of range");

  return (int16)key;
}

/**
 * Non-isolated groups store two alphas: table->alpha includes the alpha from
 * the group backdrop and table->groupAlpha excludes it.  Group alpha is used as
 * part of removing the contribution of the group backdrop from the computed
 * results to ensure when a non-isolated group is itself composited into another
 * group, the backdrop's contribution is included only once.  This routine is
 * create a new backdrop table which inherits values from another table, but
 * which adds groupAlpha.
 */
void bdt_copyToNonIsolated(BackdropTable *tableFrom, uint32 nCompsFrom,
                           BackdropTable *tableTo, uint32 nCompsTo,
                           uint8 iSlot)
{
  COLORVALUE *colorFrom = tableFrom->u.color + iSlot * nCompsFrom;
  COLORVALUE *colorTo = tableTo->u.color + iSlot * nCompsTo;

  HQASSERT(tableFrom, "tableFrom is null");
  HQASSERT(tableTo, "tableFrom is null");
  HQASSERT(tableFrom->type == BDT_ISOLATED || tableFrom->type == BDT_NONISOLATED ||
           tableFrom->type == BDT_ISOLATED_SHAPE || tableFrom->type == BDT_NONISOLATED_SHAPE,
           "backdrop table type is invalid");
  HQASSERT(tableTo->type == BDT_NONISOLATED || tableTo->type == BDT_NONISOLATED_SHAPE,
           "tableTo must be non-isolated");
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, tableFrom), "tableFrom unaligned");
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, tableTo), "tableTo unaligned");

  /* Parent may have more colorants but all the extra colorants are at the end
     and can therefore just be ignored. */
  HQASSERT(nCompsFrom >= nCompsTo,
           "Non-isolated parent can't have fewer colorants");
  bd_copyColor(colorTo, colorFrom, nCompsTo);
  tableTo->alpha[iSlot] = tableFrom->alpha[iSlot];
  tableTo->groupAlpha[iSlot] = COLORVALUE_ZERO;
  if ( tableTo->shape != NULL )
    tableTo->shape[iSlot] = COLORVALUE_ZERO;
  tableTo->info[iSlot] = tableFrom->info[iSlot];
  /* Label the initial block background empty since nothing has
     been drawn into it. */
  tableTo->info[iSlot].label = 0;
}

/**
 * For each entry in the table, composite with the page color using the 'Normal'
 * blend mode.  The pixel label for each entry is left unchanged; their setting
 * cannot not be changed by the page composite, since opaque colors will not be
 * changed (and thus keep their pixel label), and semi-transparent colors will
 * already have been labelled as being composited.
 */
void bdt_compositeToPage(BackdropTable *table, uint32 nComps, COLORVALUE *pageColor)
{
  COLORVALUE *color = table->u.color;
  COLORVALUE *alpha = table->alpha;
  uint32 i;

  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ISOLATED, "table type is invalid");
  HQASSERT(pageColor != NULL, "pageColor is null");

#if defined( DEBUG_BUILD )
  if ( nComps == 4 && /* Note, this only works for cmyk output. */
       (backdropDebug & BD_DEBUG_COMPOSITEDPIXELS) != 0 ) {
    COLORINFO *info = table->info;

    for ( i = 0u; i < table->nUsedSlots; ++i ) {
      if ( (info->label & SW_PGB_COMPOSITED_OBJECT) != 0 ) {
        /* Highlight composited pixels in Magenta (M - Must composite). */
        color[1] = 0 ;
        color[0] = color[2] = color[3] = COLORVALUE_ONE ;
      } else {
        /* Highlight normal opaque pixels in Cyan (C - Could avoid compositing). */
        color[0] = 0 ;
        color[1] = color[2] = color[3] = COLORVALUE_ONE ;
      }
      color += nComps;
      ++info;
    }
    return ;
  }
#endif

  for ( i = 0u; i < table->nUsedSlots; ++i ) {
    cceNormalPreMult(nComps, color, alpha[0],
                     pageColor, COLORVALUE_ONE, color);
    color += nComps;
    ++alpha;
  }
}

/**
 * For each entry in the table, divide out the associated alpha.
 */
void bdt_divideAlpha(BackdropTable *table, uint32 nComps)
{
  COLORVALUE *color = table->u.color;
  COLORVALUE *alpha = table->alpha;
  uint32 i;

  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ISOLATED || table->type == BDT_NONISOLATED ||
           table->type == BDT_ISOLATED_SHAPE || table->type == BDT_NONISOLATED_SHAPE,
           "table type is invalid");

  for ( i = 0u; i < table->nUsedSlots; ++i ) {
    cceDivideAlpha(nComps, color, alpha[0], color);
    color += nComps;
    ++alpha;
  }
}

/**
 * Color converts inputTable into outputTable.  Either the table is reused
 * (inputTable and outputTable are the same) or the outputTable has been
 * allocated with precisely the required number of slots (when the table is
 * being retained across a band/frame/separation boundary).  Assumes the color
 * chain is set as required.
 */
Bool bdt_colorConvert(BackdropTable *inputTable, int32 tableType,
                      uint32 nOutputComps, CV_COLCVT *converter,
                      BackdropTable *outputTable)
{
  /* Must store color ptr prior to the reset.  If reusing the same
     table, table->u.color may be changed by bdt_reset! */
  COLORVALUE *inColorValues = inputTable->u.color;
  COLORINFO *info = (tableType == BDT_ALPHA ? NULL : inputTable->info);
  uint16 nUsedSlots = inputTable->nUsedSlots;
  COLORVALUE *outColorValues = NULL;
  uint8 *outColorValues8 = NULL;

  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, outputTable), "outputTable unaligned");

  if ( inputTable != outputTable ) {
    bdt_init(tableType, nOutputComps, nUsedSlots, outputTable);
    outputTable->nUsedSlots = nUsedSlots;
  } else {
#if defined( ASSERT_BUILD )
    COLORVALUE *alpha = NULL, *shape = NULL;
    if ( tableType != BDT_OUTPUT8 && tableType != BDT_OUTPUT16 )
      alpha = inputTable->alpha, shape = inputTable->shape;
#endif

    outputTable->type = tableType;
    bdt_reset(outputTable, nOutputComps);

    /* Organising the ptrs carefully means lots of copying can be avoided. */
    HQASSERT((outputTable->info == info && outputTable->alpha == alpha &&
              outputTable->shape == shape) || tableType == BDT_ALPHA,
             "Info/alpha/shape have not been carried over into outputTable correctly");
  }

  if ( tableType == BDT_ALPHA )
    outColorValues = outputTable->alpha;
  else if ( tableType == BDT_OUTPUT8 )
    outColorValues8 = outputTable->u.color8;
  else
    outColorValues = outputTable->u.color;

  /* Color convert the table directly in the output table's color or alpha (for
     softmasks from luminosity).  Store the results in COLORVALUE containers
     except for BDT_OUTPUT8 when only 8 bit containers are needed. */
  if ( !cv_colcvt(converter, nUsedSlots, info, inColorValues,
                  outColorValues, outColorValues8) )
    return FALSE;

  /* Copy the info data if changing tables. */
  if ( info != NULL && inputTable != outputTable )
    HqMemCpy(outputTable->info, info, nUsedSlots * sizeof(COLORINFO));

  return TRUE;
}

/**
 * When compositing, the rendering intent applied to color conversions is normally
 * inherited from the containing group as per PDFRM. We also use the same rule
 * when applying intercept profiles to device spaces. An exception is made for
 * the color management of non-ICCBased page groups to the output, for which we
 * use the intent the objects and groups had when they were painted into the page
 * group.
 *
 * This function transfers the rendering intent for all backdrops painted directly
 * into the page group, irrespective of whether compositing or color conversion
 * was applied to them, so the correct intent can be used in the output stage.
 *
 * We have chosen to apply the same rule to the color model, except for NamedColor
 * because spots are composited in their own channel.
 */
void bdt_updatePageGroupLateColor(BackdropTable *table, LateColorAttrib *lca)
{
  uint32 i;

  HQASSERT(table != NULL, "table is null");

  for ( i = 0; i < table->nUsedSlots; ++i ) {
    COLORINFO_SET_RENDERING_INTENT(table->info[i].lcmAttribs, lca->renderingIntent);
    if ( table->info[i].origColorModel != REPRO_COLOR_MODEL_NAMED_COLOR )
      table->info[i].origColorModel = lca->origColorModel;
  }
}

/**
 * bdt_setAlphaFromGroupAlpha is used when completing a non-isolated backdrop
 * with the same blend space as its parent.  In this case all we need do is
 * replace alpha with groupAlpha.
 */
void bdt_setAlphaFromGroupAlpha(BackdropTable *table)
{
  HQASSERT(table->type == BDT_NONISOLATED || table->type == BDT_NONISOLATED_SHAPE,
           "Expected non-isolated table");
  table->alpha = table->groupAlpha;
}

/**
 * Apply a transfer function to the alpha channel for soft masks.
 */
void bdt_applySoftMaskTransfer(BackdropTable *table, FN_INTERNAL *transfer)
{
  uint32 i;

  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ALPHA, "Expected alpha");

  for ( i = 0; i < table->nUsedSlots; ++i ) {
    table->alpha[i] = fn_evaluateColorValue(transfer, table->alpha[i]);
  }
}

void bdt_get(BackdropTable *table, uint32 nComps, uint8 iSlot,
             COLORVALUE **color, COLORVALUE *alpha, COLORVALUE *shape,
             COLORINFO **info)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ISOLATED || table->type == BDT_NONISOLATED ||
           table->type == BDT_ISOLATED_SHAPE || table->type == BDT_NONISOLATED_SHAPE,
           "table type is invalid");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");

  *color = table->u.color + nComps * iSlot;
  *alpha = table->alpha[iSlot];
  *shape = table->shape != NULL ? table->shape[iSlot] : COLORVALUE_ONE;
  *info = table->info + iSlot;
}

void bdt_getAlpha(BackdropTable *table, uint8 iSlot, COLORVALUE *alpha)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");
  HQASSERT(alpha != NULL, "alpha is null");
  *alpha = table->alpha[iSlot];
}

void bdt_getGroupAlpha(BackdropTable *table, uint8 iSlot,
                       COLORVALUE *groupAlpha)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");
  HQASSERT(groupAlpha != NULL, "groupAlpha is null");
  HQASSERT(table->type == BDT_NONISOLATED || table->type == BDT_NONISOLATED_SHAPE,
           "invalid table type");
  *groupAlpha = table->groupAlpha[iSlot];
}

void bdt_setResultPtrs(BackdropTable *table, uint32 nComps, uint8 iSlot,
                       CompositeResult *result)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_ISOLATED || table->type == BDT_NONISOLATED ||
           table->type == BDT_ISOLATED_SHAPE || table->type == BDT_NONISOLATED_SHAPE,
           "table type is invalid");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");
  result->color = table->u.color + nComps * iSlot;
  result->info = table->info + iSlot;
  result->alpha = table->alpha + iSlot;
  result->groupAlpha = table->groupAlpha != NULL ? table->groupAlpha + iSlot : NULL;
  result->shape = table->shape != NULL ? table->shape + iSlot : NULL;
}

void bdt_getOutput(BackdropTable *table, uint32 nComps, uint8 iSlot,
                   COLORVALUE **color, uint8 **color8, COLORINFO **info)
{
  HQASSERT(table != NULL, "table is null");
  HQASSERT(table->type == BDT_OUTPUT8 || table->type == BDT_OUTPUT16,
           "backdrop table type is invalid");
  HQASSERT(iSlot < table->nUsedSlots, "iSlot is out of range");
  HQASSERT(color != NULL, "color is null");
  if ( table->type == BDT_OUTPUT8 ) {
    *color = NULL;
    *color8 = table->u.color8 + nComps * iSlot;
  } else {
    *color = table->u.color + nComps * iSlot;
    *color8 = NULL;
  }
  *info = table->info + iSlot;
}

uint16 bdt_getNUsedSlots(BackdropTable *table)
{
  HQASSERT(table != NULL, "table is null");
  return table->nUsedSlots;
}

void bdt_setNUsedSlots(BackdropTable *table, uint16 nUsedSlots)
{
  HQASSERT(table != NULL, "table is null");
  table->nUsedSlots = nUsedSlots;
}

/**
 * A more convenient form of bdt_size (when the table already exists).
 */
size_t bdt_getNBytesAlloc(BackdropTable *table, uint32 nComps)
{
  HQASSERT(table != NULL, "table is nullBackdropTableType");
  return bdt_size(table->type, nComps, table->nMaxSlots);
}

/**
 * A more convenient form of bdt_size (when the table already exists).
 */
size_t bdt_getNBytesUsed(BackdropTable *table, uint32 nComps)
{
  HQASSERT(table != NULL, "table is nullBackdropTableType");
  return bdt_size(table->type, nComps, table->nUsedSlots);
}

Bool bdt_tableToDisk(BackdropTable *table, uint32 nComps, IM_FILES *file)
{
  table->nMaxSlots = table->nUsedSlots;

  if ( !im_filewrite(file, (uint8*)table, (int32)sizeof(BackdropTable)) ||
       !im_filewrite(file, (uint8*)table->info,
                     (int32)(table->nUsedSlots * sizeof(COLORINFO))) )
    return FALSE;

  if ( table->type == BDT_OUTPUT8 ) {
    if ( !im_filewrite(file, (uint8*)table->u.color8,
                       (int32)(nComps * table->nUsedSlots * sizeof(uint8))) )
      return FALSE;
  } else {
    if ( !im_filewrite(file, (uint8*)table->u.color,
                       (int32)(nComps * table->nUsedSlots * sizeof(COLORVALUE))) )
      return FALSE;
  }
  return TRUE;
}

Bool bdt_tableFromDisk(BackdropTable *table, uint32 nComps, IM_FILES *file)
{
  if ( !im_fileread(file, (uint8*)table, (int32)sizeof(BackdropTable)) )
    return FALSE;

  bdt_reset(table, nComps);

  if ( !im_fileread(file, (uint8*)table->info,
                    (int32)(table->nUsedSlots * sizeof(COLORINFO))) )
    return FALSE;

  if ( table->type == BDT_OUTPUT8 ) {
    if ( !im_fileread(file, (uint8*)table->u.color8,
                      (int32)(nComps * table->nUsedSlots * sizeof(uint8))) )
      return FALSE;
  } else {
    if ( !im_fileread(file, (uint8*)table->u.color,
                      (int32)(nComps * table->nUsedSlots * sizeof(COLORVALUE))) )
      return FALSE;
  }

  return TRUE;
}

#if defined(DEBUG_BUILD)
void debug_print_bd_table(BackdropTable *table, uint32 nComps,
                          uint32 indent)
{
  uint32 i, iComp, iLast ;
#define MAX_INDENT 80
  char prefix[MAX_INDENT + 1] ;

  HQASSERT(table != NULL, "table is null");

  if ( indent > MAX_INDENT )
    indent = MAX_INDENT ;

  prefix[indent] = '\0' ;
  while ( indent > 0 )
    prefix[--indent] = ' ' ;

  switch ( table->type ) {
  case BDT_ISOLATED:
  case BDT_ISOLATED_SHAPE:
    monitorf((uint8 *)"%sBDT_ISOLATED %d slots [\n", prefix,
             table->nUsedSlots) ;
    for ( iLast = i = 0 ; i < table->nUsedSlots ; ++i ) {
      const COLORVALUE *interleave = &table->u.color[nComps * i] ;
      Bool same = FALSE ;

      /* Check if line matches previous. We always print the first and last
         slots. */
      if ( i > 0 && i + 1 < table->nUsedSlots ) {
        same = (table->alpha[i-1] == table->alpha[i] &&
                (table->shape == NULL || table->shape[i-1] == table->shape[i]) &&
                HqMemCmp(&interleave[-(int32)nComps], sizeof(COLORVALUE) * nComps,
                         &interleave[0], sizeof(COLORVALUE) * nComps) == 0 &&
                HqMemCmp(&table->info[i-1], (int32)sizeof(COLORINFO),
                         &table->info[i], (int32)sizeof(COLORINFO)) == 0) ;
      }

      if ( !same ) {
        char sep = '[' ;

        if ( iLast != i - 1 )
          monitorf((uint8 *)"%s ...\n", prefix) ;

        monitorf((uint8 *)"%s %d: A=%d ", prefix, i, table->alpha[i]) ;
        if ( table->shape != NULL )
          monitorf((uint8 *)"S= %d ", table->shape[i]) ;
        monitorf((uint8 *)"C=");
        for ( iComp = 0 ; iComp < nComps ; ++iComp ) {
          monitorf((uint8 *)"%c%d", sep, interleave[iComp]) ;
          sep = ' ' ;
        }
        /** \todo ajcd 2009-02-26: print backdrop info */
        monitorf((uint8 *)"]\n") ;

        iLast = i ;
      }
    }
    monitorf((uint8 *)"%s]\n", prefix) ;
    break ;
  case BDT_NONISOLATED:
  case BDT_NONISOLATED_SHAPE:
    monitorf((uint8 *)"%sBDT_NONISOLATED %d slots [\n", prefix,
             table->nUsedSlots) ;
    for ( iLast = i = 0 ; i < table->nUsedSlots ; ++i ) {
      const COLORVALUE *interleave = &table->u.color[nComps * i] ;
      Bool same = FALSE ;

      /* Check if line matches previous. We always print the first and last
         slots. */
      if ( i > 0 && i + 1 < table->nUsedSlots ) {
        same = (table->alpha[i-1] == table->alpha[i] &&
                table->groupAlpha[i-1] == table->groupAlpha[i] &&
                (table->shape == NULL || table->shape[i-1] == table->shape[i]) &&
                HqMemCmp(&interleave[-(int32)nComps], sizeof(COLORVALUE) * nComps,
                         &interleave[0], sizeof(COLORVALUE) * nComps) == 0 &&
                HqMemCmp(&table->info[i-1], (int32)sizeof(COLORINFO),
                         &table->info[i], (int32)sizeof(COLORINFO)) == 0) ;
      }

      if ( !same ) {
        char sep = '[' ;

        if ( iLast != i - 1 )
          monitorf((uint8 *)"%s ...\n", prefix) ;

        monitorf((uint8 *)"%s %d: GA=%d A=%d ", prefix, i,
                 table->groupAlpha[i], table->alpha[i]) ;
        if ( table->shape != NULL )
          monitorf((uint8 *)"S= %d ", table->shape[i]) ;
        monitorf((uint8 *)"C=");
        for ( iComp = 0 ; iComp < nComps ; ++iComp ) {
          monitorf((uint8 *)"%c%d", sep, interleave[iComp]) ;
          sep = ' ' ;
        }
        /** \todo ajcd 2009-02-26: print color info */
        monitorf((uint8 *)"]\n") ;

        iLast = i ;
      }
    }
    monitorf((uint8 *)"%s]\n", prefix) ;
    break ;
  case BDT_ALPHA:
    monitorf((uint8 *)"%sBDT_ALPHA %d slots [\n", prefix, table->nUsedSlots) ;
    for ( iLast = i = 0 ; i < table->nUsedSlots ; ++i ) {
      Bool same = FALSE ;

      /* Check if line matches previous. We always print the first and last
         slots. */
      if ( i > 0 && i + 1 < table->nUsedSlots ) {
        same = (table->alpha[i-1] == table->alpha[i]) ;
      }

      if ( !same ) {
        if ( iLast != i - 1 )
          monitorf((uint8 *)"%s ...\n", prefix) ;

        monitorf((uint8 *)"%s %d: A=%d\n", prefix, i, table->alpha[i]) ;

        iLast = i ;
      }
    }
    monitorf((uint8 *)"%s]\n", prefix) ;
    break ;
  case BDT_OUTPUT8:
    monitorf((uint8 *)"%sBDT_OUTPUT8 %d slots [\n", prefix,
             table->nUsedSlots) ;
    for ( iLast = i = 0 ; i < table->nUsedSlots ; ++i ) {
      const uint8 *interleave = &table->u.color8[nComps * i] ;
      Bool same = FALSE ;

      /* Check if line matches previous. We always print the first and last
         slots. */
      if ( i > 0 && i + 1 < table->nUsedSlots ) {
        same = (HqMemCmp(&interleave[-(int32)nComps], sizeof(uint8) * nComps,
                         &interleave[0], sizeof(uint8) * nComps) == 0 &&
                HqMemCmp(&table->info[i-1], (int32)sizeof(COLORINFO),
                         &table->info[i], (int32)sizeof(COLORINFO)) == 0) ;
      }

      if ( !same ) {
        char sep = '[' ;

        if ( iLast != i - 1 )
          monitorf((uint8 *)"%s ...\n", prefix) ;

        monitorf((uint8 *)"%s %d: C=", prefix, i) ;
        for ( iComp = 0 ; iComp < nComps ; ++iComp ) {
          monitorf((uint8 *)"%c%d", sep, interleave[iComp]) ;
          sep = ' ' ;
        }
        /** \todo ajcd 2009-02-26: print color info */
        monitorf((uint8 *)"]\n") ;

        iLast = i ;
      }
    }
    monitorf((uint8 *)"%s]\n", prefix) ;
    break ;
  case BDT_OUTPUT16:
    monitorf((uint8 *)"%sBDT_OUTPUT8 %d slots [\n", prefix,
             table->nUsedSlots) ;
    for ( iLast = i = 0 ; i < table->nUsedSlots ; ++i ) {
      const COLORVALUE *interleave = &table->u.color[nComps * i] ;
      Bool same = FALSE ;

      /* Check if line matches previous. We always print the first and last
         slots. */
      if ( i > 0 && i + 1 < table->nUsedSlots ) {
        same = (HqMemCmp(&interleave[-(int32)nComps], sizeof(COLORVALUE) * nComps,
                         &interleave[0], sizeof(COLORVALUE) * nComps) == 0 &&
                HqMemCmp(&table->info[i-1], (int32)sizeof(COLORINFO),
                         &table->info[i], (int32)sizeof(COLORINFO)) == 0) ;
      }

      if ( !same ) {
        char sep = '[' ;

        if ( iLast != i - 1 )
          monitorf((uint8 *)"%s ...\n", prefix) ;

        monitorf((uint8 *)"%s %d: C=", prefix, i) ;
        for ( iComp = 0 ; iComp < nComps ; ++iComp ) {
          monitorf((uint8 *)"%c%d", sep, interleave[iComp]) ;
          sep = ' ' ;
        }
        /** \todo ajcd 2009-02-26: print color info */
        monitorf((uint8 *)"]\n") ;

        iLast = i ;
      }
    }
    monitorf((uint8 *)"%s]\n", prefix) ;
    break ;
  default:
    monitorf((uint8 *)"%sUnknown BD table type %d\n", prefix, table->type) ;
    break ;
  }
}
#endif

/*
* Log stripped */
