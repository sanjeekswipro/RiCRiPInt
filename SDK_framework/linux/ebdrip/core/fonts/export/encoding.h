/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:encoding.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for initialising and using encodings
 */

#ifndef __ENCODING_H__
#define __ENCODING_H__

#include "objects.h"

extern NAMECACHE *StandardEncoding [ 256 ] ;
extern NAMECACHE *ISOLatin1Encoding[ 256 ] ;
extern NAMECACHE *MacExpertEncoding[ 256 ] ;
extern NAMECACHE *MacOSEncoding    [ 256 ] ;
extern NAMECACHE *MacRomanEncoding [ 256 ] ;
extern NAMECACHE *PDFDocEncoding   [ 256 ] ;
extern NAMECACHE *WinAnsiEncoding  [ 256 ] ;
extern NAMECACHE *MacTTGlyphNames  [ 258 ] ; /* N.B. 258 entries */

Bool initEncodings(void);

/* Log stripped */
#endif /* protection for multiple inclusion */
