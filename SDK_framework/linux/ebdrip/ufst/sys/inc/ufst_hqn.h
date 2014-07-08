/** \file
 * \ingroup interface
 *
 * $HopeName: GGEufst5!sys:inc:ufst_hqn.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief  This header file provides definitions for PCL and UFST interfaces
 * between the skin and a UFST PFIN (Pluggable Font Interface) module.
 */

#ifndef __UFST_HQN_H__
#define __UFST_HQN_H__

/* -------------------------------------------------------------------------- */
/** \{ */

#ifdef VXWORKS
#include "vxWorks.h"
#endif

/* UFST */
#include "cgconfig.h"     /* this must be first UFST include */
#include "ufstport.h"     /* this must be second */
#include "shareinc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/** \brief The RDR_CLASS_PCL types */

enum {
  RDR_PCL_SS = 1,     /**< UFST symbolset data */
  RDR_PCL_MAP,        /**< The mapping from UFST FCO to PCL typeface.
                           Length is actually the number of entries. */
  RDR_PCL_XLMAP,      /**< Tables used to map XL font names to PCL5 fonts */
  RDR_PCL_SSMAP,      /**< SymbolSet mapping table.
                           Length is the number of entries. */
  RDR_PCL_GLYPHS      /**< Default encoding in compact form */
} ;

/* ========================================================================== */
/** \brief Structure definitions for the above data types */

/* FONTMAP

   We may have a different font set to the target device, so map UFST's typeface
   numbers to the target device's numbers and font ordering. Fonts that have
   no equivalent in the target device retain their UFST number, and are put at
   the end of the internal font list (and omitted from this table).
  
   In some cases we map the UFST font to two target device typeface numbers, if
   the UFST set does not have duplicates.

   This must match the FCOs provided by the skin.
*/

typedef struct {
  int32  ufst;     /* UFST's "font number" */
  uint16 typeface; /* PCL "typeface number" to match */
  uint16 ord;      /* ordering in the target device font list (or 999 or UNMAPPEDFONT) */
  uint16 also;     /* optional additional PCL typeface number to match (or 0) */
  float  hmi;      /* HMI override (or 0) */
  float  scale;    /* Additional font scaling (or 0) */
  float  height;   /* Additional font height scaling (or 0) */
} PCL_FONTMAP;

/* Ordering value for unmapped fonts (and any font we want PCL to ignore) */
#define UNMAPPEDFONT 65535

/* -------------------------------------------------------------------------- */
/* SSINFO

   A mapping from PCL symbolset numbers to UFST symbolset numbers, with a
   compressed form of character requirements.

   This must match the symbolsets provided by the skin.
*/

typedef struct {
  uint16 ss ;
  uint16 ufst ;
  uint8  type ;
  uint8  msl_chrReq[3] ;       /* should be 8, but bytes 0-3 & 6 are zeros (4, 5 and 7 are set) */
  uint8  unicode_chrReq[3] ;   /* should be 8, 1-2 & 4-6 are zeros (0, 3 and 7 are set) */
} PCL_SSINFO ;

/* -------------------------------------------------------------------------- */
/* XL_DATA

   PCL XL font names can be mapped to PCL5 font criteria, so that non-XL fonts
   can be used by PCL XL, and so that close matches or synonyms can be handled.
*/

/* Mapping from PCL XL font suffix to weight, style and symbolset criteria */
typedef struct {
  uint8  length;      /* suffix length */
  uint8  offset;      /* offset into XL_DATA stylenames char array*/
  uint8  ss;          /* index into XL_DATA symbolsets uint16 array*/
  uint8  weightstyle; /* weight << 4 | style */
} XL_STYLE;

/* Enumeration for setting the above XL_STYLE weightstyle value */
enum {
  /* The weight bits are offset by 0x70 (XL_MEDIUM) */
  XL_ULTRATHIN=0, XL_EXTRATHIN=0x10,  XL_THIN=0x20,      XL_EXTRALIGHT=0x30,
  XL_LIGHT=0x40,  XL_DEMILIGHT=0x50,  XL_SEMILIGHT=0x60, XL_MEDIUM=0x70,
  XL_SEMI=0x80,   XL_DEMI=0x90,       XL_BOLD=0xA0,      XL_EXTRA=0xB0,
  XL_BLACK=0xC0,  XL_EXTRABLACK=0xD0, XL_ULTRA=0xE0,
  /* Multiple style bits can be combined */
  XL_ITALIC=1,    XL_COND=4
};

