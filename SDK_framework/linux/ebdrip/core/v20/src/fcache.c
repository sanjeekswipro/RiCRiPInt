/** \file
 * \ingroup fontcache
 *
 * $HopeName: SWv20!src:fcache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to manage the PostScript gstate font tracking and dispatching of
 * character drawing requests.
 */

#include <math.h> /* floor(), ceil() */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "swdevice.h"
#include "swstart.h"
#include "timing.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemset.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "swpdfin.h"
#include "swpdfout.h"
#include "fonts.h"
#include "fontcache.h"
#include "fontparam.h"
#include "scanconv.h"
#include "namedef_.h"

#include "often.h"

#include "pscontext.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "matrix.h"
#include "params.h"
#include "psvm.h"
#include "stacks.h"
#include "dicthash.h"
#include "ndisplay.h"
#include "graphics.h"
#include "gstack.h"
#include "pathops.h"
#include "gstate.h"
#include "swmemory.h"
#include "showops.h"
#include "devops.h"
#include "gu_path.h"
#include "routedev.h"
#include "control.h"
#include "plotops.h"
#include "display.h"
#include "dlstate.h"
#include "render.h"
#include "startup.h"
#include "system.h"
#include "adobe1.h"
#include "tt_font.h"
#include "fcache.h"
#include "miscops.h"
#include "utils.h"
#include "rlecache.h"
#include "pathcons.h"
#include "gu_ctm.h"
#include "dictops.h"
#include "stackops.h"
#include "clipops.h"
#include "gu_cons.h"
#include "upath.h"
#include "upcache.h"
#include "dl_bres.h"
#include "bresfill.h"
#include "tranState.h"

#include "idlom.h"

#include "trap.h"
#include "gschead.h"

#include "fontops.h"
#include "cidfont.h"
#include "t32font.h"
#include "pfin.h"
#include "formOps.h"

static Bool start_scan_char(corecontext_t *context,
                            charcontext_t *charcontext, LINELIST *currpt) ;
static Bool get_edef(FONTinfo *fontInfo, NAMECACHE *glyphname, int32 *glyphchar ) ;
static Bool get_cdef(FONTinfo *fontInfo, int32 glyphchar, NAMECACHE **glyphname ) ;
static void freethiscache(corecontext_t * context,
                          charcontext_t *charcontext, CHARCACHE *cptr) ;

/* --- Idlom internals --- */
static void idlom_char_init(LINELIST *currpt, int32 count, FONTinfo *fontInfo) ;

static Bool idlom_begin_cb( int32 glyphchar, OBJECT *glyphname,
                            FONTinfo *font,
                            int32 *idlomId, int32 *idlomCacheHit) ;
static Bool idlom_char_cb( int32 glyphchar, OBJECT *glyphname,
                           int32 idlomId, Bool *result ) ;
static Bool idlom_finish_dl(DL_STATE *page) ;

/** Used to defer a DL-add to after callback. */
static heldAddToDL IdlomDLSave;

/* --- Internal variables --- */
/** IdlomOldSwCache tracks whether plotchar had a cache hit before calling the
   routines to draw a character. If so, the character drawing was called in
   order to get an HDLT/PDFOut outline callback, and should not be
   re-cached. */
static CHARCACHE *IdlomOldSwCache;

/** When true, we must send character outlines to HDLT, even if HDLT is
disabled. This can happen when a character has already been sent to the
character target (thus we disable HDLT for the character), but the character
cannot be cached, meaning that it's sent directly to the display list in
char_draw(), and so it's outline is not made available to HDLT. */
static Bool hdltCharOutlineRequired = FALSE;

/** Bracket graphics stack for BuildChar and patch up the graphics frame to
   return charpath */
Bool bracket_plot(charcontext_t *charcontext, int32 *pgid)
{
  /* A gs_gpush GST_SETCHARDEVICE now clears the gstate path */
  if ( ! gs_gpush( GST_SETCHARDEVICE ))
    return FALSE ;

  *pgid = gstackptr->gId ;

  /* Reset pointers to patch charpath back into saved graphics frame. */
  if ( charcontext->showtype == DOCHARPATH ) {
    PATHINFO *lpath = &thePathInfo( *gstackptr ) ;
    PATHLIST *ppath, *cpath, *npath ;

    ppath = lpath->firstpath ;
    if ( ! ppath->next)
      thePathOf(*thecharpaths) = & lpath->firstpath ;
    else {
      cpath = ppath->next ;
      while (( npath = cpath->next) != NULL) {
        ppath = cpath ;
        cpath = npath ;
      }
      thePathOf(*thecharpaths) = & ppath->next ;
    }
    thePathInfoOf(*thecharpaths) = lpath ;
  }

  if ( ! gs_gpush( GST_GSAVE )) {
    ( void )gs_cleargstates( *pgid , GST_SETCHARDEVICE , NULL ) ;
    return FALSE ;
  }

  /* Note that we're inside a char building gframe. Various routines use this
     to search for the GST_SETCHARDEVICE gframe to find the lookup font
     matrices. */
  charcontext->bracketed = TRUE ;

  return TRUE ;
}

/** If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gframe and
   examine the fontinfo from that gstate; the user may have set a different
   font in the BuildChar procedure, and we would use the wrong caching
   information if we looked at the current font info. */
static inline FONTinfo *find_fontinfo(charcontext_t *charcontext, GSTATE *gs)
{
  if ( charcontext->bracketed ) {
    do {
      gs = gs->next ;
      HQASSERT(gs, "No GST_SETCHARDEVICE found") ;
    } while ( gs->gType != GST_SETCHARDEVICE ) ;
  }

  return &gs->theFONTinfo ;
}

/* plotcharglyph() first checks for whether either a SW cache miss or an IDLOM
 *   cache miss requires us to redraw the character, then does the IDLOM
 *   character-object stuff to see whether this instance of it is going on the
 *   display list, and finally puts it on the list if so.
 *
 * Last Modified:       26-Apr-95
 * Modification history:
 *      15-Dec-94 (N.Speciner); only save the CurrentPoint's coordinates for
 *              IDLOM when a show is actually being done. This fixes a bug in
 *              which a crash would ensue when doing a "stringwidth" operation
 *              without a current point.
 *      04-Jan-95 (N.Speciner); move assert on IdlomDLSave.tag into case where
 *              a show is actually being done. Prevents an assert when the
 *              IDLOM callback does a stringwidth, or other non-show operation.
 *      26-Apr-95 (N.Speciner); fix bug caused by delayed callbacks of char
 *              target in IDLOM. Consult FrameBegun value to figure out whether
 *              to call the character object callback -- if it has a value,
 *              then character callback is to be done (e.g., if NAME_Equivalent
 *              had been returned by the character target).
 */

/* Take some of the gross mess out of plotcharglyph in an attempt to make it
   readable again */
static void idlom_char_init(LINELIST *currpt, int32 charCount, FONTinfo *fontInfo)
{
  HQASSERT((IdlomDLSave.tag == IH_Nothing || IdlomDLSave.tag == IH_Pending),
           "IdlomDLSave not cleared since last usage?");

  IdlomDLSave.tag = IH_Pending;
  IdlomDLSave.x = theX(thePoint(*currpt));
  IdlomDLSave.y = theY(thePoint(*currpt));
  IdlomDLSave.xtrans = 0.0f;
  IdlomDLSave.ytrans = 0.0f;

  /* These are set up before the bracket_plot(), so they automatically get
     propagated into the character target, and are also available for the
     character callback itself. */
  gstateptr->theHDLTinfo.position[0] = theX(thePoint(*currpt));
  gstateptr->theHDLTinfo.position[1] = theY(thePoint(*currpt));
  gstateptr->theHDLTinfo.showCount = charCount ;
  gstateptr->theHDLTinfo.baseFontInfo = fontInfo ;
}

static Bool idlom_begin_cb( int32 glyphchar, OBJECT *glyphname,
                            FONTinfo *font,
                            int32 *idlomId, int32 *idlomCacheHit)
{
  *idlomCacheHit = IDLOM_MARKBEGINCHARACTER(glyphchar, glyphname, font, idlomId) ;
  if ( *idlomCacheHit == IB_PSErr )
    return FALSE ;

  return TRUE ;
}

/* Neaten the Return Codes from here: could just pass back the
   IDLOM_CHARACTER() result, but it's a bit of a bodge */
enum { RC_exit, RC_recache, RC_fallthru };

static Bool idlom_char_cb( int32 glyphchar, OBJECT *glyphname,
                           int32 idlomId, Bool *result)
{
  switch ( IDLOM_CHARACTER(idlomId, glyphname, glyphchar) ) {
  case NAME_false:              /* PS error in callbacks */
    *result = FALSE;
    return RC_exit;
  case NAME_Discard:            /* just pretending */
    *result = TRUE;             /* how to free unused nfill object?  (char. */
    return RC_exit;

  case NAME_Recache:
    idlom_unCacheChar( theCurrFid(*gstateptr->theHDLTinfo.baseFontInfo),
                       &theFontMatrix(*gstateptr->theHDLTinfo.baseFontInfo),
                       glyphname ) ;
    return RC_recache ; /* spin 'round again, getting the outline */

  default: /* only add, for now */
    return RC_fallthru ;
  }
}

static Bool idlom_finish_dl(DL_STATE *page)
{
  Bool result;

  switch (IdlomDLSave.tag) {
  case IH_AddChar:
    {
      int32 tx , ty;

      SC_C2D_INT(tx, IdlomDLSave.x + IdlomDLSave.args.addchar.xbear);
      SC_C2D_INT(ty, IdlomDLSave.y + IdlomDLSave.args.addchar.ybear);
      result = addchardisplay(page, IdlomDLSave.args.addchar.form, tx, ty);
      break;
    }
  case IH_AddFill:
    result = DEVICE_BRESSFILL(page, IdlomDLSave.args.addnbress.rule,
                              IdlomDLSave.args.addnbress.nfill);
    break;
  default:
    result = TRUE;
  }
  return result;
}


