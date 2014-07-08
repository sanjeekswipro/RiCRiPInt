/** \file
 * \ingroup cmap
 *
 * $HopeName: COREfonts!export:cmap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font Character map (CMap) API
 */

#ifndef __CMAP_H__
#define __CMAP_H__

/** \defgroup cmap Font Character map (CMap)
    \ingroup fonts */
/** \{ */

int32 cmap_lookup(OBJECT *cmapdict, OBJECT *stringo,
                  int32 *fontid, OBJECT *charsel);

int32 cmap_lookup_notdef(OBJECT *cmapdict, OBJECT *stringo,
			 int32 *fontid, OBJECT *charsel);

int32 cmap_getfontmatrix(OBJECT *cmapdict, int32 fontid,
                         OBJECT *matrix);

int32 cmap_getwmode(OBJECT *cmapdict, int32 *wmode);

typedef Bool (*cmap_iterator_fn)(OBJECT *mapsfrom, OBJECT *mapsto,
                                 uint32 ncodes, void *data) ;
Bool cmap_walk(OBJECT *cmapdict, cmap_iterator_fn iterator, void *data) ;

/* Constants
 *
 * Multi-byte character codes can be upto four bytes long (these may
 * input to and output from cmap_lookup.
 *
 * CMap lookups may recurse to a maximum depth of 5 levels
 * although at present, the code does not use, and is not restricted
 * to this limit.
 * (These are as per the Type0/CMap Adobe Spec)
 */
#define CMAP_MAX_CODESPACE_LEN 4
#define CMAP_MAX_LOOKUP_DEPTH  5

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
