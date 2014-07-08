/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcid.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF CID Font implementation
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "basemap.h"
#include "namedef_.h"

#include "stacks.h"

#include "pdfexec.h"
#include "pdfmatch.h"
#include "pdffont.h"
#include "pdfcid.h"
#include "pdfxref.h"



/* -------------------------------------------------------------------------- */
int32 pdf_setciddetails(PDFCONTEXT *pdfc, PDF_CIDDETAILS *cid_details,
                        OBJECT *fontdict, int32 cidtype)
{
  PDFXCONTEXT *pdfxc ;

  static NAMETYPEMATCH cidfont_match[] = {
    /* Use the enum below to index this match */
    { NAME_CIDSystemInfo,   2, { ODICTIONARY, OINDIRECT }},
    { NAME_W   | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_W2  | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_DW  | OOPTIONAL, 3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_DW2 | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_CIDToGIDMap | OOPTIONAL, 3, { ONAME, OFILE, OINDIRECT }},
    DUMMY_END_MATCH
  };
  enum { cidfont_match_CIDSystemInfo, cidfont_match_W, cidfont_match_W2,
         cidfont_match_DW, cidfont_match_DW2, cidfont_match_CIDToGIDMap } ;

  HQASSERT(pdfc,         "pdfc is NULL in pdf_setciddetails");
  HQASSERT(cid_details,  "cid_details is NULL in pdf_setciddetails");
  HQASSERT(fontdict, "fontdict is NULL in pdf_setciddetails");
  HQASSERT(oType(*fontdict) == ODICTIONARY,
           "fontdict is not a dictionary object in pdf_setciddetails");

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  if (!pdf_dictmatch(pdfc, fontdict, cidfont_match))
    return FALSE;

  /* Extract results */
  Copy(& cid_details->cidsysinfo, cidfont_match[cidfont_match_CIDSystemInfo].result);

  /* Widths - if /W is not set it to ONULL
   * and pdf_getcid_w will default to /DW value
   */
  if (cidfont_match[cidfont_match_W].result) {
    Copy(&cid_details->w, cidfont_match[cidfont_match_W].result);
    if (!pdf_resolvexrefs(pdfc, &cid_details->w))
      return FALSE;
  } else
    theTags(cid_details->w) = ONULL ;

  /* Default width - just extract the value */
  if (cidfont_match[cidfont_match_DW].result)
    cid_details->dw = object_numeric_value(cidfont_match[cidfont_match_DW].result) ;
  else /* Default to 1000, seems like a sane value */
    cid_details->dw = 1000 ;

  /* 'Vertical' widths - also nothing clever here (yet!) */
  if (cidfont_match[cidfont_match_W2].result) {
    Copy(&cid_details->w2, cidfont_match[cidfont_match_W2].result);
    if (!pdf_resolvexrefs(pdfc, &cid_details->w2))
      return FALSE;
  } else /* Default 'empty' value */
    theTags(cid_details->w2) = ONULL;

  /* Default 'vertical' width. In PDF 1.2, this was an array of 3 numbers, while
  ** in PDF 1.3, an array of 2.  In both cases, the x value of the displacement
  ** vector (absent in 1.3) is 0 (i.e. no horizontal movement).
  */
  if (cidfont_match[cidfont_match_DW2].result) {

    OBJECT *pRes;
    uint16 resLen = theILen(cidfont_match[cidfont_match_DW2].result);
    int32 ok ;

    if ( resLen < 2 || resLen > 3 )
      return error_handler(RANGECHECK) ;

    pRes = oArray(*cidfont_match[cidfont_match_DW2].result);

    ok = object_get_numeric(pRes++, &cid_details->dw2[0]) ;
    if ( ok ) {
      if (resLen == 3) {
        ok = object_get_numeric(pRes++, &cid_details->dw2[1]) ;
      } else {
        cid_details->dw2[1] = 0.0;
      }

      if ( ok )
        ok = object_get_numeric(pRes, &cid_details->dw2[2]) ;
    }

    if ( !ok )
      return error_handler(TYPECHECK) ;
  } else { /* Default values (as per PDF spec) */

    cid_details->dw2[0] = 880.0;
    cid_details->dw2[1] = 0.0;
    cid_details->dw2[2] = -1000.0;

  }

  cid_details->ncidtogids = 0 ;
  cid_details->cidtogid = NULL ;
  if ( cidfont_match[cidfont_match_CIDToGIDMap].result &&
       cidtype == FSUB_CID2 ) {
    if ( oType(*cidfont_match[cidfont_match_CIDToGIDMap].result) == OFILE ) {
      uint32 basemap_sema ;
      FILELIST *flptr = oFile(*cidfont_match[cidfont_match_CIDToGIDMap].result) ;
      uint16 *cidbasemap ;
      uint32 ncids = 0 ;
      Hq32x2 filepos;
      int32 hi, lo = 0 ;
      void *base ;
      uint32 size ;

      HQASSERT(flptr, "No CIDToGIDMap stream") ;

      /* Stream or indirect stream. Need to know number of bytes in stream
         (or number of CIDs) to know how much memory to allocate for
         CIDToGIDMap buffer. We can't get this from the stream without
         reading it, so read it and store the bytes in basemap. The number of
         bytes won't exceed 128K, so basemap will normally have enough
         space. */

      Hq32x2FromInt32(&filepos, 0);
      if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
           (*theIMySetFilePos(flptr))(flptr, &filepos) == EOF )
        return (*theIFileLastError(flptr))(flptr) ;

      basemap_sema = get_basemap_semaphore(&base, &size) ;
      if ( !basemap_sema )
        return error_handler(VMERROR) ;

      cidbasemap = (uint16 *)base ;
      size /= sizeof(uint16) ;
      while ( (hi = Getc(flptr)) != EOF && (lo = Getc(flptr)) != EOF ) {
        if ( ncids >= size ) { /* Too many bytes in stream */
          free_basemap_semaphore(basemap_sema) ;
          return error_handler(LIMITCHECK) ;
        }
        cidbasemap[ncids++] = CAST_TO_UINT16((hi << 8) | lo) ;
      }

      free_basemap_semaphore(basemap_sema) ;

      if ( hi != EOF && lo == EOF ) /* Odd number of bytes in stream */
        return error_handler(RANGECHECK) ;

      HQASSERT(hi == EOF, "Strange error in CIDToGIDMap") ;

      cid_details->cidtogid = (uint16 *)mm_alloc(pdfxc->mm_structure_pool,
                                                 sizeof(uint16) * ncids,
                                                 MM_ALLOC_CLASS_GENERAL) ;
      if ( cid_details->cidtogid == NULL )
        return error_handler(VMERROR) ;

      cid_details->ncidtogids = ncids ;

      HqMemCpy(cid_details->cidtogid, base, ncids * sizeof(uint16)) ;
    } else {
      HQASSERT(oType(*cidfont_match[cidfont_match_CIDToGIDMap].result) == ONAME,
               "CIDToGIDMap is not a stream or a name") ;
      if ( theINameNumber(oName(*cidfont_match[cidfont_match_CIDToGIDMap].result)) != NAME_Identity )
        return error_handler(RANGECHECK) ;
    }
  }

  return TRUE;
}


