/** \file
 * \ingroup fonttype3
 *
 * $HopeName: COREfonts!src:pstype3.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font methods for PostScript Type 3 fonts. These routines find the
 * information required for Type 3 BuildChar/BuildGlyph interpretation from a
 * VM-based PostScript Type 3 font.
 */

/** \defgroup fonttype3 PostScript Type 3 fonts
    \ingroup fonts */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "objects.h"  /* NAMECACHE, OBJECT */
#include "swerrors.h" /* error_handler, INVALIDFONT */
#include "context.h"  /* charcontext_t, CoreContext */
#include "namedef_.h"

#include "graphics.h" /* FONTinfo */
#include "fcache.h"   /* fontcache_base_key */

#include "fonts.h"
#include "fontcache.h"
#include "cidfont.h"

/* Exported definition of the font methods for VM-based Type 3 fonts */
static int32 ps3_lookup_char(FONTinfo *fontInfo,
                             charcontext_t *context) ;
static int32 ps3_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static void ps3_end_char(FONTinfo *fontInfo,
                         charcontext_t *context) ;

font_methods_t font_type3_fns = {
  fontcache_base_key,
  ps3_lookup_char,
  NULL, /* No subfont lookup */
  ps3_begin_char,
  ps3_end_char
} ;

/* Exported definition of the font methods for VM-based CID Font Type 1
   fonts */
static int32 cid1_lookup_char(FONTinfo *fontInfo,
                              charcontext_t *context) ;

font_methods_t font_cid1_fns = {
  fontcache_cid_key,
  cid1_lookup_char,
  NULL, /* No subfont lookup */
  ps3_begin_char,
  ps3_end_char
} ;

/*---------------------------------------------------------------------------*/
/* Font lookup and top-level charstring routines for VM-based Type 3 fonts */
static int32 ps3_lookup_char(FONTinfo *fontInfo,
                             charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == FONTTYPE_3 ||
           theIFontType(fontInfo) == FONTTYPE_4, "Not in a Type 3 or 4 font") ;

#if 0
  /* If it weren't that this messes up the object used as a cache key, this
     would be very elegant, and would replace some of the code at in
     start_scan_char. It would allow the proc and index lookup to be done by
     all Type 3 variants and glyph replacement procedures uniformly.
     Unfortunately, it reverts to keying BuildChar off the character number
     rather than name, which gives spurious results when multiple encodings
     are used. */
  if ( (theo = fast_extract_hash_name(&theMyFont(fontInfo),
                                      NAME_BuildGlyph)) != NULL ) {
    Copy(&context->definition, theo) ;
    theTags(context->glyphname) = ONAME | LITERAL ;
    oName(context->glyphname) = selector->name ;
  } else if ( (theo = fast_extract_hash_name(&theMyFont(fontInfo),
                                             NAME_BuildChar)) != NULL ) {
    Copy(&context->definition, theo) ;

    /* Setup the character index on the operand stack */
    if ( context->glyphchar == NO_CHARCODE ) {
      /* This is what Adobe do; if the glyphname couldn't be found in the
         Encoding vector then find the first /.notdef character (which
         includes ONULL!) from the end of the array. */
      if ( ! get_edef(&system_names[NAME_notdef] , &context->glyphchar) )
        return error_handler( INVALIDFONT ) ;
    }

    theTags(context->glyphname) = OINTEGER ;
    oInteger(context->glyphname) = glyphchar ;
  } else
    return error_handler(INVALIDFONT) ;
#endif

  context->chartype = CHAR_BuildChar ;

  return TRUE ;
}

/* Type 3 procedure fonts don't do anything here at the moment; it's all
   in start_scan_char. When that routine is modularised and converted to
   a font provider, the char methods may take responsibility for pushing
   the appropriate dictionaries and procs on the stack. */
static int32 ps3_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;

  HQASSERT(context, "No char context") ;

  HQASSERT(context->chartype == CHAR_BuildChar,
           "No decision about char type") ;

  return TRUE ;
}

static void ps3_end_char(FONTinfo *fontInfo,
                         charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;
}

/*---------------------------------------------------------------------------*/
/* CID font type 1 is similar to Type 3, except that a CID is used instead
   of a glyph name. It doesn't have or need an Encoding array, so avoid the
   lookups that happen for Type 3. */
static int32 cid1_lookup_char(FONTinfo *fontInfo,
                              charcontext_t *context)
{
  OBJECT *theo ;

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE1, "Not in a CID Type 1 font") ;
  /* PLRM3 p. 377: CID Type 1 does not allow BuildChar routines, only
     BuildGlyph. */
  if ( (theo = fast_extract_hash_name(&theMyFont(*fontInfo),
                                      NAME_BuildGlyph)) == NULL )
    return error_handler(INVALIDFONT) ;

  Copy(&context->definition, theo) ;

  context->chartype = CHAR_BuildChar ;

  return TRUE ;
}

/*
Log stripped */
