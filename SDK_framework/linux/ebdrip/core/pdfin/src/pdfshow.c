/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfshow.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Text "show" operators
 */

#include "core.h"
#include "swerrors.h"
#include "fileio.h"
#include "objects.h"
#include "tt_font.h"
#include "namedef_.h"

#include "stacks.h"
#include "gstate.h"
#include "vndetect.h"
#include "gu_path.h"
#include "graphics.h"
#include "routedev.h"
#include "control.h"      /* handleNormalAction */
#include "lowmem.h" /* mm_memory_is_low */
#include "mm_core.h" /* gc_safe_in_this_operator */
#include "fcache.h"
#include "showops.h"
#include "fontops.h"
#include "pathops.h"
#include "pathcons.h"
#include "dicthash.h"
#include "utils.h"
#include "cmap.h"
#include "cidfont.h"
#include "system.h"
#include "display.h"      /* dl_safe_recursion, finishaddchardisplay */
#include "gs_color.h"     /* GSC_FILL */

#include "pdfexec.h"
#include "pdfencod.h"
#include "pdffont.h"
#include "pdfcid.h"
#include "pdfxref.h"
#include "pdfin.h"

#include "swpdfout.h"
#include "pdfshow.h"
#include "encoding.h"

/** Index this table with PDF font render mode
 * (See PDF spec, page 232 for list of render modes).
 */
const int8 pdfRenderModeTable[PDFRENDERMODE_TABLESIZE] = {
  PDFRENDERMASK_FILL,
  PDFRENDERMASK_STROKE,
  PDFRENDERMASK_FILL   | PDFRENDERMASK_STROKE,
  0, /* Invisible! */
  PDFRENDERMASK_FILL   | PDFRENDERMASK_CLIP,
  PDFRENDERMASK_STROKE | PDFRENDERMASK_CLIP,
  PDFRENDERMASK_FILL   | PDFRENDERMASK_STROKE | PDFRENDERMASK_CLIP,
  PDFRENDERMASK_CLIP
};

/** For faking an italic font style from a normal font! */
OMATRIX italic_matrix = { 1.0 ,      0.0 ,
                          0.212556 , 1.0 ,
                          0.0 ,      0.0 ,
                          MATRIX_OPT_BOTH /* Unoptimised */ } ;

/** For faking a bold font style from a normal font! */
#define LINEWIDTH_Bold .25


/* -------------------------------------------------------------------------- */
Bool pdf_setTCJM( PDFCONTEXT *pdfc, OMATRIX *PQ )
{
  SYSTEMVALUE trise ;
  SYSTEMVALUE tfs , tfsth ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( PQ , "PQ NULL in pdf_setTCJM" ) ;

  MATRIX_COPY( PQ , & imc->textstate.TLM ) ;

  trise = theIPDFFRise( gstateptr ) ;
  if ( trise != 0.0 ) {
    SYSTEMVALUE tx , ty ;
    MATRIX_TRANSFORM_DXY( 0.0, trise, tx, ty, PQ ) ;
    PQ->matrix[ 2 ][ 0 ] += tx ;
    PQ->matrix[ 2 ][ 1 ] += ty ;
  }

  tfsth = theIPDFFHorizScale( gstateptr ) ;
  if ( tfsth != 1.0 ) {
    PQ->matrix[ 0 ][ 0 ] *= tfsth ;
    PQ->matrix[ 0 ][ 1 ] *= tfsth ;
  }

  /* Calculate matrix used for Text space. This is used for adjustments
   * made by character & word spacing. This includes the horizontal
   * scaling, but NOT the font size.
   */
  matrix_mult( PQ , & theIgsPageCTM( gstateptr ) , & imc->textstate.TCM ) ;

  tfs = theIPDFFFontSize( gstateptr ) ;
  if ( tfs != 1.0 ) {
    PQ->matrix[ 0 ][ 0 ] *= tfs ;
    PQ->matrix[ 0 ][ 1 ] *= tfs ;
    PQ->matrix[ 1 ][ 0 ] *= tfs ;
    PQ->matrix[ 1 ][ 1 ] *= tfs ;

    MATRIX_SET_OPT_BOTH( PQ );
  }

  /* Set the matrix to be used by the TJ operator adjustments. This
   * includes horizontal scaling and font size. It is based on a
   * scale of 1 thousandth of an em. Scaling done (optimally) later.
   */
  matrix_mult( PQ , & theIgsPageCTM( gstateptr ) , & imc->textstate.TJM ) ;
  imc->textstate.TJM.matrix[ 0 ][ 0 ] *= 0.001 ;
  imc->textstate.TJM.matrix[ 0 ][ 1 ] *= 0.001 ;
  imc->textstate.TJM.matrix[ 1 ][ 0 ] *= 0.001 ;
  imc->textstate.TJM.matrix[ 1 ][ 1 ] *= 0.001 ;

  return TRUE ;
}


/* -------------------------------------------------------------------------- */
Bool pdf_setTRM( PDFCONTEXT *pdfc, OMATRIX *PQ, OBJECT *fntmatrix)
{
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  HQASSERT( PQ , "PQ NULL in pdf_setTCJM" ) ;

  matrix_mult( PQ , & theIgsPageCTM( gstateptr ), & imc->textstate.TRM ) ;

  /* Set the matrix to be used by the character widths. This includes
   * horizontal scaling and font size. It is based on the FontMatrix
   * for a Type3 font, and the concatenation of matrices and a scale of
   * 1 thousandth for all other fonts.
   */
  if ( fntmatrix != NULL ) {
    OMATRIX T3M ;
    if ( !is_matrix( fntmatrix, & T3M ) )
      return FALSE ;
    /* clear Tx+Ty components, as they get applied later during moveto */
    T3M.matrix[2][0] = T3M.matrix[2][1] = 0.0;
    matrix_mult( & T3M , & imc->textstate.TRM , & imc->textstate.TRM ) ;
  } else {
    imc->textstate.TRM.matrix[ 0 ][ 0 ] *= 0.001 ;
    imc->textstate.TRM.matrix[ 0 ][ 1 ] *= 0.001 ;
    imc->textstate.TRM.matrix[ 1 ][ 0 ] *= 0.001 ;
    imc->textstate.TRM.matrix[ 1 ][ 1 ] *= 0.001 ;
  }

  imc->textstate.newTM = FALSE ;
  return TRUE ;
}


