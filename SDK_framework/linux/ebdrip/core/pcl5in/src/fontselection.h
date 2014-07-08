/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:fontselection.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __FONTSELECTION_H__
#define __FONTSELECTION_H__

#include "pcl5context.h"
#include "printenvironment.h"
#include "pcl5resources.h"

#define MAX_FONT_NAME_LENGTH 32

#define ROMAN_8 (277)   /* (8*32) + (85-64) */
#define PC_8    (341)   /* (10*32) + (85-64) */


/* Active font */
enum {
  PRIMARY = 1,
  SECONDARY
};

/* Internal and Invalid Font IDs, (valid downloaded ones start at zero) */
enum {
  INVALID_ID = -2,
  INTERNAL_ID = -1
};


/* Information about the last PS font set */
typedef struct PS_FontState {
  uint8        name[MAX_FONT_NAME_LENGTH] ;
  uint32       name_length ;
  PCL5Real     size ;                 /* points */
  PCL5Real     scaling_x ;            /* included in the final font matrix */
  PCL5Real     scaling_y ;            /* included in the final font matrix */
} PS_FontState ;


/* PS font validity */
/* N.B. If the ps_font_valdity is PS_FONT_PENDING_FOR_ID, it is not valid
 *      for printing text, but may be set to valid during a select by ID
 *      under some circumstances.
 */
enum {
  /* N.B. This order is relied on */
  PS_FONT_INVALID = 0,
  PS_FONT_PENDING_FOR_ID = 1,
  PS_FONT_VALID = 2
};

/* Font stability */
enum {
  UNSTABLE = 0,
  STABLE_BY_CRITERIA = 1,
  STABLE_BY_ID = 2
};

/* Font validity and stability
 *
 * Font stability helps determine whether a font, and the PS font must be
 * marked as invalid if a criterium is changed or restated, or a select by ID
 * command occurs.
 *
 * A font that is STABLE_BY_CRITERIA is one that was last selected by criteria,
 * and where no font additions or redefinitions have happened since, so no
 * reselection needs to occur, (does not need to be marked invalid), if a
 * criterium is restated.  This is useful for performance, since some jobs
 * restate the fontselection criteria for every glyph.  If a criterium is
 * changed however, both font and PS font must be marked invalid.
 *
 * A font that is STABLE_BY_ID is one that was last selected by ID, and where
 * no font additions or redefinitions have occurred since then.  Again, this is
 * useful for performance.  If a criterium is restated or changed, the font must
 * be marked invalid, and would need to be reselected before printing text.  The
 * PS font however, may be marked as PS_FONT_PENDING_FOR_ID in the event that a
 * criterium is merely restated, meaning that if a select by ID command occurs
 * before text is printed, it may be possible to omit the selection.
 *
 * A font that is UNSTABLE needs to have both font and PS font marked invalid
 * if any criterium is restated or changed, and would then need to be
 * reselected before printing text, or if a select by ID command occurs.
 *
 * Font validity determines whether a font may be used for printing text.
 *
 * A font that is simply valid, but UNSTABLE is ok to print text with,
 * or for using other properties of the font, such as the HMI, but must be
 * reselected if any criterium is restated, or a select by ID command occurs.
 *
 * A font that is not valid may not be used for printing text, nor may any
 * of its other properties such as the HMI be used.  (It is ok to use the
 * various stability/validity flags).
 */

/* Be sure to update default_font() if you change this structure. */
/* The font that was selected whether by criteria or by ID */
typedef struct Font {
  /* Flags relating to stability and validity */
  Bool         valid ;
  int32        stability ;
  int32        ps_font_validity ;

  /* Font properties */
  uint8        name[MAX_FONT_NAME_LENGTH] ;
  uint32       name_length ;
  PCL5Real     size ;             /* points */
  PCL5Real     hmi ;              /* points */
  int32        symbolset_type ;
  int32        id ;
  PCL5Real     underline_dist ;
  PCL5Real     underline_thickness ;
  int32        spacing ;              /* if the selected font is proportional */
  int32        bitmapped ;            /* if the selected font is a bitmap */
  Bool         is_stick_font ;        /* is the selected font is a stickfont (HPGL2 only) */
} Font ;


