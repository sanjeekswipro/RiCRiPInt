/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcmap.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Character Mapping (CMAP) Implementation
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "namedef_.h"

#include "stacks.h"
#include "control.h"
#include "cmap.h"

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"

#include "pdfin.h"
#include "pdfexec.h"
#include "pdfcmap.h"
#include "pdfencod.h"
#include "pdffont.h"


/* -------------------------------------------------------------------------- */
STATIC int32 pdf_predefCmap( PDFCONTEXT *pdfc , NAMECACHE *cmapname ,
			     int32 *predefcmap )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  HQASSERT( cmapname ,   "cmapname NULL in pdf_predefCmap" ) ;
  HQASSERT( predefcmap , "predefcmap NULL in pdf_predefCmap" ) ;
  HQASSERT( pdfc ,       "pdfc NULL in pdf_predefCmap" ) ;

  /* Check if cmap is one of the predefined ones */
  switch ( theINameNumber( cmapname )) {
  case NAME_GB_EUC_H:    (*predefcmap) = CMAP_GB_EUC_H    ; break ;
  case NAME_GB_EUC_V:    (*predefcmap) = CMAP_GB_EUC_V    ; break ;
  case NAME_GBpc_EUC_H:  (*predefcmap) = CMAP_GBpc_EUC_H  ; break ;
  case NAME_GBpc_EUC_V:  (*predefcmap) = CMAP_GBpc_EUC_V  ; break ;
  case NAME_GBK_EUC_H:   (*predefcmap) = CMAP_GBK_EUC_H   ; break ;
  case NAME_GBK_EUC_V:   (*predefcmap) = CMAP_GBK_EUC_V   ; break ;
  case NAME_GBKp_EUC_H:  (*predefcmap) = CMAP_GBKp_EUC_H  ; break ;
  case NAME_GBKp_EUC_V:  (*predefcmap) = CMAP_GBKp_EUC_V  ; break ;
  case NAME_GBK2K_H:     (*predefcmap) = CMAP_GBK2K_H     ; break ;
  case NAME_GBK2K_V:     (*predefcmap) = CMAP_GBK2K_V     ; break ;
  case NAME_UniGB_UCS2_H: (*predefcmap) = CMAP_UniGB_UCS2_H ; break ;
  case NAME_UniGB_UCS2_V: (*predefcmap) = CMAP_UniGB_UCS2_V ; break ;
  case NAME_UniGB_UTF16_H: (*predefcmap) = CMAP_UniGB_UTF16_H ; break ;
  case NAME_UniGB_UTF16_V: (*predefcmap) = CMAP_UniGB_UTF16_V ; break ;
  case NAME_B5pc_H:      (*predefcmap) = CMAP_B5pc_H      ; break ;
  case NAME_B5pc_V:      (*predefcmap) = CMAP_B5pc_V      ; break ;
  case NAME_HKscs_B5_H:  (*predefcmap) = CMAP_HKscs_B5_H  ; break ;
  case NAME_HKscs_B5_V:  (*predefcmap) = CMAP_HKscs_B5_V  ; break ;
  case NAME_ETen_B5_H:   (*predefcmap) = CMAP_ETen_B5_H   ; break ;
  case NAME_ETen_B5_V:   (*predefcmap) = CMAP_ETen_B5_V   ; break ;
  case NAME_ETenms_B5_H: (*predefcmap) = CMAP_ETenms_B5_H ; break ;
  case NAME_ETenms_B5_V: (*predefcmap) = CMAP_ETenms_B5_V ; break ;
  case NAME_CNS_EUC_H:   (*predefcmap) = CMAP_CNS_EUC_H   ; break ;
  case NAME_CNS_EUC_V:   (*predefcmap) = CMAP_CNS_EUC_V   ; break ;
  case NAME_UniCNS_UCS2_H: (*predefcmap) = CMAP_UniCNS_UCS2_H ; break ;
  case NAME_UniCNS_UCS2_V: (*predefcmap) = CMAP_UniCNS_UCS2_V ; break ;
  case NAME_UniCNS_UTF16_H: (*predefcmap) = CMAP_UniCNS_UTF16_H ; break ;
  case NAME_UniCNS_UTF16_V: (*predefcmap) = CMAP_UniCNS_UTF16_V ; break ;
  case NAME_83pv_RKSJ_H: (*predefcmap) = CMAP_83pv_RKSJ_H ; break ;
  case NAME_90ms_RKSJ_H: (*predefcmap) = CMAP_90ms_RKSJ_H ; break ;
  case NAME_90ms_RKSJ_V: (*predefcmap) = CMAP_90ms_RKSJ_V ; break ;
  case NAME_90msp_RKSJ_H: (*predefcmap) = CMAP_90msp_RKSJ_H ; break ;
  case NAME_90msp_RKSJ_V: (*predefcmap) = CMAP_90msp_RKSJ_V ; break ;
  case NAME_90pv_RKSJ_H: (*predefcmap) = CMAP_90pv_RKSJ_H ; break ;
  case NAME_Add_RKSJ_H:  (*predefcmap) = CMAP_Add_RKSJ_H  ; break ;
  case NAME_Add_RKSJ_V:  (*predefcmap) = CMAP_Add_RKSJ_V  ; break ;
  case NAME_EUC_H:       (*predefcmap) = CMAP_EUC_H       ; break ;
  case NAME_EUC_V:       (*predefcmap) = CMAP_EUC_V       ; break ;
  case NAME_Ext_RKSJ_H:  (*predefcmap) = CMAP_Ext_RKSJ_H  ; break ;
  case NAME_Ext_RKSJ_V:  (*predefcmap) = CMAP_Ext_RKSJ_V  ; break ;
  case NAME_H:           (*predefcmap) = CMAP_H           ; break ;
  case NAME_V:           (*predefcmap) = CMAP_V           ; break ;
  case NAME_Hojo_EUC_H:  (*predefcmap) = CMAP_Hojo_EUC_H  ; break ;
  case NAME_Hojo_EUC_V:  (*predefcmap) = CMAP_Hojo_EUC_V  ; break ;
  case NAME_UniJIS_UCS2_H: (*predefcmap) = CMAP_UniJIS_UCS2_H ; break ;
  case NAME_UniJIS_UCS2_V: (*predefcmap) = CMAP_UniJIS_UCS2_V ; break ;
  case NAME_UniJIS_UCS2_HW_H: (*predefcmap) = CMAP_UniJIS_UCS2_HW_H ; break ;
  case NAME_UniJIS_UCS2_HW_V: (*predefcmap) = CMAP_UniJIS_UCS2_HW_V ; break ;
  case NAME_UniJIS_UTF16_H: (*predefcmap) = CMAP_UniJIS_UTF16_H ; break ;
  case NAME_UniJIS_UTF16_V: (*predefcmap) = CMAP_UniJIS_UTF16_V ; break ;
  case NAME_KSC_EUC_H:   (*predefcmap) = CMAP_KSC_EUC_H   ; break ;
  case NAME_KSC_EUC_V:   (*predefcmap) = CMAP_KSC_EUC_V   ; break ;
  case NAME_KSCms_UHC_H: (*predefcmap) = CMAP_KSCms_UHC_H ; break ;
  case NAME_KSCms_UHC_V: (*predefcmap) = CMAP_KSCms_UHC_V ; break ;
  case NAME_KSCms_UHC_HW_H: (*predefcmap) = CMAP_KSCms_UHC_HW_H ; break ;
  case NAME_KSCms_UHC_HW_V: (*predefcmap) = CMAP_KSCms_UHC_HW_V ; break ;
  case NAME_KSCpc_EUC_H: (*predefcmap) = CMAP_KSCpc_EUC_H ; break ;
  case NAME_KSC_Johab_H: (*predefcmap) = CMAP_KSC_Johab_H ; break ;
  case NAME_KSC_Johab_V: (*predefcmap) = CMAP_KSC_Johab_V ; break ;
  case NAME_UniKS_UCS2_H: (*predefcmap) = CMAP_UniKS_UCS2_H ; break ;
  case NAME_UniKS_UCS2_V: (*predefcmap) = CMAP_UniKS_UCS2_V ; break ;
  case NAME_UniKS_UTF16H: (*predefcmap) = CMAP_UniKS_UTF16H ; break ;
  case NAME_UniKS_UTF16V: (*predefcmap) = CMAP_UniKS_UTF16V ; break ;
  case NAME_Identity_H:  (*predefcmap) = CMAP_Identity_H  ; break ;
  case NAME_Identity_V:  (*predefcmap) = CMAP_Identity_V  ; break ;
  default:
    PDF_CHECK_MC( pdfc ) ;
    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;
    if ( ! ixc->strictpdf )
      return error_handler( RANGECHECK ) ;
    else
      return FALSE ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* This routine takes a PDF Font dictionary and fills in a PDF_CMAPDETAILS
 * structure. (This structure contains cached information which is used when
 * we render chars?)
 */
int32 pdf_setcmapdetails( PDFCONTEXT *pdfc,
			  PDF_CMAPDETAILS *pdf_cmapdetails,
			  OBJECT *cmap )
{
  NAMECACHE *cmapname ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  static NAMETYPEMATCH cmap_match[] = {
    /* Use the enum below to index this match */
    { NAME_Type,                  2, { ONAME, OINDIRECT }},
    { NAME_CMapName,              2, { ONAME, OINDIRECT }},
    { NAME_CIDSystemInfo,         4, { ODICTIONARY, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_WMode | OOPTIONAL,     2, { OINTEGER, OINDIRECT }},
    { NAME_UseCMap | OOPTIONAL,   3, { ONAME, OFILE, OINDIRECT }},
    { NAME_UseMatrix | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    DUMMY_END_MATCH
  } ;
  enum { cmap_match_Type, cmap_match_CMapName, cmap_match_CIDSystemInfo,
         cmap_match_WMode, cmap_match_UseCMap, cmap_match_UseMatrix } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;
  HQASSERT( pdf_cmapdetails , "pdf_cmapdetails NULL in pdf_setcmapdetails" ) ;

  pdf_cmapdetails->cmap_type = CMAP_None ;

  if ( cmap ) {
    switch ( oType(*cmap) ) {
    case ONAME:
      cmapname = oName(*cmap) ;
      if ( ! pdf_predefCmap( pdfc, cmapname, & pdf_cmapdetails->cmap_type )) {
	if ( ixc->strictpdf )
	  return FALSE ;
      }

      /* Locate the CMap resource */
      if ( ! pdf_findresource(NAME_CMap, cmap) )
	return FALSE ;

      Copy( & pdf_cmapdetails->cmap_dict, theTop(operandstack) ) ;
      pop(&operandstack) ;
      break ;

    case ODICTIONARY:
      return error_handler( UNDEFINED ) ;

    case OFILE: {
      OBJECT *cmap_dict ;

      pdf_cmapdetails->cmap_type = CMAP_Embedded ;
      cmap_dict = streamLookupDict( cmap ) ;

      if ( ! ixc->strictpdf )
        cmap_match[cmap_match_Type].name |= OOPTIONAL ;
      else
        cmap_match[cmap_match_Type].name &= ~OOPTIONAL ;

      if ( ! pdf_dictmatch( pdfc, cmap_dict, cmap_match ))
	return FALSE ;

      /* Is /Type entry in object /CMap? (Maybe shouldn't do this?) */
      if ( cmap_match[cmap_match_Type].result )
        if ( oName(*cmap_match[cmap_match_Type].result) !=
	     system_names + NAME_CMap )
	  return error_handler( UNDEFINED ) ;

      /* Define and then locate the CMap resource */
      if ( ! interpreter_clean(cmap, NULL) ||
           ! pdf_findresource(NAME_CMap,
                              cmap_match[cmap_match_CMapName].result) )
	return FALSE ;

      Copy( & pdf_cmapdetails->cmap_dict, theTop(operandstack) ) ;
      pop(&operandstack) ;
      break ;
    }

    default:
      HQFAIL( "Bad case found in CMap Type" ) ;
      return error_handler( UNREGISTERED ) ;
    }
  }

  /* Get writing mode for this CMap */
  if ( !cmap_getwmode(& pdf_cmapdetails->cmap_dict, & pdf_cmapdetails->wmode) )
    return FALSE ;

  return TRUE ;
}



/* Log stripped */