/* -------------------------------------------------------------------------- */
static Bool pdf_switchbuildglyph( PDFCONTEXT *pdfc ,
                                  PDF_FONTDETAILS *pdf_fontdetails ,
                                  int32 charcode ,
                                  NAMECACHE *charname )
{
  OBJECT *charstream ;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING ;
  PDF_TYPE3DETAILS *type3_details ;

  type3_details = pdf_fontdetails->type3_details ;
  HQASSERT( type3_details , "Type3 font with missing details" ) ;
  HQASSERT( charcode >= 0 && charcode <= 255 , "charcode out of range" ) ;

  charstream = type3_details->charstrms[ charcode ] ;
  if ( charstream == NULL ) {

    if ( ! charname ) /* The encoding MUST map to a name. */
      return error_handler( UNDEFINEDRESULT ) ;

    /* Extract charname from CharProcs. */
    theTags(nameobj) = ONAME | LITERAL ;
    oName(nameobj) = charname ;
    if ( type3_details->charprocs )
      charstream = fast_extract_hash( type3_details->charprocs , &nameobj ) ;

    if ( charstream && oType(*charstream) == OINDIRECT ) {
      if ( ! pdf_lookupxref( pdfc , & charstream ,
                 oXRefID( *charstream ) , theIGen( charstream ) , FALSE))
        return FALSE ;
      if ( charstream == NULL )
        return error_handler( UNDEFINEDRESOURCE ) ;
    }

    if ( !charstream ) {
      /* Our minimal notdef definition is in PSVM to avoid HDLT problems */
      oName(nameobj) = system_names + NAME_notdef ;
      charstream = fast_extract_hash( &internaldict , &nameobj ) ;
    } else if ( oType( *charstream ) != OFILE )
      return error_handler( TYPECHECK ) ;

    type3_details->charstrms[ charcode ] = charstream ;
  }

  theTags( nameobj ) = ONAME ;
  oName( nameobj ) = system_names + NAME_BuildGlyph ;
  return fast_insert_hash( & theMyFont( theIFontInfo( gstateptr )) , & nameobj , charstream ) ;
}

typedef struct {
  PDFCONTEXT *pdfc ;
  PDF_FONTDETAILS *fd, *parent ;
  PDF_WIDTHDETAILS *width ;
  OBJECT *font ;
  OBJECT stringo ;
  OMATRIX matrix ;
  OMATRIX parent_matrix ;
  int32 cid ; /* Original CID for CIDToGID mapped fonts */
} pdfshow_details_t ;

enum { PLOT_normal, PLOT_cidfont, PLOT_glyphname, PLOT_notdef };

