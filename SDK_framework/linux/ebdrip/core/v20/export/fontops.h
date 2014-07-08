/** \file
 * \ingroup fonts
 *
 * $HopeName: SWv20!export:fontops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font operations; define, scale, select, compose, blend, matrix manipulation.
 */

#ifndef __FONTOPS_H__
#define __FONTOPS_H__

#include "objecth.h"  /* OBJECT */
#include "matrix.h"   /* OMATRIX */
#include "graphics.h" /* MFONTLOOK */

/* --- Exported Functions --- */
Bool gs_setfont(OBJECT *fontdict) ;
void gs_setfontctm(OMATRIX *fontmatrix) ;

Bool font_derivation(OBJECT *fonto, OBJECT *basename,
                     OBJECT *basedict, Bool *embedded) ;
Bool calc_font_height(SYSTEMVALUE *height);


/* --- Exported Variables --- */
extern int32 mflookpos ;
extern MFONTLOOK *oldmfonts ;
extern MFONTLOOK **poldmfonts ;
extern OBJECT  lfontdirobj ;
extern OBJECT  gfontdirobj ;
extern OBJECT *lfontdirptr ;
extern OBJECT *gfontdirptr ;
extern OBJECT * fontdirptr ;

#define FID_PROTECTED 0x80000000

/*
Log stripped */
#endif /* protection for multiple inclusion */
