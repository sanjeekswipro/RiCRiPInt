/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:jobmetrics.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of core job metrics.
 *
 * We could use the metrics API directly to count various values at
 * the job level but that would be inefficient. The metrics interface
 * has not been designed to be used that way. Instead, the expected
 * usage pattern is to have highly efficient counters and code in the
 * RIP (which have nothing to do with the metrics API) and turn those
 * counters into metrics when they are required. In the case of job
 * statistics, this is at the end of the job.
 *
 * Doing job statistics this way allows these metrics to be emitted
 * via any generic metrics interfaces which may well be accessible in
 * many different ways (via C, PS objects etc..).
 */

#include "core.h"
#include "coreinit.h"
#include "mm.h"
#include "metrics.h"
#include "jobmetrics.h"
#include "swcopyf.h"

#include "cce.h"        /* Blend modes */
#include "dl_foral.h"   /* DL_FORALL_INFO */
#include "displayt.h"   /* HDL, debug_opcode_names */
#include "hdl.h"        /* hdlEnclosingGroup */
#include "gu_chan.h"    /* guc_colorSpace */
#include "group.h"      /* groupGetAttrs() */
#include "namedef_.h"   /* NAME_ */

/** Singleton structure we use to count various job-level statistics.

    \todo ajcd 2010-11-30: These really aren't job-level, they're page level.
    When we enable DL pipelining, we'll need to count the stats separately.
    However, we also will want a job-level accumulator for the page-level
    stats, and this can perform that job. */
static struct JobInstrumentation {
  dl_metrics_t dl ;
} JobStats ;

/* Return a reference to the singleton DL metrics. Ultimately, this may go
   through the core context to get a suitable DL for the task in hand. */
dl_metrics_t *dl_metrics(void)
{
  return &JobStats.dl ;
}

/* Forward declaration. */
static Bool jobmetrics_update_transparency(sw_metrics_group *metrics) ;
static Bool rops_update_callback(uintptr_t hashkey, size_t counter, void *data) ;

static Bool jobmetrics_update(sw_metrics_group *metrics)
{
#define JOBSTATS_PEAK_POOL(name_, pool_) MACRO_START \
  if ( (pool_) != NULL ) { \
    size_t max_size = 0, max_frag = 0; \
    int32 max_objects ; \
    if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH(name_)) ) \
      return FALSE ; \
    mm_debug_total_highest((pool_), &max_size, &max_objects, &max_frag); \
    SW_METRIC_INTEGER("PeakPoolSize", CAST_SIZET_TO_INT32(max_size)) ; \
    SW_METRIC_INTEGER("PeakPoolObjects", max_objects) ; \
    SW_METRIC_INTEGER("PeakPoolFragmentation", CAST_SIZET_TO_INT32(max_frag)); \
    sw_metrics_close_group(&metrics) ; \
  } \