/* ----------------------------------------------------------------------------
   function:            set_font()         author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   Looks up the ( possibly ) new font in the font list.

---------------------------------------------------------------------------- */
Bool set_font(void)
{
  int32 temp, temp2 ;
  int32 fonttype, fonthandler;
  OBJECT *encd ;
  OBJECT *theo ;
  OBJECT *dict ;
  OBJECT *fid ;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;

  /* Extract the necessary font parameters */
  dict = &theMyFont(*fontInfo) ;
  if ( NULL == (fid = fast_extract_hash_name(dict, NAME_FID)) )
    return error_handler( INVALIDFONT ) ;

  theCurrFid(*fontInfo) = oFid(*fid) ;

  if ( NULL == (theo = fast_extract_hash_name(dict, NAME_FontType)) )
    return error_handler( INVALIDFONT ) ;

  if ( oType(*theo) != OINTEGER )
    return error_handler( INVALIDFONT ) ;
  fonthandler = fonttype = oInteger(*theo);
  if ( fonttype < 0 || fonttype > 255) {
    return error_handler( INVALIDFONT ) ;
  }

  theFontType(*fontInfo) = CAST_TO_UINT8( fonttype ) ;

  /* FontBBox is required for base fonts, but not composite. Also, if it
     contains all zeros, it doesn't give any useful information, so pretend
     it didn't exist. */
  if ( NULL != (theo = fast_extract_hash_name(dict, NAME_FontBBox)) ) {
    sbbox_t bbox ;

    if ( !object_get_bbox(theo, &bbox) )
      return FALSE ;

    if ( bbox.x1 == 0.0 &&
         bbox.y1 == 0.0 &&
         bbox.x2 == 0.0 &&
         bbox.y2 == 0.0 )
      theo = NULL ;
  }
  fontInfo->fontbbox = theo ;

  /* PFIN ownership */
  if ( fonttype == FONTTYPE_PFIN || fonttype == FONTTYPE_PFINCID ) {
    fonthandler = FONTTYPE_PFIN ;
  } else {
    if ((theLen(*fid) & PFIN_MASK) == pfin_cycle) {
      /* this font has already been vetted by PFIN */
      if (theLen(*fid) & PFIN_OWNED)
        fonthandler = FONTTYPE_PFIN ;
    } else {
      /* vet this font */
      theLen(*fid) = (uint16)((theLen(*fid) & !PFIN_FLAGS) | pfin_cycle) ;
      if (pfin_offer_font(fonttype, dict)) {
        theLen(*fid) |= PFIN_OWNED ;
        fonthandler = FONTTYPE_PFIN ;
      }
    }
  }

  /* Determine charstring type based on FontType. Some decisions may be
     deferred until known (e.g. CID Type 0 fonts), Type 1 where glyph
     replacement may happen. */
  fontInfo->fontfns = &font_invalid_fns ; /* No direct charstrings */
  switch ( fonthandler ) {
  case FONTTYPE_0:
    break ;
  case CIDFONTTYPE0C:
    fontInfo->fontfns = &font_cid0c_fns ;
    break ;
  case CIDFONTTYPE0:
    fontInfo->fontfns = &font_cid0_fns ;
    break ;
  case FONTTYPE_1:
    fontInfo->fontfns = &font_type1_ps_fns ;
    break ;
  case DLD1_CASE:
    fontInfo->fontfns = &font_dld1_fns ;
    break ;
    /* The following case is here for CFF fonts translated into PS; it
       accesses the CFF charstrings directly from a file. */
  case FONTTYPE_CFF:
    fontInfo->fontfns = &font_cff_fns ;
    break ;
  case FONTTYPE_3:
  case FONTTYPE_4:
    fontInfo->fontfns = &font_type3_fns ;
    break ;
  case CIDFONTTYPE1:
    fontInfo->fontfns = &font_cid1_fns ;
    break ;
  case FONTTYPE_TT:
    fontInfo->fontfns = &font_truetype_fns ;
    break ;
  case CIDFONTTYPE2:
    fontInfo->fontfns = &font_cid2_fns ;
    break ;
  case CIDFONTTYPE4:
    fontInfo->fontfns = &font_cid4_fns ;
    break ;
  case FONTTYPE_PFIN:
  case CIDFONTTYPEPFIN:
    fontInfo->fontfns = &font_pfin_fns ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  if ( !FONT_IS_CID(fonttype) ) {
    /* Extract the Encoding array. */
    encd = fast_extract_hash_name( dict , NAME_Encoding ) ;
    HQASSERT(encd, "No encoding array") ;
    switch ( oType(*encd) ) {
    case OARRAY :
    case OPACKEDARRAY :
      break ;
    default:
      return error_handler( INVALIDFONT ) ;
    }
    if ( !oCanRead(*encd) )
      if ( ! object_access_override(encd))
        return error_handler( INVALIDACCESS ) ;

    OCopy(theEncoding(*fontInfo), *encd);
  } else {
    /* No Encoding array. */
    theTags(theEncoding(*fontInfo)) = ONULL;
  }

  /* Extract the charstring dictionary. */
  theCharStrings(*fontInfo) = fast_extract_hash_name(dict, NAME_CharStrings);

  /* Extract the UniqueID if one. */
  theUniqueID(*fontInfo) = -1 ;
  if (( theo = fast_extract_hash_name( dict , NAME_UniqueID )) != NULL ) {
    if ( oType(*theo) != OINTEGER )
      return error_handler( INVALIDFONT ) ;
    theUniqueID(*fontInfo) = oInteger(*theo) ;
  }

  /* Only note CDevProc, Metrics and PaintType if they are used. */
  theMetrics(*fontInfo) = NULL ;
  theMetrics2(*fontInfo) = NULL ;
  theTags(theCDevProc(*fontInfo)) = ONULL ;
  theSubsVector(*fontInfo) = NULL;
  thePaintType(*fontInfo) = 0 ;
  theStrokeWidth(*fontInfo) = 0.0f ;

  switch ( fonttype ) {
  case DLD1_CASE:
  case FONTTYPE_1:
  case FONTTYPE_4:
  case FONTTYPE_TT:
  case CIDFONTTYPE0:
  case CIDFONTTYPE0C:
  case CIDFONTTYPE2:
  case FONTTYPE_PFIN:
  case CIDFONTTYPEPFIN:
  case FONTTYPE_CFF:
    /* Always set this to something useful; it may be used in generating
       a sub-font unique ID */
    theFDIndex(*fontInfo) = 0 ;

    /* Extract the metrics dictionary if one. */
    theMetrics(*fontInfo) = fast_extract_hash_name( dict , NAME_Metrics ) ;

    /*
     * Composite font extensions have a second metrics array and various other
     * fields
     */
    theMetrics2(*fontInfo) = fast_extract_hash_name( dict , NAME_Metrics2 ) ;

    /* optional CDevProc - a procedure which allows modification of metrics
       - see page 3 of composite extensions - applies to any font not just a
       type 0 (composite) font */
    if (( theo = fast_extract_hash_name( dict , NAME_CDevProc )) != NULL )
      OCopy(theCDevProc(*fontInfo), *theo) ;

    /* Extract the PaintType. */
    if (( theo = fast_extract_hash_name( dict , NAME_PaintType )) != NULL ) {
      switch ( oInteger(*theo) ) {
      case 2 :
        ++thePaintType(*fontInfo) ;
      case 1 :
        ++thePaintType(*fontInfo) ;
        theStrokeWidth(*fontInfo) = 1.0f ;
        if ( NULL != (theo = fast_extract_hash_name(dict, NAME_StrokeWidth)) ) {
          if ( !object_get_real(theo, &theStrokeWidth(*fontInfo)) )
            return FALSE ;
        }
        break ;
      case 3 :
      default:
        break ;
      }
    }

    break ;

  case FONTTYPE_0:
    /* the remaining fields apply to font type 0 (composite) fonts only */
    if ( ! theISaveCompFonts( workingsave ))
      return error_handler( INVALIDFONT ) ;
    encd = fast_extract_hash_name( dict , NAME_FDepVector ) ;
    HQASSERT(encd, "No FDepVector") ;
    switch ( oType(*encd) ) {
    case OARRAY :
    case OPACKEDARRAY :
      break ;
    default:
      return error_handler( INVALIDFONT ) ;
    }
    if ( ! oCanRead(*encd) )
      if ( ! object_access_override(encd))
        return error_handler( INVALIDACCESS ) ;
    OCopy(theFDepVector(*fontInfo), *encd) ;

    encd = fast_extract_hash_name( dict , NAME_PrefEnc ) ;
    if ( encd ) {
      switch ( oType(*encd) ) {
      case OARRAY :
      case OPACKEDARRAY :
        break ;
      default:
        return error_handler( INVALIDFONT ) ;
      }
      if ( !oCanRead(*encd) )
        if ( ! object_access_override(encd))
          return error_handler( INVALIDACCESS ) ;
      OCopy(thePrefEnc(*fontInfo), *encd) ;
    } else {
      theTags( thePrefEnc(*fontInfo)) = ONULL ;
    }

    if ( NULL == (theo = fast_extract_hash_name(dict, NAME_FMapType)) )
      return error_handler( INVALIDFONT ) ;

    if ( oType(*theo) != OINTEGER )
      return error_handler( INVALIDFONT ) ;
    temp = oInteger(*theo) ;
    if ( temp < 2 || temp > 9 )
      return error_handler( INVALIDFONT ) ;
    theFMapType(*fontInfo) = CAST_TO_UINT8( temp ) ;

    if ( temp == MAP_ESC || temp == MAP_DESC ) {
      /* escape mapping requires escape character - definefont adds it
         if not present for this type of font */
      if ( NULL == (theo = fast_extract_hash_name(dict, NAME_EscChar)) )
        return error_handler( INVALIDFONT ) ;
      if ( oType(*theo) != OINTEGER )
        return error_handler( INVALIDFONT ) ;
      temp = oInteger(*theo) ;
      if ( temp < 0 || temp > 255 )
        return error_handler( INVALIDFONT ) ;
      theEscChar(*fontInfo) = CAST_TO_UINT8( temp ) ;
    }
    else if ( temp == MAP_SUBS ) {
      /* want a subs vector: get an integer array to put it in */
      int32 bytes , length ;
      uint8 *clist ;
      uint32 *ilist ;

      theo = fast_extract_hash_name( dict , NAME_SubsVector ) ;
      if ( oType(*theo) != OSTRING || theLen(*theo) == 0)
        return error_handler( INVALIDFONT );
      if ( ! oCanRead(*theo) )
        if ( ! object_access_override(theo) )
          return error_handler( INVALIDACCESS ) ;

      clist = oString(*theo) ;
      bytes = *clist + 1 ;
      if ( bytes > 4 ) /* Unpacked values must fit into int32 */
        return error_handler ( INVALIDFONT ) ;
      theSubsBytes(*fontInfo) = CAST_TO_UINT8( bytes ) ;
      ++clist ;

      length = (theLen(*theo) - 1) / bytes ;
      if ( length * bytes != theLen(*theo) - 1 )
        return error_handler ( INVALIDFONT ) ;

      /* and construct the integer array packed into the subs vector;
       * this gets copied into a FONTINFOLIST structure below and is
       * then freed once the string is completely shown by the freeing
       * of the FONTINFOLIST (the pointer in gstate is then invalid of
       * course) */
      theSubsCount(*fontInfo) = CAST_TO_UINT16(length) ;
      if (( ilist = ( uint32 * )mm_alloc_with_header(mm_pool_temp,
                                                     (mm_size_t)
                                                       ( length *
                                                        sizeof(uint32)),
                                                     MM_ALLOC_CLASS_FONT_SUBS))
          == NULL )
        return error_handler( VMERROR );
      theSubsVector(*fontInfo) = ilist ;
      while ( length > 0 ) {
        int32 ii;
        *ilist = 0 ;
        for ( ii = 0 ; ii < bytes ; ii++ ) {
          *ilist = (*ilist) << 8 ;
          *ilist |= *clist++ ;
        }
        ++ilist ;
        --length ;
      }
    }
    else if ( temp == MAP_SHIFT ) {
      /* shift mapping requires two numbers, shift in and shift out */
      if ( NULL == (theo = fast_extract_hash_name(dict, NAME_ShiftIn)) )
        return error_handler( INVALIDFONT ) ;
      if ( oType(*theo) != OINTEGER )
        return error_handler( INVALIDFONT ) ;
      temp = oInteger(*theo) ;
      if ( temp < 0 || temp > 255 )
        return error_handler( INVALIDFONT ) ;
      theShiftIn(*fontInfo) = CAST_TO_UINT8( temp ) ;

      if ( NULL == (theo = fast_extract_hash_name(dict, NAME_ShiftOut)) )
        return error_handler( INVALIDFONT ) ;
      if ( oType(*theo) != OINTEGER )
        return error_handler( INVALIDFONT ) ;
      temp = oInteger(*theo) ;
      if ( temp < 0 || temp > 255 )
        return error_handler( INVALIDFONT ) ;
      theShiftOut(*fontInfo) = CAST_TO_UINT8( temp ) ;
    }
    else if ( temp == MAP_CMAP ) {
      /* Take a pointer to the CMap for now.
         @@ Probably want to optimise this to a pointer to the CodeMap entry in
         the CMap dict eventually */
      if ( NULL == (theo = fast_extract_hash_name(dict, NAME_CMap)) )
        return error_handler( INVALIDFONT );
      if ( oType(*theo) != ODICTIONARY )
        return error_handler( INVALIDFONT ) ;
      theCMap(*fontInfo) = theo;
    }

    break ;
  }

  /* WMode indicates which metrics to use - any font. Check always, but only
     set if not already set; setfont invalidates it, as does starting at the
     top of a composite font. Otherwise we inherit it (i.e. leave
     it unchanged.

     PLRM3 is at odds with TN5014 about fonts using CMaps. TN5014 (8 Oct
     1996, p. 48) implies that the CMap's WMode entry overrides the using
     font's WMode. PLRM3 only mentions this in the context of creating a font
     with composefont (PLRM3 p. 546). The exact wording in TN5014 is ambiguous:

     "WMode in the CMap overrides any WMode in any font or CIDFont referred
      to by the CMap file."

     Since fonts refer to CMaps, and not the other way round (except
     indirectly through the usefont declaration), this could be construed to
     be meaningless. I have implemented it such that the WMode in the Type 0
     font takes precedence over the WMode in the CMap. Our composefont
     implementation copies the WMode from the CMap anyway.

     VMode is an extension used by PCL5 fonts since these require more than the
     two modes that WMode can provide.  The WMode still indicates whether we
     are regarding this as a horizontal or vertical type mode, whilst the VMode
     provides extra mode type information.  We store the combination of WMode
     and VMode in the gstate, such that bit0 of the combination is the WMode,
     and the remaining bits represent the VMode.
  */

  theo = fast_extract_hash_name(dict, NAME_WMode);
  if ( theo == NULL && fonttype == FONTTYPE_0 &&
       theFMapType(*fontInfo) == MAP_CMAP )
    theo = fast_extract_hash_name(theCMap(*fontInfo), NAME_WMode);
  if ( theo == NULL ) {
    if ( theWModeNeeded(*fontInfo) ) {
      theWMode(*fontInfo) = 0 ;
      theWModeNeeded(*fontInfo) = FALSE ;
    }
  }
  else {
    if ( oType(*theo) != OINTEGER )
      return error_handler( INVALIDFONT ) ;
    temp = oInteger(*theo) ;
    if ( temp != 0 && temp != 1 )
      return error_handler( INVALIDFONT ) ;

    theo = fast_extract_hash_name(dict, NAME_VMode);
    if (theo != NULL) {
      if ( oType(*theo) != OINTEGER )
        return error_handler( INVALIDFONT ) ;
      temp2 = oInteger(*theo) ;
      if (temp2 < 0 || temp2 > 127)
        return error_handler( INVALIDFONT) ;
      temp = (temp2 << 1) | temp ;
    }

    if ( theWModeNeeded(*fontInfo)) {
      theWMode(*fontInfo) = CAST_TO_UINT8( temp ) ;
      theWModeNeeded(*fontInfo) = FALSE ;
    }
  }

  /* Search for a font entry in the cache, looking by FID first, and then
     trying to match all details. */
  if ( fontcache_lookup_fid(fontInfo))
    return TRUE ;

  return fontcache_lookup_font(fontInfo);
}

/* ----------------------------------------------------------------------------
   function:            set_matrix()       author:              Andrew Cave
   creation date:       24-May-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   Sets the current matrix combination.

---------------------------------------------------------------------------- */
static Bool get_fontmatrix(OMATRIX *mptr, OMATRIX *sptr)
{
  OMATRIX m1 ;
  OBJECT *dict, *theo ;

/*  Extract the matrix list from the FontMatrix in the currentfont - if needed. */
  dict = &theMyFont(theFontInfo(*gstateptr)) ;
  if ( (NULL == (theo = fast_extract_hash_name(dict, NAME_FontMatrix))) )
    return error_handler( INVALIDFONT ) ;
  if ( ! is_matrix( theo , mptr ))
    return FALSE ;

  /* ScaleMatrix gives the scaling of the font relative to the original font.
     It is used with FontCompositeMatrix to derive default userspace to font
     space conversions for accurate rendering and standard CDevProcs. */
  if ( NULL != (theo = fast_extract_hash_name(dict, NAME_ScaleMatrix)) ) {
    if ( ! is_matrix(theo, sptr) )
      return FALSE ;
  } else {
    MATRIX_COPY(sptr, &identity_matrix) ;
  }

  MATRIX_COPY( & m1 , & theFontATMTRM( *gstateptr )) ;
  m1.matrix[ 2 ][ 0 ] = 0.0 ;
  m1.matrix[ 2 ][ 1 ] = 0.0 ;

  matrix_mult( mptr , & m1 , mptr ) ;
  matrix_mult( sptr , & m1 , sptr ) ;

  return TRUE ;
}

Bool set_matrix(void)
{
  OMATRIX m2 ;

  /* keep the one unconcatenated with ctm for replacement while in
     BuildChar if necessary */
  if ( ! get_fontmatrix(&theFontCompositeMatrix(theFontInfo(*gstateptr)),
                        &theFontInfo(*gstateptr).scalematrix) )
    return FALSE ;

/*  Multiply this by the CTM to get the transformed character matrix. */
  MATRIX_COPY( & m2 , & thegsPageCTM( *gstateptr )) ;
  m2.matrix[ 2 ][ 0 ] = 0.0 ;
  m2.matrix[ 2 ][ 1 ] = 0.0 ;

  matrix_mult(&theFontCompositeMatrix(theFontInfo(*gstateptr)),
              &m2,
              &theFontMatrix(theFontInfo(*gstateptr))) ;

  /* For Type 1 fonts, we snap the font matrix elements to 1/100 pixel
     over a standard 1000 unit em-square. This increases the number of
     matrix hits (and hence the performance) at the expense of some
     accuracy. (The accuracy loss is actually a non-issue because we're
     arbitrarily mapping the font outline to a discrete grid. We can
     decide to do this any way we want, this change just removes some of
     the possible mappings of outline to raster.) We need to do this
     before the character or the metrics are calculated, so that we get
     consistent results for nearly-matching fonts. We can only do this
     for Type 1 fonts, because we don't know the design unit size for
     Type 3 fonts. */
  /* [andy: February 12, 1996] Now use the matrix_clean routine.
  */
  switch ( theFontType(theFontInfo( *gstateptr )) ) {
  case DLD1_CASE : /* DLD1 fonts */
  case FONTTYPE_1 : /* Adobe Type 1 Font */
  case FONTTYPE_CFF : /* Adobe Type 2 Font */
  case CIDFONTTYPE0:
  case CIDFONTTYPE0C:
    matrix_snap( & theFontMatrix( theFontInfo( *gstateptr )), 100000 ) ;
  }

  return TRUE ;
}

/* Macro to save adding very ugly repetitive asserts to the cases below that */
/* deal with non blank forms */
#define ASSERT_NONBLANK_FORM(form, text) \
  HQASSERT( theFormH(*(form)) != 0 && theFormW(*(form)) != 0 && \
            theFormL(*(form)) != 0 && theFormS(*(form)) != 0, \
           (text) )

Bool fillchardisplay(DL_STATE *page, FORM *tempf, int32 sx, int32 sy)
{
  CHARCACHE *thechar ;
  charcontext_t *charcontext = char_current_context() ;
  render_blit_t *rb ;
  const surface_t *surface ;

  UNUSED_PARAM(DL_STATE*, page);
  HQASSERT(charcontext, "No character context") ;

  rb = charcontext->rb ;
  HQASSERT(RENDER_BLIT_CONSISTENT(rb),
           "Character context render state is not self-consistent") ;

  surface = rb->p_ri->surface ;
  HQASSERT(surface, "No surface to render character") ;

  SET_BLITS(rb->blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  if ( clipmapid >= 0 )
    rb->clipmode = BLT_CLP_COMPLEX ;
  else
    rb->clipmode = BLT_CLP_RECT ;

 TRYAGAIN:
  switch ( theFormT(*tempf) ) {
  case FORMTYPE_CHARCACHE:
    thechar = ( CHARCACHE * )tempf ;
    tempf = theForm(*thechar) ;
    goto TRYAGAIN ;
  case FORMTYPE_CACHERLE1:
  case FORMTYPE_CACHERLE2:
  case FORMTYPE_CACHERLE3:
  case FORMTYPE_CACHERLE4:
  case FORMTYPE_CACHERLE5:
  case FORMTYPE_CACHERLE6:
  case FORMTYPE_CACHERLE7:
  case FORMTYPE_CACHERLE8:
    ASSERT_NONBLANK_FORM(tempf, "fillchardisplay: zeros in CACHERLEX case\n");
    rlechar(rb, tempf , sx , sy ) ;
    break ;
  case FORMTYPE_CACHEBITMAP:
    ASSERT_NONBLANK_FORM(tempf, "fillchardisplay: zeros in CACHEBITMAP case\n");
    DO_CHAR(rb, tempf, sx, sy) ;
    break ;
  case FORMTYPE_BLANK:  /* empty form */
    HQASSERT( theFormS(*tempf) == 0,
             "Blank form has size != 0 in fillchardisplay") ;
    HQASSERT( theFormA(*tempf) == NULL,
             "Blank form isn't empty in fillchardisplay") ;
    HQASSERT( theFormH(*tempf) == 0 && theFormW(*tempf) == 0,
             "fillchardisplay: Blank form must have zero w & h");
    HQASSERT( theFormL(*tempf),
             "fillchardisplay: Blank form must have zero lbyte");
    break ;
  default:
    HQFAIL("Invalid form type in fillchardisplay") ;
  }
  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            bitblt_char(..)    author:              Andrew Cave
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure does a fast bitblt of the cached character onto the
   retained bitmap.

---------------------------------------------------------------------------- */
static Bool bitblt_char(corecontext_t *context, CHARCACHE *cptr,
                        LINELIST *currpt, CHAR_OPTIONS options)
{
  DL_STATE *page = context->page;
  int32 i ;
  int32 sx , sy ;
  FORM *tempf ;

  tempf = theForm(*cptr) ;

  /* If the formtype is blank, don't bother putting on the DL */
  if (theFormT(*tempf) == FORMTYPE_BLANK) {
    /* \todo this appears to be required
     * as a setg may have been done with some other gstate or
     * colorType while creating the char for the cache.  Using the
     * rather ugly CHAR_SETG_BLANK to prevent this happening when
     * not in a text loop as this never would have done a setg
     * previously in this situation.
     * todo Why did it not previously need a setg here?
     */
    if ( (options & CHAR_SETG_BLANK)
        && !(options & CHAR_NO_SETG))
      if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )
        return FALSE ;
    return TRUE;
  }

#if 0
  if ( cptr->pageno != page->eraseno)
    cptr->usagecnt = 0 ;
#endif
  cptr->usagecnt++ ;

  /* Mark the character (in the cache) that used in current page. This is a
     bit inefficient if the character will be suppressed for DEVICE_NULL or
     DEVICE_PAGESUPPRESS, but the compression to RLE should probably come
     before DEVICE_SETG to avoid being confused by low memory actions
     there. */
  if ( cptr->pageno < page->eraseno)
    cptr->baseno = page->eraseno ;
  cptr->pageno = page->eraseno ;

  if ( cptr->baseno == page->eraseno) {
    if ( !rendering_prefers_bitmaps(page) ) {
      if ( theFormT(*tempf) == FORMTYPE_CACHEBITMAP && cptr->rlesize >= 0 ) {
        /* We have never tried to RLE this character if rlesize is zero,
           so we must try it and get its size */
        if ( cptr->rlesize == 0 ) {
          ( void )form_to_rle( cptr , 0 ) ;
          tempf = theForm(*cptr) ;
        }

        /* If not rle'd this char, then maybe do so if ForceFontCompress
           allows us to for this useage of char on DL */
        if ( theFormT(*tempf) == FORMTYPE_CACHEBITMAP && cptr->rlesize > 0 ) {
          for ( i = 0 ; i < MAX_FORCEFONTCOMPRESS ; ++i ) {
            if ( cptr->usagecnt <= (1 << i) ) {
              if ( cptr->usagecnt == (1 << i) ) {
                if ( cptr->rlesize < theFormS(*tempf) * context->userparams->ForceFontCompress[ i ] / 100 ) {
                  ( void )form_to_rle( cptr , cptr->rlesize ) ;
                }
              }
              break ;
            }
          }
        }
      }
    }
  }

  if (! (options & CHAR_NO_SETG) ) {
    if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )
      return FALSE ;
  }
  if ( degenerateClipping )
    return TRUE ;

  HQASSERT( currpt != NULL, "No currentpoint in bitblt_char" ) ;

  /* Rasterop result to character destination. */
  SC_C2D_INT(sx, theX(thePoint(*currpt)) + theXBearing(*cptr)) ;
  SC_C2D_INT(sy, theY(thePoint(*currpt)) + theYBearing(*cptr)) ;

  tempf = ( FORM * )cptr ;      /* Chars on DL have extra level of indirection so we can RLE them */
  if ( isHDLTEnabled( *gstateptr ) && device_current_char == addchardisplay) {
    IdlomDLSave.tag = IH_AddChar;
    IdlomDLSave.args.addchar.form = tempf;
    IdlomDLSave.args.addchar.xbear = theXBearing(*cptr);
    IdlomDLSave.args.addchar.ybear = theYBearing(*cptr);
    return TRUE;
  }
  else {
    if (options & CHAR_INVERSE) {
      if (! cptr->inverse_generated) {
        /* An inverse is required but we don't have one yet; generate it now. */
        Bool empty;
        cptr->inverse_generated = TRUE;
        cptr->inverse = formInvert(cptr->thebmapForm, &empty);
        if (cptr->inverse == NULL && ! empty)
          return FALSE;
      }
      if (cptr->inverse != NULL) {
        return DEVICE_DOCHAR(page, cptr->inverse->form,
                             sx + cptr->inverse->x, sy + cptr->inverse->y);
      }
    }
    else {
      if (! DEVICE_DOCHAR(page, tempf, sx, sy))
        return FALSE;
    }
  }
  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            start_scan_char(..) author:              Andrew Cave
   creation date:       07-Oct-1987         last modification:   ##-###-####
   arguments:           none .
   description:

   This procedure does a normal scan conversion of the filled/outlined/stroked
   font. The scan conversion/outlining/stroking is bracketed  by  a  gsave and
   a grestore.

---------------------------------------------------------------------------- */
static Bool start_scan_char(corecontext_t *context,
                            charcontext_t *charcontext, LINELIST *currpt )
{
  ps_context_t *pscontext = context->pscontext ;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  Bool result ;
  int32 i, font_type1or6 ;
  Bool vmmode;
  OBJECT *proc , *thefontmatrix ;
  OMATRIX m1 ;
  OBJECT savematrix[6];

  HQASSERT(charcontext != NULL, "Not in a character context") ;

  /* stroke adjustment is off during BuildChar/BuildGlyph */
  if ( charcontext->modtype != DOCHARPATH )
    thegsDeviceStrokeAdjust(*gstateptr) = FALSE;

  {
    Bool check ;
    uint8 gotmatrix = gotFontMatrix(*fontInfo) ;
    MATRIXCACHE *lmatrix = theLookupMatrix(*fontInfo) ;

    /* Set CTM - default CTM set in setcachedevice(2) */
    MATRIX_COPY( & m1, & theFontMatrix(*fontInfo)) ;
    if ( charcontext->modtype == DOSTRINGWIDTH ) {
      m1.matrix[ 2 ][ 0 ] = 0.0 ;
      m1.matrix[ 2 ][ 1 ] = 0.0 ;
      if ( ! nulldevice_(pscontext))
        return FALSE ;
    }
    else {
      HQASSERT( currpt != NULL, "No currentpoint in start_scan_char" ) ;

      m1.matrix[ 2 ][ 0 ] += theX(thePoint(*currpt)) ;
      m1.matrix[ 2 ][ 1 ] += theY(thePoint(*currpt)) ;
    }
    MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &m1) ;
    check = gs_setctm(&m1, FALSE) ;
    HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;

    /* The setctm above will clear the lookup matrix and gotmatrix flags. We
       want to retain the flags so that set_matrix is not called again for
       Type 4 SEAC characters. */
    gotFontMatrix(*fontInfo) = gotmatrix ;
    theLookupMatrix(*fontInfo) = lmatrix ;
  }

  /* Don't need a newpath, because this function is only ever called
   * after bracket_plot, which clears the gstate path in the gs_gpush
   * GST_SETCHARDEVICE
   */
  HQASSERT(CurrentPoint == NULL,
           "gstate path not empty in start_scan_char") ;

  /* ......................................................................... */

  /* It shouldn't be anything other than type 1, 2, 3, 4, 10, 42, or DLD */
  switch ( theFontType(*fontInfo) ) {
    int32 glyphchar ;
  case FONTTYPE_3:
  case FONTTYPE_4:
  case CIDFONTTYPE1:
    font_type1or6 = FALSE;

    /* look for BuildGlyph routine */
    if ( (proc = fast_extract_hash_name(&theMyFont(*fontInfo) ,
                                        NAME_BuildGlyph)) != NULL ) {
      /* Setup the glyph name on the operand stack, unless doing PDF stream. */
      if ( oType(*proc) != OFILE ) {
        if ( ! push2(&theMyFont(*fontInfo),
                     &charcontext->glyphname, &operandstack) )
          return FALSE ;
      }
    }
    else {
      glyphchar = charcontext->glyphchar ;

      /* look for the BuildChar routine */
      if ( NULL == (proc = fast_extract_hash_name(&theMyFont(*fontInfo),
                                                  NAME_BuildChar)) )
        return error_handler( INVALIDFONT ) ;

      /* Setup the character index on the operand stack */
      if ( glyphchar == NO_CHARCODE ) {
        /* This is what Adobe do; if the glyphname couldn't be found in the
           Encoding vector then find the first /.notdef character (which
           includes ONULL!) from the end of the array. */
        if ( ! get_edef(fontInfo, system_names + NAME_notdef, &glyphchar) )
          return error_handler( INVALIDFONT ) ;
      }
      if ( ! push(&theMyFont(*fontInfo), &operandstack) ||
           ! stack_push_integer(glyphchar, &operandstack))
        return FALSE ;
    }
    break ;

  case FONTTYPE_1:
  case FONTTYPE_CFF:
  case DLD1_CASE:
  case FONTTYPE_TT:
    glyphchar = charcontext->glyphchar ;
    font_type1or6 = TRUE ;
    if ( ! begininternal( & systemdict ) ||
         ! begininternal( & theMyFont(*fontInfo)) )
      return FALSE ;
    if ( glyphchar == NO_CHARCODE ) {
      /* For glyphshow push the character name. */
      if ( ! push(&charcontext->glyphname , & operandstack ))
        return FALSE ;
    }
    else {
      /* For show (and variants) push the character index. */
      if ( ! stack_push_integer(glyphchar, &operandstack) )
        return FALSE ;
    }
    proc = &charcontext->definition ;
    break ;

  default:
    return error_handler( INVALIDFONT ) ;
  }

  switch ( oType(*proc) ) {
  case OFILE : /* Only PDF streams. */
    break ;
  case OARRAY :
  case OPACKEDARRAY :
    if ( ! oCanExec(*proc) )
      return error_handler( INVALIDFONT ) ;
    break ;
  default:
    return error_handler( INVALIDFONT ) ;
  }

  /* execute the BuildChar / BuildGlyph procedure */
  if ( oType(*proc) != OFILE )
    if ( ! push( proc , & executionstack ))
      return FALSE ;

  /* modify the matrix to be that of the whole font tree while
     executing the BuildChar/Glyph (when this
     is not a composite font, the result is the same as before,
     though the values may be reals rather than integers).
         So remember the old one first.
   */
  thefontmatrix = fast_extract_hash_name(&theMyFont(*fontInfo), NAME_FontMatrix);
  HQASSERT(thefontmatrix != NULL, "font matrix is null before BuildChar");
  HQASSERT(oType(*thefontmatrix) == OARRAY ||
           oType(*thefontmatrix) == OPACKEDARRAY,
           "font matrix is not an array before BuildChar");
  thefontmatrix = oArray(*thefontmatrix);
  for (i = 0; i < 6; i++) {
    HQASSERT(oType(thefontmatrix[i]) == OINTEGER ||
             oType(thefontmatrix[i]) == OREAL,
             "font matrix element isnt a number before BuildChar");
    Copy(object_slot_notvm(&savematrix[i]), &thefontmatrix[i]);
    object_store_numeric(&thefontmatrix[i],
                         theFontCompositeMatrix(*fontInfo).matrix[ i >> 1 ][ i & 1 ]) ;
  }

  /* Ensure allocation mode compatible with font dictionary */
  vmmode = context->glallocmode || oGlobalValue(theMyFont(*fontInfo));
  vmmode = setglallocmode(context, vmmode);

  if ( oType(*proc) != OFILE ) {
    result = interpreter(1, NULL) ;
  }
  else {
    result = pdf_exec_stream( proc , PDF_STREAMTYPE_CHARPROC ) ;
  }

  /* put the matrix back as it was */
  for (i = 0; i < 6; i++) {
    OCopy(thefontmatrix[i], savematrix[i]);
  }

  setglallocmode(context, vmmode ) ;

  if ( font_type1or6 )
    if ( !end_(pscontext) || !end_(pscontext))    /* pop two dicts here! */
      return FALSE ;

  if ( ! result )
    return FALSE ;

  if ( charcontext->cachelevel == CacheLevel_Cached ) {
    CHARCACHE *cptr = charcontext->cptr ;
    FORM *tform = theForm(*cptr) ;

    if ( theFormT(*tform) == FORMTYPE_CACHEBITMAP ||
         theFormT(*tform) == FORMTYPE_CACHEBITMAPTORLE ) {
      /* See if the content of the form is blank. */
      register blit_t *wrdp = theFormA(*tform) ;
      /* cnt is the number of blit_t in the form data */
      register int32 cnt = theFormS(*tform) >> BLIT_SHIFT_BYTES ;

      do {
        EMPTY_STATEMENT() ;
      } while ( *wrdp++ == 0 && --cnt != 0 ) ;

      /* if data is all 0x0, then the form is blank! */
      if ( cnt == 0 ) {
        context->fontsparams->CurFontCache -= theFormS(*tform) ;
        destroy_Form( tform ) ;
        if ( NULL == (theForm(*cptr) = MAKE_BLANK_FORM()) )
          return FALSE ;
      } else if ( theFormT(*tform) == FORMTYPE_CACHEBITMAPTORLE ) {
        ( void )form_to_rle(cptr, 0) ;
      }
    }
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            setcharwidth_()    author:              Andrew Cave
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 213.

---------------------------------------------------------------------------- */
Bool setcharwidth_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_setcharwidth( & operandstack ) ;
}

Bool gs_setcharwidth( STACK *stack )
{
  corecontext_t *context = get_core_context_interp() ;
  SYSTEMVALUE args[ 6 ] ;
  Bool check ;
  charcontext_t *charcontext = context->charcontext ;

  /* Disallow setcharwidth if not in BuildChar, or if done already */
  if ( charcontext == NULL ||
       !charcontext->buildthischar ||
       charcontext->cachelevel != CacheLevel_Unset )
    return error_handler(UNDEFINED) ;

  charcontext->cachelevel = CacheLevel_Uncached ;

  args[2] = args[3] = args[4] = args[5] = 0.0 ; /* No cache box */

  if ( !stack_get_numeric(stack, args, 2) )
    return FALSE ;

  MATRIX_TRANSFORM_DXY( args[ 0 ], args[ 1 ],
                        charcontext->xwidth,
                        charcontext->ywidth,
                        & thegsPageCTM( *gstateptr )) ;

  npop( 2 , stack ) ;

  MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &thegsPageCTM(*gstateptr)) ;
  check = gs_setctm(&thegsDeviceCTM(*gstateptr), FALSE) ;
  HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;

  /* We are not translating the character's coordinates with respect to its
     caller (we didn't change the translation entries of the matrix), so do
     not update the HDLT translation. The IDLOM_MARKBEGINCHARACTER call
     already updated the HDLT offset, which is the position we want to
     report the results relative to. */

  if (IDLOM_DOBEGINCHARACTER(NAME_setcharwidth) == IB_PSErr)
    return FALSE;

  if ( pdfout_enabled() &&
       !pdfout_beginchar(&context->pdfout_h, args, FALSE /* not cached */) )
    return FALSE ;

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            cachestatus_()     author:              Andrew Cave
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 126.

---------------------------------------------------------------------------- */
Bool cachestatus_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  FONTSPARAMS *fontparams = context->fontsparams ;
  USERPARAMS *userparams = context->userparams ;

  return (stack_push_integer(fontparams->CurFontCache, &operandstack) &&
          stack_push_integer(fontparams->MaxFontCache, &operandstack) &&
          stack_push_integer(fontparams->CurCacheMatrix, &operandstack) &&
          stack_push_integer(fontparams->MaxCacheMatrix, &operandstack) &&
          stack_push_integer(fontparams->CurCacheChars, &operandstack) &&
          stack_push_integer(fontparams->MaxCacheChars, &operandstack) &&
          stack_push_integer(userparams->MaxFontItem, &operandstack)) ;
}

/* ----------------------------------------------------------------------------
   function:            setcacheparams_()  author:              Andrew Cave
   creation date:       22-Nov-1988        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page ???.

---------------------------------------------------------------------------- */
Bool setcacheparams_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  FONTSPARAMS *fontparams = context->fontsparams ;
  USERPARAMS *userparams = context->userparams ;
  int32 args[ 2 ] ;
  int32 lower , upper , maximum ;
  OBJECT * theo ;
  register int32 num ;

/*
  Get number of objects between top of stack and mark.
  If this number is negative, then no mark present.
*/
  if (( num = num_to_mark()) < 0 )
    return error_handler( UNMATCHEDMARK ) ;

  lower = LOWER_CACHE ;
  upper = UPPER_CACHE ;
  maximum = fontparams->MaxFontCache ; /* default is to leave it alone */

  switch ( num ) {
  case 0:
    break ;
  case 1:
    if ( ! stack_get_integers(&operandstack, args, 1) )
      return FALSE ;
    upper = args[ 0 ] ;
    break ;
  case 2:
    if ( ! stack_get_integers(&operandstack, args , 2) )
      return FALSE ;
    lower = args[ 0 ] ;
    upper = args[ 1 ] ;
    break ;
  default:
    if ( ! stack_get_integers(&operandstack, args , 2) )
      return FALSE ;
    lower = args[ 0 ] ;
    upper = args[ 1 ] ;
    theo = stackindex ( 2 , & operandstack ) ;
    /* the third parameter may have become real due to having started very large and
       then being manipulated in some way: Genoa test suite multiplies it by two */
    switch ( oType(*theo) ) {
      SYSTEMVALUE real;
    case OINTEGER:
      maximum = oInteger(*theo) ;
      break;
    case OREAL:
      real = oReal(*theo) ;
      real = (real < 0.0) ? 0.0 : real ;
      maximum = (real < BIGGEST_INTEGER) ? (int32)real : (int32)(BIGGEST_INTEGER - 1) ;
      break;
    default:
      return error_handler( TYPECHECK ) ;
    }
    break ;
  }
  while ( num-- >= 0 ) /* Also pop off mark. */
    pop( & operandstack ) ;

  userparams->MinFontCompress = lower ;
  userparams->MaxFontItem = upper ;
  fontparams->MaxFontCache = maximum ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            currcacheparams_() author:              Andrew Cave
   creation date:       22-Nov-1988        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page ???.

---------------------------------------------------------------------------- */
Bool currentcacheparams_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  FONTSPARAMS *fontparams = context->fontsparams ;
  USERPARAMS *userparams = context->userparams ;

  return (mark_(pscontext) &&
          stack_push_integer(fontparams->MaxFontCache , &operandstack) &&
          stack_push_integer(userparams->MinFontCompress , &operandstack) &&
          stack_push_integer(userparams->MaxFontItem , &operandstack)) ;
}

