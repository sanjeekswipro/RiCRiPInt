/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfjtf.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file processes PJTF structures that may be embedded within a PDF file.
 *
 * However, it only processes those structures necessary for extracting
 * trapping parameters and zones, and then passes on those parameters and
 * zones to TrapPro via the Trapping procset (i.e. by calling out to
 * PostScript).
 * There are two entry points (i.e. two exported functions) for this file:-
 * a)   pdf_jt_contents()
 * b)   pdf_jt_get_trapinfo()
 * Function header comments provide more information.
 *
 * References:
 * [1]  Portable Job Ticket Format.  Adobe. Technical note #5620.
 *      Version 1.1.  2 April 1999.
 * [2]  PJTF Extensions for Trapping.  Adobe. Technical note #5621.
 *       13 June 2002.
 *
 * Documentation:
 * [1]  Fix Request 11854 - [XIT] Implement Trap Zones in PDF.
 * [2]  Document entitled "Obtaining Trapping Information From PJTF Structures
 *      Embedded in a PDF Job for the Harlequin RIP". Version 2.0.
 *      File name: ImplementingPJTFinSW_V2.doc.  Attached to the Request or
 *      else available in ScriptWorks Information, or ask Andre Hopper.
 */

#include "core.h"
#include "swerrors.h"
#include "swctype.h"
#include "swcopyf.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "monitor.h"
#include "namedef_.h"
#include "hqmemcmp.h"

#include "dicthash.h"
#include "stacks.h"
#include "graphics.h"
#include "dictops.h"
#include "devops.h"
#include "gstack.h"
#include "pathcons.h"
#include "gu_cons.h"
#include "gu_path.h"
#include "miscops.h"

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfres.h"
#include "pdfcntxt.h"
#include "pdfxref.h"
#include "pdfmem.h"

#include "swpdfin.h"
#include "pdfin.h"
#include "pdfdefs.h"
#include "pdfjtf.h"


