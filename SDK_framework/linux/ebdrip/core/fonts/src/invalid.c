/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:invalid.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font methods for invalid fonts. These are set up statically, and installed
 * in the gstate at startup, and when gs_setfont invalidates the current font
 * info data. They persist until set_font() has unpacked the current font's
 * type.
 */

#include "core.h"
#include "swerrors.h" /* error_handler, INVALIDFONT */
#include "graphics.h" /* FONTinfo */
#include "fonts.h"
#include "charstring12.h"
#include "objstack.h"
#include "stacks.h"

static Bool invalid_cache_key(FONTinfo *fontInfo,
                              char_selector_t *selector,
                              charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(char_selector_t *, selector) ;
  UNUSED_PARAM(charcontext_t *, context) ;

  return error_handler(INVALIDFONT) ;
}

static Bool invalid_lookup_char(FONTinfo *fontInfo,
                                charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;

  return error_handler(INVALIDFONT) ;
}

static Bool invalid_begin_char(FONTinfo *fontInfo,
                               charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;

  return error_handler(INVALIDFONT) ;
}

static void invalid_end_char(FONTinfo *fontInfo,
                             charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;
}

font_methods_t font_invalid_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

#ifdef NO_DLD1_FONTS
font_methods_t font_dld1_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

void init_C_globals_dloader(void)
{
  EMPTY_STATEMENT() ;
}
#endif

#ifdef NO_CFF_FONTS
font_methods_t font_cff_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

font_methods_t font_cid0c_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

void init_C_globals_cff(void)
{
  EMPTY_STATEMENT() ;
}
#endif

#ifdef NO_TYPE3_FONTS
font_methods_t font_type3_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

font_methods_t font_cid1_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;
#endif

#ifdef NO_TT_FONTS
font_methods_t font_truetype_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

font_methods_t font_cid2_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

void init_C_globals_tt_font(void)
{
  EMPTY_STATEMENT() ;
}
#endif

#ifdef NO_CID0_FONTS
font_methods_t font_cid0_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

void init_C_globals_cidfont(void)
{
  EMPTY_STATEMENT() ;
}
#endif

#ifdef NO_CID4_FONTS
font_methods_t font_cid4_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

Bool t32_plot(corecontext_t *context,
              charcontext_t *charcontext, LINELIST *currpt, int32 gid)
{
  UNUSED_PARAM(corecontext_t *, context) ;
  UNUSED_PARAM(charcontext_t *, charcontext) ;
  UNUSED_PARAM(LINELIST *, currpt) ;
  UNUSED_PARAM(int32, gid) ;
  return error_handler(INVALIDFONT) ;
}

/* Stub operator definitions for Type 32 operators. We don't completely
   remove these, because the names file should not be made variant, and they
   may be used in setup PostScript. */
Bool addglyph_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize(operandstack) < 3 )
    return error_handler(STACKUNDERFLOW);

  npop(4, &operandstack);
  return TRUE;
}

Bool removeglyphs_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize(operandstack) < 2 )
    return error_handler(STACKUNDERFLOW);

  npop(3, &operandstack);
  return TRUE;
}

Bool removeall_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW);

  pop(&operandstack);
  return TRUE;
}

void init_C_globals_t32font(void)
{
  EMPTY_STATEMENT() ;
}
#endif

#ifdef NO_TYPE1_FONTS
font_methods_t font_type1_ps_fns = {
  invalid_cache_key,
  invalid_lookup_char,
  NULL, /* select_subfont */
  invalid_begin_char,
  invalid_end_char
} ;

static Bool invalid_get_info(void *data, int32 nameid, int32 index, OBJECT *object)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, nameid) ;
  UNUSED_PARAM(int32, index) ;
  UNUSED_PARAM(OBJECT *, object) ;
  return error_handler(INVALIDFONT) ;
}

static Bool invalid_begin_subr(void *data,
                               int32 subrno, Bool global,
                               uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, subrno) ;
  UNUSED_PARAM(int32, global) ;
  UNUSED_PARAM(uint8 **, subrstr) ;
  UNUSED_PARAM(uint32 *, subrlen) ;
  return error_handler(INVALIDFONT) ;
}

static void invalid_end_subr(void *data, uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(uint8 **, subrstr) ;
  UNUSED_PARAM(uint32 *, subrlen) ;
}

static Bool invalid_begin_seac(void *data,
                               int32 stdindex,
                               uint8 **seacstr, uint32 *seaclen)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, stdindex) ;
  UNUSED_PARAM(uint8 **, seacstr) ;
  UNUSED_PARAM(uint32 *, seaclen) ;
  return error_handler(INVALIDFONT) ;
}

static void invalid_end_seac(void *data, uint8 **seacstr, uint32 *seaclen)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(uint8 **, seacstr) ;
  UNUSED_PARAM(uint32 *, seaclen) ;
}

charstring_methods_t pstype1_charstring_fns = {
  NULL,  /* private data */
  invalid_get_info,
  invalid_begin_subr,
  invalid_end_subr,
  invalid_begin_seac,
  invalid_end_seac
} ;
#endif

/*
Log stripped */