/* Be sure to update default_font_sel_info() if you change this structure. */
/* The font selection criteria */
typedef struct FontSelInfo {
  /* Explicitly required (for primary font) */
  int32        symbol_set ;
  int32        spacing ;
  PCL5Real     pitch ;           /* cpi */
  PCL5Real     height ;          /* points */
  int32        style ;
  int32        stroke_weight ;
  int32        typeface ;
  int32        exclude_bitmap ;  /* always zero for PCL; from SB for HPGL */
  Bool         height_calculated ;
  Bool         criteria_changed ;
} FontSelInfo ;


/* Be sure to update default_sel_by_id_info() if you change this structure. */
/* A place to store the ID at the time the last selection by ID was made.
 * This is stored for performance reasons.
 */
typedef struct SelByIdInfo {
  int32 id ;
} SelByIdInfo ;


/* A place to store the reply from a pfin_miscop call for select by ID.
 * Since these are stored in the select by ID cache, a more compact
 * format than the sw_datum structure is desirable.
 */
typedef struct FontIdReply {
  uint8        name[MAX_FONT_NAME_LENGTH] ;
  uint32       name_length ;
  PCL5Real     size ;
  PCL5Real     hmi ;
  int32        symbol_set ;
  int32        spacing ;
  PCL5Real     pitch ;
  PCL5Real     height ;
  int32        style ;
  int32        weight ;
  int32        typeface ;
  int32        symbolset_type ;
  PCL5Real     underline_dist ;
  PCL5Real     underline_thickness ;
  int32        id ;
  int32        bitmapped ;
} FontIdReply ;


typedef struct FontIdCacheLink {
  /** List links - more efficient if first. */
  sll_link_t              sll ;           /* next FontIdCacheLink */
  Bool                    valid ;
  int32                   id ;            /* numeric font id */
  uint8*                  string ;        /* string id */
  int32                   length ;        /* length of string id */
  int32                   symbol_set ;    /* required symbolset */
  FontIdReply             reply ;         /* PFIN reply */
} FontIdCacheLink ;

typedef struct FontCritCacheLink {
  /** List links - more efficient if first. */
  sll_link_t  sll;        /* next FontCritCacheLink */
  FontSelInfo font_sel_info ;
  Font        font ;
} FontCritCacheLink ;


typedef struct FontMgtInfo {
  /* Explicitly required */
  uint32                  font_id;
  pcl5_resource_string_id string_id;
  uint32                  character_code;
  uint32                  symbol_set_id;
} FontMgtInfo;


/* Be sure to update default_font_info() if you change this structure. */
typedef struct FontInfo {
  /* Explicitly required */
  FontSelInfo  primary ;
  FontSelInfo  secondary ;
  PCL5Real     scaling_x ;       /* included in the final font matrix */
  PCL5Real     scaling_y ;       /* included in the final font matrix */

  /* Extra settings */
  SelByIdInfo  primary_sel_by_id ;    /* The ID used for the last selection by ID */
  SelByIdInfo  secondary_sel_by_id ;  /* The ID used for the last selection by ID */
  int32        active_font ;     /* Is the primary or the secondary font active? */
  Font         primary_font ;    /* selected primary font */
  Font         secondary_font ;  /* selected secondary font */
} FontInfo ;

/* This is not public at present. This is ONLY used for aliasing font
   names. */
typedef struct pcl5_font {
  pcl5_resource detail ; /* MUST be first member of structure. */
  struct pcl5_font *alias ;
} pcl5_font ;

/**
 * Free the fontselection caches.
 */
void font_sel_caches_free(void) ;

