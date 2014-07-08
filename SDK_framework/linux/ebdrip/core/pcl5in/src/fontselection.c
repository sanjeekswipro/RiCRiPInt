/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:fontselection.c(EBDSDK_P.1) $
 * $Id: src:fontselection.c,v 1.74.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font selection layer between PCL/HPGL and PFIN.
 */

#include "core.h"
#include "fontselection.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "pagecontrol.h"
#include "pcl5fonts.h"
#include "resourcecache.h"
#include "pcl5.h" /* mm_pcl_pool */

#include "dicthash.h"
#include "fileio.h"
#include "gstate.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "miscops.h"
#include "monitor.h"
#include "namedef_.h"
#include "stacks.h"
#include "swerrors.h"
#include "coreinit.h"
#include "mm.h"
#include "lowmem.h"

#include "swpfinapi.h"
#include "swpfinpcl.h"
#ifdef FONT_CACHES_REPORT
#include <stdio.h>
#endif


#define COURIER 4099

#define MAX_FONT_CRIT_CACHE_SIZE 32
#define MAX_FONT_ID_CACHE_SIZE 16

static sll_list_t font_id_cache ;
static sll_list_t font_crit_cache ;

#ifdef FONT_CACHES_REPORT
static int32 total_crit_search_depth ;
static int32 max_font_crit_entry ;
static int32 max_font_crit_hit ;
static int32 total_crit_calls ;
static int32 crit_matches ;

static int32 total_id_search_depth ;
static int32 max_font_id_entry ;
static int32 max_font_id_hit ;
static int32 total_id_calls ;
static int32 id_matches ;
#endif


/**
 * Set the passed FontSelInfo to default values.
 */
void default_font_sel_info(FontSelInfo* self)
{
  self->symbol_set = PC_8 ;    /* N.B. From reference printer rather than ROMAN8 from Tech Ref */
  self->spacing = 0 ;
  self->pitch = 10 ;
  self->height = 12 ;
  self->style = 0 ;
  self->stroke_weight = 0 ;
  self->typeface = COURIER ;
  self->exclude_bitmap = 0 ;
  self->height_calculated = FALSE ;
  self->criteria_changed = TRUE;
}

/**
 * Set the passed SelByIdInfo to default values.
 */
void default_sel_by_id_info(SelByIdInfo *self)
{
  self->id = INVALID_ID ;
}

void default_font_management(
  FontMgtInfo*  font_mgt)
{
  HQASSERT((font_mgt != NULL),
           "default_font_management: NULL font management pointer");

  font_mgt->font_id = 0;
  font_mgt->string_id.length = 0;
  font_mgt->string_id.buf = NULL;
  font_mgt->character_code = 0;
  font_mgt->symbol_set_id = 0;
}

void cleanup_font_management_ID_string(FontMgtInfo *self)
{
  pcl5_cleanup_ID_string(&self->string_id) ;
}

Bool save_font_management_info(PCL5Context *pcl5_ctxt,
                               FontMgtInfo *to_font_management,
                               FontMgtInfo *from_font_management)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  return pcl5_copy_ID_string(&(to_font_management->string_id),
                             &(from_font_management->string_id)) ;
}

void restore_font_management_info(PCL5Context *pcl5_ctxt,
                                  FontMgtInfo *to_font_management,
                                  FontMgtInfo *from_font_management)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(FontMgtInfo*, to_font_management) ;

  cleanup_font_management_ID_string(from_font_management) ;
}

/* See header for doc. */
void default_font_info(FontInfo* self)
{
  /* Note that we can't use an InitChecker here as the structure contains
  sub-structures that may be padded. */

  default_font_sel_info(&self->primary) ;
  default_font_sel_info(&self->secondary) ;
  self->scaling_x = 100 ; /* 7200 / 72, PCL to PS (HPGL overrides this) */
  self->scaling_y = -100 ;
  default_sel_by_id_info(&self->primary_sel_by_id) ;
  default_sel_by_id_info(&self->secondary_sel_by_id) ;
  self->active_font = PRIMARY ;
  self->primary_font.valid = FALSE ;
  self->secondary_font.valid = FALSE ;
  self->primary_font.stability = UNSTABLE ;
  self->secondary_font.stability = UNSTABLE ;
  self->primary_font.ps_font_validity = PS_FONT_INVALID ;
  self->secondary_font.ps_font_validity = PS_FONT_INVALID ;
}

/* ========================================================================== */
/* Functions involved in reducing the number of times we have to call
 * PS selectfont.
 */

PS_FontState* get_ps_font_state(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is Null") ;

  return &(print_state->ps_font_state) ;
}

/* Note that the PS font can no longer be assumed to
 * match any PCL font.
 */
void invalidate_ps_font_match(PCL5Context *pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe ;
  FontInfo *font_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  mpe = get_current_mpe(pcl5_ctxt) ;
  HQASSERT(mpe != NULL, "mpe is Null") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is Null") ;

  /* PCL5 fonts */
  font_info->primary_font.ps_font_validity = PS_FONT_INVALID ;
  font_info->secondary_font.ps_font_validity = PS_FONT_INVALID ;

  font_info = &(mpe->hpgl2_character_info.font_info) ;
  HQASSERT(font_info != NULL, "font_info is Null") ;

  /* HPGL2 fonts */
  font_info->primary_font.ps_font_validity = PS_FONT_INVALID ;
  font_info->secondary_font.ps_font_validity = PS_FONT_INVALID ;
}

/* Note that the PS font has changed */
void handle_ps_font_change(PCL5Context *pcl5_ctxt)
{
  PS_FontState *ps_font_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  ps_font_state = get_ps_font_state(pcl5_ctxt) ;
  HQASSERT(ps_font_state != NULL, "ps_font_state is Null") ;

  invalidate_ps_font_match(pcl5_ctxt) ;

  ps_font_state->name_length = 0 ;
  ps_font_state->size = 0 ;
  ps_font_state->scaling_x = 0 ;
  ps_font_state->scaling_y = 0 ;
}

/* Does the last PS font match what we're now being asked for? */
Bool last_ps_font_matches(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font *font)
{
  PS_FontState *ps_font_state ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(font_info != NULL, "font_info is Null") ;
  HQASSERT(font != NULL, "font is Null") ;
  HQASSERT(font->name_length != 0, "font name_length is zero") ;

  ps_font_state = get_ps_font_state(pcl5_ctxt) ;
  HQASSERT(ps_font_state != NULL, "ps_font_state is Null") ;

  if ((font->name_length != ps_font_state->name_length) ||
      (font->size * font_info->scaling_x != ps_font_state->size * ps_font_state->scaling_x) ||
      (font->size * font_info->scaling_y != ps_font_state->size * ps_font_state->scaling_y))
    return FALSE ;

  if (HqMemCmp((uint8*) &(font->name), (int32) font->name_length,
               (uint8*) &(ps_font_state->name), (int32) ps_font_state->name_length) != 0)
    return FALSE ;

  return TRUE ;
}


/* Is the PS font marked as matching the PCL font specified,
 *  and is that PCL font valid ?
 */
static
Bool ps_font_matches_valid_pcl_font(FontInfo *font_info,
                                    int32 which_font)
{
  Font *font ;

  HQASSERT(font_info != NULL, "font_info is Null") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid which_font specified");

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is Null") ;

  if (! font->valid || font->ps_font_validity != PS_FONT_VALID)
    return FALSE ;

  return TRUE ;
}


/* Is the PS font marked as matching the active PCL font for the font_info,
 * and is that PCL font valid?
 */
Bool ps_font_matches_valid_pcl(FontInfo *font_info)
{
  HQASSERT(font_info != NULL, "font_info is Null") ;

  return ps_font_matches_valid_pcl_font(font_info, font_info->active_font) ;
}


/* Set the PS_FontState to match the active font for the font_info provided,
 * and fill in the ps_font_validity flags of the PCL and HPGL fonts.
 * N.B. For a stick font do not invalidate any ps_font_validity flags, but simply
 *      set the one for the stick font itself, (if we're setting this font),
 *      as we pretend for convenience that we have a PS font match.
 */
void set_ps_font_match(PCL5Context *pcl5_ctxt, FontInfo *which_font_info)
{
  PS_FontState *ps_font_state ;
  FontInfo *font_info ;
  Font *font, *active_font ;
  Bool doing_hpgl_fonts = FALSE ;
  int32 which_font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(which_font_info != NULL, "which_font_info is Null") ;

  active_font = get_font(which_font_info, which_font_info->active_font) ;
  HQASSERT(active_font != NULL, "active_font is Null") ;
  HQASSERT(active_font->valid, "Expected a valid font") ;

  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is Null") ;

  /* Loop round all 4 fonts dealing with the ps_font_validity flags */
  while (font_info != NULL) {
    for (which_font = PRIMARY ; which_font <= SECONDARY; which_font++ ) {

      font = get_font(font_info, which_font) ;
      HQASSERT(font != NULL, "font is Null") ;

      if (font == active_font) {
        /* N.B. This should only apply to 1 of the 4 fonts */
        font->ps_font_validity = PS_FONT_VALID ;
      }
      else if (! active_font->is_stick_font && ! font->is_stick_font) {
        font->ps_font_validity = PS_FONT_INVALID ;
      }
    }

    if (!doing_hpgl_fonts) {
      font_info = &(pcl5_ctxt->print_state->mpe->hpgl2_character_info.font_info) ;
      doing_hpgl_fonts = TRUE ;
    }
    else
      font_info = NULL ;
  }

  /* Leave the PS font information alone in the case of a stick font. */
  if (!active_font->is_stick_font) {
    ps_font_state = get_ps_font_state(pcl5_ctxt) ;
    HQASSERT(ps_font_state != NULL, "ps_font_state is Null") ;

    HqMemCpy(&(ps_font_state->name), &(active_font->name), active_font->name_length) ;
    ps_font_state->name_length = active_font->name_length ;

    ps_font_state->size = active_font->size ;

    ps_font_state->scaling_x = which_font_info->scaling_x ;
    ps_font_state->scaling_y = which_font_info->scaling_y ;
  }
}


Bool save_font_info(PCL5Context *pcl5_ctxt,
                    FontInfo *to,
                    FontInfo *from,
                    Bool overlay)
{
  Bool success = TRUE ;

  UNUSED_PARAM(FontInfo*, from) ;
  UNUSED_PARAM(FontInfo*, to) ;
  UNUSED_PARAM(Bool, overlay) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;
  HQASSERT(from != NULL && to != NULL, "FontInfo is Null") ;

  /* N.B. At this point the MPE containing the 'from' FontInfo
   *      should be the current MPE, so we are asserting that
   *      this is the case.
   */
  HQASSERT(pcl5_get_font_info(pcl5_ctxt) == from,
           "Unexpected FontInfo") ;

  /* It is not strictly necessary to do pending fontselections at
   * the current level before moving up to the next macro level,
   * but it does allow for simpler code when adding or deleting
   * a font, (see the delete_font event handler in pcl5fonts.c),
   * than would otherwise be the case.
   */
  success = do_pending_fontselections(pcl5_ctxt) ;

  return success ;
}