static Bool char_pdftype0_selector(void *data, char_selector_t *selector,
                                   Bool *eod)
{
  pdfshow_details_t *details = data ;
  PDFCONTEXT *pdfc ;
  PDF_IMC_PARAMS *imc ;
  OBJECT *stringo ;
  int32 fontcode, charcode;
  NAMECACHE *charname ;
  PDF_FONTDETAILS *fd, *parent = NULL ;
  OMATRIX usemat, *usemat_ptr = NULL ;
  uint8 *clist;
  int32 length, i;
  int32 plot_state = PLOT_normal;
  OBJECT cmap_return = OBJECT_NOTVM_NOTHING,
    cmap_input = OBJECT_NOTVM_NOTHING ;
  OBJECT fmo = OBJECT_NOTVM_NOTHING, *font = NULL ;
  uint8 scratch[CMAP_MAX_CODESPACE_LEN] ;

  HQASSERT(details, "No selector data for PDF Type 0 provider") ;

  pdfc = details->pdfc ;
  HQASSERT(pdfc, "No PDF context for PDF Type 0 provider") ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  fd = details->fd ;
  HQASSERT(fd, "No fontdetails for PDF Type 0 provider") ;

  stringo = &details->stringo ;
  HQASSERT(stringo && oType(*stringo) == OSTRING,
           "No string to extract selector from") ;
  HQASSERT(selector, "No destination for character selector") ;
  HQASSERT(eod, "No end of data pointer") ;

  if ( theILen(stringo) == 0 ) {
    *eod = TRUE ;
    return TRUE ;
  }

  selector->index = -1 ;
  selector->cid = -1 ;
  selector->name = NULL ;

  Copy(object_slot_notvm(&selector->complete), stringo) ;
  selector->string =
    selector->cmap =
    selector->font = onull ; /* Struct copy to set slot properties */

  charcode = -1;
  charname = NULL ;
  fontcode = 0;
  MATRIX_COPY(&details->parent_matrix, &identity_matrix) ;

  HQASSERT(fd->font_type == FONT_Type0,
           "Not type 0 font when getting type 0 selector") ;

  do {
    /* Shouldn't be here, should have dropped out */
    if ( plot_state != PLOT_normal )
      return error_handler(INVALIDFONT);

    length = theILen(stringo) ;

    HQASSERT(fd->cmap_details, "cmap_details NULL for Type0 font" ) ;

    /* Save CMap for notdef lookups and PDF Out */
    Copy(&selector->cmap, &fd->cmap_details->cmap_dict) ;
    OCopy(selector->parent, fd->df_array) ;

    theTags(cmap_return) = OSTRING | UNLIMITED | LITERAL;
    theLen(cmap_return) = (uint16)CMAP_MAX_CODESPACE_LEN;
    oString(cmap_return) = scratch;

    /* Do lookup */
    if ( !cmap_lookup(&selector->cmap, stringo,
                      &fontcode, &cmap_return) )
      return FALSE ;

    /* Setup final string that was consumed by cmap_lookup */
    theLen(selector->string) = (uint16)(length - theILen(stringo));
    theTags(selector->string) = OSTRING | LITERAL;
    oString(selector->string) =
      oString(*stringo) - theLen(selector->string) ;

    /* Do not set up widthshow/awidthshow index, it is not used. */

    switch ( oType(cmap_return) ) {
    case ONULL:
      /* PLRM3, p.390: "If the CMap does not contain either a character
         mapping or a notdef mapping for the code, font 0 is selected
         and a glyph is substituted from the associated font or
         CIDFont. If it is a base font, the character name .notdef is
         used; if it is a CIDFont, CID 0 is used. If it is a composite
         font, the behaviour is implementation-dependent. */
      fontcode = 0 ;
      plot_state = PLOT_notdef ;

      break ;

    case OINTEGER: /* CID font */
      charcode = oInteger(cmap_return);
      plot_state = PLOT_cidfont;

      break;

    case ONAME: /* Glyph name */
      plot_state = PLOT_glyphname;
      charname = oName(cmap_return) ;

      break;

    case OSTRING: /* Mapped to string */
      /* This is the most awkward case, as it's the only one of the three
         (valid) results that can lead to going round the loop again.

         Descendent from this point can't consume more bytes from the
         show string, only from the cmap-translated result string that
         we've got back, so copy this string to the input scratch space
         and reset stringo to point at it.
         */
      theTags(cmap_input) = OSTRING | LITERAL | UNLIMITED ;
      theLen(cmap_input) = theLen(cmap_return) ;
      oString(cmap_input) = selector->codes;
      for ( i = 0 ; i < theLen(cmap_return) ; ++i )
        selector->codes[i] = scratch[i] ;

      stringo = &cmap_input ;

      break ;

    default:
      HQFAIL("Got an unexpected result from cmap_lookup");
    }

    HQASSERT(fontcode >= 0, "Invalid fontcode from cmap_lookup") ;

    if (fontcode >= fd->num_descendants)
      return error_handler( INVALIDFONT ) ;

    /* Unpack details for this font (if we need to) */
    parent = fd ;
    font = oArray(parent->df_array) + fontcode ;
    if ( (oType(*font) != OINDIRECT && oType(*font) != ODICTIONARY) ||
         !pdf_font_unpack(pdfc, font, & parent->descendants[fontcode]) )
      return FALSE ;
    fd = parent->descendants[fontcode] ;
    HQASSERT( fd, "fd NULL after unpacked font" ) ;

    /* Extract fontmatrix, and concatenate with current result */
    if ( !cmap_getfontmatrix(&selector->cmap, fontcode, &fmo) )
      return FALSE ;

    if ( oType(fmo) != ONULL ) { /* No entry */
      if ( usemat_ptr == NULL ) {
        if ( !is_matrix(&fmo, &usemat) )
          return FALSE ;
        usemat_ptr = &usemat ;
      } else {
        OMATRIX fmtmp ;
        if ( !is_matrix(&fmo, &fmtmp) )
          return FALSE ;
        /* Save a copy of old matrix in case of notdef mapping */
        MATRIX_COPY(&details->parent_matrix, usemat_ptr) ;
        matrix_mult(&fmtmp, usemat_ptr, usemat_ptr) ;
      }
    }
  } while ( fd->font_type == FONT_Type0 ) ;

  length = theILen(stringo) ;
  clist = oString(*stringo) ;

  switch ( plot_state ) {
  case PLOT_notdef:
    /* Either CID 0 or /.notdef, according to base font type */
    if ( fd->font_type == FONT_TypeCID ) {
      charcode = 0 ;
      charname = NULL ;
    } else {
      charcode = -1 ;
      charname = system_names + NAME_notdef ;
    }
    break ;
  case PLOT_normal:
    if ( fd->font_type == FONT_TypeCID )
      return error_handler(INVALIDFONT) ;

    /* Now we're down at a base font; we may not have collected a
       character on the way - if not do so now */
    if ( charcode < 0 ) {
      if (length < 1)
        return error_handler (RANGECHECK);
      theLen(selector->string) = 1;
      theTags(selector->string) = OSTRING | LITERAL;
      oString(selector->string) = clist ;
      charcode = clist [0];
      /* widthshow/awidthshow index is not used for PDF:
         selector->index = (fontcode << 8) | charcode ;
         */
      clist++; length--;
    }
    HQASSERT(charname == NULL, "Name set in charcode font selector") ;
    break ;
  case PLOT_glyphname:
    if ( fd->font_type == FONT_TypeCID )
      return error_handler(INVALIDFONT) ;
    HQASSERT(charcode < 0, "CID set in named font selector") ;
    HQASSERT(charname != NULL, "Name invalid in named font selector") ;
    break ;
  case PLOT_cidfont:
    /* PLRM3 p.389: "Under special conditions, a CID can be used when the
       descendant is a Type 3 base font. The font's BuildGlyph or BuildChar
       procedure is invoked to render a character whose code is the last
       byte originally extracted from the show string. If this procedure
       executes setfont to establish a CIDFont as the current font and then
       executes a show operation on a string consisting of just that
       character code, the code is ignored; instead, the CID determined by
       the earlier CMap mapping is used to look up the glyph in the
       CIDFont." */
    if ( fd->font_type == FONT_Type3 ) {
      selector->type3cid = charcode ;  /* Save CID we calculated */
      charcode = clist[-1] ;           /* Substitute for last character */
    } else if ( fd->font_type != FONT_TypeCID )
      return error_handler(INVALIDFONT) ;

    HQASSERT(charcode >= 0, "CID invalid in CID font selector") ;
    HQASSERT(charname == NULL, "Name set in CID font selector") ;
    break ;
  default:
    HQFAIL("Strange plot_state") ;
  }

  HQASSERT(stringo == &details->stringo || length == 0,
           "Not enough bytes consumed from final CMap") ;
  theILen(stringo) = (uint16)length ;
  oString(*stringo) = length == 0 ? NULL : clist ;

  selector->cid = charcode ;
  selector->name = charname ;

  /* Concatenate with Cmap matrix result with PQ partial result */
  if (usemat_ptr == NULL) {
    usemat_ptr = &usemat ;
    MATRIX_COPY( usemat_ptr, & imc->textstate.PQM ) ;
  } else
    matrix_mult( usemat_ptr, & imc->textstate.PQM, usemat_ptr ) ;

  /* Additionally, if we are dealing with a styled font - with an italic
   * style - then apply a shear matrix to make it look 'about right' - as
   * per Adobe stuff.
   */
  if ( fd->font_style & FSTYLE_Italic )
    matrix_mult( & italic_matrix, usemat_ptr, usemat_ptr ) ;

  /* Do we need to set new font matrix if font matrix has changed, or if
   * first time thru */
  if ( details->font == NULL || !MATRIX_EQ( usemat_ptr, &details->matrix ) ) {
    if ( !pdf_setTRM(pdfc, usemat_ptr,
                     fd->font_type == FONT_Type3 ?
                     fd->type3_details->fntmatrix : NULL) )
      return FALSE ;
    gs_setfontctm( usemat_ptr ) ;
    MATRIX_COPY( &details->matrix, usemat_ptr ) ;
  }

  /* Changed fonts so set new font details */
  if (font != details->font) {
    HQASSERT(theLen(parent->df_array) >= fontcode,
             "fontcode is not valid for this descendant font array") ;
    if ( !gs_setfont( &(fd->atmfont)) )
      return FALSE ;

    details->font = font ;
    details->width = fd->width_details ;
  }

  /* Look at updated string to see how much we consumed. If the length of the
     remaining string is zero, we've used the whole of the original string. */
  stringo = &details->stringo ;
  if ( theILen(stringo) > 0 )
    theLen(selector->complete) = (uint16)(oString(*stringo) -
                                          oString(selector->complete)) ;

  details->fd = fd ;
  details->parent = parent ;

  HQASSERT(theLen(selector->string) > 0 &&
           oString(selector->string) != NULL,
           "Selector string not set") ;

  *eod = FALSE ;

  /* Displaying the character is deferred to pdfshow */

  return TRUE ;
}

