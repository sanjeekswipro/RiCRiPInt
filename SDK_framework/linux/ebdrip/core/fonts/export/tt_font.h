/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:tt_font.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief The interface between the TrueType cache and the Core.
 */

#ifndef __TT_FONT_H__
#define __TT_FONT_H__


/* Forward references (don't include headers, they rely on too much other
   stuff) */
struct NAMECACHE ; /* from COREobjects */
struct OBJECT ;    /* from COREobjects */
struct LINELIST ;  /* from SWv20/COREgstate */

#include "fontt.h" /* charcontext_t */

/** \brief
 * Build an outline for a TrueType character, and either render it to a cache,
 * add it to the display list, or leave it in the gstate for charpath,
 * depending on the value of the context's show type.
 *
 * \param[in] context
 *   The core context of the thread constructing the character.
 * \param[in, out] charcontext
 *   The character context information, containing the definition accessors for
 *   the character, and destination rendering details. On successful exit, the
 *   \c cptr field will contain the character cache (if applicable), the \c
 *   xwidth and \c ywidth fields will contain the advance width.
 * \param[in] currpt
 *   The current point at which the character will be rendered, in device space.
 * \param[in] xps
 *   Whether XPS vertical positioning and advance height calculations should be
 *   performed. PS rules will be used otherwise.
 * \return
 *   \c TRUE if interpretation succeeds, \c FALSE otherwise.
 */
Bool tt_cache(corecontext_t *context,
              charcontext_t *charcontext, struct LINELIST *currpt, Bool xps);

/** \brief
 * Build an output encoding suitable for a TT font for use in PDF Out.
 *
 * \param[in] font
 *   The font object.
 * \param[out] encoding
 *   The output encoding, as an array of names.
 * \return
 *   \c TRUE on success, \c FALSE otherwise.
 */
Bool pdfout_buildttencoding(struct OBJECT *font,
                            struct NAMECACHE *encoding[256]) ;

/**
 * \brief
 * Return the font glyph index for the Unicode codepoint by using the Unicode
 * cmap. Also return the cmap, for reuse in later lookups.
 *
 * \param[out] gindex
 *   Pointer to returned font glyph index.
 * \param[in] font
 *   Pointer to the PostScript font.
 * \param[in] codepoint
 *   Unicode codepoint.
 * \param[in,out] cmap
 *   Pointer to a null pointer to cause cmap to be found and returned. The
 *   returned value for subsequent calls in the same font.
 *
 * \return
 *   \c TRUE if a glyph index can be found for the Unicode codepoint, else \c
 *   FALSE.
 */
Bool tt_xps_lookup(
/*@out@*/ /*@notnull@*/
  int32*  gindex,
/*@in@*/ /*@notnull@*/
  struct OBJECT* font,
  int32   codepoint,
/*@out@*/ /*@notnull@*/
  void**  cmap);

/**
 * \brief
 * Return the maximum glyph index for the font, from numGlyphs in the maxp table.
 *
 * \param[in] font
 *   Pointer to the PostScript font.
 * \param[out] max_index
 *   The maximum value (inclusive) of a glyph index.
 *
 * \return
 *   \c TRUE on success, \c FALSE otherwise.
 */
Bool tt_xps_maxindex(
/*@in@*/ /*@notnull@*/
  struct OBJECT* font,
/*@out@*/ /*@notnull@*/
  int32*  max_index);

/**
 * \brief
 * Return an invented name for a glyph ID in the form 'gid#####'.
 *
 * \param[in] gid
 *   The glyph ID, in the range 0 to 65534.
 * \return
 *   A pointer to a \c NAMECACHE of the name.
 */
struct NAMECACHE *tt_fake_glyphname(
  uint16 gid);

/*
* Log stripped */
#endif