void restore_font_info(PCL5Context *pcl5_ctxt,
                       FontInfo *to,
                       FontInfo *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(FontInfo*, from) ;
  UNUSED_PARAM(FontInfo*, to) ;
}


/* ========================================================================= */
/* These functions handle the fontselection caching.  There are two separate
 * caches, one for select by ID, and one for select by criteria.
 */

#ifdef FONT_CACHES_REPORT
void font_sel_caches_report(void)
{
  FILE *file = fopen("fontcrit.txt", "a");
  FILE *id_file = fopen("fontid.txt", "a");

  if (file != NULL) {
    fprintf(file, "Calls %d, Matches %d, Max entries %d, Hightest hit %d, ",
            total_crit_calls, crit_matches, max_font_crit_entry, max_font_crit_hit) ;

    if (total_crit_calls != 0)
      fprintf(file, "Mean Search Depth %.2f\n", (total_crit_search_depth / (float) total_crit_calls)) ;
    else
      fprintf(file, "Mean Search Depth\n") ;
  }

  fclose(file) ;

  if (id_file != NULL) {
    fprintf(id_file, "Calls %d, Matches %d, Max entries %d, Hightest hit %d, ",
            total_id_calls, id_matches, max_font_id_entry, max_font_id_hit) ;

    if (total_id_calls != 0)
      fprintf(id_file, "Mean Search Depth %.2f\n", (total_id_search_depth / (float) total_id_calls)) ;
    else
      fprintf(id_file, "Mean Search Depth\n") ;
  }

  fclose(id_file) ;
}
#endif

void init_C_globals_fontselection(void)
{
  SLL_RESET_LIST(&font_id_cache) ;
  SLL_RESET_LIST(&font_crit_cache) ;

#ifdef FONT_CACHES_REPORT
  total_crit_search_depth = 0 ;
  max_font_crit_entry = 0 ;
  max_font_crit_hit = 0 ;
  total_crit_calls = 0 ;
  crit_matches = 0 ;

  total_id_search_depth = 0;
  max_font_id_entry = 0 ;
  max_font_id_hit = 0 ;
  total_id_calls = 0 ;
  id_matches = 0 ;
#endif
}


void pcl5_id_link_cleanup(FontIdCacheLink *p_id_link)
{
  HQASSERT(p_id_link != NULL, "FontIdCacheLink is Null") ;

  if (p_id_link->string != NULL) {
    mm_free(mm_pcl_pool, p_id_link->string, p_id_link->length) ;
    p_id_link->string = NULL ;
    p_id_link->length = 0 ;
  }
}

void font_crit_cache_free(void)
{
  FontCritCacheLink *p_crit_link ;

  while (! SLL_LIST_IS_EMPTY(&font_crit_cache)) {
    p_crit_link = SLL_GET_HEAD((&font_crit_cache), FontCritCacheLink, sll);
    SLL_REMOVE_HEAD(&(font_crit_cache));

    mm_free(mm_pcl_pool, p_crit_link, sizeof(FontCritCacheLink));
  }

  SLL_RESET_LIST(&font_crit_cache) ;
}

void font_id_cache_free(void)
{
  FontIdCacheLink *p_id_link ;

  while (! SLL_LIST_IS_EMPTY(&font_id_cache)) {
    p_id_link = SLL_GET_HEAD((&font_id_cache), FontIdCacheLink, sll);
    SLL_REMOVE_HEAD(&(font_id_cache));

    pcl5_id_link_cleanup(p_id_link) ;
    mm_free(mm_pcl_pool, p_id_link, sizeof(FontIdCacheLink));
  }

  SLL_RESET_LIST(&font_id_cache) ;
}

void font_sel_caches_free(void)
{
  font_crit_cache_free() ;
  font_id_cache_free() ;
}


/** Solicit method of the PCL fonts low-memory handler. */
static low_mem_offer_t *pcl5_font_sel_solicit(low_mem_handler_t *handler,
                                               corecontext_t *context,
                                               size_t count,
                                               memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators
       || (SLL_LIST_IS_EMPTY(&font_crit_cache)
           && SLL_LIST_IS_EMPTY(&font_id_cache)) )
    return NULL;
  offer.pool = mm_pcl_pool;
  offer.offer_size = 64 * 1024; /* @@@@ */
  offer.offer_cost = 1.0;
  offer.next = NULL;
  return &offer;
}


/** Release method of the PCL fonts low-memory handler. */
static Bool pcl5_font_sel_release(low_mem_handler_t *handler,
                                  corecontext_t *context,
                                  low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  font_crit_cache_free();
  font_id_cache_free();
  return TRUE;
}


/** The PCL5 fonts low-memory handler. */
static low_mem_handler_t pcl5_font_sel_handler = {
  "PCL5 font selection caches",
  memory_tier_ram, pcl5_font_sel_solicit, pcl5_font_sel_release, TRUE,
  0, FALSE };


/** PCL5 font selection initialization */
static Bool pcl5_font_sel_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return low_mem_handler_register(&pcl5_font_sel_handler);
}


/** PCL5 font selection finishing */
static void pcl5_font_sel_finish(void)
{
  low_mem_handler_deregister(&pcl5_font_sel_handler);
}


void pcl5_font_sel_C_globals(core_init_fns *fns)
{
  init_C_globals_fontselection();
  fns->swstart = pcl5_font_sel_swstart;
  fns->finish = pcl5_font_sel_finish;
}


