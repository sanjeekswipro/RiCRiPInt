/** \file
 * \ingroup pdfout
 *
 * $HopeName: SWpdf_out!src:nopdfout.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for PDF output functions when compiled out.
 */

#include "core.h"
#include "swstart.h"
#include "objecth.h"
#include "fileioh.h"
#include "graphict.h"
#include "pathops.h"
#include "images.h"
#include "fontt.h"
#include "swpdf.h"
#include "swpdfout.h"

void pdfout_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

Bool pdfout_enabled(void)
{
  return FALSE ;
}

Bool pdfout_beginjob(corecontext_t *corecontext)
{
  UNUSED_PARAM(corecontext_t *, corecontext) ;
  return TRUE ;
}

Bool pdfout_endjob(corecontext_t *corecontext)
{
  UNUSED_PARAM(corecontext_t *, corecontext) ;
  return TRUE ;
}

Bool pdfout_setparams(PDFCONTEXT *pdfoc, OBJECT *paramdict)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(OBJECT *, paramdict) ;
  return TRUE ;
}

Bool pdfout_endpage(PDFCONTEXT **pdfoc, Bool discard)
{
  UNUSED_PARAM(PDFCONTEXT **, pdfoc) ;
  return discard ;
}

void pdfout_seterror(PDFCONTEXT *pdfc, int32 error)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(int32, error) ;
}

uint32 pdfout_pushclip(PDFCONTEXT *pdfoc, CLIPRECORD *newclip)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(CLIPRECORD *, newclip) ;
  return 0 ;
}


/* Fills. */

Bool pdfout_dofill(PDFCONTEXT *pdfc,
                   PATHINFO *thepath, uint32 type, uint32 rectp,
                   int32 colorType)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(PATHINFO *, thepath) ;
  UNUSED_PARAM(uint32, type) ;
  UNUSED_PARAM(uint32, rectp) ;
  UNUSED_PARAM(int32, colorType) ;
  return TRUE ;
}


/* Strokes. */

Bool pdfout_dostroke(PDFCONTEXT *pdfoc,
                     STROKE_PARAMS *params,
                     int32 colorType)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(STROKE_PARAMS *, params) ;
  UNUSED_PARAM(int32, colorType) ;
  return TRUE ;
}


/* Text. */

Bool pdfout_outputchar(PDFCONTEXT *pdfoc,
                       char_selector_t *selector,
                       SYSTEMVALUE x, SYSTEMVALUE y,
                       SYSTEMVALUE wx, SYSTEMVALUE wy)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(char_selector_t *, selector) ;
  UNUSED_PARAM(SYSTEMVALUE, x) ;
  UNUSED_PARAM(SYSTEMVALUE, y) ;
  UNUSED_PARAM(SYSTEMVALUE, wx) ;
  UNUSED_PARAM(SYSTEMVALUE, wy) ;
  return TRUE ;
}

Bool pdfout_begincharpath(PDFCONTEXT *pdfoc, PATHINFO *lpath)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(PATHINFO *, lpath) ;
  return TRUE ;
}

Bool pdfout_recordchar(PDFCONTEXT *pdfoc,
                       char_selector_t *selector,
                       SYSTEMVALUE x, SYSTEMVALUE y,
                       SYSTEMVALUE wx, SYSTEMVALUE wy,
                       int32 textop)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(char_selector_t *, selector) ;
  UNUSED_PARAM(SYSTEMVALUE, x) ;
  UNUSED_PARAM(SYSTEMVALUE, y) ;
  UNUSED_PARAM(SYSTEMVALUE, wx) ;
  UNUSED_PARAM(SYSTEMVALUE, wy) ;
  UNUSED_PARAM(int32, textop) ;
  return TRUE ;
}

Bool pdfout_endcharpath(PDFCONTEXT *pdfoc, Bool result)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  return result ;
}

Bool pdfout_charcached(PDFCONTEXT* pdfc,
                       int32 charpath,
                       int32 *contextid,
                       int32 *textop)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(int32, charpath) ;
  UNUSED_PARAM(int32 *, contextid) ;
  UNUSED_PARAM(int32 *, textop) ;
  return FALSE ;
}

Bool pdfout_beginchar(PDFCONTEXT** pdfoc, SYSTEMVALUE metrics[6],
                      Bool cached)
{
  UNUSED_PARAM(PDFCONTEXT **, pdfoc) ;
  UNUSED_PARAM(SYSTEMVALUE *, metrics) ;
  UNUSED_PARAM(Bool, cached) ;
  return TRUE ;
}

Bool pdfout_endchar(PDFCONTEXT** pdfoc,
                    int32 *contextid,
                    Bool result)
{
  UNUSED_PARAM(PDFCONTEXT **, pdfoc) ;
  UNUSED_PARAM(int32 *, contextid) ;
  return result ;
}

