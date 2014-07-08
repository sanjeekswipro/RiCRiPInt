/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:fontparam.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Configuration parameters for the COREfonts module.
 */

#ifndef __FONTPARAM_H__
#define __FONTPARAM_H__

enum {
  TrueTypeHints_None,           /* don't do any truetype hinting at all */
  TrueTypeHints_CheckFaults,    /* all hinting errors halt execution */
  TrueTypeHints_SafeFaults,     /* many hinting faults survived, font is reported */
  TrueTypeHints_SilentFaults    /* as above, but no report. */
  /* NOTE! Order is important in fnt.c! */
} ;
/* Not all of the font compound params (FONTSPARAMS) are made available through
   the PostScript interface. Collecting them together in a single structure is
   still good for modularity, though. */
typedef struct FONTSPARAMS {
  uint8 HintedFonts, fontfillrule ;
  int8 MPSOutlines ;            /* MPS outlines derestricted */
  uint8 TrueTypeHints ;
  int32 MaxFontCache, CurFontCache ;
  int32 MaxCacheMatrix, CurCacheMatrix ;
  int32 MaxCacheChars, CurCacheChars ;
  int32 CurCacheFonts ; /* No MaxCacheFonts */
  uint8 ReportFontRepairs ;
  uint8 ForceNullMapping ;
} FONTSPARAMS ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