/* -------------------------------------------------------------------------- */
int32 pdf_getcid_w(PDF_CIDDETAILS *cid_details, int32 cid, SYSTEMVALUE *width)
{
  HQASSERT(cid_details, "cid_details is NULL in pdf_getcid_w");
  HQASSERT(cid >= 0,    "cid parameter is negative in pdf_getcid_w");
  HQASSERT(width,       "width parameter is NULL in pdf_getcid_w");

  /* Do lookup in w array */
  if ( oType(cid_details->w) == OARRAY ||
       oType(cid_details->w) == OPACKEDARRAY ) {
    int32 olen, idx, cid_start, cid_end;
    OBJECT *olist, *cidobj;

    olist = oArray(cid_details->w);
    olen  = theLen(cid_details->w);
    idx = 0;
    while ( idx < olen ) {
      cidobj = olist + idx; /* Get CID from w array */
      if ( oType(*cidobj) != OINTEGER )
        return error_handler(TYPECHECK);
      cid_start = oInteger(*cidobj);

      if ( idx + 1 >= olen )
        return error_handler(RANGECHECK);
      switch ( oType(cidobj[1]) ) {
      case OINTEGER: /* Format type 1 */
        if ( idx + 2 >= olen )
          return error_handler(RANGECHECK);
        cid_end = oInteger(cidobj[1]);
        if ( cid >= cid_start && cid <= cid_end )
          return object_get_numeric(cidobj + 2, width);
        idx += 3;
        break;

      case OARRAY:  /* Format type 2 */
      case OPACKEDARRAY:
        cid_end = cid_start + theLen(cidobj[1]) - 1;
        if ( cid >= cid_start && cid <= cid_end )
          return object_get_numeric(oArray(cidobj[1]) + (cid - cid_start), width);
        idx += 2;
        break;

      default:
        return error_handler(TYPECHECK);
      }
    }
  }

  /* Use dw entry */
  *width = cid_details->dw;
  return TRUE;
}