/* ----------------------------------------------------------------------------
   function:            setcachelimit_()   author:              Andrew Cave
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 126.

---------------------------------------------------------------------------- */
Bool setcachelimit_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  USERPARAMS *userparams = context->userparams ;
  int32 newlimit ;

  if ( ! stack_get_integers(&operandstack, &newlimit, 1) )
    return FALSE ;

  if ( newlimit < 0 )
    return error_handler( RANGECHECK ) ;

  pop( & operandstack ) ;
  userparams->MaxFontItem = newlimit ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cachedeviceset_()  author:              Andrew Cave
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 213.

---------------------------------------------------------------------------- */
Bool setcachedevice_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_setcachedevice( & operandstack , FALSE ) ;
}

/* Does_Composite_Fonts */
/* ----------------------------------------------------------------------------
   function:            cachedevice2set_()
   creation date:       07-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 213.

---------------------------------------------------------------------------- */
Bool setcachedevice2_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_setcachedevice( & operandstack , TRUE ) ;
}

Bool gs_setcachedevice(STACK *stack, Bool want_extra_args)
{
  int32 i ;
  int32 cacheflag, currentpointflag ;
  int32 mflags ;
  SYSTEMVALUE metrics[ 10 ] ;
  CHARCACHE *cptr = NULL ;
  OMATRIX m1 ;
  FONTinfo *fontInfo ;
  Bool check ;
  int32 n_args = 6 ;
  corecontext_t *context = get_core_context_interp() ;
  ps_context_t *pscontext = context->pscontext ;
  charcontext_t *charcontext = context->charcontext ;

  /* Disallow setcachedevice if not in BuildChar or already called */
  if ( charcontext == NULL ||
       !charcontext->buildthischar ||
       charcontext->cachelevel != CacheLevel_Unset )
    return error_handler( UNDEFINED ) ;

  if ( want_extra_args ) /* Change from flag to offset */
    n_args += 4 ;

  if ( !stack_get_numeric(stack, metrics, n_args) )
    return FALSE ;

  /* Set to uncached temporarily, so error returns don't allow another
     setcachedevice/setcharwidth; this will be overwritten if a cache is
     created below. */
  charcontext->cachelevel = CacheLevel_Uncached ;

  /* If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gpush or
     the GST_GSAVE immediately after it and examine the fontinfo from that
     gstate; the user may have set a different font in the BuildChar
     procedure, and we would use the wrong caching information if we looked
     at the current font info. */
  fontInfo = find_fontinfo(charcontext, gstateptr) ;

  /* Tell a little white lie about the metrics; we say we only have the
     width, where we actually have the bbox as well (MF_LL | MF_UR). This
     prevents char_cache from trying to shift the coordinates to match the
     bounding box position. */
  mflags = MF_W0 ;
  if ( want_extra_args )
    mflags |= MF_W1 | MF_VO ;

  /* Ensure bounding box is correct way round */
  if ( metrics[ 5 ] < metrics[ 3 ] ) {
    SYSTEMVALUE swap = metrics[ 3 ] ;
    metrics[ 3 ] = metrics[ 5 ] ;
    metrics[ 5 ] = swap ;
  }
  if ( metrics[ 4 ] < metrics[ 2 ] ) {
    SYSTEMVALUE swap = metrics[ 2 ] ;
    metrics[ 2 ] = metrics[ 4 ] ;
    metrics[ 4 ] = swap ;
  }

  /* This is a hack to make 16-bit fonts which use escape characters work;
     caching is turned off for fonts in which setchardevice or setchardevice2
     are used with all parameters zero */
  cacheflag = 0 ;
  for ( i = n_args ; --i >= 0 ; )
    cacheflag |= (metrics[ i ] != 0.0) ;

  if (!cacheflag)
    fontInfo->cancache = FALSE ;

  /* if there is a current point, must preserve it after
   * setcachedevice[2], translating into cache device space
   */
  if ( CurrentPoint ) {
    /* do not preserve the rest of the path in cachedevice space
     * coordinates; it will still be around, but it is illegal to use it.
     * [bugs 4808 and 4945]
     */
    if ( ! currpoint_(pscontext) )
      return FALSE; /* keep the coordinates on the stack */
    currentpointflag = TRUE;
  }
  else
    currentpointflag = FALSE;

  MATRIX_COPY( & m1 , & thegsPageCTM( *gstateptr )) ;

  /* Store width information - in device coordinates. */
  {
    SYSTEMVALUE *wxy ;

    if ( (theWMode(*fontInfo) & 1) == 0 || (mflags & MF_W1) == 0 ) {
      HQASSERT( (mflags & MF_W0) != 0, "no horizontal width vector!" );
      wxy = &metrics[0] ;
    } else
      wxy = &metrics[6] ;

    MATRIX_TRANSFORM_DXY(wxy[0], wxy[1],
                         charcontext->xwidth,
                         charcontext->ywidth,
                         & thegsPageCTM( *gstateptr )) ;
  }

  /* The cancache clause is a hack to make 16-bit fonts which use escape
     characters work; caching is turned off for fonts in which setchardevice
     or setchardevice2 are used with all parameters zero */
  if ( charcontext->modtype != DOCHARPATH && fontInfo->cancache ) {
    SYSTEMVALUE bearings[4] ;

    char_bearings(charcontext, bearings, &metrics[2], &thegsPageCTM(*gstateptr)) ;

    /*
     * Note that we make this two pixels all around larger than would
     * appear necessary.  This is to emulate the behavior of the Adobe
     * RIPs, which appear to do this.  Some PostScript jobs depend on it
     * (for example, patternb.ps).  See the report for task #5207 for
     * more information. Note the first pair of coordinates are -minx, -miny,
     * so plus 2 is the same as -(minx - 2), -(miny - 2)
     */
    bearings[0] += 2.0 ;
    bearings[1] += 2.0 ;
    bearings[2] += 2.0 ;
    bearings[3] += 2.0 ;

    if ( (cptr = char_cache(charcontext, metrics, mflags, bearings, FALSE)) != NULL ) {
      GSTATE *gs ;
      SYSTEMVALUE dx, dy ;
      CLIPRECORD *initcliprec = NULL ;

      /* We are translating the character's coordinates with respect to its
         caller. To make sure the coordinates are handled properly, we must
         update the HDLT translation to account for the translation from the
         previous device space to the character's device space. This
         maintains a translation between the current device space and
         absolute device space, which can be used to convert between HDLT
         target frames. Note that we set up the HDLT translations always,
         even if HDLT is not active, because it may be started inside a
         gframe that has already been translated. */
      dx = m1.matrix[2][0] - bearings[0] ;
      dy = m1.matrix[2][1] - bearings[1] ;

      gstateptr->theHDLTinfo.trans[0] += dx ;
      gstateptr->theHDLTinfo.trans[1] += dy ;

      /* Preserve the relationship that Position + trans == constant, until
         begin char has had a chance to save it as the offset. */
      gstateptr->theHDLTinfo.position[0] -= dx ;
      gstateptr->theHDLTinfo.position[1] -= dy ;

      /* Shift default CTM, so that (0,0) is (-minx,-miny) */
      m1.matrix[ 2 ][ 0 ] = bearings[ 0 ] ;
      m1.matrix[ 2 ][ 1 ] = bearings[ 1 ] ;

      /* If doing vertical characters, HDLT needs to see the coordinate space
         shifted with respect to the absolute device space. The translation is
         hidden inside the character bearings, so make it explicit here. */
      if ( (mflags & MF_VO) != 0 && (theWMode(*fontInfo) & 1) != 0 ) {
        SYSTEMVALUE ax, ay ;
        MATRIX_TRANSFORM_DXY(metrics[8], metrics[9], ax, ay, &m1) ;
        gstateptr->theHDLTinfo.trans[0] -= ax ;
        gstateptr->theHDLTinfo.trans[1] -= ay ;

        /* Preserve the relationship that Position + trans == constant, until
           begin char has had a chance to save it as the offset. */
        gstateptr->theHDLTinfo.position[0] += ax ;
        gstateptr->theHDLTinfo.position[1] += ay ;
      }

      probe_begin(SW_TRACE_FONT_CACHE, (intptr_t)cptr) ;

      SET_DEVICE( DEVICE_CHAR ) ;
      charcontext->cachelevel = CacheLevel_Cached ;
      charcontext->cptr = cptr ;
      charcontext->rb = &charcontext->rs->ri.rb ;
      render_state_mask(charcontext->rs, charcontext->rb->blits,
                        charcontext->rs->forms, &mask_bitmap_surface,
                        theForm(*cptr)) ;

      gsc_initgray( gstateptr->colorInfo ) ;

      /* Inside a cached character description, we shouldn't be inheriting
         the transparency information from the surrounding context. */
      tsDefault(gsTranState(gstateptr), gstateptr->colorInfo);

      /* Invalidate front end clipping. */
      clipmapid = -1 ;

      /* Invalidate the current clip, and also clear the clip in any saves/
       * gsaves done inside the buildchar before the setcachedevice(2) - i.e.
       * upto but not including the setchardevice gpush.
       * Need to setup the device first in each gstate though.
       *
       * Note that as indicated in approval for task #21538, we could also
       * do gsc_initgray, and set the page and default ctm for these gstates
       * but I don't see the point - anyone painting after the setcachedevice
       * has been grestored away is going to completely unpredictable results
       * with different RIPs such that doing enough here to be safe ought to
       * be sufficient.
       */
      HQASSERT( gstackptr && gstackptr->next,
                "gs_setcachedevice: missing bracket_plot gframes" ) ;
      for ( gs = gstateptr ;
            gs->gType != GST_SETCHARDEVICE ;
            gs = gs->next ) {
        thegsDeviceType( *gs ) = DEVICE_CHAR ;
        thegsDeviceW( *gs ) = theFormW(*theForm(*cptr)) ;
        thegsDeviceH( *gs ) = theFormH(*theForm(*cptr)) ;

        /* We share the same initclip record between all gstates for this
           character, by capturing the initclip record after each
           gs_initclip(). The first iteration has initcliprec set to NULL, so
           the first gs_initclip() will create a new record, that will be
           propagated to the underlying gstates. */
        gs_reservecliprec(initcliprec) ;
        gs_freecliprec(&gs->thePDEVinfo.initcliprec) ;
        gs->thePDEVinfo.initcliprec = initcliprec ;

        if ( !gs_initclip(gs) )
          return FALSE ;

        initcliprec = gs->thePDEVinfo.initcliprec ;
      }
    }
  }

  if ( !cptr ) {
    if ( (mflags & MF_VO) != 0 && (theWMode(*fontInfo) & 1) != 0 ) {
      SYSTEMVALUE ax, ay ;
      MATRIX_TRANSFORM_DXY(metrics[8], metrics[9], ax, ay, &m1) ;
      m1.matrix[ 2 ][ 0 ] -= ax ;
      m1.matrix[ 2 ][ 1 ] -= ay ;
    }
  }

  MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &m1) ;
  check = gs_setctm(&m1, FALSE) ;
  HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;

  if ( currentpointflag ) { /* we have a currentpoint on the stack, set it up */
    if ( ! moveto_(pscontext) ) {
      npop( 2, & operandstack );
      return FALSE;
    }
  }

  npop(n_args, stack) ;

  if (IDLOM_DOBEGINCHARACTER(want_extra_args ? NAME_setcachedevice2 :
                             NAME_setcachedevice) == IB_PSErr)
    return FALSE;

  /* PDFOut wants the metrics for this WMode, not both WModes. Set the W1
     escapement as the W0 escapement. Ignore V0 for PDFOut; it is taken into
     account by the coordinate space translation.
  */
  /** \todo @@@ TODO FIXME: do we need to shift the BBox by V0 for WMode 1, to
     prevent the generated characters may have bogus offsets? */
  if ( (mflags & MF_W1) != 0 ) {
    metrics[0] = metrics[6] ;
    metrics[1] = metrics[7] ;
  }

  if ( pdfout_enabled() &&
       !pdfout_beginchar(&context->pdfout_h, metrics, TRUE /* cached */) )
    return FALSE ;

  return TRUE;
}