/* JobTicket object */
enum { E_jt_Cn, E_jt_dummy } ;
static NAMETYPEMATCH jt_jobtkt_dict[E_jt_dummy + 1] = {
  { NAME_Cn  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
  DUMMY_END_MATCH
};

/* JobTicketContents object */
enum { E_jtc_D = 0, E_jtc_T, E_jtc_TP, E_jtc_TR, E_jtc_TSS, E_jtc_dummy };
static NAMETYPEMATCH jt_contents_dict[E_jtc_dummy + 1] = {
/* 0 */ { NAME_D   | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 1 */ { NAME_T   | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 2 */ { NAME_TP  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 3 */ { NAME_TR  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 4 */ { NAME_TSS | OOPTIONAL,  2, { ONAME, OINDIRECT }},
         DUMMY_END_MATCH
};


/* Document object */
enum { E_jdd_Fi = 0, E_jdd_P, E_jdd_T, E_jdd_TP, E_jdd_TR, E_jdd_dummy };
static NAMETYPEMATCH jt_document_dict[E_jdd_dummy + 1] = {
/* 0 */ { NAME_Fi  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 1 */ { NAME_P   | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 2 */ { NAME_T   | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 3 */ { NAME_TP  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 4 */ { NAME_TR  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
         DUMMY_END_MATCH
};


/* JTFile object */
enum { E_jtf_Fi = 0, E_jtf_TR, E_jtf_FD, E_jtf_dummy };
static NAMETYPEMATCH jt_file_dict[E_jtf_dummy + 1] = {
/* 0 */ { NAME_Fi  | OOPTIONAL,  3, { ONAME, OSTRING, OINDIRECT }},
/* 1 */ { NAME_TR  | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/* 2 */ { NAME_FD  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
         DUMMY_END_MATCH
};


/* PageRange object */
enum { E_jpr_T = 0, E_jpr_TP, E_jpr_TR, E_jpr_W, E_jpr_dummy };
static NAMETYPEMATCH jt_pagerange_dict[E_jpr_dummy + 1] = {
/* 0 */ { NAME_T   | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 1 */ { NAME_TP  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
/* 2 */ { NAME_TR  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 3 */ { NAME_W   | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
         DUMMY_END_MATCH
};


/* Trapping object */
enum { E_jtg_T = 0, E_jtg_D, E_jtg_dummy };
static NAMETYPEMATCH jt_trapping_dict[E_jtg_dummy + 1] = {
/* 0 */ { NAME_T   | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/* 1 */ { NAME_D   | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
         DUMMY_END_MATCH
};

/* TrappingDetails object */
enum { E_jtd_CD = 0, E_jtd_TO, E_jtd_TT, E_jtd_dummy };
static NAMETYPEMATCH jt_trapdetails_dict[E_jtd_dummy + 1] = {
/* 0 */ { NAME_CD  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},  /* dict of DeviceColorants */
/* 1 */ { NAME_TO  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
/* 2 */ { NAME_TT  | OOPTIONAL,  2, { OINTEGER, OINDIRECT }},
         DUMMY_END_MATCH
};


/* DeviceColorant object */
enum { E_jdc_CN = 0, E_jdc_CT, E_jdc_ND, E_jdc_dummy };
static NAMETYPEMATCH jt_devcolorant_dict[E_jdc_dummy + 1] = {
/* 0 */  { NAME_CN  | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/* 1 */  { NAME_CT  | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/* 2 */  { NAME_ND  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
         DUMMY_END_MATCH
};


/* TrappingParameters object */
enum { E_jtp_BCL = 0, E_jtp_BDL, E_jtp_BW, E_jtp_CZD, E_jtp_E,
       E_jtp_HN, E_jtp_IIT, E_jtp_IR, E_jtp_IT, E_jtp_ITO, E_jtp_ITP,
       E_jtp_STL, E_jtp_SL, E_jtp_TCS, E_jtp_TW,
       E_jtp_ADB1, E_jtp_ADB2, E_jtp_ADB3, E_jtp_ADB4,
       E_jtp_dummy
};
static NAMETYPEMATCH jt_trapparams_dict[E_jtp_dummy + 1] = {
/* 0 */  { NAME_BCL  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/* 1 */  { NAME_BDL  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/* 2 */  { NAME_BW   | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/* 3 */  { NAME_CZD  | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},  /* to ColZone */
/* 4 */  { NAME_E    | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/* 5 */  { NAME_HN   | OOPTIONAL,  3, { ONAME, OSTRING, OINDIRECT }},
/* 6 */  { NAME_IIT  | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/* 7 */  { NAME_IR   | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/* 8 */  { NAME_IT   | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/* 9 */  { NAME_ITO  | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/*10 */  { NAME_ITP  | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/*11 */  { NAME_STL  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/*12 */  { NAME_SL   | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/*13 */  { NAME_TCS  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/*14 */  { NAME_TW   | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/*15 */  { NAME_ADB_ImageToImageTrapping | OOPTIONAL,  2, { OBOOLEAN, OINDIRECT }},
/*16 */  { NAME_ADB_TrapJoinStyle        | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/*17 */  { NAME_ADB_TrapEndStyle         | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/*18 */  { NAME_ADB_MinimumBlackWidth    | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
         DUMMY_END_MATCH
};


/* Equivalent PostScript names for TrappingParameters keys. This list
   has to correspond one-to-one with the names in jt_trapparams_dict
   above.  */
static int32 jt_tp_PSnames[] = {
  NAME_BlackColorLimit,
  NAME_BlackDensityLimit,
  NAME_BlackWidth,
  0,      /* CZD ColorantZoneDetails handled separately */
  NAME_Enabled,
  NAME_HalftoneName,      /* no documented in PLRM3 */
  NAME_ImageInternalTrapping,
  NAME_ImageResolution,
  0,      /* IT ImagemaskTrapping has no PostScript equivalent */
  NAME_ImageToObjectTrapping,
  NAME_ImageTrapPlacement,
  NAME_SlidingTrapLimit,
  NAME_StepLimit,
  NAME_TrapColorScaling,
  NAME_TrapWidth,
  NAME_ImageToImageTrapping,
  NAME_TrapJoinStyle,
  NAME_TrapEndStyle,
  NAME_MinimumBlackWidth
};

/* ColorantZoneDetails object */
enum { E_jcz_SL = 0, E_jcz_TCS, E_jcz_dummy };
static NAMETYPEMATCH jt_colzone_dict[E_jcz_dummy + 1] = {
/* 0 */  { NAME_SL  | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
/* 1 */  { NAME_TCS | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
         DUMMY_END_MATCH
};


/* TrapRegion object */
enum { E_jtr_TP = 0, E_jtr_TZ, E_jtr_dummy };
static NAMETYPEMATCH jt_trapregion_dict[E_jtr_dummy + 1] = {
/* 0 */  { NAME_TP,               2, { ONAME, OINDIRECT }},
/* 1 */  { NAME_TZ  | OOPTIONAL,  2, { OARRAY, OINDIRECT }},
         DUMMY_END_MATCH
};


/* Following enumerative is used to remember which object provided the trap
   regions. The point in the hierarchy where the search for the
   TrappingParameters commences has to match the point where the
   TrapRegions object was found. */
enum {
  E_from_PR,  /* obtained from PageRange object */
  E_from_Doc, /* obtained from the Document object */
  E_from_JTC  /* obtained from the JobTicketContents object */
};

/* Following structure provides parameters to call-back functions used with
   the 'walk_dictionary' routine. */
typedef struct cdetail_param_struct {
  PDFCONTEXT *pdfc;
  OBJECT     *pCDdict;
} CDETAIL_PARAMS;



/* ----------------------------------------------------------------------------
**  pdf_jt_contents()
**  Reads the initial JobTicket and JobTicketContents dictionaries
**  (aka 'objects') to (a) determine whether or not trapping parameters should
**  be present, and (b) to retain information about them.
*/
Bool pdf_jt_contents( PDFCONTEXT *pdfc, OBJECT *pJT )
{
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;
  OBJECT *pObj;
  OBJECT *pDoc;

  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );

  /* The following batch of asserts ensures that the static data structures
     declared at the top of this file maintain some consistency. */
  HQASSERT( NUM_ARRAY_ITEMS(jt_contents_dict)-1 == E_jtc_dummy, "Erroneous jtc arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_document_dict)-1 == E_jdd_dummy, "Erroneous Document arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_file_dict)-1 == E_jtf_dummy, "Erroneous JTFile arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_pagerange_dict)-1 == E_jpr_dummy, "Erroneous PageRange arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_trapping_dict)-1 == E_jtg_dummy, "Erroneous trapping arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_trapdetails_dict)-1 == E_jtd_dummy, "Erroneous TrapDetails arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_devcolorant_dict)-1 == E_jdc_dummy, "Erroneous DevColorant arrays" );
  HQASSERT( (NUM_ARRAY_ITEMS(jt_trapparams_dict)-1 == E_jtp_dummy) &&
            (E_jtp_dummy == NUM_ARRAY_ITEMS(jt_tp_PSnames)),
            "Erroneous TrappingParameters arrays." );
  HQASSERT( NUM_ARRAY_ITEMS(jt_colzone_dict)-1 == E_jcz_dummy, "Erroneous ColZone arrays" );
  HQASSERT( NUM_ARRAY_ITEMS(jt_trapregion_dict)-1 == E_jtr_dummy, "Erroneous TrapRegion arrays" );


  /* Assume no trapping */
  ixc->pPJTFinfo = NULL;

  /* Match up the Job Ticket dictionary */
  if (!pdf_dictmatch( pdfc, pJT, jt_jobtkt_dict ))
    return FALSE;

  if (jt_jobtkt_dict[E_jt_Cn].result == NULL) /* /Cn (contents) */
    return TRUE;

  /* The Contents is an array of exactly one JobTicketContents object.
     If there's more than one, just use the first one. */
  pObj = jt_jobtkt_dict[E_jt_Cn].result;
  HQASSERT( theLen(*pObj) == 1, "JobTicket Contents array not 1" );
  if (theLen(*pObj) < 1)
    return TRUE;

  /* Resolve to the JobTicketContents dictionary */
  pObj = &oArray(*pObj)[0];

  if (oType(*pObj) == OINDIRECT)
    if (!pdf_lookupxref( pdfc, &pObj, oXRefID(*pObj),
                         theGen(*pObj), FALSE ))
      return FALSE;

  HQASSERT( oType(*pObj) == ODICTIONARY, "JobTicketContents not a dictionary" );
  if (oType(*pObj) != ODICTIONARY)
    return TRUE;

  /* Match up the JobTicketContents dictionary */
  if (!pdf_dictmatch( pdfc, pObj, jt_contents_dict ))
    return FALSE;

  /* If TSS is present, insist it says "Contents" */
  pObj = jt_contents_dict[ E_jtc_TSS ].result;
  if (pObj != NULL) {
    if (theINameNumber( oName(*pObj) ) != NAME_Contents) {
      HQTRACE( 1, ("PDF JobTicketContents TSS not Contents.") );
      return TRUE;
    }
  }

  /* The Document entry should be an array of one document. */
  pObj = jt_contents_dict[ E_jtc_D ].result;
  if (pObj == NULL) {
    HQTRACE( 1, ("PDF JobTicketContents Document not defined.") );
    return TRUE;
  }

  HQASSERT( theLen(*pObj) == 1, "Warning: PDF JobTicketContents Document array length not one." );
  if (theLen(*pObj) < 1)
    return TRUE;

  /* Resolve the Document object */
  pDoc = &oArray(*pObj)[0];
  if (oType(*pDoc) == OINDIRECT)
    if (!pdf_lookupxref( pdfc, &pDoc, oXRefID(*pDoc),
                         theGen(*pDoc), FALSE ))
      return FALSE;

  if (oType(*pDoc) != ODICTIONARY) {
    HQTRACE( 1, ("PDF JobTicketContents Document not a dictionary") );
    return TRUE;
  }

  /* Match up the Document object */
  if (!pdf_dictmatch( pdfc, pDoc, jt_document_dict ))
    return FALSE;

  /* If the Document's "Fi" key is not present, we'll assume it meant "This".
     Otherwise, insist that the corresponding JTFile object does say "This"
     and check it's Trapped key. */
  pObj = jt_document_dict[ E_jdd_Fi ].result;
  if (pObj != NULL) {
    HQASSERT( theLen(*pObj) == 1, "JobTicket Document Fi array length not one." );
    if (theLen(*pObj) >= 1) {
      /* Resolve the JTFile object */
      pObj = &oArray(*pObj)[0];
      if (oType(*pObj) == OINDIRECT)
        if (!pdf_lookupxref( pdfc, &pObj, oXRefID(*pObj),
                             theGen(*pObj), FALSE ))
          return FALSE;

      HQASSERT( oType(*pObj) == ODICTIONARY, "JobTicket Document Fi not a dictionary." );
      if (oType(*pObj) == ODICTIONARY) {

        /* Match up the JTFile object. */
        if (!pdf_dictmatch( pdfc, pObj, jt_file_dict )) {
          HQTRACE( 1, ("JobTicket JTFile object is not a valid or contains external file.") );
          return TRUE;
        }

        /* If JTFile's "Fi" key is present, insist it says "This" */
        pObj = jt_file_dict[E_jtf_Fi].result;
        if (pObj != NULL) {
          if (oType(*pObj) == ONAME) {
            if (theINameNumber( oName(*pObj) ) != NAME_This) {
              HQTRACE( 1, ("JobTicket JTFile does not refer to This.") );
              return TRUE;
            }
          } else {
            HQASSERT( oType(*pObj) == OSTRING, "JTFile Fi neither name nor string." );
            if (HqMemCmp( oString(*pObj), (int32) theLen(*pObj), (uint8*) "This", 4 ) != 0) {
              HQTRACE( 1, ("JobTicket JTFile does not refer to This (string).") );
              return TRUE;
            }
          }
        }

        /* If the JTFile FD key is present, quit processing PJTF */
        if (jt_file_dict[E_jtf_FD].result != NULL) {
          HQTRACE( 1, ("PDF PJTF (trapping) ignored as FD present in JTFile.") );
          return TRUE;
        }

        /* Check the JTFile object's "Trapped" key */
        pObj = jt_file_dict[E_jtf_TR].result;
        if (pObj != NULL) {
          int32 TrappedValue;
          TrappedValue = theINameNumber( oName(*pObj) );
          HQASSERT( TrappedValue == NAME_True ||
                    TrappedValue == NAME_False ||
                    TrappedValue == NAME_Unknown, "Invalid value for Trapped in JTFile" );
          if (TrappedValue == NAME_True) {
            HQTRACE( 1, ("PDF PJFT (trapping) JTFile says Trapped already." ));
            return TRUE;
          }
        }
      }
    }
  }

  /* That concludes validation of the JobTicketContents and Document objects.
     Retain information in the execution context for the PageRange array and
     default trapping parameters & regions.  The presence of this information
     indicates that PJTF trapping parameters are to be processed for each page.
  */
  ixc->pPJTFinfo = (PJTFinfo *) mm_alloc( pdfxc->mm_structure_pool,
                                          sizeof(PJTFinfo),
                                          MM_ALLOC_CLASS_PDF_CONTEXT );
  if ( ixc->pPJTFinfo == NULL )
    return error_handler( VMERROR );

  ixc->pPJTFinfo->pPageArray = jt_document_dict[ E_jdd_P ].result;

  /* The Document object may specify default trapping parameters as might the
     JobTicketContents object.  Retain them all. */
  ixc->pPJTFinfo->pTrapping    = jt_contents_dict[ E_jtc_T ].result;
  ixc->pPJTFinfo->pTParamsJTC  = jt_contents_dict[ E_jtc_TP ].result;
  ixc->pPJTFinfo->pTRegionsJTC = jt_contents_dict[ E_jtc_TR ].result;

  if (jt_document_dict[ E_jdd_T ].result != NULL)
    ixc->pPJTFinfo->pTrapping = jt_document_dict[ E_jdd_T ].result;

  ixc->pPJTFinfo->pTParamsDOC  = jt_document_dict[ E_jdd_TP ].result;
  ixc->pPJTFinfo->pTRegionsDOC = jt_document_dict[ E_jdd_TR ].result;


  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_jt_get_PageRange()
** For the current PDF page, find the appropriate PageRange object and, if
** found, leave the "dictmatch" results in jt_pagerange_dict.
*/
static Bool pdf_jt_get_PageRange( PDFCONTEXT *pdfc, OBJECT **ppPageRange )
{
  int32 len;
  int32 PageNum;
  int32 num;
  OBJECT *pObj;
  OBJECT *pDict;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );


  *ppPageRange = NULL;    /* Assume no PageRange object found. */
  PageNum = ixc->pageno - 1;  /* PJFT page nums start at zero. */

  /* Start by obtaining the Document object's PageRange array from the
     ixc structure. If absent or is a zero-length array, quit now. */
  pObj = ixc->pPJTFinfo->pPageArray;
  if (pObj == NULL)
    return TRUE;

  HQASSERT( oType(*pObj)==OARRAY, "PageRange array not an array." );

  len = (int32) theLen(* pObj );
  if (len <= 0)
    return TRUE;

  /* Go through each PageRange object in the array until one is found that
     applies to the current PDF page. */
  pObj = &oArray(*pObj)[0];

  while (--len >= 0) {
    OBJECT *pWhich;

    /* Resolve indirect objects taken from the array */
    if (oType(*pObj) == OINDIRECT) {
      if (!pdf_lookupxref( pdfc, &pDict, oXRefID(*pObj),
                           theGen(*pObj), FALSE ))
        return FALSE;
    } else {
      pDict = pObj;
    }

    /* Extract the /W key.
       Note: it might seem a bit excessive to do a full 'dictmatch' on the
       whole PageRange dictionary here, just to get the 'W' key, but in
       practice there will probably only be the one PageRange object which
       will need the full dictmatch done soon enough anyway.  */
    if (!pdf_dictmatch( pdfc, pDict, jt_pagerange_dict ))
      return FALSE;

    pWhich = jt_pagerange_dict[ E_jpr_W ].result;

    /* Absence of 'W' means this PageRange applies to all pages. */
    if (pWhich == NULL) {
      *ppPageRange = pDict;
      break;
    }

    /* W's value is an array of two integers. A value of -1 means "the last
       page". We shall take an empty array to mean the same as "all pages". */
    if (theLen(*pWhich) == 0) {
      *ppPageRange = pDict;
      break;
    }
    HQASSERT( theLen(*pWhich)==2, "PageRange W array not length 2" );

    /* Check the current PDF page is within the first and last pages
       specified. */
    if ( !object_get_integer(oArray(*pWhich), &num) )
      return FALSE ;

    if (num <= PageNum) {
      /* Now check the last page number */
      if (theLen(*pWhich) != 2) {
        *ppPageRange = pDict;
        break;
      }

      if ( !object_get_integer( &oArray(*pWhich)[1], &num) )
        return FALSE ;

      if (num == -1  ||  num >= PageNum) {
        *ppPageRange = pDict;
        break;
      }
    }

    pObj++;   /* next PageRange object in the array */
  }

  return TRUE;
}


/* ----------------------------------------------------------------------------
** pdf_countkeys_CB()
** This function is a call-back for walk_dictionary() and is used merely to
** count the keys defined in the dictionary.
*/
static Bool pdf_countkeys_CB( OBJECT *pKey, OBJECT *pValue, void *pCount )
{
  int32 *pIntCount = (int32 *) pCount;
  UNUSED_PARAM( OBJECT*, pKey );
  UNUSED_PARAM( OBJECT*, pValue );

  (*pIntCount)++;

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_set_PSdict()
** Given a PDF dictionary and a NAMETYPEMATCH object, performs a dictmatch on
** the dictionary.  Then creates a PostScript dictionary (of the right size)
** and returns it (empty) via the 'pPSdict' argument.
** Note that if pMatch is null, then we can't do a match on it and so the
** number of defined keys in it is performed via a "walk" through it.
*/
static Bool pdf_set_PSdict( PDFCONTEXT *pdfc,
                            OBJECT     *pPDFdict,
                            NAMETYPEMATCH *pMatch,
                            OBJECT     *pPSdict )
{
  int32 count = 0;
  NAMETYPEMATCH *onematch;

  theTags(*pPSdict) = ONULL;   /* Assume no PS dictionary created. */

  if (pPDFdict == NULL)
    return TRUE;

  if (pMatch != NULL) {
    /* Resolve the PDF dictionary. */
    if (!pdf_dictmatch( pdfc, pPDFdict, pMatch ))
      return FALSE;

    /* Count how many keys were actually defined. */
    for ( onematch = pMatch; theISomeLeft(onematch); onematch++ ) {
      if (onematch->result != NULL)
        count++;
    }
  } else {
    /* Dictionary cannot be resolved through a NAMETYPEMATCH.  Need
       to iterate through its key "manually". */
    if (!walk_dictionary( pPDFdict, pdf_countkeys_CB, &count ))
      return FALSE;
  }

  if (count > 0) {
    if (!ps_dictionary(pPSdict, count))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
**  pdf_set_key()
**  Defines an entry (key + value) in the given dictionary.  Note that where
**  the value is a composite object (other than a dictionary), it needs re-
**  creating in PostScript VM.
*/
static Bool pdf_set_key( OBJECT *pDict, int32 psName, OBJECT *pValue )
{
  OBJECT nname = OBJECT_NOTVM_NOTHING;
  OBJECT psObj = OBJECT_NOTVM_NOTHING;

  if (pValue == NULL)   /* Nowt to do */
    return TRUE;

  theTags(nname) = ONAME | LITERAL;
  oName(nname) = system_names + psName;

  /* If the value is a composite object, it needs re-creating in PostScript
     VM.  However, dictionaries will already have been re-created thus.
     Note, we need to be careful about arrays as pdf_copyobject() performs
     a deep copy.  All the arrays that we want to copy from PJTF structures
     have simple objects for their elements. */
  if (oType(*pValue) != ODICTIONARY) {

    if (oType(*pValue) == OARRAY || oType(*pValue) == OPACKEDARRAY) {
      OBJECT *pObj = oArray(*pValue);
      int32 len = (int32) theLen(*pValue);
      while (--len >= 0) {
        if (isPSCompObj(*pObj) && oType(*pObj) != OSTRING) {
          HQFAIL( "Key array element is composite." );
          return error_handler( TYPECHECK );
        }
        pObj++;
      }
    }

    if (!pdf_copyobject( NULL, pValue, &psObj ))
      return FALSE;

    pValue = &psObj;
  }

  /* Define the key/value in the dict. */
  if (!fast_insert_hash( pDict, &nname, pValue ))
    return FALSE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_colorantdetail_CB()
** This function is a call-back to walk_dictionary().  Its purpose is to take
** the key/value pair from a ColorantDetails dictionary, resolve the value to
** a DeviceColorant dictionary, create that dictionary in PostScript, and set
** the key/new-value in the PostScript ColorantDetails dictionary which is
** passed via '*pParam'.
*/
static Bool pdf_colorantdetail_CB( OBJECT *pKey, OBJECT *pValue, void *pParam )
{
  OBJECT dc_dict = OBJECT_NOTVM_NOTHING;
  CDETAIL_PARAMS *pCDparams = (CDETAIL_PARAMS *) pParam;

  HQASSERT( pParam != NULL, "Null pParam in pdf_colorantdetail_CB" );
  HQASSERT( oType(*(pCDparams->pCDdict)) == ODICTIONARY,
            "Non dict passed to pdf_colorantdetail_CB" );
  HQASSERT( oType(*pKey) == ONAME, "Non-name key passed to pdf_colorantdetail_CB" );


  if (oType(*pValue) == OINDIRECT)
    if (!pdf_lookupxref( pCDparams->pdfc, &pValue, oXRefID(*pValue),
                         theGen(*pValue), FALSE ))
      return FALSE;

  /* Allow non-dictionary values, but ignore them */
  if (oType(*pValue) != ODICTIONARY) {
    HQTRACE( 1, ("Value not a dict in pdf_colorantdetail_CB") );
    return TRUE;
  }

  /* Work on the DeviceColorant dictionary */
  if (!pdf_set_PSdict( pCDparams->pdfc, pValue, jt_devcolorant_dict, &dc_dict ))
    return FALSE;

  if (oType(dc_dict) == ODICTIONARY) {
    if (!pdf_set_key( &dc_dict, NAME_ColorantName, jt_devcolorant_dict[E_jdc_CN].result ))
      return FALSE;

    if (!pdf_set_key( &dc_dict, NAME_ColorantType, jt_devcolorant_dict[E_jdc_CT].result ))
      return FALSE;

    if (!pdf_set_key( &dc_dict, NAME_NeutralDensity, jt_devcolorant_dict[E_jdc_ND].result ))
      return FALSE;

    /* Set the PS DeviceColorant dictionary as the value to the current key */
    if (!fast_insert_hash( pCDparams->pCDdict, pKey, &dc_dict ))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_jt_trap_details()
** Given a "Trapping" object, map its hierarchy of dictionaries onto their
** PostScript equivalents and invoke a setpagedevice on 'em.  The *pDoTrapping
** flag is returned as FALSE if further PJTF processing is not necessary.
*/
static Bool pdf_jt_trap_details( PDFCONTEXT *pdfc,
                                 OBJECT *pTrapping,
                                 Bool *pDoTrapping )
{
  ps_context_t *pscontext ;
  OBJECT *pObj;
  OBJECT setpgdev_dict = OBJECT_NOTVM_NOTHING;

  *pDoTrapping = TRUE;

  if (pTrapping == NULL)  /* Nowt to do */
    return TRUE;

  /* Resolve the Trapping dictionary and create the PostScript dictionary
     that will be used as the operand to 'setpagedevice'. */
  if (!pdf_set_PSdict( pdfc, pTrapping, jt_trapping_dict, &setpgdev_dict ))
    return FALSE;

  if (oType(setpgdev_dict) == ONULL)  /* No keys found in Trapping dict. */
    return TRUE;


  /* Translate the T (Trapping) key */
  if (!pdf_set_key( &setpgdev_dict, NAME_Trapping, jt_trapping_dict[E_jtg_T].result ))
    return FALSE;

  /* If Trapping is inhibited, ensure we return this fact, but still do the
     'setpagedevice'. */
  pObj = jt_trapping_dict[E_jtg_T].result;
  if (pObj != NULL  &&  !oBool(*pObj)) {
    *pDoTrapping = FALSE;
  } else {

    /* Check the D (TrappingDetails) key. */
    pObj = jt_trapping_dict[E_jtg_D].result;
    if (pObj != NULL) {

      /* Resolve the TrappingDetails dictionary and create its PostScript
         equivalent. */
      OBJECT trap_dict = OBJECT_NOTVM_NOTHING;

      if (!pdf_set_PSdict( pdfc, pObj, jt_trapdetails_dict, &trap_dict ))
        return FALSE;

      if (oType(trap_dict) == ODICTIONARY) {

        if (!pdf_set_key( &trap_dict, NAME_TrappingOrder, jt_trapdetails_dict[E_jtd_TO].result ))
          return FALSE;

        if (!pdf_set_key( &trap_dict, NAME_Type, jt_trapdetails_dict[E_jtd_TT].result ))
          return FALSE;

        /* The CD (ColorantDetails) key is a dictionary of named colorants each with
           a sub-dictionary (DeviceColorant) for its value. The actual key names are
           not statically known - they have to be taken on trust! */
        pObj = jt_trapdetails_dict[ E_jtd_CD ].result;
        if (pObj != NULL) {
          OBJECT cd_dict = OBJECT_NOTVM_NOTHING;

          if (!pdf_set_PSdict( pdfc, pObj, NULL, &cd_dict ))
            return FALSE;

          if (oType(cd_dict) == ODICTIONARY) {
            /* Each key is an unknown colorant name, and each value is a
               sub-dictionary. The sub-dictionary - a DeviceColorant object - is
               to be recreated in PostScript and defined (by the name colorant
               name) in the 'cd_dict' we've created here. */
            CDETAIL_PARAMS cdParams;
            cdParams.pdfc = pdfc;
            cdParams.pCDdict = &cd_dict;

            if (!walk_dictionary( pObj, pdf_colorantdetail_CB, &cdParams ))
              return FALSE;

            /* Now provide the PostScript ColorantDetails dictionary as the
               value to the /ColorantDetails key in the TrappingDetails dict. */
            if (!pdf_set_key( &trap_dict, NAME_ColorantDetails, &cd_dict ))
              return FALSE;
          }
        }

        /* Now provide the 'trap_dict' dictionary as the value to
           /TrappingDetails key in the new PostScript dictionary. */
        if (!pdf_set_key( &setpgdev_dict, NAME_TrappingDetails, &trap_dict ))
          return FALSE;
      }
    }
  }

  /* Do a 'setpagedevice' to instantiate the trapping details */
  if (!push( &setpgdev_dict, &operandstack ))
    return FALSE;

  monitorf( UVS( "Info: PDF job contains trapping information.\n" ) );

  HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
  HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
  pscontext = pdfc->pdfxc->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  return setpagedevice_(pscontext) ;
}

/* ----------------------------------------------------------------------------
** pdf_colorantzone_CB()
** This function is a call-back to walk_dictionary().  Its purpose is to take
** the key/value pair from a ColorantZoneDetails dictionary, resolve the value
** to a sub-dictionary, create that dictionary in PostScript, and set the
** key/new-value in the PostScript ColorantZoneDetails dictionary which is
** passed via '*pParam'.
*/
static Bool pdf_colorantzone_CB( OBJECT *pKey, OBJECT *pValue, void *pParam )
{
  OBJECT czd_dict = OBJECT_NOTVM_NOTHING;
  CDETAIL_PARAMS *pCDparams = (CDETAIL_PARAMS *) pParam;

  HQASSERT( pParam != NULL, "Null pParam in pdf_colorantzone_CB" );
  HQASSERT( oType(*(pCDparams->pCDdict)) == ODICTIONARY,
            "Non dict passed to pdf_colorantzone_CB" );
  HQASSERT( oType(*pKey) == ONAME, "Non-name key passed to pdf_colorantzone_CB" );


  if (oType(*pValue) == OINDIRECT)
    if (!pdf_lookupxref( pCDparams->pdfc, &pValue, oXRefID(*pValue),
                         theGen(*pValue), FALSE ))
      return FALSE;

  /* Allow non-dictionary values, but ignore them */
  if (oType(*pValue) != ODICTIONARY) {
    HQTRACE( 1, ("Value not a dict in pdf_colorantzone_CB") );
    return TRUE;
  }

  /* Work on the ColorantZoneDetails sub-dictionary */
  if (!pdf_set_PSdict( pCDparams->pdfc, pValue, jt_colzone_dict, &czd_dict ))
    return FALSE;

  if (oType(czd_dict) == ODICTIONARY) {
    if (!pdf_set_key( &czd_dict, NAME_StepLimit, jt_colzone_dict[E_jcz_SL].result ))
      return FALSE;

    if (!pdf_set_key( &czd_dict, NAME_TrapColorScaling, jt_colzone_dict[E_jcz_TCS].result ))
      return FALSE;

    /* Set the PS DeviceColorant dictionary as the value to the current key */
    if (!fast_insert_hash( pCDparams->pCDdict, pKey, &czd_dict ))
      return FALSE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
**  pdf_jt_get_trap_set()
**  Locate the named (via pParamsName) set of Trapping Parameters and establish
**  them on the operand stack.  The search can only begin from the appropriate
**  place in the object hierarchy (i.e. PageRange, Document or JobTicketContents
**  objects) and then work up - the starting place is provided by the 'from'
**  parameter.
*/
static Bool pdf_jt_get_trap_set( PDFCONTEXT *pdfc,
                                 int32 from,
                                 OBJECT *pParamsName,
                                 Bool  *pSetFound )
{
  uint32 i;
  OBJECT tp_dict = OBJECT_NOTVM_NOTHING;
  OBJECT *pObj = NULL;
  OBJECT *pDict = NULL;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );

  HQASSERT( pParamsName != NULL, "ParamsName null in pdf_jt_get_trap_set" );
  *pSetFound = FALSE;

  if (from == E_from_PR) {
    /* Go for a name match on any trapping parameters in the PageRange
       object. */
    pDict = jt_pagerange_dict[ E_jpr_TP ].result;
    if (pDict != NULL)
      pObj = fast_extract_hash( pDict, pParamsName );
  }

  if ((from == E_from_PR || from == E_from_Doc) && pObj == NULL) {
    /* Go for a name match on any trapping parameters in the Document
       object. */
    pDict = ixc->pPJTFinfo->pTParamsDOC;
    if (pDict != NULL)
      pObj = fast_extract_hash( pDict, pParamsName );
  }

  if (pObj == NULL) {
    /* Finally, try the JobTicketContents TP */
    pDict = ixc->pPJTFinfo->pTParamsJTC;
    if (pDict != NULL)
      pObj = fast_extract_hash( pDict, pParamsName );
  }

  /* If no parameters are available, tough! */
  if (pObj == NULL)
    return TRUE;

  /* Ensure we have a dictionary. */
  if (oType(*pObj) == OINDIRECT)
    if (!pdf_lookupxref( pdfc, &pObj, oXRefID(*pObj),
                         theGen(*pObj), FALSE ))
      return FALSE;

  if (oType(*pObj) != ODICTIONARY) {
    HQFAIL( "Trapping Parameters not a dictionary" );
    return error_handler( TYPECHECK );
  }

  /* Match up the TrappingParameters dictionary */
  if (!pdf_set_PSdict( pdfc, pObj, jt_trapparams_dict, &tp_dict ))
    return FALSE;

  if (oType(tp_dict) != ODICTIONARY)  /* pdf dict must have been empty */
    return TRUE;


  /* In the TrappingParameters dictionary, all keys are simple objects
     except for the ColorantZoneDetails key (CZD) whose value is a
     sub-dictionary containing keys which are colorant names each yielding
     a ColorantZoneDetails dictionary. */
  for (i = 0;  i < NUM_ARRAY_ITEMS(jt_tp_PSnames); i++) {
    if (jt_tp_PSnames[i] != 0)  /* avoid CZD & ImagemaskTrapping */
      if (!pdf_set_key( &tp_dict, jt_tp_PSnames[i], jt_trapparams_dict[i].result ))
        return FALSE;
  }

  /* The CZD (ColorantZoneDetails) key is a dictionary of named colorants each
     with a sub-dictionary for its value. The actual key names are not
     statically known - they have to be taken on trust! */
  pDict = jt_trapparams_dict[ E_jtp_CZD ].result;
  if (pDict != NULL) {
    OBJECT czd_dict = OBJECT_NOTVM_NOTHING;

    if (!pdf_set_PSdict( pdfc, pDict, NULL, &czd_dict ))
      return FALSE;

    if (oType(czd_dict) == ODICTIONARY) {
      /* Each key is an unknown colorant name, and each value is a
         sub-dictionary. Each sub-dictionary is to be recreated in PostScript
         and defined (by the name colorant name) in the 'czd_dict' we've
         created here. */
      CDETAIL_PARAMS czdParams;
      czdParams.pdfc = pdfc;
      czdParams.pCDdict = &czd_dict;

      if (!walk_dictionary( pDict, pdf_colorantzone_CB, &czdParams ))
        return FALSE;

      /* Now provide the PostScript ColorantZoneDetails dictionary as the
         value to the /ColorantZoneDetails key in the TrappingParameters
         dictionary. */
      if (!pdf_set_key( &tp_dict, NAME_ColorantZoneDetails, &czd_dict ))
        return FALSE;
    }
  }

  /* Put the PS dictionary on the stack */
  if (!push( &tp_dict, &operandstack ))
    return FALSE;

  /* run_ps_string( (uint8*)"dup /TRAPPARAMS exch ADH_dumpdict" ); *DEBUG* */

  *pSetFound = TRUE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
**  pdf_jt_set_trap_zones()
**  Given an array of trap zones (where each zone is itself an array of
**  numbers), construct a series of paths in the gstate.
*/
static Bool pdf_jt_set_trap_zones( PDFCONTEXT *pdfc,
                                   OBJECT *pZonesArr,
                                   Bool *pPathMade )
{
  int32 len;
  OBJECT *pZone;

  /* Check the Zones array is present and has entries. */
  *pPathMade = FALSE;
  if (pZonesArr == NULL)
    return TRUE;

  HQASSERT( oType(*pZonesArr) == OARRAY, "Zones not an array" );
  if (theLen(*pZonesArr) == 0)
    return TRUE;

  /* Start with a newpath. */
  if (!gs_newpath())
    return FALSE;

  /* Go through the array of arrays (i.e. array of zones) */
  pZone = oArray(*pZonesArr);
  len = (int32) theLen(*pZonesArr);
  while (--len >= 0) {
    int32 j;
    OBJECT *pNum;

    if (oType(*pZone) == OINDIRECT)
      if (!pdf_lookupxref( pdfc, &pZone, oXRefID(*pZone),
                           theGen(*pZone), FALSE ))
        return FALSE;

    HQASSERT( oType(*pZone) == OARRAY, "Zone not an array." );
    HQASSERT( theLen(*pZone) > 4, "Zone has insufficient entries." );
    HQASSERT( (theLen(*pZone) % 2) == 0, "Zone has odd number of entries." );

    /* Process the numbers in the array in pairs. */
    pNum = oArray(*pZone);
    for (j = 0;  j < (theLen(*pZone) / 2);  j++, pNum += 2 ) {
      SYSTEMVALUE coords[2];

      if ( !object_get_numeric( &pNum[0], &coords[0] ) ||
           !object_get_numeric( &pNum[1], &coords[1] ) )
        return FALSE ;

      if (j == 0) {
        /* The first pair of numbers requires a 'moveto' */
        if (!gs_moveto( TRUE, coords, &thePathInfo(*gstateptr) ))
          return FALSE;
      } else {
        /* Other pairs require a 'lineto' */
        if (!gs_lineto( TRUE, TRUE, coords, &thePathInfo(*gstateptr) ))
          return FALSE;
      }
    }

    /* An implicit closepath is required */
    if (!path_close( CLOSEPATH, &thePathInfo(*gstateptr) ))
      return FALSE;
    *pPathMade = TRUE;

    pZone++;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_jt_default_trap_zone()
**   Provides a default trap zone of the MediaBox.  Defines the path in the
**   gstate.
*/
static Bool pdf_jt_default_trap_zone( OBJECT *pMediaBox, Bool *pPathMade )
{
  SYSTEMVALUE point[2];
  sbbox_t bbox ;

  HQASSERT(pMediaBox, "No MediaBox" );
  HQASSERT( oType(*pMediaBox) == OARRAY, "MediaBox not an array" );
  HQASSERT( theLen(*pMediaBox) == 4, "MediaBox not got four numbers" );

  *pPathMade = FALSE;

  if ( !object_get_bbox(pMediaBox, &bbox) )
    return FALSE ;

  /* Construct a path representing the rectangle. */
  if (!gs_newpath())
    return FALSE;

  point[0] = bbox.x1;
  point[1] = bbox.y1;
  if (!gs_moveto( TRUE, point, &thePathInfo(*gstateptr) ))
    return FALSE;

  point[0] = bbox.x2;
  point[1] = bbox.y1;
  if (!gs_lineto( TRUE, TRUE, point, &thePathInfo(*gstateptr) ))
    return FALSE;

  point[0] = bbox.x2;
  point[1] = bbox.y2;
  if (!gs_lineto( TRUE, TRUE, point, &thePathInfo(*gstateptr) ))
    return FALSE;

  point[0] = bbox.x1;
  point[1] = bbox.y2;
  if (!gs_lineto( TRUE, TRUE, point, &thePathInfo(*gstateptr) ))
    return FALSE;

  if (!path_close( CLOSEPATH, &thePathInfo(*gstateptr) ))
    return FALSE;

  *pPathMade = TRUE;

  return TRUE;
}

/* ----------------------------------------------------------------------------
** pdf_jt_trap_regions()
** Given a "TrapRegions" object, process each of its TrapRegion objects.
*/
static Bool pdf_jt_trap_regions( PDFCONTEXT *pdfc,
                                 int32  from,    /* enumerative */
                                 OBJECT *pTrapRegions,
                                 OBJECT *pMediaBox )
{
  int32 len;
  OBJECT *pObj;
  OBJECT *pDict;

  if (pTrapRegions == NULL)
    return TRUE;        /* Nowt to do */

  HQASSERT( oType(*pTrapRegions) == OARRAY, "TrapRegions not an array." );
  len = (int32) theLen(*pTrapRegions);
  pObj = oArray(*pTrapRegions);

  while (--len >= 0) {
    if (oType(*pObj) == OINDIRECT) {
      if (!pdf_lookupxref( pdfc, &pDict, oXRefID(*pObj),
                           theGen(*pObj), FALSE ))
        return FALSE;
    } else {
      pDict = pObj;
    }

    HQASSERT( oType(*pDict) == ODICTIONARY, "TrapRegion not a dictionary." );
    if (oType(*pDict) == ODICTIONARY) {
      int32 setFound, pathMade;

      /* Match up the TrapRegion dictionary. */
      if (!pdf_dictmatch( pdfc, pDict, jt_trapregion_dict ))
        return FALSE;

      /* Find and set the corresponding set of trapping parameters.
         Note that the TP key is NOT optional. */
      if (!pdf_jt_get_trap_set( pdfc, from, jt_trapregion_dict[ E_jtr_TP ].result, &setFound ))
        return FALSE;

      /* It's an error if no parameter set found! */
      if (!setFound)
        return error_handler( UNDEFINED );

      /* Find and set the trap zones.  Note that the TZ key IS optional. */
      if (!pdf_jt_set_trap_zones( pdfc, jt_trapregion_dict[ E_jtr_TZ ].result, &pathMade ))
        return FALSE;

      /* If no trap zones were explicitly given, then we provide a default
         trap zone the size of the MediaBox. */
      if (!pathMade) {
        if (!pdf_jt_default_trap_zone( pMediaBox, &pathMade ))
          return FALSE;
      }

      /* If the trapping parameters dictionary has been put on the operand
         stack and a trap zone path created in the gstate, then execute the
         Trapping procset to instantiate it all. */
      if (setFound && pathMade) {
        if (!run_ps_string( (uint8* ) " /Trapping /ProcSet findresource exch "
                                      " 1 index /settrapparams get exec "
                                      " /settrapzone  get exec " ))
          return FALSE;
      } else if (setFound) {
        /* TrappingParameters have been pushed onto the stack, but no path has
           been made.  The stack needs popping. */
        pop( &operandstack );
      }
    }

    pObj++;
  }


  return TRUE;
}


/* ----------------------------------------------------------------------------
**  pdf_jt_get_trapinfo()
**  Given the current PDF page, this routine attempts to find a PJTF PageRange
**  object that corresponds to it.  One may or may not exist.  Either way,
**  using the hierarch of objects -  JobTicketContents -> Document -> PageRange -
**  this routine locates the appropriate Trapping (general details), TrapZone
**  and TrappingParameters objects and makes all the parameters defined therein
**  available to TrapPro by invoking the required PostScript routines.
*/
Bool pdf_jt_get_trapinfo( PDFCONTEXT *pdfc, OBJECT *pMediaBox )
{
  int32 from = E_from_PR;
  Bool doTrapping = FALSE;
  OBJECT *pPageRange;
  OBJECT *pObj;
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_GET_XC( pdfxc );
  PDF_GET_IXC( ixc );

  HQASSERT( ixc->pPJTFinfo != NULL, "No PJTFinfo in pdf_jt_get_trapinfo" );


  /* Try to find the appropropiate PageRange object for the current PDF page.
     Note that no PageRange object is a valid condition. */
  if (!pdf_jt_get_PageRange( pdfc, &pPageRange ))
    return FALSE;

  /* With the appropriate PJTF "Trapping" object, provide the PostScript
     equivalent to the setpagedevice operator. */
  if (pPageRange != NULL)
    pObj = jt_pagerange_dict[ E_jpr_T ].result;
  else
    pObj = ixc->pPJTFinfo->pTrapping;

  if (!pdf_jt_trap_details( pdfc, pObj, &doTrapping ))
    return FALSE;

  if (doTrapping) {
    OBJECT *pRegions = NULL;

    /* With the appropriate PJTF "TrapRegions" object, process each
       TrapRegion defined therein. */
    if (pPageRange != NULL) {
      pRegions = jt_pagerange_dict[ E_jpr_TR ].result;
      from = E_from_PR;
    }

    if (pRegions == NULL) {
      pRegions = ixc->pPJTFinfo->pTRegionsDOC;
      from = E_from_Doc;
    }

    if (pRegions == NULL) {
      pRegions = ixc->pPJTFinfo->pTRegionsJTC;
      from = E_from_JTC;
    }

    if ( pRegions != NULL ) {
      if (!pdf_jt_trap_regions( pdfc, from, pRegions, pMediaBox ))
        return FALSE;
    }
    else {
      HQTRACE( TRUE , ( "Expected a TrapRegions object from somewhere." )) ;
    }
  }

  return TRUE;
}

/* Log stripped */
