/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:fontt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font type definitions
 */

#ifndef __FONTT_H__
#define __FONTT_H__

/* Do NOT include any other headers, or make this file depend on any other
   headers */
typedef struct MATRIXCACHE MATRIXCACHE ; /* fontcache.h */
typedef struct FONTCACHE FONTCACHE ;     /* fontcache.h */

typedef struct char_selector_t char_selector_t ; /* charsel.h */
typedef struct charcontext_t charcontext_t ;     /* fonts.h */

typedef struct charstring_build_t charstring_build_t ; /* chbuild.h */
typedef struct charstring_methods_t charstring_methods_t ;
typedef struct font_methods_t font_methods_t ;

/* Enumeration for charstring types */
enum { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  CHAR_Undecided = 0,  /* Don't know yet */
  CHAR_Type1 = 1,      /* Type 1 charstrings */
  CHAR_Type2 = 2,      /* Type 2 charstrings */
  CHAR_BuildChar = 3,  /* BuildChar/BuildGlyph proc, including type 4 */
  CHAR_Bitmap = 32,    /* Direct cache entry */
  CHAR_TrueType = 42,  /* TrueType charstrings */
  CHAR_PFIN = 127,     /* PFIN charstrings */
  CHAR_Undefined = 255 /* No such charstring */
} ;

/* Enumeration for font types. CID types are in cidfont.h */
enum {
  FONTTYPE_0 = 0,       /* Composite */
  FONTTYPE_1 = 1,       /* Adobe Type1 */
  FONTTYPE_CFF = 2,     /* Compact Font Format */
  FONTTYPE_3 = 3,       /* PS outlines */
  FONTTYPE_4 = 4,       /* encrypted */
  FONTTYPE_DLD0 = 5,    /* a synonym for DLD1 */
  FONTTYPE_9 = 9,       /* CIDFontType0 (Type1) */
  FONTTYPE_10 = 10,     /* CIDFontType1 (Type3) */
  FONTTYPE_11 = 11,     /* CIDFontType2 (TT) */
  FONTTYPE_Chameleon = 14,
  FONTTYPE_32 = 32,     /* CIDFontType4 (Bitmaps) */
  FONTTYPE_TT = 42,     /* TrueType */
  FONTTYPE_102 = 102,   /* CIDFontType0C (CFF) */
  FONTTYPE_DLD1 = 111,  /* HQN DLD1 */
  FONTTYPE_PFIN = 126,  /* PFIN font */
  FONTTYPE_PFINCID = 127/* CIDFontTypePFIN (PFIN) */
} ;


/** Enumeration for font and fontcache purging levels */
enum {
  FONT_PURGE_LITTLE = 1,
  FONT_PURGE_SOME = 3,
  FONT_PURGE_LOTS = 7,
  FONT_PURGE_ALL = 15,
} ;


#if 0
/* NYI character providers */

struct FONTinfo ;    /* from SWv20, COREgstate */
struct FORM ;        /* from SWv20, CORErender */

typedef struct char_outline_t char_outline_t ;
typedef struct char_escapement_t char_escapement_t ;
typedef struct char_providers_t char_escapement_t ;

typedef int32 (*char_didl_provider_fn)(struct FONTinfo *fontinfo,
                                       charcontext_t *context,
                                       charstring_methods_t *charfns) ;
typedef int32 (*char_metrics_provider_fn)(struct FONTinfo *fontinfo,
                                          charcontext_t *context,
                                          charstring_methods_t *charfns,
                                          char_escapement_t *escapement,
                                          sbbox_t *bbox) ;
typedef int32 (*char_outline_provider_fn)(struct FONTinfo *fontinfo,
                                          charcontext_t *context,
                                          charstring_methods_t *charfns,
                                          char_escapement_t *escapement,
                                          char_outline_t *outline) ;
typedef int32 (*char_raster_provider_fn)(struct FONTinfo *fontinfo,
                                         charcontext_t *context,
                                         charstring_methods_t *charfns,
                                         char_escapement_t *escapement,
                                         struct FORM **raster) ;

/* NYI charstring processors */
typedef struct charstring_processor_t charstring_processor_t ;
#endif

/*
Log stripped */
#endif /* protection for multiple inclusion */
