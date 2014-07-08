/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:charstring12.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type 1 and 2 charstring methods. This is one of several definitions for
 * charstring_methods_t. This one defines the callbacks the Type 1 and Type 2
 * charstring interpreters use to get at data from a font definition.
 * Different Type 1 and 2 fonts (CIDFontType 0, DLD1, Type 1, Type 4, CFF)
 * will provide different methods to get at the data.
 */

#ifndef __CHARSTRING12_H__
#define __CHARSTRING12_H__

#include "objects.h"

struct charstring_methods_t {
  /* A private data pointer, passed to the methods below. Be careful with this;
     if the charstring methods are allocated statically, they will need to be
     saved and restored around recursive plotchar calls, or this pointer
     may be changed unintentionally. */
  void *data ;

  /* get_info is used to get named bits of information from the font dict
     and private dict. The keys that can be accessed are:

     lenIV, defaultWidthX, nominalWidthX, lenBuildCharArray, WeightVector*,
     NormalizedDesignVector*, UserDesignVector*, HintedFonts, OtherSubrs*,
     BlueValues*, OtherBlues*, FamilyBlues*, FamilyOtherBlues*, BlueScale,
     BlueShift, BlueFuzz, StdHW*, StdVW*, StemSnapH*, StemSnapV*, ForceBold,
     ForceBoldThreshold, LanguageGroup, RndStemUp, UniqueID, ExpansionFactor,
     initialRandomSeed, XUID*, FID, SubFont

     The value will normally be returned as an OINTEGER, OREAL or OBOOLEAN
     through the object supplied. If a data item does not exist, the object
     should be set to ONULL. The arrays (marked *) are special; if called
     with a negative index, the length of the array will be returned in the
     object as an integer. When called with a non-negative index, the indexed
     value will be returned from the array (this could be a composite object
     for OtherSubrs; others should be OINTEGER or OREAL). Most of the names
     refer to information in Type 1 and 2 font dictionaries and private
     dictionaries. SubFont is an integer identifier for the particular
     sub-font of a CID font selected. */
  int32 (*get_info)(void *data, int32 nameid, int32 index, OBJECT *object) ;
 
  /* begin_subr is terminated by end_subr. end_subr MUST be called with the
     same arguments as begin_subr if that function succeeds, even if code
     between begin_subr and end_subr fails. */
  int32 (*begin_subr)(void *data,
                      int32 subrno, int32 global,
                      uint8 **subrstr, uint32 *subrlen) ;
  void (*end_subr)(void *data, uint8 **subrstr, uint32 *subrlen) ;

  /* begin_seac is terminated by end_seac. end_seac MUST be called with the
     same arguments as begin_seac if that function succeeds, even if code
     between begin_seac and end_seac fails. */
  int32 (*begin_seac)(void *data,
                      int32 stdindex,
                      uint8 **seacstr, uint32 *seaclen) ;
  void (*end_seac)(void *data, uint8 **seacstr, uint32 *seaclen) ;

} ;

/* We need to know about the normal PS Type 1 charstring functions so we
   can copy them for use in Type 4 fonts (buildchar with a Type 1 proc). */
extern charstring_methods_t pstype1_charstring_fns ;

/* PostScript Type 1, CID Type 0 and DLD1 fonts share an info function that
   interrogates the PostScript font dictionary. */
int32 ps1_get_info(void *data, int32 name, int32 index, struct OBJECT *theo) ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
