/** \file
 * \ingroup ccitt
 *
 * $HopeName: COREccitt!src:ccittdat.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Data tables for CCITTFax filters.
 */

#ifndef __CCITTDAT_H__
#define __CCITTDAT_H__


/* external variables defined in ccittdat.c */
extern TERMCODE white_terminators[] ;
extern TERMCODE black_terminators[] ;
extern TERMCODE white_makeup_codes[] ;
extern TERMCODE black_makeup_codes[] ;
extern TERMCODE extended_makeup_codes[] ;
extern TERMCODE twod_codetable[] ;
extern TERMCODE Data0 ;
extern TERMCODE Data1 ;
extern TERMCODE EndOfLine ;
extern TERMCODE uncompressed_codes[] ;
extern IMAGETABLE uncompressed_image[] ;

#define CCITT_UNCI_EXIT_LEVEL 6
#define CCITT_MORE_UNCI( _code ) ((_code) < CCITT_UNCI_EXIT_LEVEL )

#endif /* protection for multiple inclusion */

/* Log stripped */