/**
 * Free the select by ID cache.
 */
void font_id_cache_free(void) ;

#ifdef FONT_CACHES_REPORT
void font_sel_caches_report() ;
#endif

/**
 * Initialise default font info.
 */
void default_font_info(FontInfo* self) ;
Bool save_font_info(PCL5Context *pcl5_ctxt, FontInfo *to, FontInfo *from, Bool overlay) ;
void restore_font_info(PCL5Context *pcl5_ctxt, FontInfo *to, FontInfo *from) ;

/**
 * Cleanup the font management structure which may have allocated a string.
 */
void cleanup_font_management_ID_string(FontMgtInfo *self) ;


extern
void default_font_management(
  FontMgtInfo*  font_mgt);

/**
 * Make the necessary changes to the ps_font_state in the PCL5PrintState
 * following a PS font change.
 */
void handle_ps_font_change(PCL5Context *pcl5_ctxt) ;

/**
 * Note that the PS font can no longer be assumed to match any PCL font.
 */
void invalidate_ps_font_match(PCL5Context *pcl5_ctxt) ;

/**
 * Is the PS font marked as matching the active PCL font for the font_info,
 * and is that font valid?
 */
Bool ps_font_matches_valid_pcl(FontInfo *font_info) ;

/**
 * Set the font in the PS gstate.
 */
Bool set_ps_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font *font) ;

/**
 * Set the PS_FontState to match the active font for the font_info provided.
 */
void set_ps_font_match(PCL5Context *pcl5_ctxt, FontInfo *font_info) ;


/* Set the given font to the default (PRIMARY) font.
 * N.B. It is thought there is only the one default font.
 */
Bool do_default_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 which_font) ;

/**
 * Get hold of the specified font.
 */
Font* get_font(FontInfo *font_info, int32 which_font) ;

/* Get the font selection info for the specified font */
FontSelInfo* get_font_sel_info(FontInfo *font_info, int32 which_font) ;

/**
 * Set the PageCtrlInfo HMI from the Font HMI, rounding from UOM.
 */
Bool set_hmi_from_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real hmi) ;

/**
 * Select the active font from its selection criteria.
 */
Bool do_fontselection_by_criteria(PCL5Context *pcl5_ctxt, FontInfo *font_info) ;

/** Select the specified font by Id */
Bool do_fontselection_by_id(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 id,
                            uint8 *string, int32 length, int32 which_font) ;

/**
 * Do any pending font selections at the current MPE level.
 */
Bool do_pending_fontselections(PCL5Context *pcl5_ctxt) ;

/**
 * Downgrade any fonts at any MPE level, except the default
 * PJL_CURRENT_ENV level, with a validity of STABLE_FONT to
 * VALID_FONT.
 */
void downgrade_stable_fonts(PCL5Context *pcl5_ctxt) ;

/**
 * Mark fonts using the ID at any MPE level, except the default
 * PJL_CURRENT_ENV level, as invalid.
 */
void mark_fonts_invalid_for_id(PCL5Context *pcl5_ctxt, int32 id) ;

/**
 * Find out the PS /WMode and /VMode from the current fontdictionary.
 */
Bool get_ps_text_mode(PCL5Context *pcl5_ctxt, int32 *wmode, int32 *vmode) ;

/**
 * Poke the supplied values into the /WMode and /VMode in the current
 * fontdictionary, and set a combination of the 2 in the gstate
 * FONTinfo.
 */
Bool set_ps_text_mode(PCL5Context *pcl5_ctxt, int32 wmode, int32 vmode) ;

/**
 * Set the PS font to the active PCL font.
 */
Bool set_fontselection(PCL5Context *pcl5_ctxt, FontInfo *font_info) ;

/**
 * Get the width of the character in the active font, (or -1 if it doesn't exist).
 */
Bool pfin_get_metrics(PCL5Context *pcl5_ctxt, Font *font, uint16 ch, int32 *char_width) ;