MACRO_END

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("DL")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Opcodes")) )
    return FALSE ;

  SW_METRIC_INTEGER("Erases", JobStats.dl.opcodes[RENDER_erase]) ;
  SW_METRIC_INTEGER("Chars", JobStats.dl.opcodes[RENDER_char]) ;
  SW_METRIC_INTEGER("Rects", JobStats.dl.opcodes[RENDER_rect]) ;
  SW_METRIC_INTEGER("Quads", JobStats.dl.opcodes[RENDER_quad]);
  SW_METRIC_INTEGER("Fills", JobStats.dl.opcodes[RENDER_fill]) ;
  SW_METRIC_INTEGER("Masks", JobStats.dl.opcodes[RENDER_mask]) ;
  SW_METRIC_INTEGER("Images", JobStats.dl.opcodes[RENDER_image]) ;
  SW_METRIC_INTEGER("Vignettes", JobStats.dl.opcodes[RENDER_vignette]) ;
  SW_METRIC_INTEGER("Gourauds", JobStats.dl.opcodes[RENDER_gouraud]) ;
  SW_METRIC_INTEGER("Shfills", JobStats.dl.opcodes[RENDER_shfill]) ;
  SW_METRIC_INTEGER("ShfillPatches", JobStats.dl.opcodes[RENDER_shfill_patch]) ;
  SW_METRIC_INTEGER("Hdls", JobStats.dl.opcodes[RENDER_hdl]) ;
  SW_METRIC_INTEGER("Groups", JobStats.dl.opcodes[RENDER_group]) ;
  SW_METRIC_INTEGER("Cells", JobStats.dl.opcodes[RENDER_cell]) ;
  sw_metrics_close_group(&metrics) ; /* Opcodes*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Store")) )
    return FALSE ;
  SW_METRIC_INTEGER("setg_count", JobStats.dl.store.setgCount) ;
  SW_METRIC_INTEGER("stateobject_count", JobStats.dl.store.stateCount) ;
  SW_METRIC_INTEGER("nfill_count", JobStats.dl.store.nfillCount) ;
  SW_METRIC_INTEGER("clip_count", JobStats.dl.store.clipCount) ;
  SW_METRIC_INTEGER("gstag_count", JobStats.dl.store.gstagCount) ;
  SW_METRIC_INTEGER("pattern_count", JobStats.dl.store.patternCount) ;
  SW_METRIC_INTEGER("softmask_count", JobStats.dl.store.softMaskCount) ;
  SW_METRIC_INTEGER("latecolor_count", JobStats.dl.store.latecolorCount) ;
  SW_METRIC_INTEGER("transparency_count", JobStats.dl.store.transparencyCount) ;
  SW_METRIC_INTEGER("hdl_count", JobStats.dl.store.hdlCount) ;
  SW_METRIC_INTEGER("pcl_count", JobStats.dl.store.pclCount) ;
  sw_metrics_close_group(&metrics) ; /* Store */

  sw_metrics_close_group(&metrics) ; /* DL */

  /* MM stats */
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Arena")) )
    return FALSE ;
  SW_METRIC_INTEGER("Committed",
                    CAST_SIZET_TO_INT32(mps_arena_committed(mm_arena))) ;
  SW_METRIC_INTEGER("CommitMax",
                    CAST_SIZET_TO_INT32(mps_arena_committed_max(mm_arena))) ;
  sw_metrics_close_group(&metrics) ;

  /* Temp pool */
  JOBSTATS_PEAK_POOL("Temp", mm_pool_temp) ;

  /* PS VM pools */
  JOBSTATS_PEAK_POOL("PSGlobal", mm_pool_ps_global) ;
  JOBSTATS_PEAK_POOL("PSLocal", mm_pool_ps_local) ;
  JOBSTATS_PEAK_POOL("PSTypedGlobal", mm_pool_ps_typed_global) ;
  JOBSTATS_PEAK_POOL("PSTypedLocal", mm_pool_ps_typed_local) ;

  /* Other PS related pools. */
  JOBSTATS_PEAK_POOL("Color", mm_pool_color) ;
  JOBSTATS_PEAK_POOL("ColorCache", mm_pool_coc) ;

  sw_metrics_close_group(&metrics) ; /* MM */

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Regions")) )
    return FALSE ;
  SW_METRIC_INTEGER("total", JobStats.dl.regions.total) ;
  SW_METRIC_INTEGER("backdrop_rendered", JobStats.dl.regions.backdropRendered) ;
  SW_METRIC_INTEGER("direct_rendered", JobStats.dl.regions.directRendered) ;
  sw_metrics_close_group(&metrics) ; /*Regions*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Group")) )
    return FALSE ;
  SW_METRIC_INTEGER("count", JobStats.dl.groups.groups);
  SW_METRIC_INTEGER("groups_eliminated", JobStats.dl.groups.groupsEliminated);
  SW_METRIC_INTEGER("groups_storing_shape", JobStats.dl.groups.groupsStoringShape);
  sw_metrics_close_group(&metrics) ; /*Group*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PCL")) )
    return FALSE ;

  SW_METRIC_INTEGER("patterned_objects", JobStats.dl.pcl.patternedObjects);

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Rops")) )
    return FALSE ;

  {
    int32 ropcount = CAST_SIZET_TO_INT32(sw_metric_hashtable_key_count(JobStats.dl.pcl.rops)) ;
    SW_METRIC_INTEGER("RopCount", ropcount);
  }

  if (! sw_metric_hashtable_enumerate(JobStats.dl.pcl.rops,
                                      rops_update_callback,
                                      metrics) )
    return FALSE ;

  sw_metrics_close_group(&metrics) ; /*Rops*/
  sw_metrics_close_group(&metrics) ; /*PCL*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Images")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("ColorSpace")) )
    return FALSE ;
  SW_METRIC_INTEGER("rgb", JobStats.dl.images.colorspaces.rgb);
  SW_METRIC_INTEGER("cmyk", JobStats.dl.images.colorspaces.cmyk);
  SW_METRIC_INTEGER("gray", JobStats.dl.images.colorspaces.gray);
  SW_METRIC_INTEGER("other", JobStats.dl.images.colorspaces.other);
  sw_metrics_close_group(&metrics) ; /*ColorSpace*/

  SW_METRIC_INTEGER("orthogonal", JobStats.dl.images.orthogonal);
  SW_METRIC_INTEGER("rotated", JobStats.dl.images.rotated);
  SW_METRIC_INTEGER("degenerate", JobStats.dl.images.degenerate);

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("TileCache")) )
    return FALSE ;
  SW_METRIC_INTEGER("tiles_cached", JobStats.dl.imagetiles.tiles_cached) ;
  SW_METRIC_INTEGER("tile_memory", JobStats.dl.imagetiles.tile_memory) ;
  SW_METRIC_INTEGER("possible_shapes", JobStats.dl.imagetiles.possible_shapes) ;
  SW_METRIC_INTEGER("actual_shapes", JobStats.dl.imagetiles.actual_shapes) ;
  if ( !sw_metric_histogram(metrics,
                            METRIC_NAME_AND_LENGTH("TimesUsed"),
                            &JobStats.dl.imagetiles.times_used.info) )
    return FALSE ;
  sw_metrics_close_group(&metrics) ; /*TileCache*/

  sw_metrics_close_group(&metrics) ; /*Images*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Transparency")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("BlendModes")) )
    return FALSE ;
  SW_METRIC_INTEGER("opaqueNormal", JobStats.dl.blendmodes.opaqueNormal);
  SW_METRIC_INTEGER("normal", JobStats.dl.blendmodes.normal);
  SW_METRIC_INTEGER("multiply", JobStats.dl.blendmodes.multiply);
  SW_METRIC_INTEGER("screen", JobStats.dl.blendmodes.screen);
  SW_METRIC_INTEGER("overlay", JobStats.dl.blendmodes.overlay);
  SW_METRIC_INTEGER("softLight", JobStats.dl.blendmodes.softLight);
  SW_METRIC_INTEGER("hardLight", JobStats.dl.blendmodes.hardLight);
  SW_METRIC_INTEGER("colorDodge", JobStats.dl.blendmodes.colorDodge);
  SW_METRIC_INTEGER("colorBurn", JobStats.dl.blendmodes.colorBurn);
  SW_METRIC_INTEGER("darken", JobStats.dl.blendmodes.darken);
  SW_METRIC_INTEGER("lighten", JobStats.dl.blendmodes.lighten);
  SW_METRIC_INTEGER("difference", JobStats.dl.blendmodes.difference);
  SW_METRIC_INTEGER("exclusion", JobStats.dl.blendmodes.exclusion);
  SW_METRIC_INTEGER("hue", JobStats.dl.blendmodes.hue);
  SW_METRIC_INTEGER("saturation", JobStats.dl.blendmodes.saturation);
  SW_METRIC_INTEGER("color", JobStats.dl.blendmodes.color);
  SW_METRIC_INTEGER("luminosity", JobStats.dl.blendmodes.luminosity);
  sw_metrics_close_group(&metrics) ; /*BlendModes*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("TransparencyAttributeCombinations")) )
    return FALSE ;
  if (! jobmetrics_update_transparency(metrics)) /* Detailed DL object metrics. */
    return FALSE ;
  sw_metrics_close_group(&metrics) ; /*TransparencyAttributeCombinations*/

  sw_metrics_close_group(&metrics) ; /*Transparency*/

  return TRUE ;
}

/* -------------------------------------------------------------------------
   Object transparency information.
   ------------------------------------------------------------------------- */

/* Destination *MUST* be of type uintptr_t. Bit counting starts from
   left (MSB) with the first bit index being zero. Put numbits_ at
   position startbit_ into dest_ using integer from val_. These macros
   work in this context as we know the key is all zeros. They are not
   intended as general SETBITS/GETBITS macro. */
#define SETBITS(dest_, startbit_, numbits_, val_) MACRO_START \
    ((dest_) |= (((uintptr_t)(val_)) << ( /* num bits to shift */ \
                                         (sizeof(uintptr_t) << 3) - (startbit_) - (numbits_))) ) ; \
MACRO_END

