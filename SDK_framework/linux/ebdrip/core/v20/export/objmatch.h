/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:objmatch.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header file for object matching callbacks for use with walk_dictionary.
 * This is used for spot function matching and CIE colourspace
 * encode/decode/transform procedure matching.
 */

#ifndef __OBJMATCH_H__
#define __OBJMATCH_H__


typedef struct {
  OBJECT *obj ;
  OBJECT *key ;
} OBJECT_MATCH ;

extern int32 wd_match_obj(OBJECT *key, OBJECT *value, void *arg) ;

/* Log stripped */
#endif /* protection for multiple inclusion */