/* -------------------------------------------------------------------------- */
/* pdf_getcid_w2
 *   See PDF spec, page 155
 *   widths[0] => Wy (y component, vertical advance vector)
 *   widths[1] => Vx (x component, position vector)
 *   widths[2] => Vy (y component, position vector)
 */
int32 pdf_getcid_w2(PDF_CIDDETAILS *cid_details, int32 cid, SYSTEMVALUE *widths)
{
  HQASSERT(cid_details, "cid_details is NULL in pdf_getcid_w2");
  HQASSERT(cid >= 0,    "cid parameter is negative in pdf_getcid_w2");
  HQASSERT(widths,      "widths parameter is NULL in pdf_getcid_w2");

  /* Do lookup in w array */
  if ( oType(cid_details->w2) == OARRAY ||
       oType(cid_details->w2) == OPACKEDARRAY ) {
    int32 olen, idx, cid_start, cid_end;
    OBJECT *olist, *cidobj;

    olist = oArray(cid_details->w2);
    olen  = theLen(cid_details->w2);
    idx = 0;
    while ( idx < olen ) {
      cidobj = olist + idx; /* Get CID from w2 array */
      if ( oType(*cidobj) != OINTEGER )
        return error_handler(TYPECHECK);
      cid_start = oInteger(*cidobj);

      if ( idx + 1 >= olen )
        return error_handler(RANGECHECK);
      switch ( oType(cidobj[1]) ) {
      case OINTEGER:  /* Format type 1 */
        if ( idx + 4 >= olen )
          return error_handler(RANGECHECK);
        cid_end = oInteger(cidobj[1]);
        if ( cid >= cid_start && cid <= cid_end ) {
          return (object_get_numeric(cidobj + 2, widths) &&
                  object_get_numeric(cidobj + 3, widths + 1) &&
                  object_get_numeric(cidobj + 4, widths + 2));
        }
        idx += 5;
        break;

      case OARRAY:  /* Format type 2 */
      case OPACKEDARRAY:
        cid_end = ((cid_start + theLen(cidobj[1])) / 3) - 1;
        if ( theLen(cidobj[1]) % 3 )
          return error_handler(RANGECHECK);
        if ( cid >= cid_start && cid <= cid_end ) {
          OBJECT *w = oArray(cidobj[1]) + (3 * (cid - cid_start));
          return (object_get_numeric(w, widths) &&
                  object_get_numeric(w + 1, widths + 1) &&
                  object_get_numeric(w + 2, widths + 2));
        }
        idx += 2;
        break;

      default:
        return error_handler(TYPECHECK);
      }
    }
  }

  /* Use dw2 entry */
  HQASSERT( cid_details->dw2[1] == 0.0,
            "PDF spec says CID Font Wx component of dw2 entry must be 0 "
            "- unexpected case, please send job to ericp" );
  widths[0] = cid_details->dw2[2]; /* Wy */
  if (!pdf_getcid_w(cid_details, cid, widths + 1))
    return FALSE;
  widths[1] /= 2.0 ; /* Vx = hw / 2 */
  widths[2] = cid_details->dw2[0]; /* Vy */
  return TRUE;
}

int32 pdf_getcid_gid(PDF_CIDDETAILS *cid_details, int32 cid, int32 *gid)
{
  HQASSERT(cid_details, "cid_details is NULL");
  HQASSERT(cid >= 0, "cid parameter is negative");
  HQASSERT(gid, "nowhere to put gid");

  if ( cid_details->cidtogid ) {
    if ( cid >= cid_details->ncidtogids )
      return error_handler(RANGECHECK) ;

    *gid = cid_details->cidtogid[cid] ;
  } else { /* Identity mapping */
    *gid = cid ;
  }

  return TRUE ;
}


/* Log stripped */