#define GETBITS(from_, startbit_, numbits_) \
    ( ((1 << (numbits_)) - 1) & /* mask for upper bits */ \
    (((uintptr_t)(from_)) >> ( /* num bits to shift */ \
                              (sizeof(uintptr_t) << 3) - (startbit_) - (numbits_))) )

/* We track the following attribute types. Their values are held
   within a uintptr_t, high bit first in the following order: */
#define ATTR_OBJECT_TYPE_STARTBIT           0 /* attribute 1,  2 bits */
#define ATTR_OBJECT_TYPE_NUMBITS            2
#define SETBITS_OBJECT_TYPE(k_, v_) SETBITS((k_), ATTR_OBJECT_TYPE_STARTBIT, ATTR_OBJECT_TYPE_NUMBITS, (v_))
#define GETBITS_OBJECT_TYPE(k_) GETBITS((k_), ATTR_OBJECT_TYPE_STARTBIT, ATTR_OBJECT_TYPE_NUMBITS)

#define ATTR_OBJECT_COLOR_SPACE_STARTBIT    2 /* attribute 2,  3 bits */
#define ATTR_OBJECT_COLOR_SPACE_NUMBITS     3
#define SETBITS_OBJECT_COLOR_SPACE(k_, v_) SETBITS((k_), ATTR_OBJECT_COLOR_SPACE_STARTBIT, ATTR_OBJECT_COLOR_SPACE_NUMBITS, (v_))
#define GETBITS_OBJECT_COLOR_SPACE(k_) GETBITS((k_), ATTR_OBJECT_COLOR_SPACE_STARTBIT, ATTR_OBJECT_COLOR_SPACE_NUMBITS)

#define ATTR_OBJECT_BLEND_SPACE_STARTBIT    5 /* attribute 3,  3 bits */
#define ATTR_OBJECT_BLEND_SPACE_NUMBITS     3
#define SETBITS_OBJECT_BLEND_SPACE(k_, v_) SETBITS((k_), ATTR_OBJECT_BLEND_SPACE_STARTBIT, ATTR_OBJECT_BLEND_SPACE_NUMBITS, (v_))
#define GETBITS_OBJECT_BLEND_SPACE(k_) GETBITS((k_), ATTR_OBJECT_BLEND_SPACE_STARTBIT, ATTR_OBJECT_BLEND_SPACE_NUMBITS)

#define ATTR_OBJECT_BLEND_MODE_STARTBIT     8 /* attribute 4,  4 bits */
#define ATTR_OBJECT_BLEND_MODE_NUMBITS      4
#define SETBITS_OBJECT_BLEND_MODE(k_, v_) SETBITS((k_), ATTR_OBJECT_BLEND_MODE_STARTBIT, ATTR_OBJECT_BLEND_MODE_NUMBITS, (v_))
#define GETBITS_OBJECT_BLEND_MODE(k_) GETBITS((k_), ATTR_OBJECT_BLEND_MODE_STARTBIT, ATTR_OBJECT_BLEND_MODE_NUMBITS)

#define ATTR_CONSTANT_ALPHA_STARTBIT       12 /* attribute 5,  1 bit  */
#define ATTR_CONSTANT_ALPHA_NUMBITS         1
#define SETBITS_CONSTANT_ALPHA(k_, v_) SETBITS((k_), ATTR_CONSTANT_ALPHA_STARTBIT, ATTR_CONSTANT_ALPHA_NUMBITS, (v_))
#define GETBITS_CONSTANT_ALPHA(k_) GETBITS((k_), ATTR_CONSTANT_ALPHA_STARTBIT, ATTR_CONSTANT_ALPHA_NUMBITS)

#define ATTR_ACTIVE_SOFTMASK_STARTBIT      13 /* attribute 6,  1 bit  */
#define ATTR_ACTIVE_SOFTMASK_NUMBITS        1
#define SETBITS_ACTIVE_SOFTMASK(k_, v_) SETBITS((k_), ATTR_ACTIVE_SOFTMASK_STARTBIT, ATTR_ACTIVE_SOFTMASK_NUMBITS, (v_))
#define GETBITS_ACTIVE_SOFTMASK(k_) GETBITS((k_), ATTR_ACTIVE_SOFTMASK_STARTBIT, ATTR_ACTIVE_SOFTMASK_NUMBITS)

#define ATTR_ISOLATED_OR_KNOCKOUT_STARTBIT 14 /* attribute 7,  1 bit  */
#define ATTR_ISOLATED_OR_KNOCKOUT_NUMBITS   1
#define SETBITS_ISOLATED_OR_KNOCKOUT(k_, v_) SETBITS((k_), ATTR_ISOLATED_OR_KNOCKOUT_STARTBIT, ATTR_ISOLATED_OR_KNOCKOUT_NUMBITS, (v_))
#define GETBITS_ISOLATED_OR_KNOCKOUT(k_) GETBITS((k_), ATTR_ISOLATED_OR_KNOCKOUT_STARTBIT, ATTR_ISOLATED_OR_KNOCKOUT_NUMBITS)

#define ATTR_ALPHA_IS_SHAPE_STARTBIT       15 /* attribute 8,  1 bit  */
#define ATTR_ALPHA_IS_SHAPE_NUMBITS         1
#define SETBITS_ALPHA_IS_SHAPE(k_, v_) SETBITS((k_), ATTR_ALPHA_IS_SHAPE_STARTBIT, ATTR_ALPHA_IS_SHAPE_NUMBITS, (v_))
#define GETBITS_ALPHA_IS_SHAPE(k_) GETBITS((k_), ATTR_ALPHA_IS_SHAPE_STARTBIT, ATTR_ALPHA_IS_SHAPE_NUMBITS)

#define ATTR_SOFTMASK_STARTBIT             16 /* attribute 9,  2 bits */
#define ATTR_SOFTMASK_NUMBITS               2
#define SETBITS_SOFTMASK(k_, v_) SETBITS((k_), ATTR_SOFTMASK_STARTBIT, ATTR_SOFTMASK_NUMBITS, (v_))
#define GETBITS_SOFTMASK(k_) GETBITS((k_), ATTR_SOFTMASK_STARTBIT, ATTR_SOFTMASK_NUMBITS)

#define ATTR_CONVERTED_TO_STARTBIT         18 /* attribute 10, 6 bits */
#define ATTR_CONVERTED_TO_NUMBITS           6
#define SETBITS_CONVERTED_TO(k_, v_) SETBITS((k_), ATTR_CONVERTED_TO_STARTBIT, ATTR_CONVERTED_TO_NUMBITS, (v_))
#define GETBITS_CONVERTED_TO(k_) GETBITS((k_), ATTR_CONVERTED_TO_STARTBIT, ATTR_CONVERTED_TO_NUMBITS)