/**
 * PFIN call to select the default font.
 */
Bool do_pfin_select_default_font(PCL5Context *pcl5_ctxt, FontSelInfo *font_sel_info, Font *font, int32 font_number, int32 font_source) ;

/**
 * Determine from the symbolset type whether the character is printable.
 */
Bool character_is_printable(Font *font, uint16 ch) ;

/**
 * Is the spacing fixed?
 */
Bool is_fixed_spacing(Font *font) ;

/**
 * Get the currently active font corresponding to the font_info.
 * N.B. This will always ensure the font is valid.
 */
Bool get_active_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, Font **font, FontSelInfo **font_sel_info) ;

/**
 * Which kind of font did PFIN set?
 * TRUE => bitmap font
 * FALSE => scalable font
 */
Bool is_bitmap_font(FontInfo *font_info, int32 which_font) ;

void set_exclude_bitmap(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer exclude_bitmap, int32 which_font) ;

void set_typeface(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer typeface, int32 which_font) ;

void set_weight(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer weight, int32 which_font) ;

void set_style(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer style, int32 which_font) ;

void set_height(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real height, int32 which_font) ;

void set_pitch(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Real pitch, int32 which_font) ;

void set_spacing(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer spacing, int32 which_font) ;

void set_symbolset(PCL5Context *pcl5_ctxt, FontInfo *font_info, PCL5Integer symbol_set, int32 which_font) ;

Bool switch_to_font(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 which_font,
                    Bool within_text_run) ;

void handle_sel_criteria_change(PCL5Context *pcl5_ctxt, FontInfo *font_info, int32 changed_font) ;

Bool save_font_management_info(PCL5Context *pcl5_ctxt,
                               FontMgtInfo *to_font_management,
                               FontMgtInfo *from_font_management);

void restore_font_management_info(PCL5Context *pcl5_ctxt,
                                  FontMgtInfo *to_font_management,
                                  FontMgtInfo *from_font_management);

Bool set_font_string_id(PCL5Context *pcl5_ctxt, uint8 *string_id, int32 string_length) ;

Bool associate_font_string_id(PCL5Context *pcl5_ctxt, uint8 *string, int32 length) ;

Bool select_font_string_id_as_primary(PCL5Context *pcl5_ctxt, uint8 *string, int32 length) ;

Bool select_font_string_id_as_secondary(PCL5Context *pcl5_ctxt, uint8 *string, int32 length) ;

Bool delete_font_associated_string_id(PCL5Context *pcl5_ctxt) ;

Bool pcl5_id_cache_insert_aliased_font(PCL5IdCache *id_cache, pcl5_font *orig_font, int16 id) ;

Bool pcl5_id_cache_insert_font(PCL5IdCache *id_cache, int16 id, pcl5_font *font, pcl5_font **new_font) ;

Bool pcl5_string_id_cache_insert_font(PCL5StringIdCache *string_id_cache,
                                      PCL5IdCache *id_cache,
                                      uint8 *string, int32 length,
                                      pcl5_font *font, pcl5_font **new_font) ;

Bool pcl5_string_id_cache_insert_aliased_font(PCL5StringIdCache *string_id_cache,
                                              PCL5IdCache *id_cache,
                                              pcl5_font *orig_font,
                                              uint8 *string, int32 length) ;

pcl5_font* pcl5_string_id_cache_get_font(PCL5StringIdCache *string_id_cache,
                                         uint8 *string, int32 length) ;

pcl5_font* pcl5_id_cache_get_font(PCL5IdCache *id_cache, int16 id) ;

void find_font_numeric_alias(PCL5Context *pcl5_ctxt, uint16 alias_id,
                             uint8 **aliased_string, int32 *aliased_length, uint16 *id) ;

struct core_init_fns;
void pcl5_font_sel_C_globals(struct core_init_fns *fns);

/* ============================================================================
* Log stripped */

#endif