static
Bool add_new_crit_cache_link(void)
{
  FontCritCacheLink *p_link ;

  p_link = mm_alloc(mm_pcl_pool, sizeof(FontCritCacheLink), MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if (p_link == NULL)
    return error_handler(VMERROR) ;

  bzero((char *)p_link, sizeof(FontCritCacheLink));

  SLL_RESET_LINK(p_link, sll) ;
  SLL_ADD_HEAD(&(font_crit_cache), p_link, sll) ;

  return TRUE ;
}

static
Bool add_new_id_cache_link(void)
{
  FontIdCacheLink *p_link ;

  p_link = mm_alloc(mm_pcl_pool, sizeof(FontIdCacheLink), MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if (p_link == NULL)
    return error_handler(VMERROR) ;

  bzero((char *)p_link, sizeof(FontIdCacheLink));

  SLL_RESET_LINK(p_link, sll) ;
  SLL_ADD_HEAD(&(font_id_cache), p_link, sll) ;

  return TRUE ;
}

void add_criteria_cache_entry(FontSelInfo *font_sel_info, Font *font)
{
  FontCritCacheLink *p_link ;

  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;

  p_link = SLL_GET_HEAD(&(font_crit_cache), FontCritCacheLink, sll);

  HqMemCpy(&(p_link->font_sel_info), font_sel_info, sizeof(FontSelInfo)) ;
  HqMemCpy(&(p_link->font), font, sizeof(Font)) ;
}

static
Bool add_id_cache_entry(int32 id,
                        uint8* string,
                        int32 length,
                        int32 symbol_set,
                        sw_datum *pfin_reply,
                        FontIdReply **cached_reply)
{
  FontIdCacheLink *p_link ;
  FontIdReply *reply ;

  enum {  /* result indices */
    r_name = 0, r_size, r_hmi, r_ss, r_space, r_pitch, r_height, r_style,
    r_weight, r_font, r_sstype, r_offset, r_thick, r_ref, r_bitmap
  } ;

  HQASSERT(pfin_reply != NULL, "pfin_reply is NULL") ;
  HQASSERT(cached_reply != NULL, "cached_reply is NULL") ;

  p_link = SLL_GET_HEAD(&(font_id_cache), FontIdCacheLink, sll) ;
  HQASSERT(p_link != NULL, "Null p_link for select by ID cache") ;
  reply = &(p_link->reply) ;

  /* Clean up old string IDs */
  pcl5_id_link_cleanup(p_link) ;

  /* Copy any string ID */
  if (string != NULL) {
    p_link->string = mm_alloc(mm_pcl_pool, length, MM_ALLOC_CLASS_PCL_CONTEXT) ;

    if (p_link->string == NULL)
      return error_handler(VMERROR) ;

    bzero((char *)p_link->string, length);
    p_link->length = length ;
    HqMemCpy(p_link->string, string, length) ;
  }

  /* Copy the other inputs to the pfin_miscop */
  p_link->id = id ;
  p_link->symbol_set = symbol_set ;

  /* Copy the entries from the pfin reply to the FontIdReply structure */
  reply->name_length = (uint32)pfin_reply[r_name].length ;
  HQASSERT(reply->name_length <= (uint32) MAX_FONT_NAME_LENGTH, "Font name too long") ;
  HqMemCpy(reply->name, pfin_reply[r_name].value.string, pfin_reply[r_name].length);

  reply->size = pfin_reply[r_size].value.real ;
  reply->hmi = pfin_reply[r_hmi].value.real ;
  reply->symbol_set = pfin_reply[r_ss].value.integer ;
  reply->spacing = pfin_reply[r_space].value.integer ;
  reply->pitch = pfin_reply[r_pitch].value.real ;
  reply->height = pfin_reply[r_height].value.real ;
  reply->style = pfin_reply[r_style].value.integer ;
  reply->weight = pfin_reply[r_weight].value.integer ;
  reply->typeface = pfin_reply[r_font].value.integer ;
  reply->symbolset_type = pfin_reply[r_sstype].value.integer ;
  reply->underline_dist = pfin_reply[r_offset].value.real ;
  reply->underline_thickness = pfin_reply[r_thick].value.real ;
  reply->id = pfin_reply[r_ref].value.integer ;
  reply->bitmapped = pfin_reply[r_bitmap].value.boolean ;

  /* This link is now valid */
  p_link->valid = TRUE ;
  *cached_reply = reply ;

  return TRUE ;
}

Bool id_link_matches(FontIdCacheLink *p_link, int32 id, uint8* string, int32 length, int32 symbol_set)
{
  HQASSERT(p_link != NULL, "FontIdCacheLink is NULL") ;

  if (p_link->symbol_set != symbol_set)
    return FALSE ;

  if (p_link->string == NULL && string == NULL)
    return (p_link->id == id) ;

  if (p_link->length != length)
    return FALSE ;

  if (HqMemCmp(p_link->string, p_link->length, string, length) != 0)
    return FALSE ;

  return TRUE ;
}


/* Either move the head link to the tail, or remove it, depending
 * on whether it is a valid link.
 */
void id_cache_deal_with_unused_head_link(void)
{
  FontIdCacheLink *p_head ;

  p_head = SLL_GET_HEAD(&(font_id_cache), FontIdCacheLink, sll) ;
  SLL_REMOVE_HEAD(&(font_id_cache));

  if (SLL_LIST_IS_EMPTY(&font_id_cache))
    SLL_RESET_LIST(&font_id_cache) ;

  if (p_head->valid)
    SLL_ADD_TAIL(&(font_id_cache), p_head, sll) ;
  else {
    pcl5_id_link_cleanup(p_head) ;
    mm_free(mm_pcl_pool, p_head, sizeof(FontIdCacheLink));
  }
}

/* Search the fontselection by criteria cache.
 * The cache is kept in MRU order, and if no match is found it will
 * either allocate a new entry up to the maximum number, or recycle
 * the oldest entry.
 *
 * Depending on whether a match is found, the head link will either
 * contain the matching font information or will be the place
 * where such information is to be filled in following a PFIN/UFST
 * fontselection.
 */
static
Bool search_criteria_cache(FontSelInfo *font_sel_info, Bool *match_found, Font **font)
{
  FontCritCacheLink *p_link, *p_prev_link = NULL, *p_tail ;
  int32 link_num = 0 ;

  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(match_found != NULL, "found is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(MAX_FONT_CRIT_CACHE_SIZE >=1, "no cache entries allowed") ;

  *match_found = FALSE ;

#ifdef FONT_CACHES_REPORT
  total_crit_calls++ ;
#endif

  if (!SLL_LIST_IS_EMPTY(&(font_crit_cache))) {

    p_link = SLL_GET_HEAD(&(font_crit_cache), FontCritCacheLink, sll) ;
    p_tail = SLL_GET_TAIL(&(font_crit_cache), FontCritCacheLink, sll) ;
    HQASSERT(p_link != NULL && p_tail != NULL, "List is empty") ;

    for (;;) {
#ifdef FONT_CACHES_REPORT
        total_crit_search_depth++ ;
#endif

      if (HqMemCmp((uint8*) &(p_link->font_sel_info), sizeof(FontSelInfo),
                   (uint8*) font_sel_info, sizeof(FontSelInfo)) == 0) {
        *match_found = TRUE ;
        break ;
      }

      if (p_link == p_tail)
        break ;

      link_num++ ;
      p_prev_link = p_link ;
      p_link = SLL_GET_NEXT(p_link, FontCritCacheLink, sll) ;
    }

    if (*match_found || link_num == MAX_FONT_CRIT_CACHE_SIZE -1) {
      /* If we haven't found a match recycle the link */
      if (link_num != 0) {
        SLL_REMOVE_NEXT(p_prev_link, sll) ;
        SLL_RESET_LINK(p_link, sll) ;
        SLL_ADD_HEAD(&(font_crit_cache), p_link, sll) ;
      }

#ifdef FONT_CACHES_REPORT
      if (*match_found && (link_num + 1 > max_font_crit_hit))
        max_font_crit_hit = link_num + 1;
#endif
    }
  }

  HQASSERT(link_num <= MAX_FONT_CRIT_CACHE_SIZE - 1 , "Too many links") ;

  if (SLL_LIST_IS_EMPTY(&(font_crit_cache)) ||
      (! *match_found && link_num < MAX_FONT_CRIT_CACHE_SIZE - 1)) {
    /* We don't have a match but are allowed more links */

#ifdef FONT_CACHES_REPORT
    if (SLL_LIST_IS_EMPTY(&(font_crit_cache)) && (1 > max_font_crit_entry))
      max_font_crit_entry = 1 ;
    else if (link_num + 2 > max_font_crit_entry)
      max_font_crit_entry = link_num + 2 ;
#endif
    if (! add_new_crit_cache_link())
      return FALSE ;
  }

  /* The head of the list now either contains the match, or is the place
   * where the new fontselection should be cached.
   */
  p_link = SLL_GET_HEAD(&(font_crit_cache), FontCritCacheLink, sll);
  *font = &(p_link->font) ;

  return TRUE ;
}


/* Search the fontselection by criteria cache.
 * The cache is kept in MRU order, and if no match is found it will
 * either allocate a new entry up to the maximum number, or recycle
 * the oldest entry.
 *
 * Depending on whether a match is found, the head link will either
 * contain the matching font information or will be the place
 * where such information is to be filled in following a PFIN/UFST
 * fontselection.
 */
static
Bool search_id_cache(int32 id,
                     uint8 *string,
                     int32 length,
                     int32 symbol_set,
                     Bool *match_found,
                     FontIdReply **reply)
{
  FontIdCacheLink *p_link, *p_prev_link = NULL, *p_tail ;
  int32 link_num = 0 ;

  HQASSERT(match_found != NULL, "found is NULL") ;
  HQASSERT(reply != NULL, "reply is NULL") ;
  HQASSERT(MAX_FONT_ID_CACHE_SIZE >= 1, "no cache entries allowed");

  *match_found = FALSE ;

#ifdef FONT_CACHES_REPORT
  total_id_calls++ ;
#endif

  if (!SLL_LIST_IS_EMPTY(&(font_id_cache))) {

    p_link = SLL_GET_HEAD(&(font_id_cache), FontIdCacheLink, sll) ;
    p_tail = SLL_GET_TAIL(&(font_id_cache), FontIdCacheLink, sll) ;
    HQASSERT(p_link != NULL && p_tail != NULL, "List is empty") ;

#ifdef FONT_CACHES_REPORT
    total_id_search_depth++ ;
#endif

    for (;;) {
#ifdef FONT_CACHES_REPORT
      total_id_search_depth++ ;
#endif
      if (id_link_matches(p_link, id, string, length, symbol_set)) {
        *match_found = TRUE;
        break ;
      }

      if (p_link == p_tail)
        break ;

      link_num++ ;
      p_prev_link = p_link ;
      p_link = SLL_GET_NEXT(p_link, FontIdCacheLink, sll) ;
    }

    if (*match_found || link_num == MAX_FONT_ID_CACHE_SIZE - 1) {
      /* If we haven't found a match, recycle the link */
      if (link_num != 0) {
        SLL_REMOVE_NEXT(p_prev_link, sll) ;
        SLL_RESET_LINK(p_link, sll) ;
        SLL_ADD_HEAD(&(font_id_cache), p_link, sll) ;
      }

#ifdef FONT_CACHES_REPORT
      if (*match_found && (link_num + 1 > max_font_id_hit))
        max_font_id_hit = link_num + 1 ;
#endif
    }
  }

  HQASSERT(link_num <= MAX_FONT_ID_CACHE_SIZE - 1, "Too many links");

  if (SLL_LIST_IS_EMPTY(&(font_id_cache)) ||
      (! *match_found && link_num < MAX_FONT_ID_CACHE_SIZE - 1)) {
    /* We don't have a match but are allowed more links */

#ifdef FONT_CACHES_REPORT
    if (SLL_LIST_IS_EMPTY(&(font_id_cache)) && (1 > max_font_id_entry))
      max_font_id_entry = 1 ;
    else if (link_num + 2 > max_font_id_entry)
      max_font_id_entry = link_num + 2 ;
#endif
    if (! add_new_id_cache_link())
      return FALSE ;
  }

  /* The head of the list now either contains the match, or is the place
   * where the new font should be cached.
   */
  p_link = SLL_GET_HEAD(&(font_id_cache), FontIdCacheLink, sll);
  *reply = &(p_link->reply) ;

  HQASSERT(! *match_found || (*match_found && p_link->valid),
           "Unexpected font id cache link validity") ;

  return TRUE ;
}

/* ========================================================================= */
/* Wrappers for calls to PFIN module */

void do_pfin_selection_normalisation(PCL5PrintState *print_state, Font *font)
{
  UNUSED_PARAM(PCL5PrintState*, print_state) ;

  /* Turn dist and thickness into positive point count. */

  /* Font underline is returned as a percentage point Y
     movement. (i.e. Negative fraction). Thickness is returned as a
     percentage point size. */

  /* Make positive. */
  if (font->underline_dist < 0)
    font->underline_dist = -font->underline_dist ;

  /* Turn into point count. Points are always defined at 72dpi. */
  font->underline_dist = font->underline_dist * font->size ;
  font->underline_thickness = font->underline_thickness * font->size ;

  HQASSERT(font->size >= 0, "Negative font size") ;
  HQASSERT(font->hmi >= 0, "Negative HMI") ;
  HQASSERT(font->symbolset_type >= 0 && font->symbolset_type <= 3,
           "Unexpected symbolset_type") ;
  HQASSERT(font->id >= -1, "Invalid ID") ;
  HQASSERT(font->spacing >= 0 && font->spacing <= 1, "Bad spacing flag") ;
  HQASSERT(font->bitmapped >= 0 && font->bitmapped <= 1, "Bad bitmap flag") ;
}


/* Search the fontselection by criteria cache.
 * The cache is kept in MRU order, and if no match is found it will
 * either allocate a new entry up to the maximum number, or recycle
 * the oldest entry.
 *
 * Depending on whether a match is found, the sw_datum pointer will
 * either point to the matching font information or to the place
 * where such information is to be filled in following a PFIN/UFST
 * fontselection.
 */
static
Bool do_pfin_select_by_criteria(PCL5Context *pcl5_ctxt, FontSelInfo *font_sel_info, Font *font)
{
  PCL5PrintState *print_state ;
  sw_pfin *ufst ;
  sw_pfin_result result ;
  sw_datum *param, *reply ;
  Font *cached_font ;
  Bool match_found ;

  static sw_datum select_by_criteria[] = {
   SW_DATUM_ARRAY(select_by_criteria + 1, 10),
   SW_DATUM_INTEGER(PCL_MISCOP_SPECIFY), /* reason code (select by criteria) */
   SW_DATUM_INTEGER(0),                /* symbolset */
   SW_DATUM_INTEGER(0),                /* spacing */
   SW_DATUM_FLOAT(SW_DATUM_0_0F),      /* pitch */
   SW_DATUM_FLOAT(SW_DATUM_0_0F),      /* height */
   SW_DATUM_INTEGER(0),                /* style */
   SW_DATUM_INTEGER(0),                /* weight */
   SW_DATUM_INTEGER(0),                /* typeface */
   SW_DATUM_BOOLEAN(FALSE),            /* exclude bitmap */
   SW_DATUM_INTEGER(0),                /* print resolution */
  } ;

  enum { /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_ss, p_space, p_pitch, p_height, p_style, p_weight,
    p_font, p_bitmap, p_print_resolution
  } ;

  enum { /* result indices */
    r_name = 0, r_size, r_hmi, r_sstype, r_space, r_offset, r_thick, r_ref,
    r_bitmap, r_array_length
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  match_found = FALSE ;

  /* See if we already have this one cached */
  if (! search_criteria_cache(font_sel_info, &match_found, &cached_font))
    return FALSE ;

  if (match_found) {
    /* Find out whether we are already using this PS font */
    HQASSERT(cached_font != NULL, "cached_font is NULL") ;
    *font = *cached_font ;
#ifdef FONT_CACHES_REPORT
    crit_matches++ ;
#endif
  }
  else {
    /* Get hold of the PFIN module (currently always UFST) */
    ufst = print_state->ufst ;
    HQASSERT(ufst != NULL, "ufst is NULL") ;

    /* Now poke in our values for the call */
    param = select_by_criteria ;

    param[p_ss    ].value.integer = font_sel_info->symbol_set ;
    param[p_space ].value.integer = font_sel_info->spacing ;
    param[p_pitch ].value.real    = (float) font_sel_info->pitch ;
    param[p_height].value.real    = (float) font_sel_info->height ;
    param[p_style ].value.integer = font_sel_info->style ;
    param[p_weight].value.integer = font_sel_info->stroke_weight ;
    param[p_font  ].value.integer = font_sel_info->typeface ;
    param[p_bitmap].value.boolean = font_sel_info->exclude_bitmap ;
    param[p_print_resolution].value.integer = (int32)ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt));

    /* Call into UFST */
    result = pfin_miscop(ufst, &param) ;

    if (result != SW_PFIN_SUCCESS)
      return error_handler(pfin_error(result)) ;

    /* Check the reply is as expected */
    HQASSERT(param != NULL, "param is NULL") ;
    HQASSERT(param->type == SW_DATUM_TYPE_ARRAY && param->length >= r_array_length,
             "Unexpected reply from pfin_miscop") ;
    HQASSERT(param->owner == 0, "Unexpected param owner") ;

    /* Unpack the result - we have just asserted that this is a raw datum */
    reply = (sw_datum*) param->value.opaque ;

    /* Check the reply is formatted as expected */
    HQASSERT(reply[r_name  ].type == SW_DATUM_TYPE_STRING &&
             reply[r_size  ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_hmi   ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_sstype].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_space ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_offset].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_thick ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_ref   ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_bitmap].type == SW_DATUM_TYPE_BOOLEAN,
             "Unexpected reply array from pfin_miscop") ;

    /* Name */
    font->name_length = (uint32)reply[r_name].length ;
    HQASSERT(font->name_length <= (uint32) MAX_FONT_NAME_LENGTH, "Font name too long") ;
    HqMemCpy(&(font->name), reply[r_name].value.string, reply[r_name].length);

    /* Size */
    font->size = reply[r_size].value.real ;

    /* HMI - the value returned is the 'em', so scale to points */
    font->hmi = font->size * reply[r_hmi].value.real ;
    /* Symbolset type */
    font->symbolset_type = reply[r_sstype].value.integer ;
    /* Spacing */
    font->spacing = reply[r_space].value.integer ;
    /* Underline metrics */
    font->underline_dist = reply[r_offset].value.real ;
    font->underline_thickness = reply[r_thick].value.real ;

    /* The reference number of the soft font chosen, or -1*/
    font->id = reply[r_ref].value.integer ;

    /* bitmap status */
    font->bitmapped = reply[r_bitmap].value.boolean ;

    /* Currently PFIN does not supply stick fonts */
    font->is_stick_font = FALSE ;

    /* Cache it */
    add_criteria_cache_entry(font_sel_info, font) ;
  }

  do_pfin_selection_normalisation(print_state, font) ;

  /* Update the validity */
  font->stability = STABLE_BY_CRITERIA ;
  font->valid = TRUE ;

  return TRUE ;
}

