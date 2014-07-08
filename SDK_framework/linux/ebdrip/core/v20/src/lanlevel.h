/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:lanlevel.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS language level API
 */

#ifndef __LANLEVEL_H__
#define __LANLEVEL_H__

/* Exported functions */

extern int32 setlanguagelevel(corecontext_t *context) ;

/* Exported Variables */

extern OBJECT level1sysdict ;
extern OBJECT level2sysdict ;
extern OBJECT level3sysdict ;

extern OBJECT *initLanguageLevelDicts(void);
extern void finishLanguageLevelDicts(void);


#endif /* protection for multiple inclusion */

/* Log stripped */
