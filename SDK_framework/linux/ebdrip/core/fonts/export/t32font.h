/** \file
 * \ingroup fonttype32
 *
 * $HopeName: COREfonts!export:t32font.h(EBDSDK_P.1) $:
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for CID Type 4 (Type 32) fonts
 */

#ifndef __T32FONT_H__
#define __T32FONT_H__

/** \defgroup fonttype32 CID Type 4 (Type 32) fonts
    \ingroup fonts */
/** \{ */

struct LINELIST ; /* from SWv20 (will be COREgstate) */

#include "fontt.h"

/** \brief
 * Render a Type 32/CID 4 character.
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
 * \param[in] gid
 *   The gstate identifier of the GST_SETCHARDEVICE frame, or -1 if no
 *   gstate has been pushed; this is used to avoid bracketing the character
 *   if it has already been done so.
 * \return
 *   \c TRUE if interpretation succeeds, \c FALSE otherwise.
 */
Bool t32_plot(corecontext_t *context,
              charcontext_t *charcontext, struct LINELIST *currpt, int32 gid) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
