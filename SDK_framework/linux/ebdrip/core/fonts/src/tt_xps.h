/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:tt_xps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * An interface between the CFF and TT code to support XPS metrics.
 */

#ifndef __TT_XPS_H__
#define __TT_XPS_H__

#include "coretypes.h" /* Bool, USERVALUE, int32 */
#include "objects.h"   /* OBJECT */
#include "graphict.h"  /* FONTinfo */

/** \brief
 * Callback from adobe_cache when rendering a CFF OpenType in XPS.
 *
 * \param[in] fontInfo
 *   The FONTinfo of the current font.
 * \param[in] glyphname
 *   The OINTEGER glyph identifier.
 * \param[in] bbox
 *   The bounding box of the outline.
 * \param[in,out] metrics
 *   The metrics array to update.
 * \param[in,out] mflags
 *   The metrics content flags will also be updated.
 * \return
 *   \c TRUE if there was no error.
 */
Bool tt_xps_metrics( FONTinfo *fontInfo,
                    OBJECT* glyphname,
                    SYSTEMVALUE bbox[4],
                    SYSTEMVALUE metrics[ 10 ],
                    int32* mflags );

/* $Log
 */
#endif
