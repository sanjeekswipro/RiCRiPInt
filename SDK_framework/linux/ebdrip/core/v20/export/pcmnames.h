/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:pcmnames.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Process Color Model (PCM) names
 */

#ifndef __PCMNAMES_H__
#define __PCMNAMES_H__  (1)

/*
 * Each array consists of NAMECACHE pointers for the process colorants implied
 * by the array name, followed by a NULL pointer.  Hence the array size being 1
 * larger than the number of colorants.  This means the array can be iterated
 * over with an indexing variable for the right number of compoents for the set,
 * or via an advancing pointer which can be terminated when what it points to is
 * a NULL pointer. e.g.
 *
 *  NAMECACHE** pn = pcm...Names;
 *
 *  do {
 *    ...
 *  } while ( *pn++ != NULL );
 */

/* Single colorant PCM - Gray and its complementary Black */
extern NAMECACHE* pcmGyName[2];
extern NAMECACHE* pcmKName[2];

/* Three colorant PCM - RGB, its extended PCM RGBK, and L1 CMYK complementary RGBGy */
extern NAMECACHE* pcmRGBNames[4];
extern NAMECACHE* pcmRGBKNames[5];
extern NAMECACHE* pcmRGBGyNames[5];

/* Four colorant PCM - CMYK, its reduced PCM CMY, and RGBK complementary CMYKGy */
extern NAMECACHE* pcmCMYKNames[5];
extern NAMECACHE* pcmCMYNames[4];
extern NAMECACHE* pcmCMYGyNames[5];

/* All PCM colorante (CMYKRGBGy) in a single array */
extern NAMECACHE* pcmAllNames[9];

#endif /* !__PCMNAMES_H__ */


/* Log stripped */
