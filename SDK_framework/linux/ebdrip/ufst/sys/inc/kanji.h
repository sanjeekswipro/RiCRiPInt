/* $HopeName: GGEufst5!sys:inc:kanji.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:kanji.h,v 1.3.8.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:04 $ */

/* kanji.h */
/*
 *
 * REVISION HISTORY
 *_____________________________________________________________________
 * 11-Mar-93 maib Created this file
 * 11-Jun-93 rs   Add JistoSJis() & SJisToJis() externs.
 * 16-Jun-93 jfd  Corrected function prototype for JisToSJis().
 * 30-Jun-93 jfd/gy Added #define for EUC encoding.
 * 23-Jul-93 rs   Add extern prototype for 'EucToSJis()'.
 * 09-Nov-93 gy/my  Add #define for UNICODE encoding.
 * 17-Nov-93 jfd  Added new function prototypes: EucToJis(), JisToEuc() and 
 *                SJisToEuc().
 *                Enabled function prototype SJisToJis().
 * 23-Nov-93 rs   Add define for BIG5 & TCA encoding.
 * 07-Dec-93 maib Added JIS and Unicode mapping support
 * 08-Dec-93 maib Modified method for mapping to Unicode
 * 05-Jan-94 rs   Add support for "GB" encoding.
 * 18-Feb-94 maib Error codes for invalid mappings and mapping table errors added
 * 11-Jul-94 jfd  Moved #define for KJ here from TTKAN.C .
 * 08-Aug-96 mby  #define MASK_ASIAN_ENC 0xFF00
 * 01-Oct-96 jfd  Added prototype for ASIANexit().
 * 18-Nov-96 dlk  Added typedefs for BIG5 and GB mapping, and added function
 *                prototypes for Unicode to BIG5, Unicode to GB, as well as
 *                BIG5 to Unicode, and GB to Unicode.
 * 19-Feb-97 dlk  Added defines for CP936, CP949, and CP950 so that updated
 *                version of 'newtext', and a new 'dot file' generator called
 *                'makadot' will work.  Also began to add support for CODE
 *                PAGE encoded Asian fonts by defining CP9362UNI_MAP,
 *                CP9492UNI_MAP, and CP9502UNI_MAP. 
 * 21-May-97 dlk  Added defines for WANSUNG and JOHAB Korean character encoding.
 * 13-Nov-00 jfd  Added structure JISUNIMAP.
 *                Added pointer PJISUNIMAP.
 * 01-Dec-00 slg  Added "special" symbolset value RAW_GLYPH, to be used for
 *				  raw-glyph-index mode in TrueType (i.e. don't use the cmap
 *				  table). This symbolset needs to be treated as an "Asian"
 *				  symbolset, because there may be more than 255 values.
 *				  Also added a #define to test whether a symbol set is Asian,
 *				  to replace the numerous multiple-SS tests in various places
 *				  (no need to modify it if we add a new Asian encoding).
 * 22-Dec-00 ks	  Removed JISUNIMAP, PJISUNIMAP definitions. 
 * 25-Sep-02 jfd        Added define for SYMBOL encoding.
*/

#ifndef __KANJI__
#define __KANJI__

#define JIS                   0xFFFF   /* JIS character encoding */
#define SHIFT_JIS             0xFFFE   /* Shift-JIS character encoding */
#define EUC                   0xFFFD   /* EUC character encoding */
#define UNICODE               0xFFFC   /* UNICODE character encoding */
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

#endif	/* __KANJI__ */
