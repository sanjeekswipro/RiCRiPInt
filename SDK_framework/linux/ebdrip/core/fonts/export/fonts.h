/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:fonts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External definitions and structures for the COREfonts compound. Include
 * fonth.h if you do not need the full structure definition, or fontt.h if
 * you only need typedefs for forward declarations.
 */

#ifndef __FONTS_H__
#define __FONTS_H__

#include "fonth.h"
#include "objects.h"

struct FONTinfo ;    /* from SWv20/COREgstate */
struct CHARCACHE ;   /* currently SWv20, later will move to COREfonts */
struct render_state_t ; /* from SWv20 */
struct render_blit_t ;  /* from SWv20 */

/**
 * \defgroup fonts Font subsystem.
 * \ingroup core

   These are the main abstractions in the font subsystem. These are:

   1) Font info. This is unpacked from the PostScript VM representation and
      held in the gstate. It contains information used for caching and
      rendering such as the UniqueID and current font matrices. It is set
      lazily by gs_setfont, and populated by set_font. Font info structures
      are saved and restored during composite font and CID font traversal.

      There is no abstraction for an uninstantiated font.

   2) Character selectors. These represent characters extracted from a string
      or CIDs, including the composite font hierarchy traversal history.

   3) Font definition methods. This structure contains methods used to find
      the definition of a character from a font. The character lookup method
      is the first method called; it looks up the definition of a character,
      indicating if it is undefined or defined. The character begin and end
      routines must be well-nested, and are called to gain access to the
      character definition and clean up afterwards.

   4) Charstring methods. These are format-specific methods used to get
      information required by the charstring interpreter routines from a font
      definition. Charstring methods are used during construction of a
      character raster, outline, or metrics, and are accessed through a
      reference in the character context.

   5) Character context. This structure defines the transient information
      required during creation of a character outline, raster, or
      interpretation of character definitions. It is used to store character
      definition data, glyph names for recursive and Type 3 characters, cache
      hits for Type 4, charstring method pointers, etc.

   The remaining abstractions are not yet implemented, but will provide the
   customisation interface to allow the font sub-system to plug in different
   character renderers and interpreters easily.

   6) Character provider. These objects are the main access interface between
      the internal representation of the characters in a font, and the
      clients of the font subsystem. Having created handles to the font and
      worked out the character index, a client will create a chain of
      provider objects, and request the information required about the
      character. There are four subclasses of provider, for metrics,
      outlines, rasters, and DIDL callbacks. The DIDL callback provider can
      be used to implement all of the other methods, however specialised
      rasterisers may decide to replace the providers with custom renders or
      interpreters.

   7) Charstring processors. These are filters to get bytes from a stream,
      doing any decryption required. Charstring filters can be layers on top
      of each other to provide translation between formats.
*/

/** \{ */

/** Internal C structure for building caching characters. The font cache
   information is lexically scoped. The buildchar field indicates if *any*
   procedural character definition is in effect (recursive or not).
   buildthischar indicates if the current plotchar() is building a procedural
   character definition (Type3/Type4/Type32). cachelevel indicates if the
   current plotchar() character definition is cached or not. The glyphname and
   definition routines should not be GC-scanned, they are transient copies
   of other data, only used during plotchar(). */
struct charcontext_t { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  SYSTEMVALUE xwidth, ywidth ;
  OBJECT glyphname ;
  OBJECT definition ;
  int32  glyphchar ;
  struct CHARCACHE *cptr ;
  uint8 cachelevel, buildchar, buildthischar, showtype ;
  uint8 modtype, chartype, bracketed, inverse ;
  charstring_methods_t *methods ;
  struct render_state_t *rs ; /* The state to setup if we cache the character */
  struct render_blit_t *rb ;  /* The state to blit character fills to. */
} ;

enum {
  NO_CHARCODE = -1  /**< Value of glyphchar if the char is unencoded */
} ;

/** Is the current character cached? (independent of whether or not enclosing
 * character is being cached)
 */
enum {
  CacheLevel_Unset = 0, /**< Started character, but no setcachedevice yet. */
  CacheLevel_Uncached,  /**< Non-caching (setcharwidth, setcachedevice or
                             Type1/2/42/DLD etc) */
  CacheLevel_Cached,    /**< Caching setcachedevice, Type1/2/4/42/DLD etc */
  CacheLevel_Found,     /**< Found by non-standard lookup (e.g. T32 wmode) */
  CacheLevel_Error      /**< Removed cache because of error. */
} ;