/* --------------------------------------------------------------------------*/
static Bool get_edef(FONTinfo *fontInfo, NAMECACHE *glyphname,
                      int32 *glyphchar)
{
  int32 len ;
  int32 index ;
  OBJECT *encoding ;

  HQASSERT( fontInfo , "No font info" ) ;
  HQASSERT( glyphname , "glyphname NULL in get_edef" ) ;

  encoding = & theEncoding(*fontInfo) ;
  HQASSERT(oType(*encoding) == OARRAY, "Encoding is not an array") ;

  len = theLen(*encoding) ;
  encoding = oArray(*encoding) + ( len - 1 ) ;
  for ( index = len - 1 ; index >= 0 ; --index ) {
    if ( oType(*encoding) == ONAME ) {
      if ( glyphname == oName(*encoding)) {
        (*glyphchar) = index ;
        return TRUE ;
      }
    }
    else if ( oType(*encoding) == ONULL ) {
      if ( glyphname == system_names + NAME_notdef ) {
        (*glyphchar) = index ;
        return TRUE ;
      }
    }
    --encoding ;
  }
  (*glyphchar) = NO_CHARCODE ;
  return FALSE ;
}

/*-------------------------------------------------------------------------- */
static Bool get_cdef(FONTinfo *fontInfo, int32 glyphchar,
                     NAMECACHE **glyphname)
{
  OBJECT *encoding, *glyph ;
  uint16 len ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT( fontInfo , "No font info" ) ;
  HQASSERT( glyphname , "glyphname NULL in get_cdef" ) ;
  HQASSERT( glyphchar >= 0 , "glyphchar out of range in get_cdef" ) ;

  encoding = & theEncoding(*fontInfo) ;
  HQASSERT(oType(*encoding) == OARRAY, "Encoding is not an array") ;

  len = theLen(*encoding) ;

  if (len == 0 && oCPointer(*encoding) == &onothing) {
    /* Special PDF CID/CFF construction, from cff_makeFont() [12764] */
    *glyphname = NULL ;
    return TRUE ;
  }

  if (glyphchar < 0 || glyphchar >= len)
    return error_handler( RANGECHECK ) ;

  glyph = oArray(*encoding) + glyphchar ;
  switch (oType(*glyph)) {

  case ONAME:
    /* The usual (Red Book mandated) case */
    *glyphname = oName(*glyph) ;
    break ;

  case ONULL:
    /* If the Encoding entry is an ONULL then show the /.notdef character. */
    *glyphname = system_names + NAME_notdef ;
    break ;

  case OSTRING:
    /* Our relaxation of the spec to accomodate PFIN fonts.
     * The problem is that there is currently no way for a module to create
     * PostScript ONAMEs through the sw_data API - only OSTRINGs.
     * Rather than special-case the Encoding array creation in PFIN, we allow
     * arrays of strings, and cvn them on the fly...
     */
    *glyphname = cachename(oString(*glyph), (uint32)theLen(*glyph)) ;

    /* Do the equivalent of cvn, for efficiency's sake - PFIN doesn't care. */
    if (*glyphname && oCanWrite(*encoding) &&
        check_asave(oArray(*encoding), len, oGlobalValue(*encoding),
                    corecontext)) {
      theTags(*glyph) ^= OSTRING ^ ONAME ;  /* change type ONLY */
      oName(*glyph) = *glyphname ;
    }
    break ;

  default:
    return error_handler( TYPECHECK ) ;
  }

  return TRUE ;
}