/* Select the specified font by Id */
static
Bool do_pfin_select_by_id(PCL5Context *pcl5_ctxt,
                          FontSelInfo *font_sel_info,
                          Font *font,
                          int32 id,
                          uint8 *string,
                          int32 length,
                          Bool *font_found)
{
  PCL5PrintState *print_state ;
  sw_pfin *ufst ;
  sw_pfin_result result ;
  sw_datum *param, *reply ;
  FontIdReply *cached ;
  Bool match_found ;

  static sw_datum select_by_id[] = {
   SW_DATUM_ARRAY(select_by_id + 1, 4),
   SW_DATUM_INTEGER(PCL_MISCOP_SELECT),/* reason code (select by id) */
   SW_DATUM_INTEGER(0),                /* id (or string id) */
   SW_DATUM_INTEGER(0),                /* symbol set */
   SW_DATUM_INTEGER(PCL_ALPHANUMERIC)  /* data type (if string id) */
  } ;

  enum {  /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_id, p_symbolset, p_datatype
  };

  enum {  /* result indices */
    r_name = 0, r_size, r_hmi, r_ss, r_space, r_pitch, r_height, r_style,
    r_weight, r_font, r_sstype, r_offset, r_thick, r_ref, r_bitmap,
        r_array_length
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(id >= 0 && id <= 32767, "Unexpected id") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  *font_found = FALSE ;
  match_found = FALSE ;

  /* See if we already have this one cached */
  if (! search_id_cache(id, string, length, font_sel_info->symbol_set,
                        &match_found, &cached))
    return FALSE ;

#ifdef FONT_CACHES_REPORT
  if (match_found)
    id_matches++ ;
#endif

  if (! match_found) {

    /* Get hold of the PFIN module (currently always UFST) */
    ufst = print_state->ufst ;
    HQASSERT(ufst != NULL, "ufst is NULL") ;

    /* Now poke in our values for the call */
    param = select_by_id ;

    /* Symbol set is needed if font is a scalable typeface. See "Font
       Selection By ID", section 8-26 in the PCL5 Technical Reference
       documentation. */
    param[p_symbolset].value.integer = font_sel_info->symbol_set ;

    if (string) {
      param[p_array].length = 4 ;
      param[p_id].type = SW_DATUM_TYPE_STRING ;
      param[p_id].value.string = (const char *)string ;
      param[p_id].length = length ;
    } else {
      param[p_array].length = 3 ;
      param[p_id].type = SW_DATUM_TYPE_INTEGER ;
      param[p_id].value.integer = id ;
    }

    /* Call into UFST */
    result = pfin_miscop(ufst, &param) ;

    if (result != SW_PFIN_SUCCESS)
      return error_handler(pfin_error(result)) ;

    if (param == 0) {
       /* No such font, so remove the link we just added to the cache,
        * or put it back on the tail end if we moved it from there.
        */
      id_cache_deal_with_unused_head_link() ;
      return TRUE ;
    }

    /* Check the reply is as expected */
    HQASSERT(param->type == SW_DATUM_TYPE_ARRAY &&
                     param->length >= r_array_length,
             "Unexpected reply from pfin_miscop") ;
    HQASSERT(param->owner == 0, "Unexpected param owner") ;

    /* Unpack the result - we have just asserted that this is a raw datum */
    reply = (sw_datum*) param->value.opaque ;

    /* Check the reply is formatted as expected */
    HQASSERT(reply[r_name  ].type == SW_DATUM_TYPE_STRING &&
             reply[r_size  ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_hmi   ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_ss    ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_space ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_pitch ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_height].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_style ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_weight].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_font  ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_sstype].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_offset].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_thick ].type == SW_DATUM_TYPE_FLOAT &&
             reply[r_ref   ].type == SW_DATUM_TYPE_INTEGER &&
             reply[r_bitmap].type == SW_DATUM_TYPE_BOOLEAN,
             "Unexpected reply array from pfin_miscop") ;

    /* Cache it */
    add_id_cache_entry(id, string, length, font_sel_info->symbol_set, reply, &cached) ;
  }

  *font_found = TRUE ;
  HQASSERT(cached != NULL, "cached pfin reply is NULL") ;

  /* Name */
  font->name_length = cached->name_length ;
  HqMemCpy(font->name, cached->name, cached->name_length);

  /* Size */
  if (cached->size > 0)
    font->size = cached->size ;

  /* HMI - the value returned is the 'em', so scale to points */
  font->hmi = font->size * cached->hmi ;
  /* Spacing */
  font->spacing = cached->spacing ;
  /* Symbolset type */
  font->symbolset_type = cached->symbolset_type ;
  /* Underline metrics */
  font->underline_dist = cached->underline_dist ;
  font->underline_thickness = cached->underline_thickness ;

  /* Reference number of the soft font chosen, or -1 */
  font->id = cached->id ;

  /* bitmap status */
  font->bitmapped = cached->bitmapped ;

  /* Currently PFIN does not supply stick fonts */
  font->is_stick_font = FALSE ;

  /* Override the selection criteria */
  {
    PCL5Real height  = font_sel_info->height ;
    PCL5Real pitch   = font_sel_info->pitch ;
    int      spacing = cached->spacing ;

    if (cached->bitmapped) {
      if (cached->height >= 0)
        height = cached->height ;
      if (cached->pitch >= 0)
        pitch = cached->pitch ;
    }

    if (string == NULL) {
      font_sel_info->symbol_set    = cached->symbol_set ;
      font_sel_info->spacing       = spacing ;
      if (font->bitmapped) {
        font_sel_info->height      = height ;
        if (spacing == 0)
          font_sel_info->pitch     = pitch ;
      }
      font_sel_info->style         = cached->style ;
      font_sel_info->stroke_weight = cached->weight ;
      font_sel_info->typeface      = cached->typeface ;
      font_sel_info->criteria_changed = TRUE ;
    }

    /* Now update font size if it wasn't returned by UFST */
    if (cached->size <= 0) {
      PCL5Real fontsize ;

      if (spacing) {
        if (font_sel_info->height_calculated) {
          fontsize = font->size ;
        } else {
          fontsize = height ;
          font_sel_info->height_calculated = FALSE ;
        }
      } else {
        HQASSERT(cached->hmi != 0 && pitch != 0,
                 "Unexpected HMI or pitch") ;
        fontsize = 72.0f / (cached->hmi * pitch) ;
        font_sel_info->height_calculated = TRUE ;
      }

      if (fontsize > 999.75f)  fontsize = 999.75f ;
      if (fontsize < 0.25f)    fontsize = 0.25f ;

      font->size = ((int32)(4 * fontsize + 0.5f)) / 4.0f ;
      font->hmi = font->size * cached->hmi ;
    } else {
      font_sel_info->height_calculated = FALSE ;
    }
  }

  do_pfin_selection_normalisation(print_state, font) ;

  /* Fill in other Font items */
  font->stability = STABLE_BY_ID ;
  font->valid = TRUE ;

  return TRUE ;
}


