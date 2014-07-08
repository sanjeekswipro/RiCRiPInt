/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:mtchptrn.h(EBDSDK_P.1) $
 * $Id: src:mtchptrn.h,v 1.5.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header file for mtchptrn.c
 */

#ifndef __MTCHPTRN_H__
#define __MTCHPTRN_H__

#include "mskscalr.h"

/* --Public datatypes-- */

typedef struct MatchPattern_s {
  uint32 setPattern;
  uint32 dontCareMask;
  uint32 targetData;
}MatchPattern;

/* --Public methods-- */
extern MatchPattern* matchPatternAddCandidates(uint32* count);
extern void matchPatternReleaseAddCandidates(void);
extern MatchPattern* matchPatternRemoveCandidates(uint32* count);
extern void matchPatternReleaseRemoveCandidates(void);

/* --Description--

MatchPattern is a helper class for MaskScaler - it creates the set of 
patterns that are used by the MaskScaler object to increase binary image
resolution
*/

#endif

/* Log stripped */
