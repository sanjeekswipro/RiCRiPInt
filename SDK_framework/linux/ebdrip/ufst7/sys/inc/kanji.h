
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* kanji.h */


#ifndef __KANJI__
#define __KANJI__

#define JIS                   0xFFFF   /* JIS character encoding */
#define SHIFT_JIS             0xFFFE   /* Shift-JIS character encoding */
#define EUC                   0xFFFD   /* EUC character encoding */
#define UFST_UNICODE          0xFFFC   /* UNICODE character encoding */
#define BIG5                  0xFFFB   /* BIG5 character encoding */
#define TCA                   0xFFFA   /* TCA character encoding */
#define GB                    0xFFF9   /* GB character encoding */
#define KSC                   0xFFF8   /* K character encoding */
#define WANSUNG               0xFFF4   /* WANSUNG character encoding - KOREAN */
#define JOHAB                 0xFFF3   /* JOHAB character encoding - KOREAN */
#define SYMBOL                0xFFF2   /* SYMBOL character encoding */	/* 08-29-02 jfd */

#define RAW_GLYPH			  0xFF11   /* special symbol-set value for raw-glyph-index mode */
									   /* currently only relevant for TrueType */
#define UCS4                  0xFF12   /* UCS-4 character encoding  03-05-02 jfd */
#define USER_CMAP			  0xFF13   /* use passedin user platId / specId values */

/*** moved from cgif.h */
#define DL_SYMSET          0x8000   /* ssnum downloadable symbol set flag */

/*** PS related items (moved from sym.h) ***/
/*  CFFENCODING is defined with the same value as T1ENCODING, but     */
/*  no conflict should arise because a font cannot be CFF and regular */
/*  Type 1 at the same time.  The processing code paths are separate. */
/*                                                                    */
#define     CFFENCODING 0x8100   /* CFF Type2 Encoding */
#define     CFFCHARSET  0x8200   /* CFF SID values, and Charset Array */
#define     T1ENCODING  0x8100   /* Type 1 Encoding */

#define MASK_ASIAN_ENC        0xFF00   /* mask for any Asian encoding */

#define is_it_Asian(ss) ((ss & MASK_ASIAN_ENC) == MASK_ASIAN_ENC)

/*
 * Unicode mapping tables, values correspond with possible
 * values for UNICODE_MAPPING in cgconfig.h
 */
#define JIS2UNI_MAP           0x01
#define BIG52UNI_MAP          0x02
#define GB2UNI_MAP            0x04
#define KSC2UNI_MAP           0x08
#define JOHAB2UNI_MAP         0x10 

#if (UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP))
typedef struct {
  UW16 unicode;
  UW16 jis;
} UNIJISMAP;
typedef UNIJISMAP * PUNIJISMAP;
#endif /* (UNICODE_IN || (UNICODE_MAPPING & JIS2UNI_MAP)) */

#if (UNICODE_IN || (UNICODE_MAPPING & KSC2UNI_MAP))
typedef struct {
  UW16 unicode;
  UW16 ksc;
} UNIKSCMAP;
typedef UNIKSCMAP * PUNIKSCMAP;
#endif /* (UNICODE_IN || (UNICODE_MAPPING & KSC2UNI_MAP)) */

#if (UNICODE_IN || (UNICODE_MAPPING & BIG52UNI_MAP))
typedef struct {
  UW16 unicode;
  UW16 big5;
} UNIBIG5MAP;
typedef UNIBIG5MAP * PUNIBIG5MAP;
#endif /* (UNICODE_IN || (UNICODE_MAPPING & BIG52UNI_MAP)) */

#if (UNICODE_IN || (UNICODE_MAPPING & GB2UNI_MAP))
typedef struct {
  UW16 unicode;
  UW16 gb;
} UNIGBMAP;
typedef UNIGBMAP * PUNIGBMAP;
#endif /* (UNICODE_IN || (UNICODE_MAPPING & GB2UNI_MAP)) */

#if (UNICODE_IN || (UNICODE_MAPPING & JOHAB2UNI_MAP))
typedef struct {
  UW16 unicode;
  UW16 johab;
} UNIJOHABMAP;
typedef UNIJOHABMAP * PUNIJOHABMAP;
#endif /* (UNICODE_IN || (UNICODE_MAPPING & JOHAB2UNI_MAP)) */
#endif	/* __KANJI__ */