/* Select the default font
 * N.B. This is very similar to select by ID
 */
Bool do_pfin_select_default_font(PCL5Context *pcl5_ctxt,
                                 FontSelInfo *font_sel_info,
                                 Font *font,
                                 int32 font_number,
                                 int32 font_source)
{
  PCL5PrintState *print_state ;
  sw_pfin *ufst ;
  sw_pfin_result result ;
  sw_datum *param, *reply ;

  static sw_datum select_default_font[] = {
   SW_DATUM_ARRAY(select_default_font + 1, 6),
   SW_DATUM_INTEGER(PCL_MISCOP_SELECT),/* reason code (select by id) */
   SW_DATUM_INTEGER(0),                /* font number */
   SW_DATUM_INTEGER(0),                /* font location */  /** \todo This should be able to handle a string */
   SW_DATUM_INTEGER(0),                /* symbolset */
   SW_DATUM_FLOAT(SW_DATUM_0_0F),      /* pitch */
   SW_DATUM_FLOAT(SW_DATUM_0_0F)       /* height */
  } ;

  enum { /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_number, p_source, p_ss, p_pitch, p_height
  } ;

  enum {  /* result indices */
    r_name = 0, r_size, r_hmi, r_ss, r_space, r_pitch, r_height, r_style,
    r_weight, r_font, r_sstype, r_offset, r_thick, r_ref, r_bitmap,
        r_array_length
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  /* Get hold of the PFIN module (currently always UFST) */
  ufst = print_state->ufst ;
  HQASSERT(ufst != NULL, "ufst is NULL") ;

  /* Now poke in our values for the call */
  param = select_default_font ;
  param[p_number].value.integer = font_number ;
  param[p_source].value.integer = font_source ;
  param[p_ss    ].value.integer = font_sel_info->symbol_set ;
  param[p_pitch ].value.real = (float) font_sel_info->pitch ;
  param[p_height].value.real = (float) font_sel_info->height ;

  /* Call into UFST */
  result = pfin_miscop(ufst, &param) ;

  if (result != SW_PFIN_SUCCESS)
    return error_handler(pfin_error(result)) ;

  /* N.B. This shouldn't happen if the user has been allowed to select from
   *  an available list of fonts and locations.  However, returning false here
   *  does at least give a chance of e.g. select by criteria.
   */
  if (param == 0)  /* No such font */
    return FALSE ;

  /* Check the reply is as expected */
  HQASSERT(param->type == SW_DATUM_TYPE_ARRAY
               && param->length >= r_array_length,
           "Unexpected reply from pfin_miscop") ;
  HQASSERT(param->owner == 0, "Unexpected param owner") ;

  /* Unpack the result - we have just asserted that this is a raw datum */
  reply = (sw_datum*) param->value.opaque ;

  /* Check the reply is formatted as expected */
  HQASSERT(reply[r_name  ].type == SW_DATUM_TYPE_STRING &&
           reply[r_size  ].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_hmi   ].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_ss    ].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_space ].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_pitch ].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_height].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_style ].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_weight].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_font  ].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_sstype].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_offset].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_thick ].type == SW_DATUM_TYPE_FLOAT &&
           reply[r_ref   ].type == SW_DATUM_TYPE_INTEGER &&
           reply[r_bitmap].type == SW_DATUM_TYPE_BOOLEAN,
           "Unexpected reply array from pfin_miscop") ;

  /* Name */
  font->name_length = (uint32) reply[r_name].length ;
  HQASSERT(font->name_length <= (uint32) MAX_FONT_NAME_LENGTH, "Font name too long") ;
  HqMemCpy(&(font->name), reply[r_name].value.string, reply[r_name].length);

  /* Size */
  if (reply[r_size].value.real > 0)
    font->size = reply[r_size].value.real ;
  else {
    /* Must default the size since it will not have been set up before */
    font->size = font_sel_info->height ;
  }

  /* HMI - the value returned is the 'em', so scale to points */
  font->hmi = font->size * reply[r_hmi].value.real ;
  /* Spacing */
  font->spacing = reply[r_space].value.integer ;
  /* Symbolset type */
  font->symbolset_type = reply[r_sstype].value.integer ;
  /* Underline metrics */
  font->underline_dist = reply[r_offset].value.real ;
  font->underline_thickness = reply[r_thick].value.real ;

  /* Since this is the default font it must be an internal ID */
  font->id = INTERNAL_ID ;

  /* bitmap status */
  font->bitmapped = reply[r_bitmap].value.boolean ;

  /* Currently PFIN does not supply stick fonts */
  font->is_stick_font = FALSE ;

  /* Override the selection criteria */
  font_sel_info->symbol_set    = reply[r_ss].value.integer ;
  font_sel_info->spacing       = reply[r_space].value.integer ;
  if (font->bitmapped) {
    font_sel_info->height      = reply[r_height].value.real ;
    if (font->spacing == 0)
      font_sel_info->pitch     = reply[r_pitch].value.real ;
  }
  font_sel_info->style         = reply[r_style].value.integer ;
  font_sel_info->stroke_weight = reply[r_weight].value.integer ;
  font_sel_info->typeface      = reply[r_font].value.integer ;
  font_sel_info->criteria_changed = TRUE ;

  do_pfin_selection_normalisation(print_state, font) ;

  /* Fill in other Font items */
  font->stability = UNSTABLE ;
  font->valid = TRUE ;

  return TRUE ;
}

/* Check whether the given character exists in the given font, and if so get the metrics,
 * (currently just the character width).
 * If the character does not exist, char_width will be set to -1.
 */
Bool pfin_get_metrics(PCL5Context *pcl5_ctxt, Font *font, uint16 ch, int32 *char_width)
{
  PCL5PrintState *print_state ;
  sw_pfin *ufst ;
  sw_pfin_result result ;
  sw_datum *param ;
  PCL5Real internal_unit_width ;

  static sw_datum get_metrics[] = {
   SW_DATUM_ARRAY(get_metrics + 1, 4),
   SW_DATUM_INTEGER(PCL_MISCOP_METRICS),/* reason code (get metrics) */
   SW_DATUM_STRING(""),                /* fontname (or SW_DATUM_TYPE_NULL to reuse previous) */
   SW_DATUM_INTEGER(32),               /* character code */
   SW_DATUM_INTEGER(-1)                /* previous char code for kerning (-1 == ignore) */
  } ;

  enum {
    p_array = 0, p_reason, p_name, p_code, p_prev
  } ;

  enum {
    r_width = 0, r_kern, r_array_length
  } ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  /* Assume the character does not exist */
  *char_width = -1 ;

  /* Get hold of the PFIN module (currently always UFST) */
  ufst = print_state->ufst ;
  HQASSERT(ufst != NULL, "ufst is NULL") ;

  /* Now poke in our values for the call */
  param = get_metrics ;

  /* Fontname */
  /** \todo A 'previous font' optimisation using SW_DATUM_TYPE_NULL */
  param[p_name].length  = (int32) font->name_length ;
  param[p_name].value.string = (char *) &font->name ;

  /* Character code */
  param[p_code].value.integer = (int32) ch ;

  /* N.B. param[4] is the previous character code (for kerning) or -1.
   *      It is currently ignored anyway.
   */

  /* Call into UFST */
  result = pfin_miscop(ufst, &param) ;

  if (result != SW_PFIN_SUCCESS)
    return error_handler(pfin_error(result)) ;

  /* A zero reply means the character does not exist */
  if (param == 0)
    return TRUE ;

  /* The character does exist - check the reply is sane */
  HQASSERT(param->type == SW_DATUM_TYPE_ARRAY &&
               param->length == r_array_length,
           "Unexpected reply from pfin_miscop") ;
  HQASSERT(param->owner == 0, "Unexpected param owner") ;

  /* Unpack the result - we have just asserted that this is a raw datum */
  param = (sw_datum*) param->value.opaque ;

  /* Check the reply is formatted as expected */
  HQASSERT(param[r_width].type == SW_DATUM_TYPE_FLOAT &&
           param[r_kern].type == SW_DATUM_TYPE_FLOAT,
           "Unexpected reply array from pfin_miscop") ;

  /* Character Width - the value returned is in 'ems' so multiply by the font
   * size to scale to points, and by 7200/72 to scale to PCL internal units.
   * Round to the nearest PCL Unit.
   */
  internal_unit_width = font->size * param[r_width].value.real * 100 ;
  *char_width = round_pcl_internal_to_pcl_unit(pcl5_ctxt, internal_unit_width) ;

  return TRUE ;
}

/* ========================================================================= */

const FontInfo* get_default_font_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  const PCL5PrintEnvironment *mpe ;
  const FontInfo *font_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  mpe = get_default_mpe(print_state) ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  font_info = &(mpe->font_info) ;

  return font_info ;
}


/* Get the currently held font selection result for the specified font */
Font* get_font(FontInfo *font_info, int32 which_font)
{
  Font *font ;

  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  if (which_font == PRIMARY)
    font = &(font_info->primary_font) ;
  else
    font = &(font_info->secondary_font) ;

  return font ;
}


/* Get the font selection info for the specified font */
FontSelInfo* get_font_sel_info(FontInfo *font_info, int32 which_font)
{
  FontSelInfo *font_sel_info ;

  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  if (which_font == PRIMARY)
    font_sel_info = &(font_info->primary) ;
  else
    font_sel_info = &(font_info->secondary) ;

  return font_sel_info ;
}

/* Get the sel by ID info for the specified font */
SelByIdInfo *get_sel_by_id_info(FontInfo *font_info, int32 which_font)
{
  SelByIdInfo *sel_by_id_info ;

  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  if (which_font == PRIMARY)
    sel_by_id_info = &(font_info->primary_sel_by_id) ;
  else
    sel_by_id_info = &(font_info->secondary_sel_by_id) ;

  return sel_by_id_info ;
}

/* Get the Font and FontSelInfo for the active font corresponding
 * to the FontInfo.
 * N.B. This does not necessarily return a valid font.
 */
void get_active_font_details(FontInfo *font_info, Font **font, FontSelInfo **font_sel_info)
{
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  HQASSERT(font != NULL, "font pointer is NULL") ;
  HQASSERT(font_sel_info != NULL, "font sel pointer is NULL") ;

  HQASSERT(font_info->active_font == PRIMARY ||
           font_info->active_font == SECONDARY,
           "Invalid changed_font specified");

  if (font_info->active_font == PRIMARY) {
    *font = &(font_info->primary_font) ;
    *font_sel_info = &(font_info->primary) ;
  }
  else {
    *font = &(font_info->secondary_font) ;
    *font_sel_info = &(font_info->secondary) ;
  }
}