/** Font format methods. */
struct font_methods_t {
  /** Set a cache key suitable for use in the font cache from the selector
     details. The charcode or CID of the character will also be unpacked for
     use by HDLT and debugging. This routine is expected to set the cache
     key in context->glyphname and the charcode/CID in context->glyphchar. */
  /*@notnull@*/
  Bool (*cache_key)(struct FONTinfo *fontInfo,
                    char_selector_t *selector,
                    charcontext_t *context) ;

  /** Look up the definition of a character. Details on the character selected
     may be unpacked into the char context, ready for begin_char(). The
     context's charstring type value may be set by this routine or by subfont
     selection. If this routine sets the type to be CHAR_Undefined, the CID
     font notdef routine is invoke to look up notdef mappings. This routine
     may set context->definition to an object representing any data required
     to identify the character to later routines. At the time this is called,
     the font cache key in context->glyphname is initialised as is the
     HDLT/debug char index in context->glyphchar. */
  /*@notnull@*/
  Bool (*lookup_char)(struct FONTinfo *fontInfo,
                      charcontext_t *context) ;

  /** Select a sub-font in a CID font. This routine is called within a context
     in which the font info is saved, so any modification to the fontinfo
     will be undone before the next plotchar(). Details on the character
     selected may be unpacked into the char context, ready for
     begin_char(). */
  /*@null@*/
  Bool (*select_subfont)(struct FONTinfo *fontInfo,
                         charcontext_t *context) ;

  /** To access a character definition, the begin callback should be called,
     providing a context into which a reference to the character definition
     and charstring access methods will be put. This same (unmodified) object
     should be passed to the end callback, which will clean up any memory
     allocation. The object created will be a string for Type 1 and Type 2
     charstrings, or a glyph index for TrueType charstrings. The charstring
     type can be accessed through the context's chartype field. */
  /*@notnull@*/
  Bool (*begin_char)(struct FONTinfo *fontInfo,
                      charcontext_t *context) ;
  /*@notnull@*/
  void (*end_char)(struct FONTinfo *fontInfo,
                   charcontext_t *context) ;
} ;


#if 0
/** \todo @@@ TODO FIXME ajcd 2005-09-19: NYI character providers. */

/* When implemented, the char_methods_t and type parameters returned by the
   font definition's begin_char routine will change to a single
   char_provider_t. plotchar() will select the appropriate provider based on
   the show type, and call the provider routine. All of the mechanics of
   unpacking strings and building paths etc. will be inside the provider
   functions. plotchar() will degenerate to the sequence lookup_char(),
   select_subfont(), begin_char(), provider(), end_char(). */
struct char_providers_t {
  char_methods_t *charfns ;
  char_didl_provider_fn    didl_provider ;
  char_metrics_provider_fn metrics_provider ;
  char_outline_provider_fn outline_provider ;
  char_raster_provider_fn  raster_provider ;
} ;

/* Callbacks for font outlines. Call sequence is:

     begin [bbox] (moveto (lineto|curveto)* closepath)* end

   The data pointer is a pointer supplied by the caller of the outline
   provider. The callee may return a false value (0) and call error_handler
   to signal an error on any call. In this case, the only other callback made
   will be the end callback, but only if the begin callback succeeded. The OK
   flag on the end callback will be true if all callbacks succeed, or any
   callback returns false and sets the error value to NO_ERROR, signalling it
   has finished. The callee should return a true value to continue from any
   callback. The bbox callback may be called if the font format supports
   bounding box information. */
struct char_outline_t {
  Bool (*begin)(SYSTEMVALUE wx, SYSTEMVALUE wy, uint8 protection, void *data) ;
  Bool (*bbox)(SYSTEMVALUE bbox, void *data) ;
  Bool (*moveto)(SYSTEMVALUE x, SYSTEMVALUE y, void *data) ;
  Bool (*lineto)(SYSTEMVALUE x, SYSTEMVALUE y, void *data) ;
  Bool (*curveto)(SYSTEMVALUE x1, SYSTEMVALUE y1,
                  SYSTEMVALUE x2, SYSTEMVALUE y2,
                  SYSTEMVALUE x3, SYSTEMVALUE y3, void *data) ;
  Bool (*closepath)(void *data) ;
  Bool (*end)(Bool ok, void *data) ;
} ;

/* Character escapement needs to be returned by all providers*/
struct char_escapement_t {
  SYSTEMVALUE wx, wy ;
} ;

/* NYI charstring processors. Something like this. */
struct charstring_processor_t {
  void *private ;
  Bool (*begin_charstring)(charstring_processor_t *self, void *private) ;
  int32 (*get_byte)(void *private) ; /* EOF when finished */
  void (*end_charstring)(void *private) ;
} ;

#endif

/** \} */

/*
Log stripped */
#endif /* protection for multiple inclusion */