/* How many bits from the uintptr_t are relevant. */
#define ATTR_BITS_USED                     24 /* keep me sane! Max can
                                                 not exceed 31 as we
                                                 count from zero */

#define DL_TRANS_HASH_TABLE_SIZE 1023 /* Size of the hash table we are
                                         going to allocate. */

/* 1. Object type: char, image, vector, shfill (4 types, so 2
   bits). */
#define ATTR_OBJECT_TYPE_CHAR   0x0
#define ATTR_OBJECT_TYPE_IMAGE  0x1
#define ATTR_OBJECT_TYPE_VECTOR 0x2
#define ATTR_OBJECT_TYPE_SHFILL 0x3
/* 2. Object colorspace: Gray, RGB, CMYK, ICC 3-channel, ICC
      4-channel, DeviceN (3 bits) */
#define ATTR_OBJECT_COLOR_SPACE_GRAY    0x0
#define ATTR_OBJECT_COLOR_SPACE_RGB     0x1
#define ATTR_OBJECT_COLOR_SPACE_CMYK    0x2
#define ATTR_OBJECT_COLOR_SPACE_ICC3    0x3
#define ATTR_OBJECT_COLOR_SPACE_ICC4    0x4
#define ATTR_OBJECT_COLOR_SPACE_DEVICEN 0x5
/* 3. Blend group for object: Gray, RGB, CMYK, ICC 3-channel, ICC
   4-channel, DeviceN (3 bits) */
#define ATTR_OBJECT_BLEND_SPACE_GRAY    0x0
#define ATTR_OBJECT_BLEND_SPACE_RGB     0x1
#define ATTR_OBJECT_BLEND_SPACE_CMYK    0x2
#define ATTR_OBJECT_BLEND_SPACE_ICC3    0x3
#define ATTR_OBJECT_BLEND_SPACE_ICC4    0x4
#define ATTR_OBJECT_BLEND_SPACE_DEVICEN 0x5
/* 4. Blend Mode: 4 bits */
#if 0 /* Not sure what this is. */
#define ATTR_OBJECT_BLEND_MODE_OPAQUENORMAL 0x0
#endif

#define ATTR_OBJECT_BLEND_MODE_NORMAL       0x0
#define ATTR_OBJECT_BLEND_MODE_MULTIPLY     0x1
#define ATTR_OBJECT_BLEND_MODE_SCREEN       0x2
#define ATTR_OBJECT_BLEND_MODE_OVERLAY      0x3
#define ATTR_OBJECT_BLEND_MODE_SOFTLIGHT    0x4
#define ATTR_OBJECT_BLEND_MODE_HARDLIGHT    0x5
#define ATTR_OBJECT_BLEND_MODE_COLORDODGE   0x6
#define ATTR_OBJECT_BLEND_MODE_COLORBURN    0x7
#define ATTR_OBJECT_BLEND_MODE_DARKEN       0x8
#define ATTR_OBJECT_BLEND_MODE_LIGHTEN      0x9
#define ATTR_OBJECT_BLEND_MODE_DIFFERENCE   0xA
#define ATTR_OBJECT_BLEND_MODE_EXCLUSION    0xB
#define ATTR_OBJECT_BLEND_MODE_HUE          0xC
#define ATTR_OBJECT_BLEND_MODE_SATURATION   0xD
#define ATTR_OBJECT_BLEND_MODE_COLOR        0xE
#define ATTR_OBJECT_BLEND_MODE_LUMINOSITY   0xF
/* 5. Constant alpha == 1.0: 1 bit */
#define ATTR_NO_CONSTANT_ALPHA  0x0 /* not 1.0 */
#define ATTR_HAS_CONSTANT_ALPHA 0x1 /*  is 1.0 */
/* 6. Soft mask active: 1 bit */
#define ATTR_NO_ACTIVE_SOFTMASK  0x0
#define ATTR_HAS_ACTIVE_SOFTMASK 0x1
/* 7. Isolated/KO group: 1 bit */
#define ATTR_NO_ISOLATED_OR_KNOCKOUT  0x0
#define ATTR_HAS_ISOLATED_OR_KNOCKOUT 0x1
/* 8. AlphaIsShape: 1 bit */
#define ATTR_NO_ALPHA_IS_SHAPE  0x0
#define ATTR_HAS_ALPHA_IS_SHAPE 0x1
/* 9. In softmask: No, Alpha, Luminosity: 2 bits */
#define ATTR_NO_SOFTMASK         0x0
#define ATTR_ALPHA_SOFTMASK      0x1
#define ATTR_LUMINOSITY_SOFTMASK 0x2
/*10. Converted to: Gray, RGB, CMYK, ICC 3-channel, ICC 4-channel,
      DeviceN (this is a set of all the colorspaces the object was
      converted through, so 6 bits) */
#define ATTR_CONVERTED_TO_GRAY    0x01  /* 000001 */
#define ATTR_CONVERTED_TO_RGB     0x02  /* 000010 */
#define ATTR_CONVERTED_TO_CMYK    0x04  /* 000100 */
#define ATTR_CONVERTED_TO_ICC3    0x08  /* 001000 */
#define ATTR_CONVERTED_TO_ICC4    0x10  /* 010000 */
#define ATTR_CONVERTED_TO_DEVICEN 0x20  /* 100000 */

#define SYNTHESISED_ELEMENT_NAME_MAX_LEN (SW_METRIC_MAX_OUT_LENGTH + 1) /* Allow for terminating NULL. */

#define EXTEND_ELEMENT_NAME(buf_, str_) \
  AssertedStrCat( (buf_), SYNTHESISED_ELEMENT_NAME_MAX_LEN, METRIC_NAME_AND_LENGTH(str_) )

static void AssertedStrCat(char *dest, size_t dest_buf_size, char *src, size_t src_len)
{
  size_t dest_used = strlen(dest) ;

  /* Allow for terminating NULL. */
  if ( (dest_used + src_len + 1) > dest_buf_size) {
    HQASSERT(0, "Appending this string would exceed destination buffer size. Ignored.") ;
    return ;
  }
  strncat(dest, (const char *)src, src_len) ;
}

/* Gets called back per object transparency combination. We use this
   to synthesise XML element names. This is not ideal as typically one
   would use XML attributes for this sort of thing (if designing good
   XML) but as it happens, synthesising long element names makes this
   data easier to analyse in something like Excel.  */