/*-------------------------------------------------------------------------- */
Bool get_sdef(FONTinfo *fontInfo, OBJECT *glyphname, OBJECT *charstring)
{
  OBJECT *string ;
  OBJECT *charstrings ;

  HQASSERT( fontInfo , "No font info" ) ;
  HQASSERT( glyphname , "glyphname NULL in get_sdef" ) ;
  HQASSERT( charstring , "charstring NULL in get_sdef" ) ;

/* Extract the string (description) from CharStrings. */
  charstrings = theCharStrings(*fontInfo) ;
  if ( ! charstrings )
    return error_handler( INVALIDFONT ) ;

  string = extract_hash( charstrings , glyphname ) ;
  if ( string == NULL ) {
    /* If not found in the CharStrings dictionary then change the
     * name from that given to /.notdef.
     */
    string = fast_extract_hash_name( charstrings , NAME_notdef ) ;
    if ( string == NULL )
      return error_handler( UNDEFINED ) ;
  }

  Copy(charstring, string) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            get_metrics(..)    author:              Andrew Cave
   creation date:       08-Mar-1989        last modification:   ##-###-####
   arguments:           nameo , args , iswidth , bear , isbear .
   description:

   This procedure attempts to locate any metric information.
   note that get_metrics has changed substantially; metrics are now returned
   in the font system in an array, rather than individually in device space.

---------------------------------------------------------------------------- */
/* Does_Composite_Fonts */
Bool get_metrics( OBJECT *glyphname ,
                  SYSTEMVALUE mvalues[ 10 ] ,
                  int32 *mflags_p )
{
/* mvalues is to contain the 10 arguments to the setcachedevice2 operator,
     also derivable from the Metrics and Metrics2 dictionaries and the
     CDevProc procedure, and from the built in fonts. The values in the
     arrays are UNADJUSTED: the font matrix is not applied;
   *mflags_p indicates which values have been supplied (or defaulted);
     a 0 bit means not set, 1 means set, other bits are used to indicate
     differences in use (e.g. for bearings, replace or subtract)
   mvalues and *mflags_p are not set at all yet - this routine
     initialises them. Metrics and/or Metrics2 may not be present.
*/
  OBJECT *them ;

/* Modify if a metrics entry. */
  error_clear_newerror();
  *mflags_p = 0 ;

  them = theMetrics( theFontInfo( *gstateptr )) ;
  if ( them ) {
    them = extract_hash( them , glyphname ) ;
    if ( them ) {
      switch ( oType(*them) ) {
      case OINTEGER :
      case OREAL :
      case OINFINITY :
        if ( ! object_get_numeric(them, mvalues) )
          return error_handler( INVALIDFONT ) ;
        mvalues[ 1 ] = 0.0 ; /* an x value implies a y value */
        *mflags_p = MF_W0 ;
        break ;
      case OARRAY :
      case OPACKEDARRAY :
        if ( ! oCanRead(*them) )
          return error_handler( INVALIDFONT ) ;
        switch ( theLen(*them) ) {
        case 4 :
          them = oArray(*them) ;
          if ( !object_get_numeric(&them[0], mvalues + 2) ||
               !object_get_numeric(&them[1], mvalues + 3) ||
               !object_get_numeric(&them[2], mvalues + 0) ||
               !object_get_numeric(&them[3], mvalues + 1) )
            return error_handler( INVALIDFONT ) ;

          /* PLRM3 implies a different interpretation of m[3] than this:
             It is the new left sidebearing point, rather than a translation
             on the bbox Y values. */
          *mflags_p = MF_W0 | MF_LL | MF_UR ; /* four values set */
          break ;
        case 2 :
          them = oArray(*them) ;
          mvalues[ 3 ] = mvalues[ 1 ] = 0.0 ;
          if ( !object_get_numeric(&them[0], mvalues + 2) ||
               !object_get_numeric(&them[1], mvalues + 0) )
            return error_handler( INVALIDFONT ) ;

          *mflags_p = MF_W0 | MF_LL ; /* four values set (two of them defaulted) */
          break ;
        default:
          return error_handler( INVALIDFONT ) ;
        }
        break ;
      default:
        return error_handler( INVALIDFONT ) ;
      }
    }
  }
/* Does_Composite_Fonts */
  them = theMetrics2( theFontInfo( *gstateptr ));
  if ( them ) {
    them = extract_hash( them , glyphname ) ;
    if ( them ) {
      switch ( oType(*them) ) {
      case OARRAY :
      case OPACKEDARRAY :
        if ( ! oCanRead(*them) )
          return error_handler( INVALIDFONT ) ;
        switch ( theLen(*them) ) {
        case 4 :
          them = oArray(*them) ;
          if ( !object_get_numeric(&them[0], mvalues + 6) ||
               !object_get_numeric(&them[1], mvalues + 7) ||
               !object_get_numeric(&them[2], mvalues + 8) ||
               !object_get_numeric(&them[3], mvalues + 9) )
            return error_handler( INVALIDFONT ) ;

          *mflags_p |= MF_W1 | MF_VO ; /* four (more) values set */
          break ;
        default:
          return error_handler( INVALIDFONT ) ;
        }
        break ;
      default:
        return error_handler( INVALIDFONT ) ;
      }
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            char_cache(..) author:              Andrew Cave
   creation date:       07-Oct-1987          last modification:   ##-###-####
   arguments:           bear , isbear , aoffset .
   description:

   This procedure attempts to cache the character, setting up a cache bitmap.
   Input arguments are the metrics array and flags (set up from Metrics,
   Metrics2, CDevProc, and the character description), the metrics flags,
   cache form offset and bounding box (as computed by char_bearings), and a
   flag indicating if the character has any marks.

---------------------------------------------------------------------------- */
CHARCACHE *char_cache(charcontext_t *charcontext,
                      SYSTEMVALUE metrics[ 10 ] ,
                      int32 mflags ,
                      SYSTEMVALUE offset[ 4 ],
                      Bool blank)
{
  corecontext_t *context = get_core_context_interp();
  register int32 w , h , size ;
  register CHARCACHE *newchar ;
  FORM *charform ;
  OMATRIX *mmatrix ; /* Matrix to transform metrics */
  SYSTEMVALUE minx , miny ;
  FONTinfo *fontInfo ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == context->charcontext,
           "Character context is not current") ;

  /* If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gpush or
     the GST_GSAVE immediately after it and examine the fontinfo from that
     gstate; the user may have set a different font in the BuildChar
     procedure, and we would use the wrong caching information if we looked
     at the current font info. */
  fontInfo = find_fontinfo(charcontext, gstateptr) ;

  if ( !blank ) {
    SYSTEMVALUE fw = offset[ 2 ] + offset[ 0 ] ;
    SYSTEMVALUE fh = offset[ 3 ] + offset[ 1 ] ;
    int32 l;

    /* cope with possible overflow in very very large scalings */
    if (fw > (SYSTEMVALUE)MAXINT32 || fh > (SYSTEMVALUE)MAXINT32)
      return NULL ;

    minx = -offset[ 0 ] ;
    miny = -offset[ 1 ] ;

    w = ( int32 )fw ;
    h = ( int32 )fh ;

    HQASSERT(w > 0, "width of character must be greater than 0") ;
    HQASSERT(h > 0, "height of character must be greater than 0") ;

    l = FORM_LINE_BYTES(w);
    if ( ( SYSTEMVALUE )l * ( SYSTEMVALUE )h > context->userparams->MaxFontItem ) {
      /* Allow bitmap fonts to break the individual limit.  Adobe seem to
         ignore it in this case too. */
      if ( charcontext->chartype != CHAR_Bitmap )
        return NULL ;
    }
    size = l * h ;
    charform = make_Form( w, h );
  } else {
    minx = miny = 0.0 ;
    size = w = h = 0 ;
    charform = MAKE_BLANK_FORM();
  }

  if ( charform == NULL )
    return NULL ;

  if ( (newchar = fontcache_new_char(fontInfo,
                                     &charcontext->glyphname)) == NULL ) {
    destroy_Form(charform) ;
    return NULL ;
  }

  theForm(*newchar) = charform ;

  theXBearing(*newchar) =
    minx        + theFontMatrix( *fontInfo ).matrix[ 2 ][ 0 ] ;
  theYBearing(*newchar) =
    miny        + theFontMatrix( *fontInfo ).matrix[ 2 ][ 1 ] ;

  /* Transform metrics by matrix to get them into device space; Type 3/4
     characters need to use the concatenated matrix; in this case
     theFontMatrix contains just the font matrix parts, not the
     concatenation. */
  mmatrix = (charcontext->buildthischar ?
             &thegsPageCTM(*gstateptr) :
             &theFontMatrix(*fontInfo)) ;

  if ( (MF_LL & mflags) != 0 ) {
    SYSTEMVALUE ax, ay ;
    MATRIX_TRANSFORM_DXY(metrics[ 2 ], 0.0, ax, ay, mmatrix) ;
    theXBearing(*newchar) += ax ;
    theYBearing(*newchar) += ay ;
  }

  if ( (MF_UR & mflags) != 0 ) {
    SYSTEMVALUE ax, ay ;
    MATRIX_TRANSFORM_DXY(0.0, metrics[ 3 ], ax, ay, mmatrix) ;
    theXBearing(*newchar) += ax ;
    theYBearing(*newchar) += ay ;
  }

  /* Does_Composite_Fonts */
  if ( (theICharWMode(newchar) & 1) != 0 && (mflags & MF_VO) != 0 ) {
    SYSTEMVALUE ax, ay ;
    /* use the existing bearings, but adjust for the relative origin
       specified with writing mode 1 (metrics 2) for composite font
       extensions, spec pages 1 and 3; put v into device space */
    MATRIX_TRANSFORM_DXY( metrics[ 8 ], metrics[ 9 ], ax, ay, mmatrix) ;
    theXBearing(*newchar) -= ax ;
    theYBearing(*newchar) -= ay ;
  }

  theCharXWidth(*newchar) = charcontext->xwidth ;
  theCharYWidth(*newchar) = charcontext->ywidth ;

  if ( !blank ) {
    if ( size > context->userparams->MinFontCompress ||
         context->userparams->MinFontCompress == 0 ||
         fontcache_is_compressing() )
      theFormT(*theForm(*newchar)) = FORMTYPE_CACHEBITMAPTORLE ;
  }

  context->fontsparams->CurFontCache += ALIGN_FORM_SIZE(size);

  return  newchar  ;
}

/* ----------------------------------------------------------------------------
   function:            char_bearings(..)
   creation date:       07-Oct-1987          last modification:   ##-###-####
   arguments:           offset , bbox  .
   description:

   calculates the bearings for the character - split from cache_char. The
   input bbox is in font coordinates. The output offset array contains the
   negated lower left corner and the upper right corner of the FORM bbox,
   in device space. The first pair of offsets is the shift to put the character
   at the origin, the second pair plus the first pair is the character size.

---------------------------------------------------------------------------- */
void char_bearings(charcontext_t *charcontext,
                   SYSTEMVALUE offset[4] , SYSTEMVALUE bbox[4],
                   OMATRIX *matrix)
{
  int32 extra ;
  register SYSTEMVALUE maxx , minx , maxy , miny ;
  SYSTEMVALUE points[ 8 ], *coords = points ;
  FONTinfo *fontInfo ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == char_current_context(),
           "Character context is not current") ;

  /* If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gpush or
     the GST_GSAVE immediately after it and examine the fontinfo from that
     gstate; the user may have set a different font in the BuildChar
     procedure, and we would use the wrong caching information if we looked
     at the current font info. */
  fontInfo = find_fontinfo(charcontext, gstateptr) ;

  minx = bbox[ 0 ] ;
  miny = bbox[ 1 ] ;
  maxx = bbox[ 2 ] ;
  maxy = bbox[ 3 ] ;

  if ( thePaintType(*fontInfo) != 0 ) {
    SYSTEMVALUE dtemp = 0.5 * (SYSTEMVALUE)theStrokeWidth( *fontInfo ) ;
    maxy += dtemp ;
    maxx += dtemp ;
    miny -= dtemp ;
    minx -= dtemp ;
  }

  /* Calculate transformed bounding box in device space. */
  MATRIX_TRANSFORM_DXY(minx, miny, points[ 0 ], points[ 1 ], matrix) ;
  MATRIX_TRANSFORM_DXY(maxx, maxy, points[ 2 ], points[ 3 ], matrix) ;
  if ( matrix->opt == MATRIX_OPT_BOTH ) {
    MATRIX_TRANSFORM_DXY(minx, maxy, points[ 4 ], points[ 5 ], matrix) ;
    MATRIX_TRANSFORM_DXY(maxx, miny, points[ 6 ], points[ 7 ], matrix) ;
    extra = 3 ;
  } else
    extra = 1 ;

  minx = maxx = coords[ 0 ] ;
  miny = maxy = coords[ 1 ] ;
  do {
    register SYSTEMVALUE tmp ;

    coords += 2 ;

    tmp = coords[ 0 ] ;
    if ( tmp < minx )
      minx = tmp ;
    else if ( tmp > maxx )
      maxx = tmp ;

    tmp = coords[ 1 ] ;
    if ( tmp < miny )
      miny = tmp ;
    else if ( tmp > maxy )
      maxy = tmp ;
  } while ( --extra > 0 ) ;

  /* Calculate the size of bmap required and see if it can be obtained. The
     character path should never overflow these limits, so use floor/ceil
     (SC_C2D_UNTF can round up coordinates very close to a pixel
     boundary). */
  minx = floor(minx) ;
  miny = floor(miny) ;
  maxx = ceil(maxx + SC_PIXEL_ROUND) ;
  maxy = ceil(maxy + SC_PIXEL_ROUND) ;

  /*
    Set up amount to shift to get the character coordinate system into
    the bitmap coordinate system for the character bitmap.
  */
  offset[ 0 ] = -minx ;
  offset[ 1 ] = -miny ;
  offset[ 2 ] = maxx ;
  offset[ 3 ] = maxy ;
}

Bool char_metrics(charcontext_t *charcontext,
                  SYSTEMVALUE metrics[10], int32 *mflags, SYSTEMVALUE bbox[4])
{
  FONTinfo *fontInfo ;
  SYSTEMVALUE *wxy ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == char_current_context(),
           "Character context is not current") ;

  /* If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gpush or
     the GST_GSAVE immediately after it and examine the fontinfo from that
     gstate; the user may have set a different font in the BuildChar
     procedure, and we would use the wrong caching information if we looked
     at the current font info. This should only apply to Type 4 characters,
     since this function is not called for Type 3, and they shouldn't have
     changed the font, so this is a paranoia measure. */
  fontInfo = find_fontinfo(charcontext, gstateptr) ;

  /* [Bug #4546] at this point, metrics contains:
   * [ 0 ] : x-width, possibly from the char outline, MF_W0 is set
   * [ 1 ] : y-width, possibly from the char outline, MF_W0 is set
   * [ 2 ] : MF_LL => new lhs (x) of bbox
   * [ 3 ] : MF_UR => translation (y) of bbox
   * [ 4 ] : unset
   * [ 5 ] : unset
   * [ 6 ] : MF_W1 => x-width-v
   * [ 7 ] : MF_W1 => y-width-v
   * [ 8 ] : MF_VO => x-vec-oH-oV
   * [ 9 ] : MF_VO => y-vec-oH-oV
   *
   * also, UR => LL => W0
   *       W1 <=> VO
   *
   * We have the bbox of the actual path in bbox.
   */

  /* ********** CDEVPROC time *********** */
  if ( oType(theCDevProc(*fontInfo)) != ONULL ) {
    int32 i ;

    /* this will adjust the metrics and associated flags according to
       the users CDevProc procedure */

    /* X-sidebearing from Metrics is a move TO so apply difference */
    if ( (MF_LL & *mflags) != 0 ) {
      /* metrics[ 2 ] is unchanged, it is the LHSbearing */
      metrics[ 4 ] = bbox[ 2 ] + (metrics[ 2 ] - bbox[ 0 ]);
    } else {
      metrics[ 2 ] = bbox[ 0 ]; /* llx */
      metrics[ 4 ] = bbox[ 2 ]; /* urx */
    }

    /* Y-sidebearing from Metrics is a move BY so apply its value */
    if ( (MF_UR & *mflags) != 0 ) {
      /* BEWARE overloading of metrics[3] */
      metrics[ 5 ] = bbox[ 3 ] + metrics[ 3 ];
      metrics[ 3 ] = bbox[ 1 ] + metrics[ 3 ];
    } else {
      metrics[ 3 ] = bbox[ 1 ]; /* ury */
      metrics[ 5 ] = bbox[ 3 ]; /* lly */
    }

    /* all we need to do now is compensate for any lack of vertical writing
       metrics. Not altogether clear what we should do, but I think making
       the origin offset (0,0) and the width vector w1 the same as w0 is
       a reasonable bet */
    if ( (*mflags & MF_W1) == 0 ) {
      HQASSERT( (*mflags & MF_VO) == 0, "got VO, but no W1!" );
      metrics[ 6 ] = metrics[ 0 ] ;
      metrics[ 7 ] = metrics[ 1 ] ;
      metrics[ 8 ] = 0.0 ;
      metrics[ 9 ] = 0.0 ;
      *mflags |= MF_W1 | MF_VO ;
    }

    /* [Bug #4546] at this point, metrics contains:
     * [ 0 ] : x-width
     * [ 1 ] : y-width
     * [ 2 ] : llx, adjusted TO metrics X if MF_LL
     * [ 3 ] : lly, adjusted BY metrics Y if MF_UR
     * [ 4 ] : urx, adjusted by same as llx if MF_LL
     * [ 5 ] : ury, adjusted BY metrics Y if MF_UR
     * [ 6 ] : x-width-v
     * [ 7 ] : y-width-v
     * [ 8 ] : x-vec-oH-oV
     * [ 9 ] : y-vec-oH-oV
     *
     * W0 is set, VO and W1 are set.
     * We have the bbox of the actual path in bbox.
     */

    /* start calling cdevproc */
    /* the operands for CDevProc: 10 metrics values and
       the character name, onto the operand stack */
    for ( i = 0 ; i < 10 ; i++ ) {
      if ( ! stack_push_real( metrics[ i ], &operandstack )) {
        if ( i > 0 ) {
          npop ( i, & operandstack ) ;
        }
        return FALSE ;
      }
    }
    if ( ! push(&charcontext->glyphname, & operandstack) )
      return FALSE ;

    /* and finally the users own procedure */
    if ( ! push(&theCDevProc(*fontInfo), &executionstack) ) {
      npop( 11 , & operandstack ) ;
      return FALSE ;
    }

    /* call the interpreter to run it */
    NO_PURGE_INTERPRETER();

    /* get the metrics off the operand stack - 10 values starting at 0 offset
       on the stack; then we have them all so set all the flags */
    /* ll and ur data do not imply sidebearings, so MF_LL should not be set */
    if ( ! stack_get_numeric(&operandstack, metrics, 10) )
      return FALSE ;

    npop ( 10 , & operandstack );

    /* IF the llx OR ury have changed, we must move the bbox to line up with
     * those coords - nasty but true.  (lly and urx are ignored!)
     * We compare with the real bbox and reset flags appropriately.
     * The comparison is via USERVALUE because the numbers have been sullied
     * in their accuracy by inclusion in the PostScript world.
     */

    if ( (USERVALUE)metrics[ 2 ] != (USERVALUE)bbox[ 0 ] ) {
      metrics[ 2 ] -= bbox[ 0 ];
      *mflags |= MF_LL;
    } else
      *mflags &=~MF_LL; /* clear the flag - no change */

    if ( (USERVALUE)metrics[ 5 ] != (USERVALUE)bbox[ 3 ] ) {
      metrics[ 3 ] = metrics[ 5 ] - bbox[ 3 ];
      *mflags |= MF_UR;
    } else
      *mflags &=~MF_UR; /* clear the flag - no change */
  } else {
    /* NO CDevProc - so adjust the metrics we have got appropriately. */
    /* if LL, metrics[ 2 ] is where to put the LHS, record the delta */
    if ( (*mflags & MF_LL) != 0 )
      metrics[ 2 ] -= bbox[ 0 ];
    /* if UR, metrics[ 3 ] is a delta to translate ( == where to put
     * the baseline) so we can let well alone.
     */
  }

  /* [Bug #4546] at this point, metrics contains:
   * [ 0 ] : x-width, possibly from the char outline, MF_W0 is set
   * [ 1 ] : y-width, possibly from the char outline, MF_W0 is set
   * [ 2 ] : MF_LL => translation (x) of bbox
   * [ 3 ] : MF_UR => translation (y) of bbox
   * [ 4 ] : dont care
   * [ 5 ] : dont care
   * [ 6 ] : MF_W1 => x-width-v
   * [ 7 ] : MF_W1 => y-width-v
   * [ 8 ] : MF_VO => x-vec-oH-oV
   * [ 9 ] : MF_VO => y-vec-oH-oV
   *
   * also, UR =!> LL, not any more.
   *       W1 <=> VO
   *
   * We still have the bbox of the actual path in bbox.
   */

  /* we've definitely got all the information we need to compute the width
   * vector now - so do it now, unconditionally, and we can early exit if
   * that is all that is needed.
   */
  if ( (theWMode(*fontInfo) & 1) == 0 || (*mflags & MF_W1) == 0 ) {
    HQASSERT( *mflags & MF_W0, "no horizontal width vector!" );
    wxy = &metrics[0];
  } else
    wxy = &metrics[6];

  COMPUTE_STRINGWIDTH(wxy, charcontext);

  return TRUE ;
}

/* Find out if character is small enough to use accurate renderer. Side
   effects set the size of the font's characters (by FontBBox or the EM
   square) in pixels and flags determining if the accurate renderer can be
   used, and if the glyph is mirrored. */
Bool char_accurate(SYSTEMVALUE *glyphsize, uint32 *accurate)
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  SYSTEMVALUE thisglyphsize, x, y ;
  sbbox_t bbox ;
  int32 threshold = systemparams->AccurateRenderThreshold ;
  int32 twopass = systemparams->AccurateTwoPassThreshold ;

  /* Calculate the -ve of the determinant (y-flip is normal case in PS
   * world) so flip in this sense is +ve determinant, => -ve temp var.
   * Faster in usual (+ve temporary) case too! */
  thisglyphsize =
    theFontMatrix(*fontInfo).matrix[0][1] *
      theFontMatrix(*fontInfo).matrix[1][0] -
        theFontMatrix(*fontInfo).matrix[0][0] *
          theFontMatrix(*fontInfo).matrix[1][1];
  if ( thisglyphsize < 0.0 )
    thisglyphsize = (-thisglyphsize);
  /* Now have area scale factor of the fontmatrix, to pixel coordinates */

  if ( fontInfo->fontbbox ) {
    if ( !object_get_bbox(fontInfo->fontbbox, &bbox) )
      return FALSE ;
  } else {
    /* If no FontBBox, use the font's unconcatenated matrix to work out the
       font coordinate system. Assume that the original unscaled font matrix
       maps to a unit em square. Note we do not allow accurate rendering in
       this case, because we are not guaranteed that the values are not
       exact. */
    OMATRIX ifcmatrix ;

    if ( !matrix_inverse(&theFontCompositeMatrix(*fontInfo), &ifcmatrix) )
      return error_handler(UNDEFINEDRESULT) ;

    matrix_mult(&fontInfo->scalematrix, &ifcmatrix, &ifcmatrix) ;

    /* Find corners of EM square, in font coordinates */
    MATRIX_TRANSFORM_DXY(0.0, 0.0, bbox.x1, bbox.y1, &ifcmatrix) ;
    MATRIX_TRANSFORM_DXY(1.0, 1.0, bbox.x2, bbox.y2, &ifcmatrix) ;
  }

  x = fabs(bbox.x2 - bbox.x1) ; /* Maximum X size */
  y = fabs(bbox.y2 - bbox.y1) ; /* Maximum Y size */

  thisglyphsize *= x * y ; /* area of em square in square pixels */
  *glyphsize = thisglyphsize;

  /* If it's not explicitly switched off, and we're not stroking, test if we
     can use the accurate renderer. */
  *accurate = 0 ;
  if ( threshold > 0 && thePaintType(*fontInfo) == 0 ) {
    /* Transform EM square dimensions into device space */
    MATRIX_TRANSFORM_DXY(x, 0, bbox.x1, bbox.y1, &theFontMatrix(*fontInfo)) ;
    MATRIX_TRANSFORM_DXY(0, y, bbox.x2, bbox.y2, &theFontMatrix(*fontInfo)) ;

    x = fabs(bbox.x1) + fabs(bbox.x2) ; /* Maximum X size */
    y = fabs(bbox.y1) + fabs(bbox.y2) ; /* Maximum Y size */

    /* Allow accurate rendering if none of the coordinates would overflow
       the pixel size limit */
    if ( x <= threshold && y <= threshold ) {
      *accurate |= SC_FLAG_ACCURATE ;

      /* Allow two-pass rendering if none of the coordinates would overflow
         the pixel size limit */
      if ( twopass > 0 && x <= twopass && y <= twopass )
        *accurate |= SC_FLAG_TWOPASS ;
    }
  }

  return TRUE ;
}