Bool pdfout_beginimage(PDFCONTEXT* pdfc, int32 colorType,
                       IMAGEARGS* image, int32** idecodes)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(IMAGEARGS *, image) ;
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(int32 **, idecodes) ;
  return TRUE ;
}

Bool pdfout_endimage(PDFCONTEXT *pdfoc, int32 Height,
                     IMAGEARGS* image)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(IMAGEARGS *, image) ;
  UNUSED_PARAM(int32, Height) ;
  return TRUE ;
}

Bool pdfout_imagedata(PDFCONTEXT *pdfoc, int32 nprocs,
                      uint8 *buf[], int32 len, Bool maskdata)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(int32, nprocs) ;
  UNUSED_PARAM(uint8 **, buf) ;
  UNUSED_PARAM(int32, len) ;
  UNUSED_PARAM(Bool, maskdata) ;
  return TRUE ;
}


Bool pdfout_patterncached(PDFCONTEXT* pdfc, int32 patternId)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(int32, patternId) ;
  return TRUE ;
}

Bool pdfout_beginpattern(PDFCONTEXT** pdfoc,
                         int32 patternId, OBJECT* poPatternS)
{
  UNUSED_PARAM(PDFCONTEXT **, pdfoc) ;
  UNUSED_PARAM(int32, patternId) ;
  UNUSED_PARAM(OBJECT *, poPatternS) ;
  return TRUE ;
}

Bool pdfout_endpattern(PDFCONTEXT** pdfoc,
                       int32 patternId, OBJECT* poPatternS,
                       Bool fOkay)
{
  UNUSED_PARAM(PDFCONTEXT **, pdfoc) ;
  UNUSED_PARAM(int32, patternId) ;
  UNUSED_PARAM(OBJECT *, poPatternS) ;
  return fOkay ;
}


Bool pdfout_doshfill(PDFCONTEXT* pdfoc, OBJECT *poShadingS,
                     OBJECT *poDataSource)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfoc) ;
  UNUSED_PARAM(OBJECT *, poShadingS) ;
  UNUSED_PARAM(OBJECT *, poDataSource) ;
  return TRUE ;
}


void pdfout_endshfill(PDFCONTEXT* pdfc)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
}


Bool pdfout_vignettefill(PDFCONTEXT* pdfc,
                         PATHINFO*          thepath,
                         uint32             type,
                         uint32             rectp,
                         int32              colorType)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(PATHINFO *, thepath) ;
  UNUSED_PARAM(uint32, type) ;
  UNUSED_PARAM(uint32, rectp) ;
  UNUSED_PARAM(int32, colorType) ;
  return TRUE ;
}

Bool pdfout_vignettestroke(PDFCONTEXT*    pdfc,
                           STROKE_PARAMS*        params,
                           int32                 colorType)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(STROKE_PARAMS *, params) ;
  UNUSED_PARAM(int32, colorType) ;
  return TRUE ;
}

Bool pdfout_vignetteend(PDFCONTEXT* pdfc,
                        Bool fTreatAsVignette)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(Bool, fTreatAsVignette) ;
  return TRUE ;
}


Bool pdfout_streamclosing(FILELIST *filter, int32 length, int32 checksum)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  UNUSED_PARAM(int32, length) ;
  UNUSED_PARAM(int32, checksum) ;
  return TRUE ;
}


Bool pdfout_UpdateInfoString(PDFCONTEXT* pdfc,
                             int32  keynum,
                             OBJECT* data)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(int32, keynum) ;
  UNUSED_PARAM(OBJECT *, data) ;
  return TRUE ;
}

Bool pdfout_annotation(PDFCONTEXT * pdfc,
                       OBJECT* contents,
                       OBJECT* open,
                       OBJECT* rect)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(OBJECT *, contents) ;
  UNUSED_PARAM(OBJECT *, open) ;
  UNUSED_PARAM(OBJECT *, rect) ;
  return TRUE ;
}

Bool pdfout_setcropbox(PDFCONTEXT * pdfc,
                       OBJECT* cropbox,
                       Bool fPages)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(OBJECT *, cropbox) ;
  UNUSED_PARAM(Bool, fPages) ;
  return TRUE ;
}

Bool pdfout_Hqn_mark(PDFCONTEXT* pdfc,
                     int32 feature,
                     int32 name,
                     OBJECT* value)
{
  UNUSED_PARAM(PDFCONTEXT *, pdfc) ;
  UNUSED_PARAM(int32, feature) ;
  UNUSED_PARAM(int32, name) ;
  UNUSED_PARAM(OBJECT *, value) ;
  return TRUE ;
}

/* Log stripped */