static Bool jobmetrics_update_transparency_callback(uintptr_t hashkey,
                                                    size_t counter, void *data)
{
  sw_metrics_group *metrics = data ;
  uintptr_t val ;
  static char element_name[SYNTHESISED_ELEMENT_NAME_MAX_LEN] ;
  int32 num = CAST_SIZET_TO_INT32(counter) ;

  /* This function effectively decodes the hash key bits into a
     readable XML element name. */
  element_name[0] = '\0' ; /* Initialise as an empty string. */

  /* 1. Object type. */
  val = GETBITS_OBJECT_TYPE(hashkey) ;
  switch (val) {
  case ATTR_OBJECT_TYPE_CHAR:
    EXTEND_ELEMENT_NAME(element_name, "Char_") ;
    break ;
  case ATTR_OBJECT_TYPE_IMAGE:
    EXTEND_ELEMENT_NAME(element_name, "Imag_") ;
    break ;
  case ATTR_OBJECT_TYPE_VECTOR:
    EXTEND_ELEMENT_NAME(element_name, "Vect_") ;
    break ;
  case ATTR_OBJECT_TYPE_SHFILL:
    EXTEND_ELEMENT_NAME(element_name, "Shfi_") ;
    break ;
  }
  /* 2. Object colorspace. */
  val = GETBITS_OBJECT_COLOR_SPACE(hashkey) ;
  switch (val) {
  case ATTR_OBJECT_COLOR_SPACE_GRAY:
    EXTEND_ELEMENT_NAME(element_name, "GRAY_") ;
    break ;
  case ATTR_OBJECT_COLOR_SPACE_RGB:
    EXTEND_ELEMENT_NAME(element_name, "RGB_") ;
    break ;
  case ATTR_OBJECT_COLOR_SPACE_CMYK:
    EXTEND_ELEMENT_NAME(element_name, "CMYK_") ;
    break ;
  case ATTR_OBJECT_COLOR_SPACE_ICC3:
    EXTEND_ELEMENT_NAME(element_name, "ICC3_") ;
    break ;
  case ATTR_OBJECT_COLOR_SPACE_ICC4:
    EXTEND_ELEMENT_NAME(element_name, "ICC4_") ;
    break ;
  case ATTR_OBJECT_COLOR_SPACE_DEVICEN:
    EXTEND_ELEMENT_NAME(element_name, "DEVN_") ;
    break ;
  }

  /* 3. Blend group for object. */
  val = GETBITS_OBJECT_BLEND_SPACE(hashkey) ;
  switch (val) {
  case ATTR_OBJECT_BLEND_SPACE_GRAY:
    EXTEND_ELEMENT_NAME(element_name, "GRAY_") ;
    break ;
  case ATTR_OBJECT_BLEND_SPACE_RGB:
    EXTEND_ELEMENT_NAME(element_name, "RGB_") ;
    break ;
  case ATTR_OBJECT_BLEND_SPACE_CMYK:
    EXTEND_ELEMENT_NAME(element_name, "CMYK_") ;
    break ;
  case ATTR_OBJECT_BLEND_SPACE_ICC3:
    EXTEND_ELEMENT_NAME(element_name, "ICC3_") ;
    break ;
  case ATTR_OBJECT_BLEND_SPACE_ICC4:
    EXTEND_ELEMENT_NAME(element_name, "ICC4_") ;
    break ;
  case ATTR_OBJECT_BLEND_SPACE_DEVICEN:
    EXTEND_ELEMENT_NAME(element_name, "DEVN_") ;
    break ;
  }

  /* 4. Blend Mode. */
  val = GETBITS_OBJECT_BLEND_MODE(hashkey) ;
  switch (val) {
  case ATTR_OBJECT_BLEND_MODE_NORMAL:
    EXTEND_ELEMENT_NAME(element_name, "Normal_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_MULTIPLY:
    EXTEND_ELEMENT_NAME(element_name, "Multiply_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_SCREEN:
    EXTEND_ELEMENT_NAME(element_name, "Screen_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_OVERLAY:
    EXTEND_ELEMENT_NAME(element_name, "Overlay_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_SOFTLIGHT:
    EXTEND_ELEMENT_NAME(element_name, "Softlight_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_HARDLIGHT:
    EXTEND_ELEMENT_NAME(element_name, "Hardlight_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_COLORDODGE:
    EXTEND_ELEMENT_NAME(element_name, "Colordodge_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_COLORBURN:
    EXTEND_ELEMENT_NAME(element_name, "Colorburn_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_DARKEN:
    EXTEND_ELEMENT_NAME(element_name, "Darken_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_LIGHTEN:
    EXTEND_ELEMENT_NAME(element_name, "Lighten_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_DIFFERENCE:
    EXTEND_ELEMENT_NAME(element_name, "Difference_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_EXCLUSION:
    EXTEND_ELEMENT_NAME(element_name, "Exclusion_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_HUE:
    EXTEND_ELEMENT_NAME(element_name, "Hue_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_SATURATION:
    EXTEND_ELEMENT_NAME(element_name, "Saturation_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_COLOR:
    EXTEND_ELEMENT_NAME(element_name, "Color_") ;
    break ;
  case ATTR_OBJECT_BLEND_MODE_LUMINOSITY:
    EXTEND_ELEMENT_NAME(element_name, "Luminosity_") ;
    break ;
  }

  /* 5. Constant alpha. */
  val = GETBITS_CONSTANT_ALPHA(hashkey) ;
  if (val == ATTR_HAS_CONSTANT_ALPHA) {
    EXTEND_ELEMENT_NAME(element_name, "NAlpha_") ; /* is 1.0 */
  } else {
    EXTEND_ELEMENT_NAME(element_name, "Alpha_") ;  /* not 1.0 */
  }

  /* 6. Soft mask active. */
  val = GETBITS_ACTIVE_SOFTMASK(hashkey) ;
  if (val == ATTR_NO_ACTIVE_SOFTMASK) {
    EXTEND_ELEMENT_NAME(element_name, "NMask_") ;
  } else {
    EXTEND_ELEMENT_NAME(element_name, "Mask_") ;
  }

  /* 7. Isolated/KO group. */
  val = GETBITS_ISOLATED_OR_KNOCKOUT(hashkey) ;
  if (val == ATTR_NO_ISOLATED_OR_KNOCKOUT) {
    EXTEND_ELEMENT_NAME(element_name, "NIsolatedOrKnockout_") ;
  } else {
    EXTEND_ELEMENT_NAME(element_name, "IsolatedOrKnockout_") ;
  }

  /* 8. AlphaIsShape. */
  val = GETBITS_ALPHA_IS_SHAPE(hashkey) ;
  if (val == ATTR_NO_ALPHA_IS_SHAPE) {
    EXTEND_ELEMENT_NAME(element_name, "NAlphaIsShape_") ;
  } else {
    EXTEND_ELEMENT_NAME(element_name, "AlphaIsShape_") ;
  }

  /* 9. In softmask: No, Alpha, Luminosity. */
  val = GETBITS_SOFTMASK(hashkey) ;
  switch (val) {
  case ATTR_NO_SOFTMASK:
    EXTEND_ELEMENT_NAME(element_name, "NSoftMask_") ;
    break ;
  case ATTR_ALPHA_SOFTMASK:
    EXTEND_ELEMENT_NAME(element_name, "AlphaSoftMask_") ;
    break ;
  case ATTR_LUMINOSITY_SOFTMASK:
    EXTEND_ELEMENT_NAME(element_name, "LuminositySoftMask_") ;
    break ;
  }

  /*10. Converted to. */
  {
    Bool at_least_one_conversion = FALSE ;
    val = GETBITS_CONVERTED_TO(hashkey) ;
    EXTEND_ELEMENT_NAME(element_name, "ConvertedTo-") ;

    if (val & ATTR_CONVERTED_TO_GRAY) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "GRAY") ;
      at_least_one_conversion = TRUE ;
    }
    if (val & ATTR_CONVERTED_TO_RGB) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "RGB") ;
      at_least_one_conversion = TRUE ;
    }
    if (val & ATTR_CONVERTED_TO_CMYK) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "CMYK") ;
      at_least_one_conversion = TRUE ;
    }
    if (val & ATTR_CONVERTED_TO_ICC3) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "ICC3") ;
      at_least_one_conversion = TRUE ;
    }
    if (val & ATTR_CONVERTED_TO_ICC4) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "ICC4") ;
      at_least_one_conversion = TRUE ;
    }
    if (val & ATTR_CONVERTED_TO_DEVICEN) {
      if (at_least_one_conversion)
        EXTEND_ELEMENT_NAME(element_name, "-") ;
      EXTEND_ELEMENT_NAME(element_name, "DEVN") ;
      at_least_one_conversion = TRUE ;
    }
  }

  /* Simply a terminator. */
  EXTEND_ELEMENT_NAME(element_name, "_E") ;

  { /* We have the name, update the XML metrics. */
    size_t element_name_len = strlen(element_name) ;
    if (element_name_len > 0) {
      if (! sw_metric_integer(metrics, element_name, element_name_len, num))
        return FALSE;
    }
  }

  return TRUE ;
}

static Bool jobmetrics_update_transparency(sw_metrics_group *metrics)
{
  HQASSERT(JobStats.dl.trans_attributes != NULL, "trans_attributes is NULL") ;

  {
    int32 key_count = CAST_SIZET_TO_INT32(sw_metric_hashtable_key_count(JobStats.dl.trans_attributes)) ;
    SW_METRIC_INTEGER("CombinationCount", key_count);
  }

  if (! sw_metric_hashtable_enumerate(JobStats.dl.trans_attributes,
                                      jobmetrics_update_transparency_callback,
                                      metrics) )
    return FALSE ;

  return TRUE ;
}

Bool dl_transparency_is_icc_colorspace(OBJECT *colorSpace)
{
  HQASSERT(colorSpace != NULL, "colorSpace is NULL") ;

  /* DeviceN color spaces are not fully constructed from a call
     to gsc_currentcolorspace() so we should not make a call to
     gsc_getcolorspacetype() when we have a DeviceN color space
     because a PS error will be raised. */
  if (oType(*colorSpace) == ONAME && oName(*colorSpace) != &system_names[ NAME_DeviceN ]) {
    COLORSPACE_ID colorspaceID;

    if (gsc_getcolorspacetype(colorSpace, &colorspaceID) && colorspaceID == SPACE_ICCBased) {
      return TRUE ;
    }
  }
  return FALSE ;
}


static Bool populate_dl_transparency_metrics_hashtable_callback(DL_FORALL_INFO *info)
{
  LISTOBJECT *lobj = info->lobj;

  if ( lobj->opcode == RENDER_erase || lobj->opcode == RENDER_void ||
       /* lobj->opcode == RENDER_shfill_patch || */
       lobj->objectstate == NULL || lobj->objectstate->tranAttrib == NULL) {
    return TRUE;
  }

  {
    HDL *hdl = info->hdl;
    Group *group = hdlEnclosingGroup(hdl);
    const GroupAttrs *attrs = groupGetAttrs(group);
    sw_metric_hashtable *trans_table = info->data; /* The hash table which tracks the metrics. */
    uintptr_t trans_hash_key = 0 ; /* The key we are building. */

    /* 1. Object type. */
    switch ( DISPOSITION_REPRO_TYPE_UNMAPPED(lobj->disposition) ) {
      case REPRO_TYPE_PICTURE:
        SETBITS_OBJECT_TYPE(trans_hash_key, ATTR_OBJECT_TYPE_IMAGE) ;
        break;
      case REPRO_TYPE_TEXT:
        SETBITS_OBJECT_TYPE(trans_hash_key, ATTR_OBJECT_TYPE_CHAR) ;
        break;
      case REPRO_TYPE_VIGNETTE:
        SETBITS_OBJECT_TYPE(trans_hash_key, ATTR_OBJECT_TYPE_SHFILL) ;
        break;
      case REPRO_TYPE_OTHER:
        SETBITS_OBJECT_TYPE(trans_hash_key, ATTR_OBJECT_TYPE_VECTOR) ;
        break;
    }

    /* 2. Object colorspace. */
    HQASSERT( lobj->objectstate->lateColorAttrib != NULL, "lateColorAttrib is missing" );
    if ( lobj->objectstate->lateColorAttrib ) {

      if (lobj->objectstate->lateColorAttrib->is_icc) {
        switch (lobj->objectstate->lateColorAttrib->origColorModel) {
        case REPRO_COLOR_MODEL_RGB:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_ICC3) ;
          break;
        case REPRO_COLOR_MODEL_CMYK:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_ICC4) ;
          break;
        }
      } else {
        switch (lobj->objectstate->lateColorAttrib->origColorModel) {
        case REPRO_COLOR_MODEL_GRAY:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_GRAY) ;
          break;
        case REPRO_COLOR_MODEL_RGB:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_RGB) ;
          break;
        case REPRO_COLOR_MODEL_CMYK:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_CMYK) ;
          break;
        case REPRO_N_COLOR_MODELS:
          SETBITS_OBJECT_COLOR_SPACE(trans_hash_key, ATTR_OBJECT_COLOR_SPACE_DEVICEN) ;
          break;
        /* Other cases we are not interested in. */
        }
      }
    }

    /* 3. Blend group for object. */
    {
      Bool is_icc ;
      int32 nColorants = 0 ;
      DEVICESPACEID deviceSpaceId ;
      OBJECT colorSpace = OBJECT_NOTVM_NOTHING ;
      const GUCR_RASTERSTYLE *pRasterStyle = groupInputRasterStyle(group) ;

      guc_deviceColorSpace(pRasterStyle, &deviceSpaceId, &nColorants) ;
      guc_colorSpace(pRasterStyle, &colorSpace) ;

      is_icc = dl_transparency_is_icc_colorspace(&colorSpace) ;

      if (is_icc) {
        if (nColorants == 3) {
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_ICC3) ;
        } else if (nColorants == 4) {
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_ICC4) ;
        }
      } else {
        switch (deviceSpaceId) {
        case DEVICESPACE_Gray:
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_GRAY) ;
          break ;
        case DEVICESPACE_CMYK:
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_CMYK) ;
          break ;
        case DEVICESPACE_RGB:
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_RGB) ;
          break ;
        case DEVICESPACE_N:
          SETBITS_OBJECT_BLEND_SPACE(trans_hash_key, ATTR_OBJECT_BLEND_SPACE_DEVICEN) ;
          break;
        }
      }
    }

    /* 4. Blend Mode. */
    switch (lobj->objectstate->tranAttrib->blendMode) {
    case CCEModeNormal:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_NORMAL) ;
      break;
    case CCEModeMultiply:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_MULTIPLY) ;
      break;
    case CCEModeScreen:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_SCREEN) ;
      break;
    case CCEModeOverlay:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_OVERLAY) ;
      break;
    case CCEModeSoftLight:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_SOFTLIGHT) ;
      break;
    case CCEModeHardLight:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_HARDLIGHT) ;
      break;
    case CCEModeColorDodge:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_COLORDODGE) ;
      break;
    case CCEModeColorBurn:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_COLORBURN) ;
      break;
    case CCEModeDarken:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_DARKEN) ;
      break;
    case CCEModeLighten:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_LIGHTEN) ;
      break;
    case CCEModeDifference:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_DIFFERENCE) ;
      break;
    case CCEModeExclusion:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_EXCLUSION) ;
      break;
    case CCEModeHue:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_HUE) ;
      break;
    case CCEModeSaturation:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_SATURATION) ;
      break;
    case CCEModeColor:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_COLOR) ;
      break;
    case CCEModeLuminosity:
      SETBITS_OBJECT_BLEND_MODE(trans_hash_key, ATTR_OBJECT_BLEND_MODE_LUMINOSITY) ;
      break;
    }

    /* 5. Constant alpha. */
    if (lobj->objectstate->tranAttrib->alpha == 65280) { /* Constant alpha == 1.0 */
      SETBITS_CONSTANT_ALPHA(trans_hash_key, ATTR_HAS_CONSTANT_ALPHA) ;
    } else {
      SETBITS_CONSTANT_ALPHA(trans_hash_key, ATTR_NO_CONSTANT_ALPHA) ;
    }

    /* 6. Soft mask active. */
    if (lobj->objectstate->tranAttrib->softMask != NULL) {
      SETBITS_ACTIVE_SOFTMASK(trans_hash_key, ATTR_HAS_ACTIVE_SOFTMASK) ;
    } else {
      SETBITS_ACTIVE_SOFTMASK(trans_hash_key, ATTR_NO_ACTIVE_SOFTMASK) ;
    }

    /* 7. Isolated/KO group. */
    if (attrs != NULL && (attrs->isolated || attrs->knockout)) {
      SETBITS_ISOLATED_OR_KNOCKOUT(trans_hash_key, ATTR_HAS_ISOLATED_OR_KNOCKOUT) ;
    } else {
      SETBITS_ISOLATED_OR_KNOCKOUT(trans_hash_key, ATTR_NO_ISOLATED_OR_KNOCKOUT) ;
    }

    /* 8. AlphaIsShape. */
    if (lobj->objectstate->tranAttrib->alphaIsShape) {
      SETBITS_ALPHA_IS_SHAPE(trans_hash_key, ATTR_HAS_ALPHA_IS_SHAPE) ;
    } else {
      SETBITS_ALPHA_IS_SHAPE(trans_hash_key, ATTR_NO_ALPHA_IS_SHAPE) ;
    }

    /* 9. In softmask: No, Alpha, Luminosity. */
    if (attrs != NULL) {
      switch (attrs->softMaskType) {
      case EmptySoftMask:
        SETBITS_SOFTMASK(trans_hash_key, ATTR_NO_SOFTMASK) ;
        break;
      case AlphaSoftMask:
        SETBITS_SOFTMASK(trans_hash_key, ATTR_ALPHA_SOFTMASK) ;
        break;
      case LuminositySoftMask:
        SETBITS_SOFTMASK(trans_hash_key, ATTR_LUMINOSITY_SOFTMASK) ;
        break;
      default:
        break;
      }
    }

    /*10. Converted to. */
    {
      Group *parent = group ; /* Containing group. */
      int32 mask = 0 ;

      while (parent != NULL) {
        Bool is_icc ;
        const GUCR_RASTERSTYLE *pRasterStyle = groupOutputRasterStyle(parent) ;
        int32 nColorants = 0 ;
        DEVICESPACEID deviceSpaceId ;
        OBJECT colorSpace = OBJECT_NOTVM_NOTHING ;

        if (pRasterStyle != NULL) {
          guc_deviceColorSpace(pRasterStyle, &deviceSpaceId, &nColorants) ;
          guc_colorSpace(pRasterStyle, &colorSpace) ;

          is_icc = dl_transparency_is_icc_colorspace(&colorSpace) ;

          if (is_icc) {
            if (nColorants == 3) {
              mask |= ATTR_CONVERTED_TO_ICC3 ;
            } else if (nColorants == 4) {
              mask |= ATTR_CONVERTED_TO_ICC4 ;
            }
          } else {
            switch (deviceSpaceId) {
            case DEVICESPACE_Gray:
              mask |= ATTR_CONVERTED_TO_GRAY ;
              break ;
            case DEVICESPACE_CMYK:
              mask |= ATTR_CONVERTED_TO_CMYK ;
              break ;
            case DEVICESPACE_RGB:
              mask |= ATTR_CONVERTED_TO_RGB ;
              break ;
            case DEVICESPACE_N:
              mask |= ATTR_CONVERTED_TO_DEVICEN ;
              break;
            }
          }
        }

        parent = groupParent(parent) ;
      }

      SETBITS_CONVERTED_TO(trans_hash_key, mask) ;
    }

    /* Update transparency metrics hash table entry. */
    if (! sw_metric_hashtable_increment_key_counter(trans_table, trans_hash_key))
      return FALSE ;
  }

  return TRUE;
}