/* Mapping from PCL XL font name to PCL 5 font number */
typedef struct {
  uint8   length;  /* length of the fontname plus XL_MONO monospaced index */
  uint8   offset;  /* offset into XL_DATA fontnames char array */
  uint16  font;    /* typeface number */
} XL_FONT;

/* Bit and mask definitions to extract or exclude monospaced flag in length
   field of above XL_FONT structure.
   
   Monospaced bitimaged fonts require an HMI so the pitch can be calculated
   from the XL font size. This will usually only affect LinePrinter. Note that
   ((XL_FONT->length >> XL_HMI) - 1) is the index into the optional HMI array.
 */
#define XL_ITAL   (1 << 4)
#define XL_HMI    5
#define XL_MONO   (1 << XL_HMI)
#define XL_LENGTH (XL_ITAL - 1)        /* Mask for the fontname length */

/* The XL tables are indirected. */
typedef struct {
  XL_FONT *  fonts;             /**< Array of XL->PCL font map structures */
  int        fonts_length;      /**< and the number of entries in that array */
  char *     fontnames;         /**< Ptr to buffer containing all font names */
  XL_STYLE * styles;            /**< Array of XL->PCL style mappings */
  int        styles_length;     /**< and the number of entries in that array */
  uint16 *   symbolsets;        /**< Array of symbolset numbers, for XL_STYLE */
  int        symbolsets_length; /**< and the number of entries in that array */
  char *     stylenames;        /**< Ptr to buffer containing style suffixes */
  float *    monoHMIs;          /**< Optional array of HMIs for mono fonts */
} XL_DATA;

/* ========================================================================== */
/** \brief The RDR_CLASS_FONT types

    These should not really be here, but there's nowhere else currently
*/

enum {
  /* Numbers 0 to 255 are reserved for PostScript font types */
  RDR_FONT_FCO = 1000,   /**< UFST FCO data */
  RDR_FONT_PCLEO         /**< ROM font in PCL command form, eg lineprinter */
} ;

/* ========================================================================== */
/** \brief The RDR_CLASS_API types

    The CGIF routines from UFST are encapsulated here.
*/

enum {
  RDR_API_CGIF = 123456,  /**< The skin-supplied UFST CGIF and support fns */
  RDR_API_UFSTCALLBACKS   /**< The module-supplied UFST callback fns */
} ;

/* ========================================================================== */
/** \brief CGIF trampoline functions.

    The UFST library is accessed through these funtion pointers so that we can
    cope with any PMS arrangement including a UFST embedded in the firmware.
*/

/**
 * \brief Routine to access UFST fco data.
 */
typedef UW16 (CGIFfco_AccessFn)( FSP LPUB8 fcPathName, UW16 fco_Index,
                                 UW16 Info_Key, LPUW16 pSize, LPSB8 pBuffer );

/**
 * \brief Routine to install fco plugin.
 */
typedef UW16 (CGIFfco_PluginFn)( FSP SW16 FChandle );

/**
 * \brief Routine to open UFST fco data.
 */
typedef UW16 (CGIFfco_OpenFn)( FSP LPUB8 fcName, LPSW16 FChandle );

/**
 * \brief Routine to configure UFST module.
 */
typedef SW16 (CGIFinitRomInfoFn)( FSP LPUB8 fblock_ptr, LPUB8 * ss_ptr );

/**
 * \brief Routine to enter UFST module.
 */
typedef UW16 (CGIFenterFn)( FSP0 );

/**
 * \brief Routine to config UFST font.
 */
typedef UW16 (CGIFconfigFn)( FSP PIFCONFIG cfg );

/**
 * \brief Routine to initialize UFST module.
 */
typedef UW16 (CGIFinitFn)( FSP0 );

/**
 * \brief Routine to close UFST fco data.
 */
typedef UW16 (CGIFfco_CloseFn)(FSP SW16 FChandle );

/**
 * \brief Routine to make a UFST character.
 */
typedef UW16 (CGIFmakecharFn)(FSP PIFBITMAP bm, LPUB8 nzw_buf );

/**
 * \brief Routine to obtain the memory size of a UFST character.
 */
typedef UW16 (CGIFchar_sizeFn)(FSP UL32 chId, LPSL32 size, SW16 alt_width );

/**
 * \brief Routine to access UFST font data.
 */
typedef UW16 (CGIFfontFn)(FSP PFONTCONTEXT fc );

/**
 * \brief Routine to swap PCL font header endianness.
 */
