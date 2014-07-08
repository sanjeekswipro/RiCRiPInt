/** \file
 * \ingroup fonttype1
 *
 * $HopeName: COREfonts!export:adobe1.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for Adobe Type 1 font interpretation
 */

#ifndef __ADOBE1_H__
#define __ADOBE1_H__

/** \defgroup fonttype1 Adobe Type 1 font interpretation
    \ingroup fonts */
/** \{ */

#include "fontt.h"

/* Forward declarations */
struct OBJECT ;   /* from COREobjects */
struct LINELIST ; /* from SWv20/COREgstate */
struct FONTinfo ; /* from SWv20/COREgstate */
struct PATHINFO ; /* from SWv20/COREgstate */

/* --- Exported Functions --- */

/** \brief
 * Build an outline for a Type 1/Type 2 character, and either render it to a
 * cache, add it to the display list, or leave it in the gstate for charpath,
 * depending on the value of the context's show type.
 *
 * \param[in] context
 *   The core context of the thread constructing the character.
 * \param[in,out] charcontext
 *   The character context information, containing the definition accessors for
 *   the character, and destination rendering details. On successful exit, the
 *   \c cptr field will contain the character cache (if applicable), the \c
 *   xwidth and \c ywidth fields will contain the advance width.
 * \param[in] protected
 *   The protection type of the font. This routine will not decrypt protected
 *   outlines (use \c adobe_cache_encrypted for that), but will use this to
 *   mark the protection flags of the outline generated.
 * \param[in] currpt
 *   The current point at which the character will be rendered, in device space.
 * \param[in] xps
 *   True if XPS metrics are to be used for OpenType CFFs.
 * \return
 *   \c TRUE if interpretation succeeds, \c FALSE otherwise.
 */
Bool adobe_cache(corecontext_t *context,
                 charcontext_t *charcontext,
                 int32 protected,
                 struct LINELIST *currpt,
                 Bool xps);

/** \brief
 * Build an outline for a (possibly encrypted) Type 1/Type 2 character, and
 * either render it to a cache, add it to the display list, or leave it in the
 * gstate for charpath, depending on the value of the context's show type.
 *
 * \param[in] context
 *   The core context of the thread constructing the character.
 * \param[in,out] charcontext
 *   The character context information, containing the definition accessors for
 *   the character, and destination rendering details. On successful exit, the
 *   \c cptr field will contain the character cache (if applicable), the \c
 *   xwidth and \c ywidth fields will contain the advance width.
 * \param[in] protected
 *   The protection type of the font. This routine will decrypt protected
 *   outlines, using this protection type first, then trying any font decoding
 *   filters present.
 * \param[in] currpt
 *   The current point at which the character will be rendered, in device space.
 * \param[in] xps
 *   True if XPS metrics are to be used for OpenType CFFs.
 * \return
 *   \c TRUE if interpretation succeeds, \c FALSE otherwise.
 */
Bool adobe_cache_encrypted(corecontext_t *context,
                           charcontext_t *charcontext,
                           int32 protected,
                           struct LINELIST *currpt,
                           Bool xps);

/** \brief
 * Build an outline for a Type 4 character, described by a string object,
 * either rendering it to a cache, adding it to the display list, or leaving
 * it in the gstate for charpath, depending on the value of the context's show
 * type.
 *
 * \param[in] context
 *   The core context of the thread constructing the character.
 * \param[in,out] charcontext
 *   The character context information, containing the definition accessors for
 *   the character, and destination rendering details. On successful exit, the
 *   \c cptr field will contain the character cache (if applicable), the \c
 *   xwidth and \c ywidth fields will contain the advance width.
 * \param[in] fontInfo
 *   The font info structure of the font.
 * \param[in] stringo
 *   The string object containing a Type 1 charstring.
 * \param[in] protection
 *   The protection type of the font. This routine will decrypt protected
 *   outlines, using this protection type first, then trying any font decoding
 *   filters present.
 * \param[in] currpt
 *   The current point at which the character will be rendered, in device space.
 * \return
 *   \c TRUE if interpretation succeeds, \c FALSE otherwise.
 */
Bool adobe_cache_type4(corecontext_t *context,
                       charcontext_t *charcontext,
                       struct FONTinfo *fontInfo,
                       OBJECT *stringo,
                       int32 protection,
                       struct LINELIST *currpt) ;


/* --- Exported Variables --- */
extern struct PATHINFO adobepath;

/* --- Macros for controlling support of FontType 5 == DLD1 feature --- */

/* either hit this file to undef TYPE5isDLD1 below, or use compile flags to
 * set TYPE5_DLD1_FONTS to 1 or 0 to force or disable it externally.
 */

#ifdef TYPE5_DLD1_FONTS
#if TYPE5_DLD1_FONTS    /* only define feature on if this is TRUE */
#define TYPE5isDLD1
#endif
#else                   /* no external control */
                        /* default is ON */
#define TYPE5isDLD1

#endif

#define _T_DLD1 (0x6f)
#define _T_FIVE (5)

#ifdef TYPE5isDLD1

#define         DLD1_CASE               _T_FIVE: /* DROPTHRU */ case _T_DLD1
#define         FONT_IS_DLD1( _t_ )     (_T_FIVE == (_t_) || _T_DLD1 == (_t_))

#else /* TYPE5is -NOT- DLD1 */

#define         DLD1_CASE               _T_DLD1
#define         FONT_IS_DLD1( _t_ )     (_T_DLD1 == (_t_))

#endif /* TYPE5isDLD1 */

/** \} */

#endif /* protection for multiple inclusion */

/*
Log stripped */