Bool populate_dl_transparency_metrics_hashtable(void)
{
  corecontext_t *context = get_core_context() ;
  DL_STATE *page = context->page ;
  DL_FORALL_INFO info = {0} ;
  HDL *hdl ;

  info.page = page;
  info.inflags = DL_FORALL_USEMARKER | DL_FORALL_PATTERN |
                 DL_FORALL_SOFTMASK | DL_FORALL_SHFILL  |
                 DL_FORALL_GROUP | DL_FORALL_NONE;

  info.data = JobStats.dl.trans_attributes ; /* Transparency metrics hash table. */

  hdl = dlPageHDL(page);
  if (hdl != NULL) {
    info.hdl = hdl ;
    if (! dl_forall(&info, populate_dl_transparency_metrics_hashtable_callback) )
      return FALSE;
  }

  /* At this stage, the pages currentHDL has not been closed and hence
     might be empty, so we need to also traverse the currentGroup HDL
     as well. They are mutually exclusive. */
  if (page->currentGroup != NULL) {
    hdl = groupHdl(page->currentGroup) ;
    if (hdl != NULL) {
      info.hdl = hdl ;
      if (! dl_forall(&info, populate_dl_transparency_metrics_hashtable_callback) )
        return FALSE;
    }
  }

  return TRUE ;
}