/* Get the Font and FontSelInfo for the active font, corresponding
 * to the FontInfo.
 * N.B. This now selects a font if necessary to ensure the active
 * font is valid before returning it.
 */
Bool get_active_font(PCL5Context *pcl5_ctxt,
                     FontInfo *font_info,
                     Font **font,
                     FontSelInfo **font_sel_info)
{
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  HQASSERT(font != NULL, "font pointer is NULL") ;
  HQASSERT(font_sel_info != NULL, "font sel pointer is NULL") ;

  get_active_font_details(font_info, font, font_sel_info) ;

  HQASSERT(*font != NULL, "font is NULL") ;
  HQASSERT(*font_sel_info != NULL, "font_sel_info is NULL") ;

  /* N.B. If we had a select by ID the font should always be valid */
  if (!(*font)->valid) {
    if (! do_fontselection_by_criteria(pcl5_ctxt, font_info))
      return FALSE ;
  }

  return TRUE ;
}


/* Set the HMI value provided, rounding it from the unit of measure.
 * A negative value is used to indicate that the value must be found from
 * the currently active font.  (When a font has just been selected, the
 * font HMI can be passed in directly instead).
 */
/** \todo What if hmi is bigger than page width - see vmi? */

Bool set_hmi_from_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real font_hmi)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  if (font_hmi < 0) {
    HQASSERT(font_info != NULL, "font_info is NULL") ;

    if (! get_active_font(pcl5_ctxt, font_info, &font, &font_sel_info))
      return FALSE ;

    HQASSERT(font != NULL, "font is NULL") ;

    font_hmi = font->hmi ;
    HQASSERT(font_hmi > 0, "Zero or negative font HMI") ;
  }

  scale_hmi_for_page(pcl5_ctxt, font_hmi) ;

  return TRUE ;
}


/* Find out the /WMode and /VMode in the current fontdictionary
 * \todo Currently this functon doesn't save any time over just setting the mode.
 */
Bool get_ps_text_mode(PCL5Context *pcl5_ctxt, int32 *current_wmode, int32 *current_vmode)
{
  FONTinfo* ps_font_info = &theFontInfo(*gstateptr) ;

  static OBJECT wmode_key = OBJECT_NOTVM_NAME(NAME_WMode, LITERAL) ;
  OBJECT* o_wmode = NULL ;

  static OBJECT vmode_key = OBJECT_NOTVM_NAME(NAME_VMode, LITERAL) ;
  OBJECT* o_vmode = NULL ;

  OBJECT* o_current_font_dict = NULL ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(current_wmode != NULL, "current_wmode is NULL") ;
  HQASSERT(current_vmode != NULL, "current vmode is NULL") ;

  *current_wmode = 0 ;
  *current_vmode = 0 ;

  o_current_font_dict = &theMyFont(*ps_font_info);

  if (((o_wmode = fast_extract_hash(o_current_font_dict, &wmode_key)) != NULL) &&
      (oType(*o_wmode) == OINTEGER))
    *current_wmode = oInteger(*o_wmode) ;

  if (((o_vmode = fast_extract_hash(o_current_font_dict, &vmode_key)) != NULL) &&
      (oType(*o_vmode) == OINTEGER))
    *current_vmode = oInteger(*o_vmode) ;

  return TRUE ;
}


/* Do the work of poking /WMode and /VMode into the fontdictionary,
 * (or setting the value if already there), and set a combination
 * of the two in the gstate FONTinfo.
 */
Bool set_ps_text_mode(PCL5Context *pcl5_ctxt, int32 wmode, int32 vmode)
{
  FONTinfo* ps_font_info = &theFontInfo(*gstateptr) ;

  static OBJECT wmode_key = OBJECT_NOTVM_NAME(NAME_WMode, LITERAL) ;
  OBJECT wmode_value = OBJECT_NOTVM_INTEGER(0) ;
  OBJECT* o_wmode = NULL ;

  static OBJECT vmode_key = OBJECT_NOTVM_NAME(NAME_VMode, LITERAL) ;
  OBJECT vmode_value = OBJECT_NOTVM_INTEGER(0) ;
  OBJECT* o_vmode = NULL ;

  OBJECT* o_current_font_dict = NULL ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(wmode == 0 || wmode == 1, "Unexpected WMode") ;
  HQASSERT(vmode == 0 || vmode == 1, "Unexpected VMode") ;

  o_current_font_dict = &theMyFont(*ps_font_info);

  /* Extract or insert a /WMode into the current font dictionary. */
  if ( ((o_wmode = fast_extract_hash(o_current_font_dict, &wmode_key)) != NULL) &&
       (oType(*o_wmode) == OINTEGER ))
    oInteger(*o_wmode) = wmode ;
  else {
    oInteger(wmode_value) = wmode ;
    insert_hash_even_if_readonly(o_current_font_dict, &wmode_key, &wmode_value) ;
  }

  /* Extract or insert a /VMode into the current font dictionary. */
  if ( ((o_vmode = fast_extract_hash(o_current_font_dict, &vmode_key)) != NULL) &&
       (oType(*o_vmode) == OINTEGER ))
    oInteger(*o_vmode) = vmode ;
  else {
    oInteger(vmode_value) = vmode ;
    insert_hash_even_if_readonly(o_current_font_dict, &vmode_key, &vmode_value) ;
  }

  /* N.B. If the font hasn't changed since last time, the /WMode and /VMode
   *      values will be ignored.  To ensure that we take note of the values,
   *      also need to change the value in the FONTinfo in the PS gstate.
   *      This value is a munged combination of the /WMode and /VMode values.
   */
  ps_font_info->wmode = (uint8) ((vmode << 1) | wmode) ;

  return TRUE ;
}


/* Set the font in the PS gstate */
Bool set_ps_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font *font)
{
  OBJECT font_matrix ;
  struct PCL5PrintState *print_state;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(font->valid, "font is invalid") ;
  HQASSERT(!font->is_stick_font, "Unexpected stick font") ;

  HQASSERT(!ps_font_matches_valid_pcl(font_info), "PS font is already valid") ;

  /* Last ditch effort to see if we can avoid doing the work after all */
  if (last_ps_font_matches(pcl5_ctxt, font_info, font)) {
    font->ps_font_validity = PS_FONT_VALID ;
    return TRUE ;
  }

  /* Set the font in the PS state with the correct fontmatrix */
  if ( (oName(nnewobj) = cachename(font->name, font->name_length)) == NULL )
    return FALSE;

  /* Set up the font matrix */
  if (! ps_array(&font_matrix, 6))
    return FALSE ;

  object_store_real(&oArray(font_matrix)[0], (USERVALUE) (font->size * font_info->scaling_x) ) ;
  object_store_integer(&oArray(font_matrix)[1], 0) ;
  object_store_integer(&oArray(font_matrix)[2], 0) ;
  object_store_real(&oArray(font_matrix)[3], (USERVALUE) (font->size * font_info->scaling_y) ) ;
  object_store_integer(&oArray(font_matrix)[4], 0) ;
  object_store_integer(&oArray(font_matrix)[5], 0) ;

  if (! push2(&nnewobj, &font_matrix, &operandstack))
    return FALSE ;

  if (! selectfont_(pcl5_ctxt->corecontext->pscontext))
    return FALSE ;

  /* Note success and fill in details of the font requested */
  set_ps_font_match(pcl5_ctxt, font_info) ;

  print_state = pcl5_ctxt->print_state ;
  print_state->setg_required += 1 ;

  return TRUE ;
}


/* Select the active font from its font criteria */
Bool do_fontselection_by_criteria(PCL5Context *pcl5_ctxt, FontInfo *font_info)
{
  Font *font ;
  FontSelInfo *font_sel_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  get_active_font_details(font_info, &font, &font_sel_info) ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  HQASSERT(!font->valid, "Font is already valid") ;

  /* Select the font from the font criteria */
  if (! do_pfin_select_by_criteria(pcl5_ctxt, font_sel_info, font))
    return FALSE ;

  HQASSERT(font->valid, "Font is invalid") ;

  if (font_info == pcl5_get_font_info(pcl5_ctxt))
    invalidate_hmi(pcl5_ctxt) ;

  font->ps_font_validity = PS_FONT_INVALID ;

  return TRUE ;
}


/* Do any pending font selections for the current MPE level */
Bool do_pending_fontselections(PCL5Context *pcl5_ctxt)
{
  FontInfo *font_info ;
  Font *font ;
  FontSelInfo *font_sel_info ;
  int32 which_font, hpgl_active_font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  font_info = pcl5_get_font_info(pcl5_ctxt) ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  /* Do the PCL5 fontselections */
  for (which_font = PRIMARY ; which_font <= SECONDARY; which_font++ ) {

    if (which_font == PRIMARY) {
      font_sel_info = &(font_info->primary) ;
      font = &(font_info->primary_font) ;
    } else {
      font_sel_info = &(font_info->secondary) ;
      font = &(font_info->secondary_font) ;
    }

    if (!font->valid) {
      if (which_font == font_info->active_font) {
        if (! do_fontselection_by_criteria(pcl5_ctxt, font_info))
          return FALSE ;
      }
      else {
        if (! do_pfin_select_by_criteria(pcl5_ctxt, font_sel_info, font))
          return FALSE ;

        font->ps_font_validity = PS_FONT_INVALID ;
      }
    }
  }

  /* Do the HPGL fontselections */
  font_info = &(pcl5_ctxt->print_state->mpe->hpgl2_character_info.font_info) ;

  for (which_font = PRIMARY ; which_font <= SECONDARY; which_font++ ) {

    if (which_font == PRIMARY) {
      font_sel_info = &(font_info->primary) ;
      font = &(font_info->primary_font) ;
    } else {
      font_sel_info = &(font_info->secondary) ;
      font = &(font_info->secondary_font) ;
    }

    if (!font->valid) {
      if (which_font == font_info->active_font) {
        if (! hpgl2_set_font(pcl5_ctxt, font_info))
          return FALSE ;
      }
      else {
        /** \todo FIXME This is a temporary hack - should be able to specify
         *  which HPGL font to set, and not to set PS for the non-active
         *  font.
         */
         hpgl_active_font = font_info->active_font ;
         font_info->active_font = which_font ;

         if (! hpgl2_set_font(pcl5_ctxt, font_info))
           return FALSE ;

         font_info->active_font = hpgl_active_font ;
       }
     }
   }

  return TRUE ;
}


/* Downgrade all fonts at the given MPE level to UNSTABLE.
 * This indicates that a font has been added or redefined since the last
 * fontselection by ID or criteria, so the font must be reselected if
 * any criterium or the ID is restated, to allow for the possibility
 * of selecting the new font.
 */
