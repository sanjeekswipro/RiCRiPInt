/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:agl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Headers for Adobe Glyph List encoding routines
 */

#ifndef __AGL_H__
#define __AGL_H__

/* Return AGL name for a unicode character. The AGL may contain multiple names
   for each code point, we pick the "most definitive" by using the WGL name
   in preference to others. */
NAMECACHE *agl_getname(uint16 unicode) ;

/* Find a Unicode for an AGL name. Returns FALSE if the name is not found. */
Bool agl_getencoding(NAMECACHE *name, uint16 *code) ;

/* Step through the whole AGL array */
Bool agl_iterate(int32* index, uint8** name, uint16* len, uint16* code) ;

/*  
Log stripped */

#endif /* protection for multiple inclusion */