static uint32 transparency_metrics_hash(uintptr_t hashkey)
{
  uint32 pos ;
  pos = (uint32)( (hashkey >> ((sizeof(uintptr_t) << 3) - (uintptr_t)ATTR_BITS_USED))
                   % DL_TRANS_HASH_TABLE_SIZE ) ;
  return pos ;
}

#define ROPS_HASH_TABLE_SIZE 127 /* Deliberately not a power of two. */

static uint32 rops_metrics_hash(uintptr_t hashkey)
{
  return (uint32)(hashkey % ROPS_HASH_TABLE_SIZE) ;
}

static Bool rops_update_callback(uintptr_t hashkey, size_t counter, void *data)
{
  sw_metrics_group *metrics = data ;
  static char element_name[SYNTHESISED_ELEMENT_NAME_MAX_LEN] ;
  int32 num = CAST_SIZET_TO_INT32(counter) ;
  uint8 rop = (uint8)hashkey ;
  const char *usage = (hashkey & ROP_METRIC_BACKDROP) ? "Backdrop" :
    (hashkey & ROP_METRIC_DIRECT) ? "Direct" :
    "Original" ;
  const char *strans = (hashkey & ROP_METRIC_S_TRANS) ? "_STrans" : "" ;
  const char *ptrans = (hashkey & ROP_METRIC_P_TRANS) ? "_PTrans" : "" ;
  const char *foreground = (hashkey & ROP_METRIC_F_WHITE) ? "_FWhite" :
     (hashkey & ROP_METRIC_F_WHITE) ? "_FBlack" : "" ;

  swcopyf((uint8 *)element_name, (uint8 *)"ROP_%d_%s%s%s%s", (int32)rop,
          usage, strans, ptrans, foreground) ;
  return sw_metric_integer(metrics, element_name, strlen(element_name), num) ;
}

