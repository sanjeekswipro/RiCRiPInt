/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_version!copyrite.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Harlequin rip copyright information
 */

#ifndef __COPYRITE_H__
#define __COPYRITE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define LATEST_COPYRIGHT_YEAR "2013"
#define COPYRIGHT_FROM        "1989-"
#define THE_HARLEQUIN_GROUP   "Global Graphics Software Ltd"

#define COPYRIGHT_PREFIX      "Copyright"
#define COPYRIGHT_SUFFIX      COPYRIGHT_FROM LATEST_COPYRIGHT_YEAR " " THE_HARLEQUIN_GROUP "."

/* For compatibility */
#define COPYRIGHT             COPYRIGHT_PREFIX " "
#define SOFTWARE_COPYRIGHT    "Software copyright "
#define STANDARD_COPYRIGHT    COPYRIGHT COPYRIGHT_FROM
/*********************/

#define COPYRIGHT_NOTICE      COPYRIGHT_PREFIX " " COPYRIGHT_SUFFIX

#ifdef __cplusplus
}
#endif


#endif /* ! __COPYRITE_H__ */