/* HDLT/IDLOM code for coping with the case where the character is cached
   already, but HDLT wants a callback. We save the old cache form in
   IdlomOldSwCache, run most of the character drawing code to get the HDLT
   callbacks, and then restore the old cache entry. Note that these macros
   rely on plotchar to restore the old entry. */
static inline void restore_old_cachehit(corecontext_t *context,
                                        CHARCACHE *cptr,
                                        charcontext_t *charcontext)
{
  if (IdlomOldSwCache) {
    if (charcontext->cptr == cptr) {
      if ( charcontext->cachelevel == CacheLevel_Cached )
        probe_end(SW_TRACE_FONT_CACHE, (intptr_t)cptr) ;

      charcontext->cptr = NULL;
      charcontext->cachelevel = CacheLevel_Uncached;
    }
    freethiscache(context, charcontext, cptr);
  }
}

static inline void free_new_cache_restoring_old(corecontext_t *context,
                                                CHARCACHE *cptr,
                                                charcontext_t *charcontext)
{
  if (IdlomOldSwCache && charcontext->cptr == cptr) {
    if ( charcontext->cachelevel == CacheLevel_Cached )
      probe_end(SW_TRACE_FONT_CACHE, (intptr_t)cptr) ;

    charcontext->cptr = NULL;
    charcontext->cachelevel = CacheLevel_Uncached;
  }
  freethiscache(context, charcontext, cptr);
}

static void idlom_saveAdjust_char_offset(OMATRIX *oldCTM,
                                         OMATRIX *oldDevCTM,
                                         SYSTEMVALUE oldtrans[2],
                                         SYSTEMVALUE oldpos[2],
                                         SYSTEMVALUE metrics[10],
                                         int32 mflags,
                                         FONTinfo *fontInfo,
                                         CHARCACHE *cptr,
                                         SYSTEMVALUE xoff,
                                         SYSTEMVALUE yoff)
{
  Bool check ;
  OMATRIX m1 ;

  /* Save first */
  MATRIX_COPY(oldCTM, &thegsPageCTM(*gstateptr)) ;
  MATRIX_COPY(oldDevCTM, &thegsDeviceCTM(*gstateptr)) ;
  oldtrans[0] = gstateptr->theHDLTinfo.trans[0] ;
  oldtrans[1] = gstateptr->theHDLTinfo.trans[1] ;
  oldpos[0] = gstateptr->theHDLTinfo.position[0] ;
  oldpos[1] = gstateptr->theHDLTinfo.position[1] ;

  /* Set up the fontmatrix as the page matrix, with the appropriate
   * translation. This is effectively the same calculation as
   * start_scan_char() and gs_setcachedevice(). If it has become degenerate
   * (which will occur if set_matrix snapped it to zero) then we need to
   * extract the font matrix prior to the snap so that HDLT doesn't barf.
   */
  MATRIX_COPY(&m1, &theFontMatrix(theFontInfo(*gstateptr))) ;
  m1.matrix[2][0] += gstateptr->theHDLTinfo.position[0] ;
  m1.matrix[2][1] += gstateptr->theHDLTinfo.position[1] ;

  if ( cptr != NULL ) {
    SYSTEMVALUE dx, dy ;

    /* We are translating the character's coordinates with respect to its
       caller. To make sure the coordinates are handled properly, we must
       update the HDLT translation to account for the translation from the
       previous device space to the character's device space. This
       maintains a translation between the current device space and
       absolute device space, which can be used to convert between HDLT
       target frames. Note that we set up the HDLT translations always, even
       if HDLT is not active, because it may be started inside a gframe
       that has already been translated. */
    dx = m1.matrix[2][0] - xoff ;
    dy = m1.matrix[2][1] - yoff ;

    gstateptr->theHDLTinfo.trans[0] += dx;
    gstateptr->theHDLTinfo.trans[1] += dy;

    /* Preserve the relationship the Position + trans == constant, until
       begin char has had a chance to save it as the offset. */
    gstateptr->theHDLTinfo.position[0] -= dx;
    gstateptr->theHDLTinfo.position[1] -= dy;

    /* Shift the default CTM, so that (0,0) is (xoff,yoff) */
    m1.matrix[2][0] = xoff ;
    m1.matrix[2][1] = yoff ;

    /* If doing vertical characters, HDLT needs to see the coordinate space
       shifted with respect to the absolute device space. The translation is
       hidden inside the character bearings, so make it explicit here. */
    if ( (mflags & MF_VO) != 0 && (theWMode(*fontInfo) & 1) != 0 ) {
      SYSTEMVALUE ax, ay ;
      MATRIX_TRANSFORM_DXY(metrics[8], metrics[9], ax, ay, &m1) ;
      gstateptr->theHDLTinfo.trans[0] -= ax ;
      gstateptr->theHDLTinfo.trans[1] -= ay ;

      /* Preserve the relationship the Position + trans == constant, until
         begin char has had a chance to save it as the offset. */
      gstateptr->theHDLTinfo.position[0] += ax;
      gstateptr->theHDLTinfo.position[1] += ay;
    }
  }

  MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &m1) ;
  check = gs_setctm(&m1, FALSE) ;
  HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;
}

static void idlom_restore_char_offset(OMATRIX *oldCTM,
                                      OMATRIX *oldDevCTM,
                                      SYSTEMVALUE oldtrans[2],
                                      SYSTEMVALUE oldpos[2])
{
  MATRIX_COPY(&thegsPageCTM(*gstateptr), oldCTM) ;
  MATRIX_COPY(&thegsDeviceCTM(*gstateptr), oldDevCTM) ;
  gstateptr->theHDLTinfo.trans[0] = oldtrans[0] ;
  gstateptr->theHDLTinfo.trans[1] = oldtrans[1] ;
  gstateptr->theHDLTinfo.position[0] = oldpos[0] ;
  gstateptr->theHDLTinfo.position[1] = oldpos[1] ;
}


/** Perform drawing of Type1/TrueType character into form or display list, and
   appropriate HDLT callbacks for the fill/stroke. The character path is
   passed in an arbitrary coordinate space, with a transform matrix to take
   it to device space. The path may be further transformed and/or stolen by
   char_draw(). The caller is responsible for disposing of the path if it is
   not re-initialised here. */
Bool char_draw(charcontext_t *charcontext,
               LINELIST *currpt, CHARCACHE *cptr,
               SYSTEMVALUE metrics[10], int32 mflags,
               SYSTEMVALUE xoff, SYSTEMVALUE yoff,
               Bool blank, uint32 accurate,
               PATHINFO *cpath, OMATRIX *dtransform)
{
  corecontext_t *context = get_core_context_interp();
  DL_STATE *page = context->page;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  OMATRIX dmatrix ;
  STROKE_PARAMS sparams ;
  Bool result = FALSE ;
  int32 fontfillrule = context->fontsparams->fontfillrule ;
  int32 showtype ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == context->charcontext,
           "Character context is not current") ;

  showtype = charcontext->modtype ;
  HQASSERT(showtype != DOSTRINGWIDTH,
           "Should not be trying to draw character when getting stringwidth") ;

  /* Check for space character. Don't even bother building outline if we did
     not detect any marks. */
  if ( blank ) {
    HQASSERT( cptr == NULL || theFormS(*theForm(*cptr)) == 0,
              "Non 0 size in blank form");
    HQASSERT( cptr == NULL || theFormH(*theForm(*cptr)) == 0,
              "Non 0 height in blank form");
    HQASSERT( cptr == NULL || theFormW(*theForm(*cptr)) == 0,
              "Non 0 width in blank form");
    HQASSERT( cptr == NULL || theFormL(*theForm(*cptr)) == 0,
              "Non 0 lbytes in blank form");
    HQASSERT( cptr == NULL || theFormA(*theForm(*cptr)) == NULL,
              "Blank form not empty");
    HQASSERT( cptr == NULL || theFormT(*theForm(*cptr)) == FORMTYPE_BLANK,
              "Blank form has wrong type");

    if ( showtype != DOCHARPATH && isHDLTEnabled( *gstateptr )) {
      OMATRIX savedCTM, savedDevCTM ;
      SYSTEMVALUE savedTrans[2], savedPos[2];
      int32 retcode ;

      idlom_saveAdjust_char_offset(&savedCTM, &savedDevCTM,
                                   savedTrans, savedPos,
                                   metrics, mflags, fontInfo,
                                   cptr, xoff, yoff) ;
      retcode = IDLOM_DOBEGINCHARACTER(NAME_undefined) ;
      idlom_restore_char_offset(&savedCTM, &savedDevCTM, savedTrans, savedPos) ;

      if ( retcode == IB_PSErr) {
        free_new_cache_restoring_old(context, cptr, charcontext) ;
        return FALSE;
      }
    }
    restore_old_cachehit(context, cptr, charcontext);
    return TRUE ;
  }

  if ( cptr ) {
    /* Set up output state for caching */
    HQASSERT(showtype != DOCHARPATH, "Can't cache charpath") ;

    probe_begin(SW_TRACE_FONT_CACHE, (intptr_t)cptr) ;

    charcontext->cachelevel = CacheLevel_Cached ;
    charcontext->cptr = cptr ;

    if ( theFontType(*fontInfo) == FONTTYPE_4 ) {
      SET_DEVICE(DEVICE_CHAR);
      thegsDeviceW( *gstateptr ) = theFormW(*theForm(*cptr)) ;
      thegsDeviceH( *gstateptr ) = theFormH(*theForm(*cptr)) ;

      /* Invalidate front-end clipping form; shouldn't need to do this, but
         you never know what tricks the users will get upto when they discover
         IDLOM callbacks... */
      clipmapid = -1 ;
    }

    /* Clipform and blit stack location are set by plotchar, we pick them up
       from the context's render state so that their lifetime matches the
       render state. */
    charcontext->rb = &charcontext->rs->ri.rb ;
    render_state_mask(charcontext->rs, charcontext->rb->blits,
                      charcontext->rs->forms, &mask_bitmap_surface,
                      theForm(*cptr)) ;

    /* Set up clipping for make_nfill */
    cclip_bbox.x1 = 0;
    cclip_bbox.y1 = 0;
    cclip_bbox.x2 = theFormW(*theForm(*cptr)) - 1;
    cclip_bbox.y2 = theFormH(*theForm(*cptr)) - 1;
    if ( accurate ) {
      HQASSERT(thePaintType(*fontInfo) == 0,
               "Can't use accurate renderer when stroking") ;
      /* Scaled clipping for make_nfill, fill uses charcontext->rs->ri.clip */
      cclip_bbox.x2 = cclip_bbox.x2 * AR_FACTOR + AR_FRACBITS;
      cclip_bbox.y2 = cclip_bbox.y2 * AR_FACTOR + AR_FRACBITS;
      fl_setflat( ( USERVALUE )( 0.5 * AR_FACTOR * fcache_flatness(page))) ;
    } else {
      fl_setflat( fcache_flatness(page)) ;
    }
  } else {
    if (charcontext->inverse) {
      /* There's nothing sensible we can do if generating an inverse and the
       * character is not cached; abort now. */
      return TRUE;
    }

    /* Not caching; apply writing mode/sidebearing translations to current
       position. */
    HQASSERT(!IdlomOldSwCache,
             "Old cache hit, but not caching this time") ;

    charcontext->cachelevel = CacheLevel_Uncached ;
    /* Do not change context's cptr, we may be drawing an uncached char into
       a parent cached char. */

    accurate = 0 ; /* Don't use accurate renderer if not caching */

    fl_setflat(fcache_flatness(page)) ;

    /* FontMatrix translation is treated as an offset */
    if ( theFontType(*fontInfo) != FONTTYPE_4 ) {
      HQASSERT(currpt != NULL, "No currentpoint") ;
      xoff = theX(thePoint(*currpt)) +
                     theFontMatrix( *fontInfo ).matrix[ 2 ][ 0 ] ;
      yoff = theY(thePoint(*currpt)) +
                     theFontMatrix( *fontInfo ).matrix[ 2 ][ 1 ] ;
    } else {
      xoff = thegsPageCTM(*gstateptr).matrix[ 2 ][ 0 ] ;
      yoff = thegsPageCTM(*gstateptr).matrix[ 2 ][ 1 ] ;
    }

    /* I believe this calculation is the same for type 4 fonts as for type 1 */
    if ( (theWMode( *fontInfo ) & 1) != 0 && (mflags & MF_VO) != 0 ) {
      /* adjust for vertical writing origin */
      SYSTEMVALUE ax, ay ;
      MATRIX_TRANSFORM_DXY( metrics[ 8 ], metrics[ 9 ],
                            ax, ay,
                            & theFontMatrix( *fontInfo )) ;
      xoff -= ax ;
      yoff -= ay ;
    }
    if ( (mflags & MF_LL) != 0 ) {
      /* adjust for user-specified left-side bearing */
      SYSTEMVALUE ax, ay ;
      MATRIX_TRANSFORM_DXY( metrics[ 2 ], 0.0,
                            ax, ay,
                            & theFontMatrix( *fontInfo )) ;
      xoff += ax ;
      yoff += ay ;
    }
    if ( (mflags & MF_UR) != 0 ) {
      /* adjust for user-specified top-side bearing */
      SYSTEMVALUE ax, ay ;
      MATRIX_TRANSFORM_DXY( 0.0, metrics[ 3 ],
                            ax, ay,
                            & theFontMatrix( *fontInfo )) ;
      xoff += ax ;
      yoff += ay ;
    }

    /* Round the position to an integral position, so results are similar to
       caching. */
    SC_C2D_INTF(xoff, xoff) ;
    SC_C2D_INTF(yoff, yoff) ;
  }

  /* Create a single transform combining the font space to device space
     transform, the offset translation, and the accurate renderer adjustment,
     and transform the path by it. The translational components are
     overridden, to adjust the path to fit a minimal-sized character cache
     form. */
  MATRIX_COPY(&dmatrix, dtransform) ;
  MATRIX_20(&dmatrix) = xoff ;
  MATRIX_21(&dmatrix) = yoff ;

  if ( accurate ) {
    static OMATRIX amatrix = { AR_FACTOR, 0.0, 0.0, AR_FACTOR, 0.0, 0.0,
                               MATRIX_OPT_0011 } ;

    matrix_mult(&dmatrix, &amatrix, &dmatrix) ;
  }

  path_transform(cpath, &dmatrix) ;

  if ( showtype == DOCHARPATH ) {
    /* NOTE: This tests PaintType 1, which is an old, now undocumented form
       reserved for "stroked fonts". These were defined by a centreline and
       StrokeWidth. The original Adobe Courier font used PaintType 1.
       Don't need to free HDLT cache hits here, because charpath isn't
       cached. */
    if ( thePaintType(*fontInfo) == 1 && doStrokeIt(*thecharpaths) ) {
      set_font_stroke(context->page, &sparams, cpath, cpath) ;

      /* replace path with stroked path */
      if ( ! dostroke(&sparams, GSC_ILLEGAL, STROKE_NOT_VIGNETTE))
        return FALSE ;
    }

    /* Adding a charpath steals the path we've built, so no cleanup needed. */
    return add_charpath(cpath, FALSE) ;
  }

  if ( thePaintType(*fontInfo) != 0 )
    set_font_stroke(context->page, &sparams, cpath, NULL);

  /* Treatment of HDLT callbacks used to be inconsistent w.r.t. fill/stroke
     in character; stroked characters had DEVICE_CHAR set and clipmapid
     invalidated, filled characters did not. Now both are same as old fill
     (neither DEVICE_CHAR nor clipmapid invalid). */
  if ( isHDLTEnabled( *gstateptr )) {
    OMATRIX savedCTM, savedDevCTM, idlomAdjust ;
    SYSTEMVALUE savedTrans[2], savedPos[2];
    int32 hdltname ;

    idlom_saveAdjust_char_offset(&savedCTM, &savedDevCTM,
                                 savedTrans, savedPos,
                                 metrics, mflags, fontInfo,
                                 cptr, xoff, yoff) ;

    if (IDLOM_DOBEGINCHARACTER(NAME_undefined) == IB_PSErr) {
      idlom_restore_char_offset(&savedCTM, &savedDevCTM, savedTrans, savedPos) ;
      free_new_cache_restoring_old(context, cptr, charcontext);
      return FALSE ;
    }

    MATRIX_COPY(&idlomAdjust, &identity_matrix) ;
    if ( accurate ) {           /* factor out the accuracy "ballooning" */
      idlomAdjust.matrix[0][0] =
        idlomAdjust.matrix[1][1] = 1.0 / AR_FACTOR;
    }

    if ( thePaintType(*fontInfo) == 0 ) /* Filling character */
      hdltname = IDLOM_FILL(GSC_FILL, fontfillrule, cpath, &idlomAdjust) ;
    else
      hdltname = IDLOM_STROKE(GSC_FILL, &sparams, &idlomAdjust) ;

    idlom_restore_char_offset(&savedCTM, &savedDevCTM, savedTrans, savedPos) ;

    switch ( hdltname ) {
    case NAME_false:    /* PS error in IDLOM callbacks */
      free_new_cache_restoring_old(context, cptr, charcontext);
      return FALSE ;
    case NAME_Discard:  /* just pretending */
      restore_old_cachehit(context, cptr, charcontext);
      return TRUE;
    default:            /* only add, for now */
      EMPTY_STATEMENT() ;
    }
  }

  /* Re-use variable "blank" to mean we should not make marks */
  blank = CURRENT_DEVICE_SUPPRESSES_MARKS() ;

  /* If we have a previous cache hit, but are re-running the character just
     for HDLT/IDLOM's sake, then use the previous cache hit and avoid
     re-drawing the character. */
  if ( IdlomOldSwCache ) { /* Use previous cache hit */
    restore_old_cachehit(context, cptr, charcontext) ;
    return TRUE ;
  } else if ( thePaintType(*fontInfo) == 0 ) { /* Filling character */
    NFILLOBJECT *nfill;

    if ( cptr ) { /* Draw character into the cache */
      uint32 flags = NFILL_ISCHAR ;
      /* If doing a two-pass render, set up the NFILLOBJECT so that X and Y
         can be swapped. */
      if ( (accurate & SC_FLAG_TWOPASS) != 0 )
        flags |= NFILL_XYSWAP ;

      if ( make_nfill(page, thePath(*cpath), flags, &nfill) ) {
        if ( accurate ) {
          result = accfillnbressdisplay(page, fontfillrule, nfill,
                                        accurate);
        } else
          result = fillnbressdisplay(page, fontfillrule , nfill);
      }
    } else if ( !blank && !degenerateClipping ) {
      /* Draw to DL, or save for HDLT/IDLOM to do later */
      if ( make_nfill(page, thePath(*cpath), NFILL_ISCHAR, &nfill) ) {
        if ( (isHDLTEnabled( *gstateptr ) || hdltCharOutlineRequired) &&
             device_current_bressfill == add2dl_nfill ) {
          if ( nfill != NULL ) {
            IdlomDLSave.tag = IH_AddFill;
            IdlomDLSave.args.addnbress.rule = fontfillrule;
            IdlomDLSave.args.addnbress.nfill = nfill;
          }
          result = TRUE ;
        } else { /* Draw to DL */
          /** \todo @@@ TODO FIXME:
              There may be a missing DEVICE_SETG() for Type 4 fonts here. */
          result = DEVICE_BRESSFILL(page, fontfillrule, nfill);
        }
      }
    } else /* Suppress character */
      result = TRUE ;
  } else { /* Stroking character */
    if ( cptr ) { /* Stroke it into the cache */
      int32 savedcstate = CURRENT_DEVICE();

      SET_DEVICE(DEVICE_CHAR);
      result = dostroke_draw(&sparams);
      SET_DEVICE(savedcstate);
    } else if ( !blank && !degenerateClipping ) { /* Stroke character to DL */
      /** \todo @@@ TODO FIXME:
         There may be a missing DEVICE_SETG() for Type 4 fonts here. */
      result = dostroke_draw(&sparams);
    } else /* Suppress character */
      result = TRUE ;
  }

  /* FontType 4 calls start_scan_char, which will convert the form to RLE
     when it is finished. */
  if ( cptr && result && theFontType(*fontInfo) != FONTTYPE_4 ) {
    FORM *tform = theForm(*cptr) ;

    if (theFormT(*tform) == FORMTYPE_CACHEBITMAPTORLE)
      (void)form_to_rle(cptr, 0);
  }

  /* We have now either drawn the character directly, or created a cache form
     for it. If the latter, we defer blitting the form to plotchar(), to
     ensure that we are in the caller's context for recursive characters. */

  return result ;
}