static void jobmetrics_reset(int reason)
{
  struct JobInstrumentation statsInit = {0} ;

  if ( reason != SW_METRICS_RESET_BOOT ) {
    /* Retain hashtables across resets */
    statsInit.dl.trans_attributes = JobStats.dl.trans_attributes ;
    statsInit.dl.pcl.rops = JobStats.dl.pcl.rops ;
  }

  JobStats = statsInit;

  sw_metric_histogram_reset(&JobStats.dl.imagetiles.times_used.info,
                            SW_METRIC_HISTOGRAM_SIZE(JobStats.dl.imagetiles.times_used),
                            SW_METRIC_HISTOGRAM_LINEAR,
                            /* Linear mapping with 1:1 correspondence */
                            1, SW_METRIC_HISTOGRAM_SIZE(JobStats.dl.imagetiles.times_used) + 1) ;

  if (JobStats.dl.trans_attributes != NULL) {
    sw_metric_hashtable_reset(JobStats.dl.trans_attributes) ;
  }

  if (JobStats.dl.pcl.rops != NULL) {
    sw_metric_hashtable_reset(JobStats.dl.pcl.rops) ;
  }
}

static sw_metrics_callbacks jobmetrics_hook = {
  jobmetrics_update,
  jobmetrics_reset,
  NULL
} ;

/* Reset those globals. */
static void init_C_globals_jobmetrics(void)
{
  jobmetrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&jobmetrics_hook) ;
}

/* Job metrics initialisation. */
static Bool sw_jobmetrics_postboot(void)
{
  if ( !sw_metric_hashtable_create(&JobStats.dl.trans_attributes,
                                   DL_TRANS_HASH_TABLE_SIZE,
                                   transparency_metrics_hash,
                                   NULL /* Key does not need deallocation. */) )
    return FALSE ;

  if ( !sw_metric_hashtable_create(&JobStats.dl.pcl.rops,
                                   ROPS_HASH_TABLE_SIZE,
                                   rops_metrics_hash,
                                   NULL /* Key does not need deallocation. */))
    return FALSE ;

  return TRUE ;
}

/* Job metrics shutdown. */
static void sw_jobmetrics_finish(void)
{
  if (JobStats.dl.trans_attributes != NULL)
    sw_metric_hashtable_destroy(&JobStats.dl.trans_attributes) ;

  if (JobStats.dl.pcl.rops != NULL)
    sw_metric_hashtable_destroy(&JobStats.dl.pcl.rops) ;
}

/** Module runtime initialisation */
void sw_jobmetrics_C_globals(core_init_fns *fns)
{
  init_C_globals_jobmetrics() ;
  fns->postboot = sw_jobmetrics_postboot ;
  fns->finish = sw_jobmetrics_finish ;
}

/* ============================================================================
* Log stripped */