/** Resolve notdef CID characters. */
static Bool pdf_notdef_show(char_selector_t *selector, int32 type, int32 charCount,
                            FVECTOR *advance, void *data)
{
  char_selector_t selector_copy ;
  pdfshow_details_t *details = data ;

  HQASSERT(selector, "No char selector for PDF notdef character") ;
  HQASSERT(details, "No show details for PDF notdef character") ;

  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(details->cid > 0, "PDF notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* No CMap lookup for notdef. Use CID 0 (notdef) in current font instead */
  details->cid = 0 ;
  if ( !pdf_getcid_gid(details->fd->cid_details, 0, &selector_copy.cid) )
    return FALSE ;

  return plotchar(&selector_copy, type, charCount, NULL, NULL, advance, CHAR_NORMAL) ;
}

/** This function is tried first, allowing the CMap notdef mapping to be
    consulted to try and find a character definition. The recursive call
    to plotchar uses the function above to use the notdef CID 0 if the
    notdef mapping turns out to be bad. */
static Bool pdf_notdef_cmap(char_selector_t *selector, int32 type,
                            int32 charCount,
                            FVECTOR *advance, void *data)
{
  char_selector_t selector_copy ;
  pdfshow_details_t *details = data ;
  PDFCONTEXT *pdfc ;
  PDF_IMC_PARAMS *imc ;
  PDF_FONTDETAILS *fd, *parent ;
  OBJECT cidobj = OBJECT_NOTVM_NOTHING, fmo = OBJECT_NOTVM_NOTHING, *font ;
  int32 fontcode ;
  OMATRIX usemat ;

  HQASSERT(selector, "No char selector for PDF notdef character") ;
  HQASSERT(details, "No font details for PDF notdef character") ;

  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(details->cid > 0, "PS notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  pdfc = details->pdfc ;
  HQASSERT(pdfc, "No PDF context") ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  /* PLRM3 p390: Go back to CMap for a notdef lookup. Note that the font
     number in the notdef mapping can be different from the font number in
     the original character mapping. This should re-select the descendent
     font from the Type 0 font which used the CMap. */
  HQASSERT(oType(selector_copy.cmap) == ODICTIONARY,
           "CMap is not valid") ;

  if ( ! cmap_lookup_notdef(&selector_copy.cmap, &selector_copy.string,
                            &fontcode, &cidobj) )
    return FALSE ;

  HQASSERT(oType(cidobj) == OINTEGER,
           "Notdef mapping did not give a CID") ;
  selector_copy.cid = oInteger(cidobj) ;

  /* Select new font from parent, verify it is a CID font. */
  parent = details->parent ;
  HQASSERT(parent, "No parent font for PDF notdef character") ;

  if ( fontcode >= parent->num_descendants )
    return error_handler(INVALIDFONT) ;

  /* Unpack details for this font (if we need to) */
  font = oArray(parent->df_array) + fontcode ;
  if ( (oType(*font) != OINDIRECT && oType(*font) != ODICTIONARY) ||
       !pdf_font_unpack(details->pdfc, font, & parent->descendants[fontcode]) )
    return FALSE ;

  fd = parent->descendants[fontcode] ;
  HQASSERT( fd, "fd NULL after unpacked font" ) ;

  /* Extract fontmatrix, and concatenate with current result */
  if ( !cmap_getfontmatrix(&selector_copy.cmap, fontcode, &fmo) )
    return FALSE ;

  if ( oType(fmo) != ONULL ) {
    if ( !is_matrix(&fmo, &usemat) )
      return FALSE ;
    matrix_mult(&usemat, &details->parent_matrix, &usemat) ;
  } else {
    MATRIX_COPY(&usemat, &details->parent_matrix) ;
  }

  matrix_mult(&usemat, &imc->textstate.PQM, &usemat) ;

  if ( fd->font_style & FSTYLE_Italic )
    matrix_mult( & italic_matrix, &usemat, &usemat ) ;

  /* Do we need to set new font matrix if font matrix has changed, or if
   * first time thru */
  HQASSERT(details->font != NULL, "Shouldn't be first time through in notdef") ;
  if ( !MATRIX_EQ( &usemat, &details->matrix ) ) {
    if ( !pdf_setTRM(pdfc, &usemat,
                     fd->font_type == FONT_Type3 ?
                     fd->type3_details->fntmatrix : NULL) )
      return FALSE ;
    gs_setfontctm( &usemat ) ;
    MATRIX_COPY( &details->matrix, &usemat ) ;
  }

  /* Changed fonts so set new font details */
  if (font != details->font) {
    HQASSERT(theLen(parent->df_array) >= fontcode,
             "fontcode is not valid for this descendant font array") ;
    if ( !gs_setfont( &(fd->atmfont)) )
      return FALSE ;
  }

  /* The sub-font selected must be a CID font. This will break if a Type 3
     intermediary is used (PLRM p.389); we have no evidence about how Adobe
     treat these, since Distiller 4.0 locks up on trying such a font. */
  if ( !FONT_IS_CID(theFontType(theFontInfo(*gstateptr))) )
    return error_handler(INVALIDFONT) ;

  details->cid = selector_copy.cid ;
  if ( !pdf_getcid_gid(details->fd->cid_details, selector_copy.cid,
                       &selector_copy.cid) )
    return FALSE ;

  return plotchar(&selector_copy, type, charCount, pdf_notdef_show, data,
                  advance, CHAR_NORMAL) ;
}

/* -------------------------------------------------------------------------- */
Bool pdf_show( PDFCONTEXT *pdfc ,
               PDF_FONTDETAILS *pdf_fontdetails ,
               OBJECT *theo , int32 type, PDF_SW_PARAMS *pSWParams )
{
  DL_STATE *page = pdfc->corecontext->page;

  int32 len ;

  SYSTEMVALUE Tx, Ty ;

  Bool useTc ;
  Bool gotTc = FALSE ;
  SYSTEMVALUE Tc ;
  SYSTEMVALUE Tcx = 0.0 , Tcy = 0.0 ; /* To keep compiler quite. */

  Bool useTw ;
  Bool gotTw = FALSE ;
  SYSTEMVALUE Tw ;
  SYSTEMVALUE Twx = 0.0 , Twy = 0.0 ; /* To keep compiler quite. */

  SYSTEMVALUE fauxwidth = LINEWIDTH_Bold ;

  Bool render_fill, render_stroke, render_clip, render_none ;
  PATHINFO *path ;

  pdfshow_details_t details ;

  int saved_dl_safe_recursion = dl_safe_recursion ;
  int32 charCount = 0 ;
  int32 cmap_wmode = 0;   /* default writing mode 0 == horizontally. */

  PDFXCONTEXT    *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdf_fontdetails , "pdf_fontdetails NULL in pdf_show" ) ;
  HQASSERT( theo , "theo NULL in pdf_show" ) ;

  details.pdfc = pdfc ;
  details.fd = pdf_fontdetails ;
  details.font = NULL ;

  len = theILen( theo ) ;
  if ( len == 0 ) /* Length of array or string; either will do(!). */
    return TRUE ;

  Tc = theIPDFFCharSpace( gstateptr ) ;
  useTc = (Tc != 0.0) ;
  Tw = theIPDFFWordSpace( gstateptr ) ;
  useTw = (Tw != 0.0) ;

  path = &theIPathInfo( gstateptr );
  render_fill   = isPDFRenderModeFill( theIPDFFRenderMode( gstateptr )) ;
  render_stroke = isPDFRenderModeStroke( theIPDFFRenderMode( gstateptr )) ;
  render_clip   = isPDFRenderModeClip( theIPDFFRenderMode( gstateptr )) ;
  render_none   = !render_fill && !render_stroke && !render_clip ;


  if (pSWParams == NULL) {    /* not doing just a 'stringwidth' */

    if ( ! flush_vignette( VD_Default ))
      return FALSE ;

    /* Either add the start point of our text, or make sure current point correct. */
    if ( imc->textstate.newCP ) {
      imc->textstate.newCP = FALSE ;
      Tx = imc->textstate.TRM.matrix[ 2 ][ 0 ] ;
      Ty = imc->textstate.TRM.matrix[ 2 ][ 1 ] ;
      if ( ! path->lastline) {
        if ( ! path_moveto( Tx , Ty , MOVETO , path ))
          return FALSE ;
      }
    }
    else {
      Tx = imc->textstate.CP.x ;
      Ty = imc->textstate.CP.y ;
      HQASSERT(   path->lastline , "should now be a current point" ) ;
    }

    if ( type == OSTRING ) {
      if ( ! finishaddchardisplay(page, len))
        return FALSE ;
      len = 1 ; /* So we only go round outer loop once. */
    }
    else {
      theo = oArray( *theo ) ;
    }

    if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )
      return FALSE ;
  }
  else {  /* For string-width, only need the displacement */
    Tx = 0.0;
    Ty = 0.0;
    if ( type == OSTRING )
      len = 1; /* So we only go round outer loop once. */
    else
      theo = oArray( *theo ) ;
  }

  /* This will be changed when descendant of Type0 is selected */
  details.width = ( pdf_fontdetails->width_details ) ;

  /* If the cmap details are set, retain the required horizontal/vertical
  ** writing mode.  Note that subsequent correct use of this 'wmode' assumes
  ** that PDF files can only dictate vertical mode through a Type0's CMap.
  */
  if (pdf_fontdetails->cmap_details != NULL)
    cmap_wmode = pdf_fontdetails->cmap_details->wmode;

  details.stringo = onull ; /* Struct copy to set slot properties */

  { /* Adjust faux bold stroke width in line with job CTM. [368485]
     * The ratio of the defaultCTM to the current CTM tells us how much to
     * adjust the faux bolding by. Calculated outside the loop as the CTMs
     * don't change inside. */
    SYSTEMVALUE det_CTM, det_def ;
    OMATRIX   * CTM = & thegsPageCTM(*gstateptr),
              * def = & imc->defaultCTM ;

    /* Calculate the unsigned determinants */
    det_CTM = fabs((CTM->matrix[0][0] * CTM->matrix[1][1]) -
                   (CTM->matrix[0][1] * CTM->matrix[1][0])) ;
    det_def = fabs((def->matrix[0][0] * def->matrix[1][1]) -
                   (def->matrix[0][1] * def->matrix[1][0])) ;

    /* If current CTM isn't silly, adjust faux bolding stroke width */
    if (det_CTM >= 0.00001 && det_def != det_CTM)
      fauxwidth *= sqrt(det_def / det_CTM) ;
  }

  /* Loop through each item (width-number or string) in TJ's array */
  while ((--len) >= 0 ) {
    char_selector_t selector = {0} ;
    char_selector_fn get_selector ;
    void *get_selector_data ;

    static char_selector_t *parent_selector = NULL ;
    char_selector_t *ancestor ;

    selector.pdf = TRUE ;

    /* Copy object from array so we can modify it */
    Copy(&details.stringo, theo) ;
    ++theo ;

    /* Deal with the case of either a number or string. */
    if ( type == OARRAY || type == OPACKEDARRAY ) {
      if ( oType(details.stringo) == OSTRING ) {
        if ( pSWParams == NULL ) {    /* Not just doing a 'stringwidth' */
          if ( ! finishaddchardisplay(page, theLen(details.stringo)) )
            return FALSE ;
        }
      }
      else {
        SYSTEMVALUE tl ;
        SYSTEMVALUE tx , ty ;

        if ( !object_get_numeric(&details.stringo, &tl) )
          return FALSE ;

        /* Vertical writing: Use TJ's added displacement on y, not x */
        if (cmap_wmode == 1)
          MATRIX_TRANSFORM_DXY( 0.0, tl, tx, ty, &imc->textstate.TJM );
        else
          MATRIX_TRANSFORM_DXY( tl, 0.0, tx, ty, &imc->textstate.TJM );

        Tx -= tx ;
        Ty -= ty ;
        continue ;
      }
    }

    if ( pdf_fontdetails->font_type == FONT_TypeCID ) {
      if ( parent_selector && parent_selector->type3cid >= 0 &&
           theLen(details.stringo) == 1 &&
           oString(details.stringo)[0] == parent_selector->cid ) {
        /* PLRM3 p.389: "Under special conditions, a CID can be used when the
           descendant is a Type 3 base font. The font's BuildGlyph or
           BuildChar procedure is invoked to render a character whose code is
           the last byte originally extracted from the show string. If this
           procedure executes setfont to establish a CIDFont as the current
           font and then executes a show operation on a string consisting of
           just that character code, the code is ignored; instead, the CID
           determined by the earlier CMap mapping is used to look up the
           glyph in the CIDFont." */
        get_selector = char_ancestor_selector ;
        ancestor = parent_selector ;
        get_selector_data = &ancestor ;
      } else
        return error_handler(INVALIDFONT);
    } else if ( pdf_fontdetails->font_type == FONT_Type0 ) {
      get_selector = char_pdftype0_selector ;
      get_selector_data = &details ;
    } else {
      get_selector = char_base_selector ;
      get_selector_data = &details.stringo ;
    }

    /* Loop through each character in a string */
    for (;;) {
      int32 widthcode, result ;
      char_selector_t *old_parent = parent_selector ;
      FVECTOR advance = {0.0, 0.0} ;

      /* CID display info */
      SYSTEMVALUE w = 0.0, w2[3] ; /* Silence stupid compiler */

      /* Try to make sure the reserve is full (like doalltheshows_internal()) */
      if ( mm_memory_is_low || dosomeaction ) {
        dl_erase_nr eraseno_before = page->eraseno;
        int current_dl_safe_recursion = dl_safe_recursion ;
        dl_safe_recursion = saved_dl_safe_recursion ;
        gc_safe_in_this_operator();
        result = handleNormalAction();
        gc_unsafe_from_here_on();
        dl_safe_recursion = current_dl_safe_recursion ;
        if ( ! result )
          return FALSE ;
        if ( page->eraseno != eraseno_before
             && !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )
          return FALSE ;
      }

      details.fd = pdf_fontdetails ;

      if ( !(*get_selector)(get_selector_data, &selector, &result) )
        return FALSE ;

      if ( result )
        break ;

      HQASSERT(oType(selector.string) == OSTRING &&
               theLen(selector.string) > 0,
               "Selector has invalid string") ;
      HQASSERT(oType(selector.complete) == OSTRING &&
               theLen(selector.complete) > 0,
               "Selector has invalid complete string") ;
      HQASSERT((selector.cid >= 0 && selector.name == NULL) ||
               (selector.cid < 0 && selector.name != NULL),
               "Selector should have either CID or name") ;

      charCount++ ;
      widthcode = selector.cid ;

      /* Do some pre-show stuff */
      switch (details.fd->font_type) {

      case FONT_TypeCID:
        /* Vertical writing mode -> obtain and apply hwmOrigin->vwmOrigin
         * position vector, and get 'vertical' width
         */
        if ( pdf_fontdetails->cmap_details->wmode ) {
          if ( !pdf_getcid_w2(details.fd->cid_details, selector.cid, w2) )
            return FALSE ;
          w = 0.0 ; /* No horizontal advance */

          /* Apply position vector from horizontal writing mode origin
           * to vertical writing mode origin
           */
          MATRIX_TRANSFORM_DXY( w2[1], w2[2], w2[1], w2[2], & imc->textstate.TRM ) ;
          Tx -= w2[1] ;
          Ty -= w2[2] ;
        } else { /* Horizontal writing mode -> get width */
          if ( !pdf_getcid_w(details.fd->cid_details, selector.cid, &w) )
            return FALSE ;
          w2[0] = 0.0 ; /* No vertical advance */
        }

        /* Apply CIDToGIDMap if present, to give a new CID. TrueType fonts
           have identity CIDMaps, so we apply the mapping here. It would be
           nicer to apply it in place of the font's CIDMap, but we can't
           easily do that. */
        details.cid = selector.cid ;
        if ( !pdf_getcid_gid(details.fd->cid_details, selector.cid,
                             &selector.cid) )
          return FALSE ;
        break ;

      case FONT_Type3:
        details.cid = -1 ;
        if (!pdf_getencoding(details.fd->encode_details,
                             &selector.cid, &selector.name))
          return FALSE ;

        if (!selector.name) {
          /* [12617] use .notdef */
          selector.name = system_names + NAME_notdef ;
        }
        /* Need to switch BuildGlyph in the current font to what
         * we're going to show.
         */
        if (!pdf_switchbuildglyph(pdfc, details.fd, widthcode, selector.name))
          return FALSE ;
        break ;

      case FONT_TrueType:
        {
          /*OBJECT * key, * charstrings, * gid*/ ;

          details.cid = -1 ;

          /* This is significantly borked by trying to match Acrobat's
           * non-compliant behaviour. This horrendous 'if' (it should have been
           * purely the Nonsymbolic flag), tt_fake_glyphname and the shenanigans
           * in tt_ps_charstrings are required to match Acrobat's mad behaviour
           * in the case of an encoded font with a (3,1) cmap but marked as
           * Symbolic. See tt_ps_charstrings comment.
           */
          /** \todo I'm removing the "based on MR/WA" heuristic again, because it
           * incorrectly hits too many subsets. The key factor is that a (3,1)
           * was chosen as the Nonsymbolic cmap, but the Symbolic flag is set.
           * Our current scheme allows different Symbolic and Nonsymbolic cmaps,
           * but Acrobat is not so sophisticated and preselects one cmap only.
           * We may therefore need a "chose a (3,1)" flag here...
           */
          if (pdf_fontdetails->encode_details->enc_type == ENC_MacRoman ||
              pdf_fontdetails->encode_details->enc_type == ENC_WinAnsi ||
              details.fd->font_flags & 32) {
            /* Nonsymbolic font:
             * look up the chr code in the Encoding (and failing that,
             * StandardEncoding) and then look the name up in Charstrings.
             */

            if ((details.fd->font_flags & 32) == 0) {
              /* Nonsymbolic lookup implied by Encoding, but flags say Symbolic!
               * Acrobat gets confused and looks up the character code DIRECTLY
               * in the cmap. We emulate this by augmenting the AGL lookup with
               * invented glyphnames such that we can always choose a glyphname
               * that will map to the original code, which smacks of what I was
               * trying to get rid of.
               */
              selector.name = tt_fake_glyphname((uint16)selector.cid) ;

            } else {

              if (!pdf_getencoding(details.fd->encode_details,
                                   &selector.cid, &selector.name))
                return FALSE ;

              if (!selector.name && selector.cid < 256)
                selector.name = StandardEncoding[selector.cid] ;

              if (!selector.name)
                selector.name = system_names + NAME_notdef ;

            }
            /*oName(nnewobj) = selector.name ;
            key = &nnewobj ;*/

          } else {
            /* Symbolic font:
             * look up the chr code directly in Charstrings. */

            /*oInteger(inewobj) = selector.cid ;
            key = &inewobj ;*/

          }
          /* now look up in Charstrings */
          /*
          charstrings = fast_extract_hash_name(&details.fd->atmfont,
                                               NAME_CharStrings) ;

          if (charstrings == NULL || oType(*charstrings) != ODICTIONARY )
            return error_handler(INVALIDFONT) ;

          gid = extract_hash(charstrings, key) ;
          if (!gid) {
            selector.cid = 0 ;
            selector.name = system_names + NAME_notdef ;
          }
          */
          break ;
        }

      default:

        if ( ! pdf_getencoding(details.fd->encode_details,
                             &selector.cid, &selector.name) )
          return FALSE ;
        break ;
      }

      if (pSWParams == NULL) {    /* Not just doing a 'stringwidth' */
        /* Update the current point as used by gs. */
        if ( ! path_moveto( Tx , Ty , MOVETO , path ))
          return FALSE ;
      }

      result = TRUE ;
      old_parent = parent_selector ;
      parent_selector = &selector ;

      /* Do NOT return between here and restore of parent_selector without
         cleaning it up. */

      /* Show! (must fill before stroke). A render_none is treated akin to
       * a stringwidth - to calculate character width in case it cannot be
       * obtained elsewhere. */
      if ( pSWParams != NULL  ||   /* If doing stringwidth... */
           ( render_none && details.width->wid_diffs ) ) {
        result = plotchar( &selector, DOSTRINGWIDTH, charCount,
                           pdf_notdef_cmap, &details, &advance, CHAR_NORMAL) ;
      }
      else if ( render_fill ) {
        /* If > 1 glyph to be shown in Tr mode of 'fill-and-then-stroke', setg needs
           doing to reset things after stroking the previous char. */
        if (render_stroke && charCount > 0)
          if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )
            return FALSE;

        result = plotchar(&selector, DOSHOW, charCount,
                          pdf_notdef_cmap, &details, &advance, CHAR_NORMAL) ;
      }

      if ( result && ( pSWParams == NULL ) && !render_none ) {
        if ( render_stroke || render_clip || (details.fd->font_style & FSTYLE_Bold) ) {
          STROKE_PARAMS params ;
          int32 dobold ;

          if ( init_charpath(render_stroke) ) {
            result = plotchar(&selector, DOCHARPATH, charCount,
                              pdf_notdef_cmap, &details, &advance, CHAR_NORMAL) ;

            /* Faking a Bold style for this font -
             * To mimic Adobe, we simply stroke the outline of the
             * character in the fill colour, using the 'magic' line width
             * (for that perfectly horrible effect). For the faux-bold stroke,
             * no linejoin is ideal, they can all cause quality issues at low
             * point-sizes and resolutions. But 'bevel' seems to be the least
             * objectionable, after studying output for all possible linejoins.
             * So force linestyle to bevel. Then miterlimit will not be
             * relevant, and linecap is not important as paths are closed.
             * Note that if we are supposed to stroking the outline, and
             * the current linewidth is greater than this magic value, the
             * emboldened outline will not be seen, as so there is no point
             * doing it!
             */
            dobold = ((details.fd->font_style & FSTYLE_Bold) &&
                      (!render_stroke ||
                       theLineWidth(theILineStyle(gstateptr)) < fauxwidth)) ;
            if ( result && dobold ) {
              set_gstate_stroke(& params, path, NULL, FALSE) ;
              theLineWidth(params.linestyle) = (USERVALUE)fauxwidth ;
              params.linestyle.linejoin = BEVEL_JOIN;
              result = (flush_vignette(VD_Default) &&
                        dostroke(& params, GSC_FILL, STROKE_NOT_VIGNETTE)) ;
            }
            if ( result && render_stroke ) {
              int32 flush = ( render_fill || ! dobold ) ;
              USERVALUE adjust = ixc->TextStrokeAdjust ;
              USERVALUE * linewidth = &params.linestyle.linewidth ;

              set_gstate_stroke(& params, path, NULL, FALSE) ;
              if (adjust != 0.0) { /* Adjust the stroke width [65751] */
                if (adjust < 0) {
                  *linewidth += adjust ;
                  if (*linewidth < 0.0)
                    *linewidth = 0.0 ;
                } else
                  *linewidth *= adjust ;
              }

              if ( flush )
                result = (flush_vignette(VD_Default) &&
                          dostroke(&params, GSC_STROKE, STROKE_NOT_VIGNETTE)) ;
              else
                result = dostroke(&params, GSC_STROKE, STROKE_NORMAL) ;
            }

            if ( result && !render_clip ) {
              /* Dummy new path op -> CP is ignored */
              result = (gs_newpath() && path_moveto(0.0, 0.0, MOVETO, path)) ;
            }
            result = end_charpath(result) ;
          } else
            result = FALSE ;
        }
      }
      parent_selector = old_parent ;
      if ( !result )
        return FALSE ;

      /* Do horizontal/vertical advance */
      if ( details.fd->font_type == FONT_TypeCID ) {
        MATRIX_TRANSFORM_DXY( w, w2[0], w, w2[0], & imc->textstate.TRM ) ;
        Tx += w ;
        Ty += w2[0] ;

        /* Undo earlier position vector if vertical wmode */
        if ( pdf_fontdetails->cmap_details->wmode ) {
          Tx += w2[1] ;
          Ty += w2[2] ;
        }
      } else {
        SYSTEMVALUE tw ;
        SYSTEMVALUE tx , ty ;

        /* Override character widths if necessary. */
        if ( details.width->wid_diffs ) {
          if ( widthcode >= details.width->wid_firstchar &&
               widthcode <= details.width->wid_lastchar ) {
            int32 idx ;
            idx = widthcode - details.width->wid_firstchar ;
            tw = object_numeric_value(&details.width->wid_diffs[idx]);
            MATRIX_TRANSFORM_DXY( tw, 0.0, tx, ty, & imc->textstate.TRM ) ;
            Tx += tx ;
            Ty += ty ;
          } else {
            tw = ( SYSTEMVALUE )details.width->wid_mwidth ;
            MATRIX_TRANSFORM_DXY( tw, 0.0, tx, ty, & imc->textstate.TRM ) ;
            Tx += tx ;
            Ty += ty ;
          }
        } else {
          Tx += advance.x ;
          Ty += advance.y ;
        }
      }

      /* Extra word spacing - also applies to "single-byte codes" in CID fonts */
      if ( useTw && theLen(selector.complete) == 1 &&
                    oString(selector.complete)[0] == 32 ) {
        if ( ! gotTw ) {
          gotTw = TRUE ;
          if (cmap_wmode == 1)
            MATRIX_TRANSFORM_DXY( 0.0, Tw, Twx, Twy, & imc->textstate.TCM ) ;
          else
            MATRIX_TRANSFORM_DXY( Tw, 0.0, Twx, Twy, & imc->textstate.TCM ) ;
        }
        Tx += Twx ;
        Ty += Twy ;
      }

      /* Extra character spacing */
      if ( useTc ) {
        if ( ! gotTc ) {
          gotTc = TRUE ;

          /* For vertical writing, use Tc for y's displacment, not x's */
          if (cmap_wmode == 1)
            MATRIX_TRANSFORM_DXY( 0.0, Tc, Tcx, Tcy, & imc->textstate.TCM ) ;
          else
            MATRIX_TRANSFORM_DXY( Tc, 0.0, Tcx, Tcy, & imc->textstate.TCM ) ;
        }
        Tx += Tcx ;
        Ty += Tcy ;
      }

      if ( pSWParams != NULL ) {
        /* For 'pdf_stringwidth', invoke the call-back function if there is one.*/
        if (pSWParams->CallBackFn != NULL) {
          if (!(*(pSWParams->CallBackFn))(
                       pSWParams->pState,         /* call-back function's state */
                       cmap_wmode ? Ty : Tx,      /* Current "position"         */
                       &selector ))               /* Glyph selector information */
            return FALSE;
        }
      }

    }  /* end of 'for' each glyph */
  } /* end 'while' */

  if ( pSWParams == NULL ) {
    imc->textstate.CP.x = Tx ;
    imc->textstate.CP.y = Ty ;

    if ( ! finishaddchardisplay(page, 1) )
      return FALSE ;
  } else {    /* for 'stringwidth()' return displacement */
    pSWParams->Tx = Tx;
    pSWParams->Ty = Ty;
  }

  return TRUE ;
}