typedef UW16 (PCLswapHdrFn)( FSP LPUB8 p, UW16 gifct );

/**
 * \brief Routine to swap PCL glyph header endianness.
 */
typedef UW16 (PCLswapCharFn)( FSP LPUB8 p );

/* ========================================================================== */
/** \brief Support functions supplied by PMS. */

/* Callbacks to the UFST module from the UFST library for finding softloaded
   glyph definitions.

   This will be reworked once the UFST library is built in re-entrant mode, as
   different callback functions may be required per instance - superclassing
   the if_state in that case will be necessary, at which point PMS will no
   longer need pre-registration of callback functions.
*/

typedef LPUB8 (PCLchId2PtrFn)(IF_STATE * pIFS, UW16 code);

#if GG_CUSTOM
typedef void * (PCLallocFn)(IF_STATE * pIFS, size_t size);
typedef void (PCLfreeFn)(IF_STATE * pIFS, void * block);
#endif

#define USEUFSTCALLBACKS

#ifdef USEUFSTCALLBACKS
typedef void (UFSTSetCallbacksFn)( PCLchId2PtrFn *chId2Ptr
                                 , PCLchId2PtrFn *glyphId2Ptr
#if GG_CUSTOM
                                 , PCLallocFn    *PCLalloc
                                 , PCLfreeFn     *PCLfree
#endif
                                 );
#endif

/* -------------------------------------------------------------------------- */
/* The following are now obsoleted in favour of RDRs */

#define USEPMSDATAPTRFNS 1

#ifdef USEPMSDATAPTRFNS
/**
 * \brief Routine to return the UFST PS3 fco data.
 *
 * \param ppData[in] ptr to ptr to be filled in
 */
typedef int (UFSTGetPS3FontDataPtrFn)( unsigned char **ppData );

/**
 * \brief Routine to return the UFST WingDings fco data.
 *
 * \param ppData[in] ptr to ptr to be filled in
 */
typedef int (UFSTGetWingdingFontDataPtrFn)( unsigned char **ppData );

/**
 * \brief Routine to return the UFST symbolset data.
 *
 * \param ppData[in] ptr to ptr to be filled in
 * \param which[in]  starting at 0
 */
typedef int (UFSTGetSymbolSetDataPtrFn)( unsigned char **ppData, int which );

/**
 * \brief Routine to return the UFST fco plugin data.
 *
 * \param ppData[in] ptr to ptr to be filled in
 */
typedef int (UFSTGetPluginDataPtrFn)( unsigned char **ppData );

#ifdef USEPCL45FONTSET
/**
 * \brief Routine to return whether to use dark courier.
 */
typedef int (UFSTUseDarkCourierFn)( void );
#endif
#endif

/* -------------------------------------------------------------------------- */
/** \brief The skin-supplied CGIF call table, and support functions */

typedef struct pfin_ufst5_fns
{
  CGIFfco_AccessFn             * pfnCGIFfco_Access;
  CGIFfco_PluginFn             * pfnCGIFfco_Plugin;
  CGIFfco_OpenFn               * pfnCGIFfco_Open;
  CGIFinitRomInfoFn            * pfnCGIFinitRomInfo;
  CGIFenterFn                  * pfnCGIFenter;
  CGIFconfigFn                 * pfnCGIFconfig;
  CGIFinitFn                   * pfnCGIFinit;
  CGIFfco_CloseFn              * pfnCGIFfco_Close;
  CGIFmakecharFn               * pfnCGIFmakechar;
  CGIFchar_sizeFn              * pfnCGIFchar_size;
  CGIFfontFn                   * pfnCGIFfont;
  PCLswapHdrFn                 * pfnPCLswapHdr;
  PCLswapCharFn                * pfnPCLswapChar;
#ifdef USEUFSTCALLBACKS
  UFSTSetCallbacksFn           * pfnUFSTSetCallbacks;
#endif
#ifdef USEPMSDATAPTRFNS
  UFSTGetPS3FontDataPtrFn      * pfnUFSTGetPS3FontDataPtr;
  UFSTGetWingdingFontDataPtrFn * pfnUFSTGetWingdingFontDataPtr;
  UFSTGetSymbolSetDataPtrFn    * pfnUFSTGetSymbolSetDataPtr;
  UFSTGetPluginDataPtrFn       * pfnUFSTGetPluginDataPtr;
#endif
} pfin_ufst5_fns;

/* ========================================================================== */
#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWPFIN_H__ */