/* ----------------------------------------------------------------------------
   function:            freethiscache(..)  author:              Andrew Cave
   creation date:       08-Mar-1989        last modification:   ##-###-####
   arguments:           gptr , (userfont) .
   description:

   This procedure frees the current cache store.

---------------------------------------------------------------------------- */
static void freethiscache(corecontext_t *context,
                          charcontext_t *charcontext, CHARCACHE *cptr)
{
  /* If we're in a BuildChar, we have to find the GST_SETCHARDEVICE gpush or
     the GST_GSAVE immediately after it and examine the fontinfo from that
     gstate; the user may have set a different font in the BuildChar
     procedure, and we would use the wrong caching information if we looked
     at the current font info. */
  fontcache_free_char(context->fontsparams,
                      find_fontinfo(charcontext, gstateptr), cptr) ;
}

/* ----------------------------------------------------------------------------
   function:            flushcache_()       author:              Andrew Cave
   creation date:       08-Mar-1989         last modification:   ##-###-####
   arguments:           value .
   description:

   This function removes (as much as possible from) the font cache.

---------------------------------------------------------------------------- */
Bool flushcache_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);

  if ( char_doing_cached() || no_purge != 0 )
    return push( & fnewobj , & operandstack ) ;

  font_caches_clear(context);
  /* Clear out the userpath cache while we're at it */
  purge_ucache(context);

  return push( & tnewobj , & operandstack ) ;
}

/* plot character; this does the work of finding the definition of the
   character from its selector, finding a cache form if possible,
   initialising and calling HDLT and PDF out, calling the low-level font
   machinery to get an outline, rendering the outline if necessary, and
   adding it to the display list if appropriate.

   A callback is used for notdef characters in some cases (CID fonts), where
   we may select an alternate font temporarily for the notdef character. */