/** pdf_stringwidth()
 * This function performs the effective equivalent of a Tj operation on a single
 * string except that nothing is drawn.  pdf_show() is called to determine the
 * string's width only.
 * The "width" is the difference between the current point at the start and
 * the current point after.  This depends on the current Tc setting, among
 * others, writing mode, font used (fontS if composite!), etc.
 */
Bool pdf_stringwidth( PDFCONTEXT *pdfc,
                      PDF_FONTDETAILS *pdf_fontdetails,
                      OBJECT *pString,
                      SYSTEMVALUE *pTx,    /* Returns string horiz. displacement   */
                      SYSTEMVALUE *pTy,    /* Returns string vertical displacement */
                      PDF_GLYPH_CALLBACK_FUNC *pCb,
                      void *pState )
{
  SYSTEMVALUE rx = 0.0;
  SYSTEMVALUE ry = 0.0;
  OMATRIX mtx;
  PDF_SW_PARAMS Params;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;


  /* Pseudo-"show":  i.e. just find the width */
  Params.CallBackFn = pCb;
  Params.pState = pState;
  Params.Tx = 0.0;
  Params.Ty = 0.0;

  if (!pdf_show( pdfc, pdf_fontdetails, pString, oType(*pString), &Params ))
    return FALSE;


  /* Translate the string width(s) through the inverse CTM to regain user-space */
  if (!matrix_inverse( &theIgsPageCTM(gstateptr), &mtx ))
    return FALSE;

  MATRIX_TRANSFORM_DXY( Params.Tx, Params.Ty, rx, ry, &mtx );


  /* Return string dimensions to the caller */
  *pTx = fabs(rx);
  *pTy = fabs(ry);

  return TRUE ;
}


/* Log stripped */
