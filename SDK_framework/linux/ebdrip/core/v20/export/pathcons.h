/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:pathcons.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS path construction API
 */

#ifndef __PATHCONS_H__
#define __PATHCONS_H__

#define ARCT_LINETO     (0x01)
#define ARCT_MOVETO     (0x02)
#define ARCT_CURVETO    (0x04)
#define ARCT_ARC        (0x08)
#define ARCT_ARCN       (0x10)
Bool arct_convert(SYSTEMVALUE cx, SYSTEMVALUE cy,
                  SYSTEMVALUE args[8], SYSTEMVALUE tangents[4],
                  int32* flags);
Bool gs_newpath(void);

Bool gs_currentpoint(PATHINFO *path, SYSTEMVALUE *currx, SYSTEMVALUE *curry);

#endif /* protection for multiple inclusion */

/* Log stripped */