Bool plotchar(char_selector_t *selector,
              int32 showtype, int32 charCount,
              Bool (*notdef_fn)(char_selector_t *selector,
                                int32 showtype, int32 charCount,
                                FVECTOR *advance,
                                void *notdef_data),
              void *notdef_data,
              FVECTOR *advance,
              CHAR_OPTIONS options)
{
  corecontext_t *context = get_core_context_interp() ;
  int32 modtype ;
  Bool result = FALSE ;
  int32 gid = GS_INVALID_GID ;
  CHARCACHE *cptr = NULL ;

  Bool swCacheHit = FALSE;     /* boolean flag for SW cached hit/miss */
  Bool pdfout_was_enabled = FALSE; /* Test if pdfout was enabled beforehand */
  int32 pdfout_chartext = 0 ;       /* Allow text ops on charpaths */
  int32 pdfout_context = PDFOUT_INVALID_CONTEXT; /* ID of current PDF context */
  LINELIST *currpt = CurrentPoint ;
  charcontext_t *ocharcontext, lcharcontext ;
  int32 idlomCacheHit;          /* from IDLOM begin, enum idlomBeginRules */
  int32 idlomId = 0;            /* IDLOM cache ID for the character */
  int8 oldIdlomState;           /* used to disable IDLOM during non-shows */
  heldAddToDL cachedDLSave = { 0 }; /* latch global IdlomDLSave in rec. fonts */
  HDLTinfo savedHDLT = { 0 } ;  /* save HDLT info */
  CHARCACHE *oldGlobalSwCache = IdlomOldSwCache; /* save, for nested fonts */
  OMATRIX *fontMatrix = NULL ;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  FONTinfo cidfontinfo, *savedfontinfo = NULL ;
  int32 fonttype = theFontType(*fontInfo);
  font_methods_t *fontfns ;
  render_state_t char_rs ;
  render_forms_t char_forms ;
  blit_chain_t char_blits ;
  Bool xps = ((showtype & DOXPS) == DOXPS) ;

  /* This is not nice; save a copy of the global variable so we can restore it
  if we decide to change it (this function can go recursive). */
  Bool savedHdltCharOutlineRequired = hdltCharOutlineRequired;

  showtype = showtype & ~DOXPS ;
  ocharcontext = context->charcontext ;

  HQASSERT(selector->cid >= 0 || selector->name != NULL,
           "Selector should have CID and/or name") ;
  HQASSERT(selector->cid >= 0 || selector->cid == NO_CHARCODE,
           "Symbolic used by IDLOM has changed: 'no glyphchar code' isn't -1");

  fontcache_check_limits(context->fontsparams);
  /* If we're in a recursive font and are doing a charpath or stringwidth,
     then we need to do the same operation for all sub-shows. */
  modtype = showtype ;
  if ( showtype != DOSTRINGWIDTH && showtype != DOCHARPATH )
    if ( ocharcontext != NULL && ocharcontext->modtype == DOCHARPATH )
      modtype = DOCHARPATH ;

  /* Type 32 fonts just return an empty path to charpath. However, they
     should set the advance width, so we'll change the show type to
     stringwidth. */
  if ( modtype == DOCHARPATH && fonttype == CIDFONTTYPE4 )
    modtype = DOSTRINGWIDTH ;

  oldIdlomState = theIdlomState( *gstateptr ) ;
  if ( modtype == DOSHOW || modtype == DOTYPE4SHOW ) {
    HQASSERT(currpt != NULL, "No current point") ;
    if ( oldIdlomState != HDLT_DISABLED ) {
      cachedDLSave = IdlomDLSave ;
      savedHDLT = gstateptr->theHDLTinfo ;
      idlom_char_init(currpt, charCount, fontInfo) ;
    }
  } else {
    /* for non-shows, don't want IDLOM callbacks */
    theIdlomState(*gstateptr) = HDLT_DISABLED;
  }

  /* From here on, call plotchar_quit instead of returning directly to
     restore the character context properly. If the return was just false,
     rather than a specific error, invalidfont will be raised. */
#define return DO_NOT_return_-_GO_TO_plotchar_quit_INSTEAD!
  error_clear_newerror_context(context->error);

  /* Initially we set this to false for the duration of this function; it will
  be enabled as required depending on the HDLT state. */
  hdltCharOutlineRequired = FALSE ;

  context->charcontext = &lcharcontext ;

  /* Initialise local character context; if in recursive show, and caching
     ancestor, retain the cache form. Uncached Type 3 characters will
     draw directly into this form. Cached Type 3 or other types will
     create their own form, which will be blitted into this form. */
#if defined( DEBUG_BUILD ) ||  defined( ASSERT_BUILD )
  HqMemSet8((uint8 *)&char_rs, 0xff, sizeof(char_rs)) ;
  HqMemSet8((uint8 *)&char_forms, 0xff, sizeof(render_forms_t)) ;
#endif

  /* Inherit some settings from the old character context. */
  if ( ocharcontext ) {
    lcharcontext.cptr = ocharcontext->cptr ;
    lcharcontext.buildchar = ocharcontext->buildchar ;
    lcharcontext.rb = ocharcontext->rb ;
  } else {
    lcharcontext.cptr = NULL ;
    lcharcontext.buildchar = FALSE ;
    lcharcontext.rb = NULL ;
  }

  lcharcontext.xwidth = 0.0 ; /* Make metrics problems obvious */
  lcharcontext.ywidth = 0.0 ;
  lcharcontext.showtype = CAST_TO_UINT8(showtype) ;
  lcharcontext.modtype = CAST_TO_UINT8(modtype) ;
  lcharcontext.cachelevel = CacheLevel_Unset ; /* Cache for *this* char? */
  lcharcontext.buildthischar = FALSE ;         /* Buildchar for *this* char? */
  lcharcontext.bracketed = FALSE ;             /* Bracketed *this* char? */
  lcharcontext.inverse = CAST_TO_UINT8(options & CHAR_INVERSE) ;
  lcharcontext.chartype = CHAR_Undefined ;
  lcharcontext.glyphchar = -1 ;
  lcharcontext.methods = NULL ;
  lcharcontext.rs = &char_rs ;
  char_rs.forms = &char_forms ;
  char_rs.ri.rb.clipform = &char_forms.clippingform ;
  char_rs.ri.rb.blits = &char_blits ;
  /* Leave charcontext->rb at value inherited from parent until we've decided
     if we're caching this character individually. */

  /* Initialise the cache key and definition to null, just in case of
     errors. */
  lcharcontext.glyphname =
    lcharcontext.definition = onull ; /* Struct copy to set slot properties */

  /* Now do the minimum amount of work to find out whether the character is
     already cached. If we haven't unpacked the font details, we cannot have
     a lookup matrix set, and therefore there is no way we can find the
     character. We should not look in the cache for CID Font Type 0/0C,
     because the sub-font selection concatenates another font matrix, so the
     current one is incomplete. */

  /* If we don't have a valid lookup matrix, there is no way we can
     possibly find the character in the cache. Unpack the font details,
     find a cache key and look for it a bit harder. */
  if ( !gotFontMatrix(*fontInfo) ) {
    HQASSERT(theLookupMatrix(*fontInfo) == NULL,
             "Lookup matrix set but font matrix wasn't unpacked") ;
    if ( !set_font() || !set_matrix() )
      goto plotchar_quit;
    gotFontMatrix(*fontInfo) = TRUE;
  }

  /* We want to use the font methods for this font, regardless of what happens
     in callbacks for Type 3 characters (which may reset the font). */
  fontfns = fontInfo->fontfns ;
  HQASSERT(fontfns, "No font functions") ;

  /* Set font matrix for HDLT. This will be overridden by the lookup font
     matrix if the character is found in the font cache. */
  fontMatrix = &theFontMatrix(*fontInfo) ;

  /* Set the cache key for this font. We need to have a valid font unpacked
     when we do this, as indicated by gotFontMatrix being true, because
     the cache key method may lookup the font's Encoding array. */
  if ( !(*fontfns->cache_key)(fontInfo, selector, &lcharcontext) )
    goto plotchar_quit ;

  HQASSERT(oType(lcharcontext.glyphname) != ONULL,
           "Cache key lookup failed to provide a good key") ;

  /* Lookup the character in the cache, if we don't have any further subfont
     navigation to do. */
  if ( fontfns->select_subfont == NULL )
    if ( theLookupFont(*fontInfo) || fontcache_lookup_fid(fontInfo) )
      if ( theLookupMatrix(*fontInfo) || fontcache_lookup_matrix(fontInfo) )
        if ( modtype != DOCHARPATH && modtype != DOTYPE4SHOW )
          if ( (cptr = fontcache_lookup_char(fontInfo, &lcharcontext.glyphname)) != NULL ) {
            swCacheHit = TRUE;
            fontMatrix = fontcache_current_matrix(fontInfo) ;
          }

 plotchar_recache: /* Label for HDLT to ask for character definition again */

  if ( isHDLTEnabled( *gstateptr )) { /* do IDLOM begin callback if it's on */
    HQASSERT(gid == GS_INVALID_GID, "HDLT recache, but bracket_plot done") ;

    /* Note that once bracket_plot() has been called, the 'fontInfo' pointer no
    longer points to the FONTinfo in the gstate. */
    if ( !bracket_plot(&lcharcontext, &gid) )
      goto plotchar_quit ;

    /* Only update IdlomDLSave when HDLT is enabled */
    IdlomDLSave.xtrans = fontMatrix->matrix[ 2 ][ 0 ] ;
    IdlomDLSave.ytrans = fontMatrix->matrix[ 2 ][ 1 ] ;
    if ( ! idlom_begin_cb(lcharcontext.glyphchar, &lcharcontext.glyphname,
                          fontInfo, &idlomId, &idlomCacheHit) )
      goto plotchar_quit;
  } else
    idlomCacheHit = IB_CacheHit;        /* fake it to have least impact */

  /* Remember old status of pdfout because pdfout_charcached may disable it.
   */
  /** \todo
     When glyph replacement is implemented for PDF Out, the char cache will be
     augmented with a flag to say whether a procedural definition was used
     for the glyph. This flag will be tested by pdfout_charcached to determine
     whether a character (and a Type 3 replacement container for the original
     font) should be created. */
  pdfout_was_enabled = pdfout_enabled() ;

  if ( pdfout_was_enabled &&
       !pdfout_charcached(context->pdfout_h, (modtype == DOCHARPATH),
                          &pdfout_context, &pdfout_chartext) )
    goto plotchar_quit ;

  /* If any of the caches didn't hit, we may need to run the character
     definition again. But first, prepare the character lookup and subfont
     selection, so that we can check for cached CID Type 0/0C fonts. */
  if ( !swCacheHit ||
       idlomCacheHit != IB_CacheHit ||
       pdfout_context != PDFOUT_INVALID_CONTEXT ) {
    /* Lookup the character definition, determining the type if possible. */
    if ( !(*fontfns->lookup_char)(fontInfo, &lcharcontext) )
      goto plotchar_quit ;

    switch ( fonttype ) {
    case CIDFONTTYPE0:  /* Type 1 charstrings based */
    case CIDFONTTYPE0C: /* Type 1 or Type 2 charstrings CFF based */
    case CIDFONTTYPE1:  /* BuildGlyph procedure based */
    case CIDFONTTYPE2:  /* TrueType based */
    case CIDFONTTYPE4:  /* Type 32 bitmap font */
    case CIDFONTTYPEPFIN: /* PFIN CID font */
      if ( lcharcontext.chartype == CHAR_Undefined ) {
        /* If the character does not exist, we have to run the notdef
           mappings. We can only run the notdef mapping if the CID is not 0,
           which is undefined by definition. */
        if ( selector->cid != 0 ) {
          /* The notdef mapping may switch to a different CMap subfont, so we
             have to save the state before switching to the alternate font.
             If HDLT has already forced a gsave, the corresponding grestore
             will be good enough. */
          if ( gid == GS_INVALID_GID ) {
            *(savedfontinfo = &cidfontinfo) = *fontInfo;
          } else {
            /* bracket_plot does a gsave GST_SETCHARDEVICE, which clears the
               current path rather than copy it (since there is normally a
               newpath immediately). In this case we need to retain the
               currentpoint, because the notdef mapping can call a recursive
               plotchar, which will need to current point set. */
            HQASSERT(CurrentPoint == NULL,
                     "Current point valid after bracket_plot") ;
            if ( currpt &&
                 !path_moveto(theX(thePoint(*currpt)), theY(thePoint(*currpt)),
                              MYMOVETO, &thePathInfo(*gstateptr)) )
              goto plotchar_quit ;
          }

          result = (*notdef_fn)(selector, showtype, charCount,
                                advance, notdef_data) ;
        }

        goto plotchar_quit ;
      }

      if ( fontfns->select_subfont != NULL ) {
        /* Save state before switching to subfont, if subfonts are present.
           This save has to happen in all subfont cases, because
           set_cid_subfont() alters the font info.
           NOTE: When HDLT is active, a gsave will have been performed, but
           this does not protect 'fontInfo', as that directly references the
           gstate at the time this function was called. */
        *(savedfontinfo = &cidfontinfo) = *fontInfo;

        if ( !(*fontfns->select_subfont)(fontInfo, &lcharcontext) )
          goto plotchar_quit ;

        /* If fontInfo does not point to the current FONTinfo in the gstate
        (because HDLT caused a gsave), update the transform in the gstate. */
        if ( gid != GS_INVALID_GID )
          gstateptr->theFONTinfo.fontmatrix = fontInfo->fontmatrix;
      }

      break;

    case DLD1_CASE:     /* DLD (Hqn disc Type 1 charstrings) */
    case FONTTYPE_1:    /* Type 1 */
    case FONTTYPE_CFF:  /* Type 2 (CFF with Type 2 charstrings) */
    case FONTTYPE_3:    /* Type 3 (BuildChar/BuildGlyph) */
    case FONTTYPE_4:    /* Type 4 (CCRun/eCCRun/MPSRun disc Type 1 charstrings) */
    case FONTTYPE_TT:   /* TrueType */
    case FONTTYPE_PFIN: /* PFIN */

      HQASSERT(fontfns->select_subfont == NULL,
               "Base font should not have subfont selector") ;
      break ;

    case FONTTYPE_0:
      HQFAIL("Type 0 composite font should already be dealt with.");
      /* FALLTHROUGH */
    default: /* Unknown font type */
      goto plotchar_quit ;
    }
  }

  /* If we haven't found the character already, and we haven't tried to
     because we didn't have the subfont set up properly, try it now. */
  if ( !swCacheHit ) {
    if ( fontfns->select_subfont != NULL )
      if ( theLookupFont(*fontInfo) || fontcache_lookup_fid(fontInfo) )
        if ( theLookupMatrix(*fontInfo) || fontcache_lookup_matrix(fontInfo) )
          if ( modtype != DOCHARPATH && modtype != DOTYPE4SHOW )
            if ( (cptr = fontcache_lookup_char(fontInfo, &lcharcontext.glyphname)) != NULL )
              swCacheHit = TRUE;
  }

  /* If any cache missed, we need to draw the shape... In any case,
     this code fork is where the result of the function is determined. */
  if ( !swCacheHit ||
       idlomCacheHit != IB_CacheHit ||
       pdfout_context != PDFOUT_INVALID_CONTEXT ) {
    int32 idlomHitSwMiss;    /* flag to disable IDLOM if already got cache */

    /* This may need to change the gstate to draw the character so we should do
     * a DEVICE_SETG afterwards even if the caller believes that no change to
     * gstate has occurred since plotchar was last called.
     */
    options &= ~ CHAR_NO_SETG ;

    /* ...but IDLOM might need disabling */
    if ( isHDLTEnabled( *gstateptr ) && idlomCacheHit == IB_CacheHit ) {
      /* Disable HDLT since this character has already been sent to the
      character target; however, we do want to see the outline if it cannot
      be cached (in char_draw()), so we set the special hack value to true. */
      theIdlomState(*gstateptr) = HDLT_DISABLED;
      hdltCharOutlineRequired = TRUE;
      idlomHitSwMiss = TRUE;
    } else
      idlomHitSwMiss = FALSE;

    /* Set static IdlomOldSwCache to the old cache hit. char_draw uses this
       to determine whether to re-run the character description when an HDLT
       outline is requested for an already-cached char. The local variable
       oldGlobalSwCache is used as well, to handle nested fonts, so that the
       old cache of the outer font and the old cache for the inner one are
       tracked independently.  */
    IdlomOldSwCache = cptr;     /* set aside any SW cache-hit; we'll restore */
    cptr = NULL;                /* both of them later.... */

    HQASSERT(lcharcontext.chartype != CHAR_Undefined &&
             lcharcontext.chartype != CHAR_Undecided,
             "Character should not be undefined or undecided font lookups") ;

    /* Do NOT return out of the switch without calling corresponding end */
    if ( !(*fontfns->begin_char)(fontInfo, &lcharcontext) )
      goto plotchar_quit ;

    HQASSERT(!result, "Result decided prematurely") ;

    switch ( lcharcontext.chartype ) { /* Switch by charstring type */
    case CHAR_Type1:
    case CHAR_Type2:
      if ( isEncrypted(*fontInfo) ) {
        result = adobe_cache_encrypted(context, &lcharcontext,
                                       isEncrypted(*fontInfo),
                                       currpt, xps) ;
      } else {
        result = adobe_cache(context, &lcharcontext,
                             isEncrypted(*fontInfo),
                             currpt, xps) ;
      }

      break ; /* Out of switch */

    case CHAR_TrueType:
      /* The 'charstring' after lookup from the CharStrings is actually the
         integer offset into the fonts 'loca' (glyph offsets) table. */
      result = tt_cache(context, &lcharcontext, currpt, xps) ;

      break ; /* Out of switch */

    case CHAR_PFIN:
      result = pfin_cache(context, &lcharcontext, currpt, gid) ;
      break ;

    case CHAR_BuildChar:
      /* BuildChar/BuildGlyph or Glyph replacement procedure */

      /* Bracket character by a "b"racketed gsave, if not already done. Note
         that we don't reset fontInfo here, because we need to call the
         end_char method with the same fontInfo as we started with. */
      if ( gid == GS_INVALID_GID )
        result = bracket_plot(&lcharcontext, &gid) ;
      else
        result = TRUE ;

      if ( result ) {
        /* Note that we are inside a BuildChar (Type 3/4) character. Various
           routines use this to restrict vignette detection, application of
           imposition matrices, etc. */
        lcharcontext.buildchar = TRUE ;
        lcharcontext.buildthischar = TRUE ;

        result = start_scan_char(context, &lcharcontext, currpt) ;
      }

      break ;

    case CHAR_Bitmap:
      /* Type 32 characters are implemented using the gs_setcachedevice and
         gs_imagemask internal routines, so we need to pretend they are
         the same as BuildChar characters. */
      lcharcontext.buildchar = TRUE ;
      lcharcontext.buildthischar = TRUE ;
      result = t32_plot(context, &lcharcontext, currpt, gid) ;

      break ;

    default:
      HQFAIL("Unrecognised charstring type") ;
      break ;
    }

    (*fontfns->end_char)(fontInfo, &lcharcontext) ;

    /* Got a cache form? */
    if ( lcharcontext.cachelevel == CacheLevel_Cached ) {
      cptr = lcharcontext.cptr ;
      HQASSERT(cptr != NULL, "Claim to have cached, but no entry") ;

      probe_end(SW_TRACE_FONT_CACHE, (intptr_t)cptr) ;

      if ( !result ) { /* Caching failed; remove cache entry */
        freethiscache(context, &lcharcontext, cptr) ;
        lcharcontext.cachelevel = CacheLevel_Error ;
        lcharcontext.cptr = NULL ;
      }
    } else if ( lcharcontext.cachelevel == CacheLevel_Found ) {
      cptr = lcharcontext.cptr ;
      HQASSERT(cptr != NULL, "Claim to have found cache, but no entry") ;
    }

    if ( IdlomOldSwCache && cptr != IdlomOldSwCache ) {
      /* go back to old cache entry */
      if ( lcharcontext.cachelevel != CacheLevel_Found ) {
        freethiscache(context, &lcharcontext, cptr) ;
        /* We had found a cache before, use this value to indicate we're not
           caching this char again. */
        lcharcontext.cachelevel = CacheLevel_Found ;
        lcharcontext.cptr = NULL ;
      }

      cptr = IdlomOldSwCache;
      IdlomOldSwCache = NULL;
    }

    if (idlomHitSwMiss) {       /* re-enable IDLOM now that SW's re-cached */
      theIdlomState( *gstateptr ) = oldIdlomState;
      /* We'll restore 'hdltCharOutlineRequired' at plotchar_quit. */
    } else {
      HQASSERT(! isHDLTEnabled( *gstateptr ) ||
               gstateptr->theHDLTinfo.proxy == IT_NoTarget ||
               gstateptr->theHDLTinfo.frameBegun != IB_MarkedNotBegun,
               "IDLOM char. frame is finished, but hasn't been begun!");

      /* proxy can be different from character for several different reasons,
         distinguishable by the value that FrameBegun was set to.  If
         FrameBegun was unset (set to 0), then char is not to be cached. */
      if (gstateptr->theHDLTinfo.proxy != IT_Character)
        idlomCacheHit = gstateptr->theHDLTinfo.frameBegun ?
          gstateptr->theHDLTinfo.frameBegun : IB_NoCache;

      /** \todo @@@ TODO FIXME ajcd 2005-10-18: It would be useful to have
          xwidth, ywidth available here. */
      if (!IDLOM_ENDCHARACTER(result, &idlomId))
        result = FALSE; /* PS error in callbacks */
    }

    if ( !result )
      goto plotchar_quit ;
  } else /* Caches hit, must have been successful */
    result = TRUE ;

  if ( cptr ) {
    advance->x = theCharXWidth(*cptr) ;
    advance->y = theCharYWidth(*cptr) ;
  } else {
    advance->x = lcharcontext.xwidth ;
    advance->y = lcharcontext.ywidth ;
  }

  /* Re-enable PDF Out if it was disabled, since we're about to use the
     character. */
  if ( pdfout_was_enabled ) {
    result = pdfout_endchar(&context->pdfout_h, &pdfout_context, result) ;
    pdfout_was_enabled = FALSE ; /* Don't call pdfout_endchar later */
  }

  if ( gid != GS_INVALID_GID ) {
    if ( !gs_cleargstates(gid, GST_SETCHARDEVICE, NULL) ) {
      gid = GS_INVALID_GID;
      result = FALSE ;
      goto plotchar_quit;
    }
    gid = GS_INVALID_GID;               /* reset it, to not re-clear later */
  }

  /* Do idlom callback if needed */
  if ( isHDLTEnabled( *gstateptr ) && idlomCacheHit != IB_NoCache ) {
    int32 retcode = idlom_char_cb(lcharcontext.glyphchar,
                                  &lcharcontext.glyphname,
                                  idlomId, &result) ;
    if ( retcode == RC_exit )
      goto plotchar_quit;
    else if ( retcode == RC_recache ) {
      /* If HDLT is enabled, then there was a bracket_plot, and so the base
         fontinfo was not saved separately. The gsave was restored above. */
      HQASSERT(gid == GS_INVALID_GID, "HDLT recache, but still bracketed") ;
      HQASSERT(savedfontinfo == NULL, "HDLT recache, but font info saved") ;
      result = FALSE ;
      goto plotchar_recache;
    }
  }

  /* Do PDF out callback if needed. */
  if ( pdfout_enabled() ) {
    /* PDF Out may be a called in a recursive character context. In this case,
       we can't afford to screw up the show we're in, so bracket the calls
       with gsave/grestore */
    HQASSERT(gid == GS_INVALID_GID,
             "Haven't cleared previous bracket_plot properly") ;

    /* We should have restored any gsaves above. So the gotFontMatrix
       condition should always be true...this code has rotted a bit, but it
       isn't worth fixing until after DIDL. */
    HQASSERT(&theFontInfo(*gstateptr) == fontInfo,
             "Font info different from expected") ;

    if ( ! gotFontMatrix(theFontInfo(*gstateptr)) ) {
      if ( !bracket_plot(&lcharcontext, &gid) || !set_font() || !set_matrix() ) {
        result = FALSE ;
        goto plotchar_quit;
      }
      gotFontMatrix(theFontInfo(*gstateptr)) = TRUE ;
    }

    if ( showtype == DOSHOW ) {
      if ( ! pdfout_outputchar( context->pdfout_h ,
                                selector ,
                                theX(thePoint(*currpt)),
                                theY(thePoint(*currpt)),
                                advance->x,
                                advance->y) ) {
        result = FALSE ;
        goto plotchar_quit ;
      }
    } else if ( showtype == DOCHARPATH ) {
      if ( ! pdfout_recordchar( context->pdfout_h ,
                                selector ,
                                theX(thePoint(*currpt)),
                                theY(thePoint(*currpt)),
                                advance->x,
                                advance->y,
                                pdfout_chartext) ) {
        result = FALSE ;
        goto plotchar_quit ;
      }
    }

    if ( gid != GS_INVALID_GID ) {
      if ( !gs_cleargstates(gid, GST_SETCHARDEVICE, NULL) ) {
        gid = GS_INVALID_GID;
        result = FALSE ;
        goto plotchar_quit;
      }
      gid = GS_INVALID_GID;               /* reset it, to not re-clear later */
    }
  }

  HQASSERT(&theFontInfo(*gstateptr) == fontInfo,
           "Font info different from expected") ;

  /* Restore previous character context's cache pointer. This must happen
     before bitblt_char, to get the current character output form correct. It
     may be repeated again when cleaning up, but the information is the same,
     so it doesn't matter. */
  context->charcontext = ocharcontext ;

  /*\todo Should we also be doing a setg here under some circumstances?
   */
  if ( cptr && modtype != DOSTRINGWIDTH ) {
    HQASSERT(CurrentPoint == currpt, "Current point changed");
    HQASSERT(modtype != DOCHARPATH,
             "Shouldn't be blitting cache for charpath") ;

    result = bitblt_char(context, cptr, currpt, options) ;
  } else if ( (options & CHAR_SETG_BLANK) != 0 && (options & CHAR_NO_SETG) == 0 ) {
    /* Not doing bitblt_char, but still need to do a setg because the next chars
       may assume it. */
    if ( !DEVICE_SETG(context->page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
      result = FALSE ;
      goto plotchar_quit;
    }
  }

  if ( result && isHDLTEnabled( *gstateptr ))
    result = idlom_finish_dl(context->page);

 plotchar_quit:             /* label for error/early return */
  if ( pdfout_was_enabled )
    result = pdfout_endchar(&context->pdfout_h, &pdfout_context, result) ;

  IdlomOldSwCache = oldGlobalSwCache; /* restore it, going "out of scope" */
  hdltCharOutlineRequired = savedHdltCharOutlineRequired;

  /* Restore previous character context. This may be a second time if the
     character succeeded, but it doesn't matter; the information is the
     same. */
  context->charcontext = ocharcontext ;

  if ( gid != GS_INVALID_GID )
    if ( !gs_cleargstates(gid, GST_SETCHARDEVICE, NULL) )
      result = FALSE ;

  if ( savedfontinfo ) /* Restore CIDFont into the gstate */
    theFontInfo(*gstateptr) = *savedfontinfo;  /* structure copy */

  /* Restore IdlomDLSave after a show (which HDLT knew about) */
  if ( modtype == DOSHOW || modtype == DOTYPE4SHOW ) {
    if ( oldIdlomState != HDLT_DISABLED ) {
      gstateptr->theHDLTinfo = savedHDLT ;
      IdlomDLSave = cachedDLSave ;
    }
  }

  /* done with callbacks, so restore */
  theIdlomState( *gstateptr ) = oldIdlomState;

  if ( !result && !newerror )
    result = error_handler(INVALIDFONT) ;

#undef return
  return result ;
}

/** Standard CDevProc operator; mimics Adobe's standard operator, added to
   CID fonts if no CDevProc exists. Adobe's standard CDevProc appears to do
   the equivalent of the following, scaled appropriately for the font matrix:

   { pop pop pop pop 2 div 0 -1000 3 -1 roll 880 }

   (i.e. the v0 vector is half the previous wmode 1 width and .88 of the em
   height, the wmode 1 width is a vertical em. The wmode 1 width defaults to
   the wmode 0 width if none is supplied.) We work out the scaling by using
   the inverse of the FontCompositeMatrix premultiplied by the scale factor,
   giving the scaling from unit space to original font space. */
Bool stdCDevProc_(ps_context_t *pscontext)
{
  SYSTEMVALUE metrics[10] ;
  OBJECT *theo ;
  OMATRIX ifcmatrix ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  pop(&operandstack) ;

  if ( !stack_get_numeric(&operandstack, metrics, 10) )
    return FALSE ;

  if ( !matrix_inverse(&theFontCompositeMatrix(theFontInfo(*gstateptr)),
                       &ifcmatrix) )
    return error_handler(UNDEFINEDRESULT) ;

  matrix_mult(&theFontInfo(*gstateptr).scalematrix, &ifcmatrix, &ifcmatrix) ;

  MATRIX_TRANSFORM_DXY(0.0, 0.880, metrics[8], metrics[9], &ifcmatrix) ;

  /* V0y */
  theo = theTop(operandstack) ;
  object_store_numeric(theo, metrics[9]) ;

  /* V0x */
  theo = stackindex(1, &operandstack) ;
  object_store_numeric(theo, metrics[6] * 0.5);

  MATRIX_TRANSFORM_DXY(0.0, -1.0, metrics[6], metrics[7], &ifcmatrix) ;

  /* W1y */
  theo = stackindex(2, &operandstack) ;
  object_store_numeric(theo, metrics[7]);

  /* W1x */
  theo = stackindex(3, &operandstack) ;
  object_store_numeric(theo, metrics[6]);

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* Utility routines for font methods, to lookup font cache keys. CID font
   cache key just uses CID as an integer. Base font cache key looks up the
   charcode in the encoding vector. */
Bool fontcache_cid_key(FONTinfo *fontInfo,
                       char_selector_t *selector,
                       charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(selector, "No character selector") ;
  HQASSERT(context, "No character context") ;

  HQASSERT(selector->cid >= 0 && selector->name == NULL,
           "Selector doesn't contain CID in CID font") ;

  theTags(context->glyphname) = OINTEGER | LITERAL ;
  oInteger(context->glyphname) = context->glyphchar = selector->cid ;

  return TRUE ;
}

Bool fontcache_base_key(FONTinfo *fontInfo,
                        char_selector_t *selector,
                        charcontext_t *context)
{
  HQASSERT(selector, "No character selector") ;
  HQASSERT(context, "No character context") ;

  HQASSERT(!FONT_IS_CID(theFontType(*fontInfo)),
           "Base font key requested in CID font") ;

  /* Start by filling in selector's name and charcode if not known */
  if ( selector->name == NULL ) {   /* Charcode to name mapping */
    if (selector->cid > 255 && fontInfo->fonttype == FONTTYPE_PFIN) {
      theTags(context->glyphname) = OINTEGER | LITERAL ;
      oInteger(context->glyphname) = context->glyphchar = selector->cid ;
      return TRUE ;
    }
    if ( !get_cdef(fontInfo, selector->cid, &selector->name) )
      return FALSE ;
  } else if ( selector->cid < 0 && fontInfo->fonttype != FONTTYPE_3 ) { /* Name to charcode mapping */
    /* No errors here, because we might be in glyphshow for an unencoded
       character */
    (void)get_edef(fontInfo, selector->name, &selector->cid) ;
  }

  /* Cache key is the name of the character */
  if (selector->name) {
    theTags(context->glyphname) = ONAME | LITERAL ;
    oName(context->glyphname) = selector->name ;
  } else {
    /* Special PDF CID/CFF handling [12764] */
    theTags(context->glyphname) = OINTEGER | LITERAL ;
    oInteger(context->glyphname) = selector->cid ;
  }

  /* Use charcode or CID for HDLT code */
  context->glyphchar = selector->cid ;

  return TRUE ;
}

USERVALUE fcache_flatness(DL_STATE *page)
{
  USERVALUE flat ;

  /* at 300dpi, 0.3, 72dpi small, 1000dpi 1.0 */
  flat = ( USERVALUE )(( page->xdpi + page->ydpi ) / 2000.0 ) ;
  if ( flat > 1.0f ) return 1.0f ;
  if ( flat < 0.5f ) return 0.5f ;
  return flat ;
}

void init_C_globals_fcache(void)
{
  hdltCharOutlineRequired = FALSE;
  IdlomOldSwCache = NULL ;
  IdlomDLSave.tag = IH_Nothing ;
}

/*
Log stripped */
