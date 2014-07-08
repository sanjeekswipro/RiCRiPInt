/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5context.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for pcl5exec context.
 *
 * Although the context is called "PCL5Context", it does not
 * necessarily mean that the PS operator "pcl5exec" is running. It may
 * be an "pcl5open" or friend. None the less, a PCL5 context will
 * still exist, hence the name.
 */

#ifndef __PCL5_CONTEXT_H__
#define __PCL5_CONTEXT_H__

#include "pcl5.h"
#include "pcl5types.h"

#include "lists.h"
#include "objectt.h"
#include "pcl5resources.h"
#include "pcl.h"

/* Resources which are referenced by a numeric ID. */
typedef struct PCL5IdCache PCL5IdCache ;

/* Resources which are referenced by string ID. */
typedef struct PCL5StringIdCache PCL5StringIdCache ;

/* Various PCL5 resource caches. */
typedef struct {
  /* Builtin shading patterns. */
  PCL5IdCache* shading ;
  /* Builtin cross hatch patterns. */
  PCL5IdCache* cross_hatch ;
  /* Hash of numeric ID's to the PCL5 user pattern. */
  PCL5IdCache* user ;
  /* Hash of numeric ID's to the HPGL2 user pattern. */
  PCL5IdCache* hpgl2_user ;
  /* Hash of numeric ID's to the macro. */
  PCL5IdCache* macro ;
  /* Hash of string ID's to the macro. */
  PCL5StringIdCache* string_macro ;
  /* Hash of numeric ID's to font string ID's. */
  PCL5IdCache* aliased_font ;
  /* Hash of font string ID's to font string ID's. */
  PCL5StringIdCache* aliased_string_font ;
} PCL5ResourceCaches ;

/* PCL5 master context. Exists for the lifetime of the RIP
   (i.e. Across job boundaries). */
typedef struct PCL5_RIP_LifeTime_Context {
  pcl5_contextid_t  next_id;      /* Unsigned non-zero integer */
  sll_list_t        sls_contexts; /* List of active contexts */

  /* State which is shared across contexts. */
  PCL5ResourceCaches resource_caches ;
} PCL5_RIP_LifeTime_Context ;

struct PCL5ConfigParams {
  uint32    default_page_copies;
  uint8     backchannel[LONGESTDEVICENAME + LONGESTFILENAME + 2];
  uint32    backchannel_len;
  uint32    dark_courier;
  uint32    default_font_number;
  uint32    default_font_source;
  PCL5Real  default_pitch;
  PCL5Real  default_point_size;
  uint32    default_symbol_set;
  uint32    default_line_termination;
  Bool      pcl5c_enabled;
  Bool      two_byte_char_support_enabled;
  int32     vds_select ;
};

typedef struct PCL5ConfigParams PCL5ConfigParams;

/* Opaque definition. See pcl5context_private.h for concrete type. */
typedef struct PCL5Context PCL5Context;

typedef struct PCL5FunctTable PCL5FunctTable;

/* PCL5 operator */
typedef Bool (*PCL5Operator)(PCL5Context *pcl5_ctxt,
                             int32 explicit_sign, PCL5Numeric value) ;

#if defined(DEBUG_BUILD)
extern int32 debug_pcl5;

/** Debug bitmask flags for PCL5. */
enum {
  PCL5_CONTROL = 0x01,
  PCL5_MARGINS = 0x02,
  PCL5_IMAGEOPS = 0x04, /* Output only image ops in sequence */
  PCL5_NOMASK = 0x08 /* Don't convert source transparent images to masks */
} ;
#endif /* DEBUG_BUILD */

#define PCL5_CONTEXT_NAME "PCL5 Context" /* for VERIFY_OBJECT() */

/* Some commands are only allowed in some modes so we tell the PCL5
   interpreter pcl5_execops() which mode the interpreter should be
   in. The interpreter may only be in one mode. */
#define MODE_NOT_SET (0x00)
#define PCL5C_MODE (0x01)
#define PCL5E_MODE (0x02)
#define HPGL2_MODE (0x04)
#define PCL5C_MACRO_MODE (0x10)
#define PCL5E_MACRO_MODE (0x20)
#define RASTER_GRAPHICS_MODE (0x40)
#define TRANSPARENT_PRINT_MODE (0x80)
#define POLYGON_MODE (0x100)
#define TRANSPARENT_PRINT_DATA_MODE (0x200)
#define CHARACTER_DATA_MODE (0x400)
/* This MUST be one greater than the last interpreter mode required. */
#define LAST_INTERPRETER_MODE (0x800)