static void downgrade_stable_fonts_in_mpe(PCL5PrintEnvironment *mpe)
{
  FontInfo *font_info ;

  HQASSERT(mpe != NULL, "mpe is NULL") ;

  /* PCL5 fonts */
  font_info = &(mpe->font_info) ;

  font_info->primary_font.stability = UNSTABLE ;
  font_info->secondary_font.stability = UNSTABLE ;

  /* HPGL2 fonts */
  font_info = &(mpe->hpgl2_character_info.font_info) ;

  font_info->primary_font.stability = UNSTABLE ;
  font_info->secondary_font.stability = UNSTABLE ;
}

/* Downgrade all fonts at all MPE levels above PJL_CURRENT_ENV (default
 * MPE level) to UNSTABLE, (see downgrade_stable_fonts_in_mpe).
 */
void downgrade_stable_fonts(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *p_state ;
  PCL5PrintEnvironmentLink *p_env, *p_env_tail ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  p_env = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
  p_env_tail = SLL_GET_TAIL(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);

  while (p_env != p_env_tail) {
    HQASSERT(p_env->mpe_type > PJL_CURRENT_ENV, "Unexpected mpe_type") ;
    downgrade_stable_fonts_in_mpe(&(p_env->mpe)) ;

    p_env = SLL_GET_NEXT(p_env, PCL5PrintEnvironmentLink, sll);
  }
}


/* This can be used (e.g. following a font deletion) to mark any fonts
 * in the MPE with the given ID as invalid.  It can be used at any MPE
 * (macro) level.
 *
 * If the ID given is INVALID_ID, all fonts at the given MPE level will
 * be marked as invalid.
 */
static void mark_fonts_in_mpe_invalid_for_id(PCL5PrintEnvironment *mpe,
                                             int32 id)
{
  FontInfo *font_info ;
  Font *font ;
  PageCtrlInfo *page_info ;
  Bool doing_hpgl_fonts = FALSE ;
  int32 which_font ;

  HQASSERT(mpe != NULL, "mpe is NULL") ;
  HQASSERT((id >= 0 && id <= 32767) || (id == INVALID_ID),
           "Unexpected font id") ;

  page_info = &(mpe->page_ctrl) ;
  font_info = &(mpe->font_info) ;

  while (font_info != NULL) {
    for (which_font = PRIMARY ; which_font <= SECONDARY; which_font++ ) {

      if (which_font == PRIMARY)
        font = &(font_info->primary_font) ;
      else
        font = &(font_info->secondary_font) ;

      if (id == INVALID_ID || font->id == id) {
        font->ps_font_validity = PS_FONT_INVALID ;
        font->valid = FALSE ;
        font->stability = UNSTABLE ;

        if (! doing_hpgl_fonts && which_font == font_info->active_font)
          page_info->hmi = INVALID_HMI ;
      }
    }

    if (! doing_hpgl_fonts) {
      font_info = &(mpe->hpgl2_character_info.font_info) ;
      doing_hpgl_fonts = TRUE ;
    }
    else
      font_info = NULL ;
  }
}

/* Mark all fonts at all MPE levels above PJL_CURRENT_ENV (default MPE level)
 * with the given ID as invalid.  If the ID is INVALID_ID, all fonts at all
 * MPE levels above PJL_CURRENT_ENV will be marked as invalid.
 */
void mark_fonts_invalid_for_id(PCL5Context *pcl5_ctxt, int32 id)
{
  PCL5PrintState *p_state ;
  PCL5PrintEnvironmentLink *p_env, *p_env_tail ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  p_state = pcl5_ctxt->print_state ;
  HQASSERT(p_state != NULL, "p_state is NULL") ;

  HQASSERT((id >= 0 && id <= 32767) || (id == INVALID_ID),
           "Unexpected font id") ;

  p_env = SLL_GET_HEAD(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);
  p_env_tail = SLL_GET_TAIL(&(p_state->mpe_list), PCL5PrintEnvironmentLink, sll);

  while (p_env != p_env_tail) {
    HQASSERT(p_env->mpe_type > PJL_CURRENT_ENV, "Unexpected mpe_type") ;
    mark_fonts_in_mpe_invalid_for_id(&(p_env->mpe), id) ;

    p_env = SLL_GET_NEXT(p_env, PCL5PrintEnvironmentLink, sll);
  }
}

/* Select the specified font by Id */
Bool do_fontselection_by_id(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 id,
                            uint8 *string, int32 length, int32 which_font)
{
  Font *font ;
  FontSelInfo *font_sel_info ;
  SelByIdInfo *sel_by_id_info ;
  Bool do_selection = TRUE, may_set_ps_font_valid = FALSE ;
  Bool found  = TRUE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified");

  font = get_font(font_info, which_font) ;
  font_sel_info = get_font_sel_info(font_info, which_font) ;
  sel_by_id_info = get_sel_by_id_info(font_info, which_font) ;

  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;
  HQASSERT(sel_by_id_info != NULL, "sel_by_id_info is NULL") ;

  /* See if we really need to do the selection.
   * N.B. We don't do this for string IDs as any gain in not needing to compare
   *      PS font names would be offset by the string ID comparison.
   */
  if (!string) {
    HQASSERT(id != INVALID_ID, "ID is invalid") ;

    if (font->stability == STABLE_BY_ID &&
        font->ps_font_validity != PS_FONT_INVALID &&
        sel_by_id_info->id == id ) {

        do_selection = FALSE ;
        font->valid = TRUE ;
        may_set_ps_font_valid = TRUE ;
    }
  }

  /* Select the font from the font ID */
  if (do_selection) {
    if (! do_pfin_select_by_id(pcl5_ctxt, font_sel_info, font, id, string, length, &found))
      return FALSE ;

    if (found) {
      if (!string) {
        sel_by_id_info->id = id ;
      }
      else
        sel_by_id_info->id = INVALID_ID ;
    }
  }

  /* N.B. Stick with the existing font if no font with this ID was found.
   *      In this case the HMI also remains unchanged.
   */
  if (found) {

    if ((font_info == pcl5_get_font_info(pcl5_ctxt) &&
        (font_info->active_font == which_font)))
        invalidate_hmi(pcl5_ctxt) ;

      if (may_set_ps_font_valid)
        font->ps_font_validity = PS_FONT_VALID ;
      else
        font->ps_font_validity = PS_FONT_INVALID ;
  }

  return TRUE ;
}


void handle_sel_criteria_restatement(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 which_font)
{
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid which_font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(font->stability == STABLE_BY_ID || font->stability == STABLE_BY_CRITERIA,
           "Unexpected font stability") ;

  if (font->stability == STABLE_BY_ID) {
    /* The font will have to be reselected by criteria if there is no select
     * by ID command, before it is used to print text.
     */
    font->valid = FALSE ;

    /* If a select by ID command occurs before text is printed, it may be
     * possible to avoid a reselection of the font, and resetting the PS font.
     */
    if (font->ps_font_validity != PS_FONT_INVALID)
      font->ps_font_validity = PS_FONT_PENDING_FOR_ID ;
  }

  if ((font_info->active_font == which_font) &&
      (font_info == pcl5_get_font_info(pcl5_ctxt))) {

    invalidate_hmi(pcl5_ctxt);
  }
}


void handle_sel_criteria_change(PCL5Context *pcl5_ctxt,
                                FontInfo *font_info,
                                int32 changed_font)
{
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(changed_font == PRIMARY || changed_font == SECONDARY,
           "Invalid changed_font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  font = get_font(font_info, changed_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  font->valid = FALSE ;
  font->ps_font_validity = PS_FONT_INVALID ;

  if ((font_info->active_font == changed_font) &&
      (font_info == pcl5_get_font_info(pcl5_ctxt))) {

    invalidate_hmi(pcl5_ctxt);
  }
}


Bool switch_to_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 which_font,
                    Bool within_text_run)
{
  PCL5PrintState *print_state ;
  Font *font ;
  Bool doing_pcl5_font ;
  Bool success = TRUE ;

  UNUSED_PARAM(Bool, within_text_run) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid changed_to font specified");
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  doing_pcl5_font = (font_info == &(print_state->mpe->font_info)) ;

  if (font_info->active_font == which_font)
    return success ;

  font_info->active_font = which_font ;

  if (which_font == PRIMARY)
    font = &(font_info->primary_font) ;
  else
    font = &(font_info->secondary_font) ;

  if (doing_pcl5_font) {
    if (!font->valid)
      invalidate_hmi(pcl5_ctxt);
    else
      scale_hmi_for_page(pcl5_ctxt, font->hmi) ;
  }

  return success ;
}


Bool set_fontselection(PCL5Context *pcl5_ctxt, FontInfo *font_info)
{
  Font *font ;
  FontSelInfo *font_sel_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  if (! get_active_font(pcl5_ctxt, font_info, &font, &font_sel_info))
    return FALSE ;

  HQASSERT(font != NULL, "font is NULL") ;
  HQASSERT(font->valid, "font is invalid") ;

  if (! ps_font_matches_valid_pcl(font_info)) {

    if (! set_ps_font(pcl5_ctxt, font_info, font))
      return FALSE ;
  }

  return TRUE ;
}

/* Determine from the symbolset type whether the character is
   printable. */
Bool character_is_printable(Font *font, uint16 ch)
{
  Bool printable = FALSE ;

  HQASSERT(font != NULL, "font is NULL") ;

  /** \todo TODO FIXME this test will need to be made tighter.
   *
   * The text parsing mode will ensure all values are in the
   * appropriate range(s) for the text parsing mode.
   */
  if ( ch > 255 )
    return TRUE;

  switch (font->symbolset_type) {
    default:
      /* Whinge but allow minimal char range check */
      HQFAIL("Invalid symbolset_type") ;
      /* FALLTHROUGH */
    case 0:
      /* 7-bit, 32-127 are printable */
      printable = (ch >= 32 && ch <= 127) ;
      break ;

    case 1:
      /* 8-bit, 32-127 and 160-255 are printable */
      printable = (ch >= 32 && ch <= 127) || (ch >= 160) ;
      break ;

    case 2:
      /* 8-bit, 0-255 are printable but to print codes 0, 7-15, and
       * 27, the printer must be in transparency mode.
       */
      printable = (ch > 0 && ch <= 6) || (ch >=16 && ch <= 26) || (ch > 27) ;
      break ;

    case 3:
      /* 0-65534 are printable. Always true because of this compiler
         warning: Comparison is always true due to limited range of
         data type.
         printable = (ch >= 0 && ch <= 65534) ;
      */
      printable = TRUE ;
      break ;
  }
  return printable ;
}