typedef struct PCL_VALUE {
  int32   explicit_sign;    /* Value preceded by explicit sign */
  Bool    decimal_place;    /* Value contained a decimal point */
  int32   fleading;         /* Integer portion */
  int32   ntrailing;        /* Number of fractional digits */
  int32   ftrailing;        /* Fractional part as an integer */
} PCL_VALUE;

Bool pcl5_execops(PCL5Context *pcl5_ctxt) ;

/* If a PCL5 operator callback calls this function, the most recent
   call to pcl5_execops() will return success. */
void pcl5_suspend_execops() ;

/* Returns TRUE if the current suspend state of the pcl5exec()
   interpreter is on, otherwise FALSE. Useful for saving the state
   when one is about to call the PCL5 interpreter recursively. */
Bool pcl5_get_suspend_state() ;

/** \brief Create a new PCL5 context.
 */
Bool pcl5_context_create(PCL5Context** pp_pcl5_ctxt,
                         corecontext_t* corecontext,
                         OBJECT* odict,
                         PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info,
                         int32 pass_through_type) ;

/** \brief Destroy the given PCL5 context.
 */
Bool pcl5_context_destroy(PCL5Context** pp_pcl5_ctxt) ;

Bool register_pcl5_ops(PCL5FunctTable *table) ;

Bool pcl5_funct_table_create(PCL5FunctTable **table) ;

void pcl5_funct_table_destroy(PCL5FunctTable **table) ;

/* Resource cache handling for macros and patterns. */
Bool pcl5_resource_caches_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;

void pcl5_resource_caches_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;

Bool pcl5_id_cache_insert_aliased_macro(PCL5IdCache *id_cache, pcl5_macro *orig_macro, int16 id) ;

Bool pcl5_string_id_cache_insert_aliased_macro(PCL5StringIdCache *string_id_cache,
                                               PCL5IdCache *id_cache,
                                               pcl5_macro *orig_macro,
                                               uint8 *string, int32 length) ;

Bool pcl5_id_cache_insert_macro(PCL5IdCache *id_cache, int16 id, pcl5_macro *macro, pcl5_macro **new_macro) ;

pcl5_macro* pcl5_id_cache_get_macro(PCL5IdCache *id_cache, int16 id) ;

void pcl5_id_cache_remove(PCL5IdCache *id_cache, int16 id, Bool associated_only) ;

void pcl5_id_cache_remove_all(PCL5IdCache *id_cache, Bool include_permanent) ;

void pcl5_id_cache_set_permanent(PCL5IdCache *id_cache, int16 id, Bool permanent) ;

Bool pcl5_string_id_cache_insert_macro(PCL5StringIdCache *string_id_cache,
                                       PCL5IdCache *id_cache,
                                       uint8 *string, int32 length,
                                       pcl5_macro *macro, pcl5_macro **new_macro) ;

pcl5_macro* pcl5_string_id_cache_get_macro(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length) ;

void pcl5_string_id_cache_remove(PCL5StringIdCache *string_id_cache, PCL5IdCache *id_cache, uint8 *string, int32 length, Bool associated_only) ;

void pcl5_string_id_cache_set_permanent(PCL5StringIdCache *string_id_cache, uint8 *string, int32 length, Bool permanent) ;

void pcl5_string_id_cache_remove_all(PCL5StringIdCache *string_id_cache, PCL5IdCache *id_cache, Bool include_permanent) ;

void pcl5_id_cache_kill_zombies(PCL5IdCache *id_cache) ;

Bool pcl5_id_cache_insert_pattern(PCL5IdCache *id_cache, int16 id, pcl5_pattern *pattern, pcl5_pattern **new_pattern) ;

pcl5_pattern* pcl5_id_cache_get_pattern(PCL5IdCache *id_cache, int16 id) ;

uint32 pcl5_id_cache_pattern_data_size(int32 width, int32 height, int32 bitsPerPixel) ;

PCL5Context* pcl5_current_context(void) ;

/* ============================================================================
* Log stripped */
#endif /* !__PCL5_CONTEXT_H__ */