/* Set the given font to the default (PRIMARY) font.
 * N.B. It is thought there is only the one default font.
 */
Bool do_default_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 which_font)
{
  const FontInfo *default_font_info ;
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font") ;
  HQASSERT(font_info != NULL, "font_info is NULL") ;

  default_font_info = get_default_font_info(pcl5_ctxt) ;

  if (which_font == PRIMARY) {
    font = &(font_info->primary_font) ;
    font_sel_info = &(font_info->primary) ;
  }
  else {
    font = &(font_info->secondary_font) ;
    font_sel_info = &(font_info->secondary) ;
  }

  /* Copy the default font and selection criteria */
  *font = default_font_info->primary_font ;
  *font_sel_info = default_font_info->primary ;

  HQASSERT(font->valid, "Expected valid default font") ;

  if (font_info->active_font == which_font) {

    /* Mark the HMI invalid if necessary */
    if (font_info == pcl5_get_font_info(pcl5_ctxt))
      invalidate_hmi(pcl5_ctxt) ;
  }

  HQASSERT(font->ps_font_validity == PS_FONT_INVALID, "Unexpected PS font") ;
  HQASSERT(font->stability == UNSTABLE, "Unexpected stable font") ;

  return TRUE ;
}


void set_exclude_bitmap(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer exclude_bitmap, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(exclude_bitmap == 0 || exclude_bitmap == 1, "Unexpected exclude_bitmap") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  /* N.B. This is not quite the same as the usual font selection criteria.
   *      We never need to reselect if exclude_bitmap is unchanged.
   */
  if ( exclude_bitmap != font_sel_info->exclude_bitmap ) {
    font_sel_info->exclude_bitmap = exclude_bitmap ;
    font_sel_info->criteria_changed = TRUE ;

    /* We do not need to do a new font selection if the active font is
       already a scalable font and we are simply disallowing bitmap
       fonts. */
    if (!exclude_bitmap || font->bitmapped)
      handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
}


void set_typeface(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer typeface, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font* font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(typeface >= 0 && typeface <= 65535, "Unexpected typeface") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->typeface != typeface) {
    font_sel_info->typeface = typeface ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}


void set_weight(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer weight, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT((weight >= -7 && weight <= 7) || weight == 9999,
           "Unexpected weight") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->stroke_weight != weight) {
    font_sel_info->stroke_weight = weight ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}


void set_style(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer style, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(style >= 0 && style <= 32767, "Unexpected style") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->style != style) {
    font_sel_info->style = style ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}


void set_height(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real height, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(height >= 0 && height<= 32767.9999f, "Unexpected height") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->height != height ||
      font_sel_info->height_calculated ) {

    font_sel_info->height = height ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;

  /** \todo Should this be FALSE even if the value is unchanged? */
  font_sel_info->height_calculated = FALSE ;
}


void set_pitch(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real pitch, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(pitch >= 0 && pitch <= 32767.9999f, "Unexpected pitch") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->pitch != pitch) {
    font_sel_info->pitch = pitch ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}


void set_spacing(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer spacing, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(spacing == 0 || spacing == 1, "Unexpected spacing") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->spacing != spacing) {
    font_sel_info->spacing = spacing ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}

Bool is_fixed_spacing(Font *font)
{
  HQASSERT(font != NULL, "font is missing") ;
  HQASSERT(font->valid, "font is invalid") ;
  return font->spacing == 0 ;
}

/**
 * Which kind of font did PFIN set?
 * TRUE => bitmap font
 * FALSE => scalable font
 */
Bool is_bitmap_font(FontInfo *font_info, int32 which_font)
{
  Font *font = get_font(font_info, which_font) ;

  HQASSERT(font != NULL, "font is missing") ;
  HQASSERT(font->valid, "font is invalid") ;

  return font->bitmapped ;
}

void set_symbolset(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer symbol_set, int32 which_font)
{
  FontSelInfo *font_sel_info ;
  Font *font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(which_font == PRIMARY || which_font == SECONDARY,
           "Invalid font specified") ;

  font_sel_info = get_font_sel_info(font_info, which_font) ;
  HQASSERT(font_sel_info != NULL, "font_sel_info is NULL") ;

  font = get_font(font_info, which_font) ;
  HQASSERT(font != NULL, "font is NULL") ;

  if (font->stability == UNSTABLE || font_sel_info->symbol_set != symbol_set) {
    font_sel_info->symbol_set = symbol_set ;
    font_sel_info->criteria_changed = TRUE ;
    handle_sel_criteria_change(pcl5_ctxt, font_info, which_font) ;
  }
  else
    handle_sel_criteria_restatement(pcl5_ctxt, font_info, which_font) ;
}


Bool set_font_string_id(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  FontMgtInfo *font_management_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  font_management_info = pcl5_get_font_management(pcl5_ctxt) ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  /* Deallocate any existing string ID */
  pcl5_cleanup_ID_string(&(font_management_info->string_id));

  if ((font_management_info->string_id.buf = mm_alloc(mm_pcl_pool, length,
                                                      MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL)
    return FALSE ;

  font_management_info->string_id.length = length ;
  HqMemCpy(font_management_info->string_id.buf, string, length) ;

  return TRUE ;
}

Bool associate_font_string_id(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  FontMgtInfo *font_management_info ;
  pcl5_font font, *new_font ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  font_management_info = pcl5_get_font_management(pcl5_ctxt) ;

  /* Find the font via string/length.
     If it exists
       If current font ID is numeric
         Create a new numeric font ID and insert into numeric cache
         Alias to string ID
       Else
         If current font ID is not the same as string/length
           Create a new string font ID and insert into string cache
           Alias to string ID
  */
  font.detail.resource_type = SW_PCL5_FONT ;
  font.detail.permanent = FALSE ; /* Not used for fonts. */
  font.detail.device = NULL ;
  font.detail.private_data = NULL ;
  font.detail.PCL5FreePrivateData = NULL ;
  font.alias = NULL ;

  /* Insert the string as a font. */
  if (! pcl5_string_id_cache_insert_font(pcl5_rip_context.resource_caches.aliased_string_font,
                                         pcl5_rip_context.resource_caches.aliased_font,
                                         string, length, &font, &new_font))
    return FALSE ;

  if (font_management_info->string_id.buf == NULL) {
    /* Its numeric. */
    pcl5_resource_numeric_id font_numeric_id = (pcl5_resource_numeric_id)font_management_info->font_id ;

    if (! pcl5_id_cache_insert_aliased_font(pcl5_ctxt->resource_caches.aliased_font, new_font, font_numeric_id))
      return FALSE ;
  } else {
    /* Its a string. */
    uint8 *string ;
    int32 length ;
    string = font_management_info->string_id.buf ;
    length = font_management_info->string_id.length ;

    if (! pcl5_string_id_cache_insert_aliased_font(pcl5_ctxt->resource_caches.aliased_string_font,
                                                   pcl5_ctxt->resource_caches.aliased_font,
                                                   new_font, string, length))
      return FALSE ;
  }

  return TRUE ;
}

void find_font_string_alias(PCL5Context *pcl5_ctxt,
                            uint8 *string, int32 length,
                            uint8 **aliased_string, int32 *aliased_length, uint16 *id)
{
  pcl5_font *font ;

  *aliased_string = string ;
  *aliased_length = length ;
  *id = 0 ;

  font = pcl5_string_id_cache_get_font(pcl5_ctxt->resource_caches.aliased_string_font,
                                       string, length) ;

  if (font != NULL) {
    HQASSERT(font->detail.string_id.buf != NULL, "Hmm, string buf is NULL") ;
    if (font->alias != NULL) {
      font = font->alias ;
      if (font->detail.string_id.buf != NULL) {
        *aliased_string = font->detail.string_id.buf ;
        *aliased_length = font->detail.string_id.length ;
      } else {
        *aliased_string = NULL ;
        *aliased_length = 0 ;
        *id = font->detail.numeric_id ;
      }
    }
  }
}

void find_font_numeric_alias(PCL5Context *pcl5_ctxt, uint16 alias_id,
                             uint8 **aliased_string, int32 *aliased_length, uint16 *id)
{
  pcl5_font *font ;

  *aliased_string = NULL ;
  *aliased_length = 0 ;
  *id = alias_id ;

  font = pcl5_id_cache_get_font(pcl5_ctxt->resource_caches.aliased_font, alias_id) ;

  if (font != NULL) {
    HQASSERT(font->detail.string_id.buf == NULL, "Hmm, string buf is not NULL") ;
    if (font->alias != NULL) {
      font = font->alias ;
      if (font->detail.string_id.buf != NULL) {
        *aliased_string = font->detail.string_id.buf ;
        *aliased_length = font->detail.string_id.length ;
      } else {
        *id = font->detail.numeric_id ;
      }
    }
  }
}

Bool select_font_string_id_as_primary(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  uint8 *aliased_string ;
  int32 aliased_length ;
  uint16 aliased_id ;

  find_font_string_alias(pcl5_ctxt, string, length, &aliased_string, &aliased_length, &aliased_id) ;

  do_fontselection_by_id(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), aliased_id,
                         aliased_string, aliased_length, PRIMARY) ;
  return TRUE ;
}

Bool select_font_string_id_as_secondary(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  uint8 *aliased_string ;
  int32 aliased_length ;
  uint16 aliased_id ;

  find_font_string_alias(pcl5_ctxt, string, length, &aliased_string, &aliased_length, &aliased_id) ;

  do_fontselection_by_id(pcl5_ctxt, pcl5_get_font_info(pcl5_ctxt), aliased_id,
                         aliased_string, aliased_length, SECONDARY) ;
  return TRUE ;
}

Bool delete_font_associated_string_id(PCL5Context *pcl5_ctxt)
{
  FontMgtInfo *font_management_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  font_management_info = pcl5_get_font_management(pcl5_ctxt) ;

  /* If the current font ID is a string ID
       Remove current font ID from font string ID cache.
  */

  if (font_management_info->string_id.buf != NULL) {
    uint8 *string ;
    int32 length ;
    string = font_management_info->string_id.buf ;
    length = font_management_info->string_id.length ;

    pcl5_string_id_cache_remove(pcl5_ctxt->resource_caches.aliased_string_font,
                                pcl5_ctxt->resource_caches.aliased_font,
                                string, length, TRUE) ;
  } else {
    pcl5_resource_numeric_id font_numeric_id = (pcl5_resource_numeric_id)font_management_info->font_id ;

    pcl5_id_cache_remove(pcl5_ctxt->resource_caches.aliased_font,
                         font_numeric_id, TRUE) ;
  }

  return TRUE ;
}

/* ============================================================================
* Log stripped */
