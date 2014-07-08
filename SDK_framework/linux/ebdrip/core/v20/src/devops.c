/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:devops.c(EBDSDK_P.1) $
 *
 * Copyright (c) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Operators for setpagedevice et al.
 */

#include "core.h"
#include "devops.h"
#include "coreinit.h"

#include "clipops.h"            /* impositionclipping */
#include "constant.h"           /* EPSILON */
#include "control.h"            /* interpreter */
#include "dev_if.h"             /* SET_PGB_PARAM_I */
#include "devices.h"            /* find_device */
#include "dicthash.h"           /* CopyDictionary */
#include "display.h"            /* displaylistisempty */
#include "execops.h"            /* setup_pending_exec */
#include "genhook.h"            /* MAX_HOOK */
#include "groupPrivate.h"       /* groupResetColorInfo */
#include "gs_color.h"           /* gsc_invokeChainSingle */
#include "gschead.h"            /* gsc_setcolorspace */
#include "gschtone.h"           /* gsc_getSpotno */
#include "gscparams.h"          /* gsc_disableOverprint */
#include "gscxfer.h"            /* gsc_analyze_for_forcepositive */
#include "gstate.h"             /* apply_pagebasematrix_to_all_gstates */
#include "gu_ctm.h"             /* gs_setdefaultctm */
#include "gu_chan.h"            /* GUCR_* */
#include "gu_fills.h"
#include "halftone.h"           /* ht_noErase */
#include "color.h"              /* ht_colorIsClear */
#include "hqmemcpy.h"           /* HqMemMove */
#include "idlom.h"              /* IDLOM_ENDPAGE */
#include "intscrty.h"           /* maxResolution */
#include "jobmetrics.h"         /* populate_dl_transparency_metrics_hashtable() */
#include "mathfunc.h"           /* myatan2 */
#include "miscops.h"            /* run_ps_string */
#include "mmcompat.h"           /* mm_alloc_with_header */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "often.h"              /* SwOftenUnsafe */
#include "params.h"             /* SystemParams */
#include "plotops.h"            /* getLateColorState */
#include "psvm.h"               /* workingsave */
#include "rcbcntrl.h"           /* rcbn_enabled,rcb_resync_dlptrs */
#include "rectops.h"            /* rectfill_ */
#include "dlstate.h"            /* DLSTATE */
#include "renderom.h"           /* omit_blank_separations */
#include "routedev.h"           /* DEVICE_BAND */
#include "security.h"           /* DongleMaxResolution */
#include "swerrors.h"           /* TYPECHECK */
#include "swmemory.h"           /* gs_cleargstates */
#include "spdetect.h"           /* isable_separation_detection */
#include "swpdfout.h"           /* PDFOutHandle */
#include "system.h"             /* gs_freecliprec */
#include "trap.h"               /* trapSetTrappingEngine */
#include "utils.h"              /* is_matrix */
#include "vndetect.h"           /* flush_vignette */
#include "bandtable.h"          /* determine_band_size */
#include "ripmulti.h"           /* NUM_THREADS */
#include "pixelLabels.h"        /* RENDERING_PROPERTY_* */
#include "timerapi.h"           /* hqn_timer_create * */
#include "pgbproxy.h"           /* pgbproxy_reset */
#include "hqatomic.h"           /* hq_atomic_counter_t */
#include "swpdfin.h"            /* pdf_resetpagedevice */
#include "swdataimpl.h"         /* swdatum_from_object */
#include "irr.h"                /* irr_finish */

#define MESS_WITH_CURRENT_DEVICE() MACRO_START \
  corecontext_t *context = ps_core_context(pscontext) ; \
  HQASSERT(IS_INTERPRETER(), "Can only mess with page in interpreter") ; \
  dl_clear_page(context->page) ; \
/* Turn off a few (arbitary) miscellaneous bits-n-pieces. */ \
  context->page->xdpi = context->page->ydpi = 1.0 ; \
  doing_imposition = FALSE ; \
/* Same things as pagedevice_ but to rather useless values. */ \
  gs_freecliprec(&gstateptr->thePDEVinfo.initcliprec) ; \
  thegsDeviceW( *gstateptr ) = 1 ; /* So clippath not completely empty. */ \
  thegsDeviceH( *gstateptr ) = 1 ; \
  ( void )newpagedevice(context->page, 1, 1, 1, 1) ; \
  (void)dl_begin_page(context->page) ; \
  ( void )gs_setdefaultctm( & identity_matrix , TRUE ) ; \
  ( void )initclip_(pscontext) ; \
MACRO_END

#define randomiseInRIP buildOpChain

Bool doing_imposition = FALSE;
Bool doing_mirrorprint = FALSE;

#define RESOLUTION_LIMIT 9000.0

int32 showno = 0;
int32 bandid = 0;


int32  pageBaseMatrixId = PAGEBASEID_INITIAL ;
OMATRIX pageBaseMatrix = { 1.0 , 0.0 , 0.0 , 1.0 , 0.0 , 0.0 , MATRIX_OPT_0011 } ;

uint32 securityArray[8] = { 0 };

int rendering_in_progress;

/* static function prototypes */
static uint32 randomiseInRIP(uint32 val);
static Bool newpagedevice(DL_STATE *page, int32 w, int32 h,
                          int32 ResamplingFactor, int32 BandHeight);
static int32 get_numcopies( void );
static Bool runEraseColorProc(DL_STATE *page, GSTATE *gs, Bool knockout);


/* local variables */

Bool ea_check_result = FALSE;
static Bool doing_security = FALSE;
static uint32 lastcode = CODEINITIALISER;
static uint32 security = 0 ;

static int32 showpage_count = 0; /* the number of calls to showpage (not
                                    copypage) during the life of the current
                                    device - see RB2 p252 */
static int32 doing_setpagedevice = 0;
static Bool deferred_setpagedevice = FALSE;
static Bool resolutionExceeded = FALSE ;

/* ---------------------------------------------------------------------- */
/* static uint32  Rb =  (0x80000000 & 0); huh? */
static uint32 Rb = 0;
static uint32 randomiseInRIP(uint32 val)
{
  uint32 Rc;

  Rc = (val >> 1) | Rb;
  Rb = (val & 1) << 31;
  Rc = Rc ^ (val << 12);
  val = Rc ^ (Rc >> 20);

  return val ;
}


/* ---------------------------------------------------------------------- */
static Bool gfPseudoErasepage = FALSE ;

Bool is_pseudo_erasepage( void )
{
  return gfPseudoErasepage ;
}

static Bool gs_pseudo_erasepage(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;
  Bool fResult;

  /* Get the current erase color */
  fResult = gs_applyEraseColor(context, FALSE, NULL, NULL, FALSE);

  /* Rebuild dl and update insertion points */
  if ( fResult && rcbn_enabled())
    fResult = rcb_resync_dlptrs(page) ;

  if ( fResult ) {
    /* Got the current erase color is in dlc_currentcolor - compare with dl erase */

    /* Check for empty display list and the same erase color */
    if ( !displaylistisempty(page) ||
         !dlc_equal(&page->dlc_erase, dlc_currentcolor(page->dlc_context)) ) {
      /* dl had something on it or new erase color - fill the page with the erase color */

      /* Save current path etc ready for upcoming pseudo erase fill */
      fResult = gsave_(pscontext);
      if ( fResult ) {
        /* gsave worked - setup clip path for pseudo erase fill */

        /* So we don't think we're a color page and recombining */
        disable_separation_detection() ;
        rcbn_disable_interception(gstateptr->colorInfo) ;

        /* Set up clipping path */
        fResult = initclip_(pscontext) && clippath_(pscontext);

        if ( fResult ) {
          /* Flag doing pseudo erasepage so fill doesn't think whole page erase:
             in essence prevents recursion */
          gfPseudoErasepage = TRUE;

          /* Do fill with no vignette detection, and with FILL_NOT_ERASE
             turned on; this also prevents recursion even though this is the
             actual erase. It would be nice to get rid of gfPseudoErasepage
             and the is_pseudo_erasepage function, but they are used in setg()
             as well as dofill() and dorectfill(). A new setg() option could
             be used to do this, but there may be weirdness if HDLT replaces
             the pseudo-erase. */
          dl_currentexflags |= RENDER_PSEUDOERASE;
          /* Clipping path defined - define fill with no copypath */
          fResult = runEraseColorProc(page, gstateptr, FALSE) &&
                    dofill(&thePathInfo(*gstateptr), NZFILL_TYPE,
                           GSC_FILL, FILL_NOT_VIGNETTE|FILL_NOT_ERASE);
          dl_currentexflags &= (~RENDER_PSEUDOERASE);

          gfPseudoErasepage = FALSE;
        }

        rcbn_enable_interception(gstateptr->colorInfo) ;

        /* Since we managed the gsave we must attempt the grestore */
        fResult = grestore_(pscontext) && fResult;

        /* Reset recombine and color detection state to what they were */
        enable_separation_detection() ;

        /* Finish off the dl as required */
        fResult = fResult && dlskip_pseudo_erasepage(page);
      }
    }
  }

  return fResult ;
}

/* ---------------------------------------------------------------------- */
static Bool setpagedevice_internal(ps_context_t *context, OBJECT *newpagedict) ;

/** Establish a new page device, merging the request dictionary with
    the existing page device dictionary, and erasing the current page.

    The stack contains the request dictionary to merge with the current page
    device dictionary.

    The call chain for the setpagedevice operator needs to be understood in
    order to maintain code.

    \code
    setpagedevice_() calls
      setpagedevice_internal(), which calls
        /setpagedevice procedure in internaldict, which calls
          true resetpagedevice to create the internal pagedevice state,
          false resetpagedevice after running the Install hook
          initgraphics_()
          erasepage_()
          beginpage_(), which calls
            do_pagedevice_reactivate(), which calls
              do_beginpage() to call BeginPage and capture imposition clipping

          resetpagedevice in internaldict calls
            pagedevice_(), which calls
              reset_pagedevice() to unpack the dict into globals, which calls
                newpagedevice() to set the band sizes

    gs_setgstate() also calls
      call_resetpagedevice() which calls
        false resetpagedevice in internaldict to re-load the pagedevice dict
    \endcode

    The setpagedevice procedure in internaldict performs the merging of the
    existing pagedevice dictionary and the request dictionary. The
    resetpagedevice procedure in internaldict sets the pagebuffer parameters
    for the new dictionary.
*/
Bool setpagedevice_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  int32 gid = GS_INVALID_GID ;
  Bool result = TRUE ;

  int32 override ;
  OBJECT *theo ;
  OBJECT *tmpo ;
  OBJECT newpagedict = OBJECT_NOTVM_NOTHING ;
  int32 old_err = 0 ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

#if 0
  /** \todo @@@ TODO FIXME ajcd 2002-12-30: Disallow setpagedevice inside a
     recursive HDL context. This may not be the best solution to the problem,
     but we cannot easily trash the group structure. We cannot easily
     re-build the structure from the information present at this time
     either. */
  if ( self->currentHdl && hdlParent(self->currentHdl) != NULL )
    return error_handler(UNDEFINED) ;
#endif

  theo = theTop( operandstack ) ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  /* Don't allow setpagedevice during an image interpretation. */
  /** \todo ajcd 2011-03-22: Should probably extend this to pattern, char,
      etc., like gs_erasepage() does. It's hard to test what Adobe do,
      because Distiller (8.2.6.262) crashes on this test, and we don't want
      to replicate that. */
  if ( context->page->im_imagesleft > 0 )
    return error_handler(UNDEFINEDRESULT) ;

  Copy( & newpagedict , theo ) ;
  pop( & operandstack ) ;

  if ( oType( thegsDevicePageDict( *gstateptr )) != ODICTIONARY ) {
    /* First time: simply install the dictionary in the graphics state */
    Copy( & thegsDevicePageDict( *gstateptr ) , & newpagedict ) ;
    SET_DICT_ACCESS( & newpagedict , READ_ONLY ) ;
    return TRUE ;
  }

  /* Check if the pagedevice is to override. */
  override = -1 ;
  tmpo = fast_extract_hash_name( & thegsDevicePageDict( *gstateptr ) ,
                                 NAME_Override ) ;
  if ( tmpo != NULL && oType( *tmpo ) == OINTEGER ) {
    override = oInteger( *tmpo ) ;
    tmpo = fast_extract_hash_name( theo , NAME_Override ) ;
    if ( tmpo == NULL ||
         oType( *tmpo ) != OINTEGER ||
         oInteger( *tmpo ) <= override ) {
      /* Yes - override (operand already popped) */
      return initgraphics_(pscontext) && erasepage_(pscontext) ;
    }
  }

  if ( context->savelevel > 0 ) {
    /* Note this gs_gpush essentially means that we now always save the dictionary. */
    if ( ! gs_gpush( GST_SETPAGEDEVICE ))
      return FALSE ;
    gid = gstackptr->gId ;
  }

  ++doing_setpagedevice ;
  deferred_setpagedevice = FALSE ;
  result = setpagedevice_internal(pscontext, &newpagedict) ;
  deferred_setpagedevice = FALSE ;
  --doing_setpagedevice ;

  if ( !result ) /* Put operand back on stack. */
    (void)push(&newpagedict, &operandstack) ;

  if ( context->savelevel > 0 ) {
    if ( result && gid == gstackptr->gId ) {
      /* Can just throw away the one on the gstackptr and return as all's done... */
      gs_gpop() ;
      return TRUE ;
    } else {
      /* The setpagedevice failed. In order to restore the previous device we
         have to ensure that what's there thinks it is a different device, so
         increment the device's number to fool gs_cleargstates into closing
         off the part done one, and do_pagedevice_reactivate the old one. */
      error_context_t *errcontext = context->error;

      if ( error_signalled_context(errcontext) )
        error_save_context(errcontext, &old_err);
      thegsDeviceBandId( * gstateptr ) = thegsDeviceBandId( * gstackptr ) + 1 ;
      result = (gs_cleargstates(gid, GST_SETPAGEDEVICE, NULL) &&
                do_pagedevice_reactivate(pscontext, NULL)) ;

      HQASSERT( result , "Failed to reinstate old pagedevice after new one failed, I'm confused" ) ;
      if ( old_err )
        error_restore_context(errcontext, old_err);
      return !old_err;
    }
  }
  return result ;
}


static int32 security_check = 1 ;
static int32 security_index = 0 ;
static Bool setpagedevice_internal(ps_context_t *pscontext, OBJECT *newpagedict)
{
  corecontext_t *context = ps_core_context(pscontext);
  static int32 subordinate_dictionaries[] = {
    NAME_InputAttributes,
    NAME_OutputAttributes,
    NAME_Policies,
    NAME_ExtraPageDeviceKeys,
    NAME_ExtraMediaSelectionKeys,
    NAME_SeparationDetails,
    NAME_StartJob,
    NAME_EndJob,
    NAME_StartPainting,
    NAME_StartPartialRender,
    NAME_StartRender,
    NAME_EndRender,
    NAME_StartImage,
    NAME_EndImage,
    NAME_StartVignette,
    NAME_EndVignette,
    NAME_Private,
    NAME_PDFOutDetails
  };

  Bool result ;
  uint32 i ;
  OBJECT * tmpo;

  {
    deactivate_pagedevice_t dpd_res ;
    deactivate_pagedevice( gstateptr, NULL, newpagedict, &dpd_res ) ;
    deferred_setpagedevice = (dpd_res.action == PAGEDEVICE_DEFERRED_DEACTIVATE) ;
    /* Need to actually call setpagedevice in internaldict since some apps
     * (Illustrator 6.0 notably) do naughty things with /NumCopies 0.
     * If we don't accept the page device when deferring, then not only
     * does it not get setup correctly, but we don't pick up the new value
     * of /NumCopies when it goes from 0 to 1 or vica-versa.
     * Setting deferred_setpagedevice causes pagedevice & beginpage to be noops.
     */

    if ( ! deferred_setpagedevice ) {
      if ( ! do_pagedevice_deactivate( & dpd_res ))
        return FALSE ;
    }
  }

  /* during setpagedevice, the pagedevice dictionary is writable */
  SET_DICT_ACCESS(& thegsDevicePageDict (*gstateptr), UNLIMITED);

  /* lazy copy for page device dictionary; we need to copy the top level and
     InputAttributes, OutputAttributes, Policies and others; even though these
     may not be present in the incoming dictionary they may change as a result
     of querying the device state. The page device dictionary is subject to
     gsave/grestore. */
  if ( context->savelevel > 0 ) {
    OBJECT newd = OBJECT_NOTVM_NOTHING ;
    Bool ok ;
    int32 length ;

    getDictLength(length, &thegsDevicePageDict(*gstateptr));

    ok = (ps_dictionary(&newd, length) &&
          CopyDictionary(&thegsDevicePageDict(*gstateptr), &newd, NULL, NULL)) ;

    /* the original is read only again */
    SET_DICT_ACCESS(&thegsDevicePageDict(*gstateptr), READ_ONLY);

    if ( !ok )
      return FALSE ;

    /* We've done a top-level copy, now we need to duplicate the subordinate
       dictionaries which we copied into the new dictionary. */
    for (i = 0; i < NUM_ARRAY_ITEMS(subordinate_dictionaries) ; i++) {
      oName(nnewobj) = system_names + subordinate_dictionaries[i];
      tmpo = fast_extract_hash(&newd, & nnewobj);
      if (tmpo && oType(*tmpo) == ODICTIONARY) {
        OBJECT subd = OBJECT_NOTVM_NOTHING ;
        if (! ps_dictionary(&subd, theLen(*tmpo)) ||
            ! CopyDictionary(tmpo, &subd, NULL, NULL) )
          return FALSE ;

        /* Set the sub-dictionary's access to the same as the original. */
        SET_DICT_ACCESS(&subd, oAccess(*oDict(*tmpo))) ;
        if (! insert_hash(&newd, & nnewobj, &subd) )
          return FALSE ;
      }
    }

    /* Replace the page device dictionary with the new copy. This copy is
       still writable. */
    OCopy(thegsDevicePageDict(*gstateptr), newd);
  }

  /* find the corresponding PostScript procedure in internaldict and
     execute it */
  tmpo = fast_extract_hash_name(& internaldict , NAME_setpagedevice);
  if (! tmpo) {
    SET_DICT_ACCESS(&thegsDevicePageDict(*gstateptr), READ_ONLY);
    return error_handler( UNDEFINED );
  }

  result = interpreter_clean(tmpo, newpagedict, NULL) ;

  /* back to read only again */
  SET_DICT_ACCESS(& thegsDevicePageDict(*gstateptr), READ_ONLY);

  /* If the password for a chargeable revision of the RIP is wrong, or if the
   * password for a chargeable OS upgrade is wrong, then disable the RIP.
   */
  if ( !context->systemparams->RevisionPassword ||
       !context->systemparams->PlatformPassword ||
       resolutionExceeded ) {
    MESS_WITH_CURRENT_DEVICE() ;
  }

  {
    static uint8 security_checks[16] = {
      6, 13, 11,  4,  2,  9,  7,  1, 14,  5, 15, 12,  3, 10,  8, 16
    } ;
    if ((--security_check) == 0 ) {
      security_check = 2 * security_checks[ security_index ] ;
      if ((++security_index) == 16 )
        security_index = 0 ;
    }
    if (ea_check_result) {
      static uint8 *s = (uint8 *) "(tmp/spi) (w) file closefile";
      OBJECT anobject = OBJECT_NOTVM_NOTHING;

      security_check = 1 ;      /* So we always check next time round... */

      theTags( anobject ) = OSTRING | EXECUTABLE;
      theLen( anobject ) = (uint16) strlen((char *) s);
      oString( anobject ) = s;
      if (! push(&anobject, &executionstack))
        return FALSE;
      execStackSizeNotChanged = FALSE;

      MESS_WITH_CURRENT_DEVICE() ;
    }
  }

  return result ;
}

/* ====================================================================== */

typedef struct PageRange {
  OBJECT * poPageRange;
  uint32 unLength;
  uint32 unPageRangeElement;
  uint32 unCurrentPage;
  uint32 unLookAtPage;
  Bool fCurrentInclusion;
} PageRange;

static PageRange gPageRange;

static PageRange * hPageRange = NULL;

/* ---------------------------------------------------------------------- */

static Bool pageRangeStart(OBJECT * poPageRange, PageRange ** ppPageRange)
{
  uint32 unElement;
  int32 nPreviousPage;
  OBJECT * poSubArray;

  HQASSERT(poPageRange != NULL, "poPageRange unexpectedly NULL in pageRangeStart");
  HQASSERT(oType(*poPageRange) == ONULL ||
           oType(*poPageRange) == OARRAY ||
           oType(*poPageRange) == OPACKEDARRAY,
           "typecheck on poPageRange");

  /* The page range keeps track of the state for printing selective
     pages. It is presented with an array which contains elements that
     are either integers, for specific pages that should be included, or
     an array of two integers, which is a range of pages which should be
     included.

     For example, [ 3 [5 8] 12 ] means include pages 3,5,6,7,8 and
     12. All integers are strictly increasing, and pages nuber starting
     from 1. where each element is either an integer page number
     (numbering starting at 1) to be included, or an array of two
     integers representing a range of pages to be included
  */

  if ( oType(*poPageRange) == ONULL ) {
    gPageRange.poPageRange = NULL;
    * ppPageRange = NULL;
    return TRUE;
  }

  if ( oArray(*poPageRange) == gPageRange.poPageRange ) {
    /* if it is the same object repeated, ignore requests to replace it, so that we
       don't reset the page counting and state - mainly for PDF jobs and others which
       do setpagedevice calls for each page or group of pages. Note,
       gPageRange.poPageRange may be NULL, OLIST of poPageRange can never be */
    * ppPageRange = & gPageRange;
    return TRUE;
  }

  /* if the handle is allocated rather than global, reallocate here */

  gPageRange.poPageRange = oArray(*poPageRange);
  gPageRange.unLength = theLen(*poPageRange);
  gPageRange.unCurrentPage = 1; /* not a valid page number! */
  gPageRange.unLookAtPage = 1;
  gPageRange.unPageRangeElement = 0;
  gPageRange.fCurrentInclusion = TRUE;

  /* Check the array is well formed */

  nPreviousPage = 0;
  for (unElement = 0; unElement < gPageRange.unLength; unElement++) {
    switch ( oType(gPageRange.poPageRange[unElement]) ) {
    case OINTEGER:

      if ( oInteger(gPageRange.poPageRange[unElement]) <= nPreviousPage )
        return error_handler(RANGECHECK);

      nPreviousPage = oInteger(gPageRange.poPageRange[unElement]);
      break;

    case OARRAY:
    case OPACKEDARRAY:

      if ( theLen(gPageRange.poPageRange[unElement]) != 2 )
        return error_handler(RANGECHECK);

      poSubArray = oArray(gPageRange.poPageRange[unElement]);

      if ( oType(poSubArray[0]) != OINTEGER ||
           oType(poSubArray[1]) != OINTEGER )
        return error_handler(TYPECHECK);
      if ( oInteger(poSubArray[0]) <= nPreviousPage ||
           oInteger(poSubArray[0]) > oInteger(poSubArray[1]) )
        return error_handler(RANGECHECK);

      nPreviousPage = oInteger(poSubArray[1]);
      break;

    default:
      return error_handler(TYPECHECK);
    }
  }

  * ppPageRange = & gPageRange;
  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool pageRangeStep(PageRange * pPageRange,
                          Bool fAdvancePage,
                          Bool *pfMorePages)
{
  OBJECT * poElement;
  int32 nFrom, nTo;

  HQASSERT(pPageRange == NULL || /* NULL means no page range in force */
           pPageRange == & gPageRange,
           "pPageRange in stepPageRange invalid - only one context allowed at present");

  /* Possible outcomes: the function returns TRUE if the page about to start is to be
     produced. pfMorePages is FALSE if there are no more pages to be produced
     including this one, so will only be FALSE when the functuion returns FALSE */

  * pfMorePages = TRUE; /* until we learn otherwise */

  if (pPageRange == NULL)
    return TRUE;

  /* Only advance the page count on real showpage's - setpagedevice calls may be
     repeated, and because we don't reset when the page count object is the same, we
     don't want to count a page for this */

  if (fAdvancePage)
    pPageRange->unCurrentPage++;

  /* Last time we looked at the page range array, we noted where we might next have
     to change state. Have we got there yet? */

  if (pPageRange->unLookAtPage > pPageRange->unCurrentPage)
    return pPageRange->fCurrentInclusion;

  HQASSERT (pPageRange->unPageRangeElement <= pPageRange->unLength,
            "unPageRangeElement is too large");

  /* Perhaps we have gone off the end of the array of required pages (so there can be
     no more we want to print by definition) */

  if (pPageRange->unPageRangeElement == pPageRange->unLength) {
    /* no more pages */
    * pfMorePages = FALSE;
    return FALSE;
  }

  /* So what is the next page or pages to be included? See description of array
     above. To simplify things, we treat an individual page as a range to itself (eg
     4 is considered to be [4 4]). */

  poElement = & pPageRange->poPageRange[pPageRange->unPageRangeElement];

  HQASSERT (oType(*poElement) == OINTEGER ||
            oType(*poElement) == OARRAY ||
            oType(*poElement) == OPACKEDARRAY,
            "Incorrect type in pageRangeStep, despite earlier check");

  if (oType(*poElement) == OINTEGER) {

    nFrom = nTo = oInteger (*poElement);

  } else { /* an array */

    HQASSERT(theLen(*poElement) == 2 &&
             oType(oArray(*poElement)[0]) == OINTEGER &&
             oType(oArray(*poElement)[1]) == OINTEGER,
              "Incorrect subarray type in pageRangeStep despite earlier check");

    nFrom = oInteger(oArray(*poElement)[0]);
    nTo   = oInteger(oArray(*poElement)[1]);

  }

  /* We've already checked the page numbering is in sequence, so I'll just assert it here */

  HQASSERT (nFrom > 0 && nTo > 0 && nFrom <= nTo && nFrom >= (int32) pPageRange->unCurrentPage,
            "Pages out of sequence in pageRangeStep");

  if ((int32) pPageRange->unCurrentPage == nFrom) {

    /* We have a page, possibly the first of a range of pages, which we wish to
       include; advance to the next element. As we aren't going to look at it here,
       we are undecided about whether to include the next page after the range we do
       know about, so mark that as the next one to look at */

    pPageRange->unLookAtPage = (uint32) (nTo + 1);
    pPageRange->unPageRangeElement++;
    pPageRange->fCurrentInclusion = TRUE;
    return pPageRange->fCurrentInclusion;
  }

  /* The page we looked at is not in the range we just looked at, so there's no need
     to look at the array until we get to the the start of that range */

  pPageRange->unLookAtPage = (uint32) nFrom;
  pPageRange->fCurrentInclusion = FALSE;
  return pPageRange->fCurrentInclusion;

}

/* ====================================================================== */

/* ---------------------------------------------------------------------- */
Bool call_resetpagedevice(void)
{
  OBJECT *theo = fast_extract_hash_name(&internaldict, NAME_resetpagedevice);
  error_context_t *errcontext = get_core_context_interp()->error;
  int32 old_err = 0;
  Bool result;

  HQASSERT(theo, "Don't know how to reset page device") ;
  if ( theo ) {
    if ( !push(&fnewobj, &operandstack) ) /* Not initialising pagedevice */
      return FALSE ;

    if ( !push(theo, &executionstack) ) {
      pop(&operandstack) ;
      return FALSE ;
    }
    if ( error_signalled_context(errcontext) )
      error_save_context(errcontext, &old_err);
    result = interpreter(1, NULL);
    if ( old_err )
      error_restore_context(errcontext, old_err);
    if ( !result )
      return FALSE;

    showpage_count = 0 ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** \brief Reload pagedevice params, creating new target stack if necessary.

   This routine is called from the setpagedevice procedure in internaldict,
   and also also from gs_setgstate() via call_resetpagedevice() so that when
   a gstate with a different page device is restored we reset the page device
   state, copying appropriate variables from the page device dictionary into
   C.

   \param page The DL page we're resetting. This will be inputpage, but is
   a parameter to avoid TLS access, since the callers probably have it unpacked
   anyway.

   \param fInitializePageDevice TRUE if the target stack should be re-built,
   FALSE if reloading an existing page device. If FALSE, the existing target
   entries will be re-used. In any case, the imposition target default clip
   will be reset to be the same as its underlying target's default clip
   (disposing of any imposition clipping; this will be re-instated by
   do_beginpage() if imposition is true). Since the existing target stack is
   re-used if this flag is FALSE, we should be able to re-instate a
   previous pagedevice if the creation of a new pagedevice fails.

   \param w The width of the output page.

   \param h The height of the output page.

   \return TRUE if the function succeeded, FALSE otherwise. If the function
   succeeded, the target stack in the gstate will be consistent with the
   unpacked pagedevice parameters.
*/
static Bool reset_pagedevice(corecontext_t *context,
                             DL_STATE *page, Bool fInitializePageDevice,
                             int32 w, int32 h)
{
  enum {
    pdevmatch_HWResolution, pdevmatch_TrimPage, pdevmatch_Imposition,
    pdevmatch_Separations, pdevmatch_JobNumber, pdevmatch_MirrorPrint,
    pdevmatch_InsertSheet, pdevmatch_ProcessColorModel,
    pdevmatch_Preseparation, pdevmatch_ResamplingFactor, pdevmatch_Scaling,
    pdevmatch_BandHeight,
    pdevmatch_PDFOut, pdevmatch_PDFOutDetails, pdevmatch_SeparationDetails,
    pdevmatch_InterleavingStyle, pdevmatch_ValuesPerComponent,
    pdevmatch_CalibrationColorModel, pdevmatch_Private,
    pdevmatch_OmitHiddenFills,
    pdevmatch_TrappingDetails,
    pdevmatch_PageRange, pdevmatch_DefaultScreenAngles,
    pdevmatch_VirtualDeviceSpace,
    pdevmatch_BackdropAutoSeparations, pdevmatch_DeviceROP,
    pdevmatch_ObjectMap, pdevmatch_ScanConversion,
    pdevmatch_MaxBackdropWidth, pdevmatch_MaxBackdropHeight,
    pdevmatch_Halftone, pdevmatch_ObjectTypeMap,
    pdevmatch_HVDInternal,
    pdevmatch_dummy
  } ;

  static NAMETYPEMATCH pdevmatch[pdevmatch_dummy + 1] = {
    /* Use the enum above to index this match */
    { NAME_HWResolution, 2, { OARRAY, OPACKEDARRAY }},
    { NAME_TrimPage | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Imposition | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Separations | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_JobNumber | OOPTIONAL , 2 , { OINTEGER, ONULL }},
    { NAME_MirrorPrint | OOPTIONAL , 2 , { OBOOLEAN, ONULL }},
    { NAME_InsertSheet | OOPTIONAL, 2, { OBOOLEAN, ONULL }},
    { NAME_ProcessColorModel | OOPTIONAL, 2, { ONAME, OSTRING }},
    { NAME_Preseparation | OOPTIONAL, 2, { ODICTIONARY, ONULL }},
    { NAME_ResamplingFactor | OOPTIONAL , 2 , { OINTEGER, ONULL }},
    { NAME_Scaling | OOPTIONAL , 2 , { OARRAY, OPACKEDARRAY }},
    { NAME_BandHeight | OOPTIONAL , 2 , { OINTEGER, ONULL }},
    { NAME_PDFOut | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_PDFOutDetails | OOPTIONAL , 2 , { ODICTIONARY, ONULL }},
    { NAME_SeparationDetails , 1 , { ODICTIONARY }},
    { NAME_InterleavingStyle, 1, { OINTEGER }},
    { NAME_ValuesPerComponent, 1, { OINTEGER }},
    { NAME_CalibrationColorModel | OOPTIONAL, 3, { ONAME, OSTRING, ONULL }},
    { NAME_Private, 1, { ODICTIONARY }},
    { NAME_OmitHiddenFills, 1, { OBOOLEAN }},
    { NAME_TrappingDetails | OOPTIONAL , 1 , { ODICTIONARY }},
    { NAME_PageRange | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
    { NAME_DefaultScreenAngles | OOPTIONAL , 2 , { ODICTIONARY, ONULL }},
    { NAME_VirtualDeviceSpace, 2, { ONAME, ONULL }},
    { NAME_BackdropAutoSeparations | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_DeviceROP | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_ObjectMap | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_ScanConversion | OOPTIONAL, 1, { ONAME }},
    { NAME_MaxBackdropWidth | OOPTIONAL, 1, { OINTEGER }},
    { NAME_MaxBackdropHeight | OOPTIONAL, 1, { OINTEGER }},
    { NAME_Halftone, 1, { OBOOLEAN }},
    { NAME_ObjectTypeMap | OOPTIONAL, 3, { ONULL, OARRAY, OPACKEDARRAY }},
    { NAME_HVDInternal | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  /* Use a random multiplier that changes the bits in an FP number for security */
#define RES_MAGIC (0.1832)

  Bool rcbn_enable ;
  OBJECT *theo ;
  OBJECT *olist ;
  SYSTEMVALUE xscale = 1.0;
  SYSTEMVALUE yscale = 1.0;
  SYSTEMVALUE maxResolution = DongleMaxResolution();
  SYSTEMVALUE maxResolution2 = maxResolution * RES_MAGIC;
  Bool screening;
  int32 ResamplingFactor = 1 ;
  int32 BandHeight = 0 ;
  Bool result = FALSE ;

  resolutionExceeded = FALSE;

  if ( oType(thegsDevicePageDict(*gstateptr)) != ODICTIONARY )
    return TRUE ;

  /* now get the values out of the pagedevice dictionary */
  if (! dictmatch(&thegsDevicePageDict(*gstateptr), pdevmatch))
    return FALSE;

  rcbn_term();

  /* Destroy the page DL, but retain the job object, rasterstyle, and other
     pagedevice parameters. We're going to overwrite them in a minute. We
     have to be careful not to return between destroying the old display list
     and creating a new one, so we don't confuse the surface lifecycle. */
#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!
  dl_clear_page(page) ;

  if ( page->hr != NULL )
    guc_discardRasterStyle(&page->hr) ;

  /* HWResolution */
  theo = pdevmatch[pdevmatch_HWResolution].result;
  if ( theLen(*theo) < 2 ) {
    (void)error_handler(RANGECHECK);
    goto cleanup ;
  }
  olist = oArray(*theo);
  if ( !object_get_numeric(&olist[0], &page->xdpi) ||
       !object_get_numeric(&olist[1], &page->ydpi) )
    goto cleanup ;

  /* TrimPage */
  theo = pdevmatch[pdevmatch_TrimPage].result;
  trim_to_page = FALSE;
  if (theo != NULL && oType(*theo) == OBOOLEAN)
    trim_to_page = oBool(*theo);

  /* Imposition */
  theo = pdevmatch[pdevmatch_Imposition].result;
  doing_imposition = FALSE;
  if (theo != NULL && oType(*theo) == OBOOLEAN)
    doing_imposition = oBool(*theo);

  /* JobNumber */
  theo = pdevmatch[pdevmatch_JobNumber].result;
  if (theo && oType(*theo) == OINTEGER && oInteger(*theo) > 0)
    page->job_number = oInteger(*theo) ;

  /* Halftone */
  theo = pdevmatch[pdevmatch_Halftone].result ;
  screening = oBool(*theo) ;

  /* MirrorPrint */
  theo = pdevmatch[pdevmatch_MirrorPrint].result;
  doing_mirrorprint = FALSE ;
  if (theo != NULL && oType(*theo) == OBOOLEAN)
    doing_mirrorprint = oBool(*theo) ;

  /* InsertSheet not used at present */

  /* ProcessColorModel */
  /* at end, => guc_setupRasterStyle */

  /* Preseparation */
  theo = pdevmatch[pdevmatch_Preseparation].result;
  rcbn_enable = FALSE ;
  if ( theo != NULL ) {
    if ( oType( *theo ) != ONULL ) {
      rcbn_enable = TRUE ;
    }
  }

  /* ResamplingFactor */
  theo = pdevmatch[pdevmatch_ResamplingFactor].result;
  if (theo && oType(*theo) == OINTEGER) {
    ResamplingFactor = oInteger( *theo ) ;
    if ( ResamplingFactor <= 0 ) {
      (void)detail_error_handler(CONFIGURATIONERROR, "ResamplingFactor pagedevice key invalid") ;
      goto cleanup;
    }
  }

  /* Scaling */
  theo = pdevmatch[pdevmatch_Scaling].result;
  if ( theo != NULL ) {
    if ( theLen(*theo) != 2 ) {
      (void)error_handler(RANGECHECK) ;
      goto cleanup ;
    }
    olist = oArray(*theo);
    if ( !object_get_numeric(&olist[0], &xscale) ||
         !object_get_numeric(&olist[1], &yscale) ) {
      (void)detail_error_handler(CONFIGURATIONERROR, "Scaling pagedevice key invalid") ;
      goto cleanup ;
    }
  }

  /* A check for the dongle resolution limit being exceeded bearing in mind any
   * scaling and anti-aliasing that might be applied. 'resolutionExceeded' is used
   * in an unfriendly error, should someone crack the friendly error below.
   * Also bearing in mind that resolution > 9000 means unlimited resolution
   * Use maxResolution2 to make it slightly harder to crack.
   */
  /* Don't use as an assert condition the following:
   * maxResolution * RES_MAGIC == maxResolution2
   * because the assert fires on Linux for reasons explained in Request 61880
   */
  HQASSERT( fabs( maxResolution2 - maxResolution * RES_MAGIC) < 0.000000001,
            "dongle resolution limit checks awry");
  if ( maxResolution <= RESOLUTION_LIMIT ) {
    if ( page->xdpi * RES_MAGIC > maxResolution2 ||
         page->ydpi * RES_MAGIC > maxResolution2 )
      resolutionExceeded = TRUE;
  }

  /* BandHeight */
  theo = pdevmatch[pdevmatch_BandHeight].result;
  if ( theo != NULL && oType(*theo) == OINTEGER ) {
    BandHeight = oInteger(*theo);
  }

  /* TrappingDetails */
  theo = pdevmatch[pdevmatch_TrappingDetails].result ;
  if ( theo ) {
    enum {
      tdmatch_Type, tdmatch_TrappingOrder, tdmatch_ColorantDetails,
      tdmatch_HqnType, tdmatch_HqnSuppressSmallTextTrapping,
      tdmatch_HqnSmallTextThreshold , tdmatch_HqnEffort,
      tdmatch_dummy
    } ;
    static NAMETYPEMATCH tdmatch[tdmatch_dummy + 1] = {
      /* Use the enum above to index */
      { NAME_Type | OOPTIONAL , 1 , { OINTEGER }} ,
      { NAME_TrappingOrder | OOPTIONAL , 2 , { OARRAY , OPACKEDARRAY }} ,
      { NAME_ColorantDetails | OOPTIONAL , 1 , { ODICTIONARY }} ,
      { NAME_HqnType | OOPTIONAL , 1 , { ONAME }} ,
      { NAME_HqnSuppressSmallTextTrapping | OOPTIONAL , 1 , { OBOOLEAN }} , /* STTS */
      { NAME_HqnSmallTextThreshold | OOPTIONAL , 2 , { OINTEGER , OREAL }} , /* STTS */
      { NAME_HqnEffort | OOPTIONAL , 1 , { OINTEGER }} ,
      DUMMY_END_MATCH
    } ;

    if ( ! dictmatch( theo , tdmatch ))
      goto cleanup ;

    /* HqnType */
    theo = tdmatch[tdmatch_HqnType].result ;
    if ( theo ) {
      /* Allowed values are  TrapPro and TrapProLite */
      switch ( oNameNumber(*theo) ) {
      case NAME_TrapPro:
      case NAME_TrapProLite:
        break ;

      default:
        (void)namedinfo_error_handler(RANGECHECK, NAME_HqnType, theo) ;
        goto cleanup ;
      }
    }

    /* HqnEffort */
    page->trap_effort = 0 ;
    theo = tdmatch[tdmatch_HqnEffort].result ;
    if ( theo )
      page->trap_effort = oInteger( *theo) ;
  }

  /* PageRange (the PostScript one not the pdfexec one) */
  if (! pageRangeStart(pdevmatch[pdevmatch_PageRange].result, & hPageRange))
    goto cleanup;

  /* VirtualDeviceSpace */
  theo = pdevmatch[pdevmatch_VirtualDeviceSpace].result;
  if (theo != NULL) {
    int32 dummy;

    switch (oNameNumber(*theo)) {
    case NAME_DeviceGray:
    case NAME_DeviceRGB:
    case NAME_DeviceCMYK:
      break;
    default:
      /* Invalid space name. */
      (void)detail_error_handler(CONFIGURATIONERROR,
                                 "VirtualDeviceSpace pagedevice key invalid") ;
      goto cleanup ;
    }

    if (!gsc_getcolorspacesizeandtype(gstateptr->colorInfo, theo,
                                      &page->virtualDeviceSpace, &dummy))
      goto cleanup;
  }

  /* BackdropAutoSeparations */
  page->backdropAutoSeparations = TRUE;
  theo = pdevmatch[pdevmatch_BackdropAutoSeparations].result;
  if (theo != NULL)
    page->backdropAutoSeparations = oBool(*theo);

  /* DeviceROP */
  page->deviceROP = TRUE;
  theo = pdevmatch[pdevmatch_DeviceROP].result;
  if (theo != NULL)
    page->deviceROP = oBool(*theo);

  /* OmitHiddenFills */
  page->fOmitHiddenFills = FALSE ;
  theo = pdevmatch[pdevmatch_OmitHiddenFills].result;
  if ( theo != NULL && oBool(*theo) )
    page->fOmitHiddenFills = TRUE ;

  /* ObjectMap */
  page->output_object_map = FALSE;
  theo = pdevmatch[pdevmatch_ObjectMap].result;
  if ( theo != NULL )
    page->output_object_map = oBool(*theo);

  /* ScanConversion */
  gstateptr->thePDEVinfo.scanconversion = SC_RULE_HARLEQUIN ;
  theo = pdevmatch[pdevmatch_ScanConversion].result ;
  if ( theo ) {
    if ( !scanconvert_from_name(theo, 0 /*disallow nothing*/,
                                &gstateptr->thePDEVinfo.scanconversion) ) {
      (void)error_handler(RANGECHECK) ;
      goto cleanup ;
    }
  }
  page->ScanConversion = gstateptr->thePDEVinfo.scanconversion ;

  /* MaxBackdropWidth */
  page->max_region_width = 0;
  theo = pdevmatch[pdevmatch_MaxBackdropWidth].result;
  if ( theo != NULL ) {
    if ( oInteger(*theo) < 0 ) {
      (void)detail_error_handler(RANGECHECK, "MaxBackdropWidth too small") ;
      goto cleanup ;
    }
    page->max_region_width = oInteger(*theo);
  }

  /* MaxBackdropHeight */
  page->max_region_height = 0;
  theo = pdevmatch[pdevmatch_MaxBackdropHeight].result;
  if ( theo != NULL ) {
    if ( oInteger(*theo) < 0 ) {
      (void)detail_error_handler(RANGECHECK, "MaxBackdropHeight too small") ;
      goto cleanup ;
    }

    page->max_region_height = oInteger(*theo);
  }

  /* HVDInternal */
  page->irr.generating = FALSE;
  theo = pdevmatch[pdevmatch_HVDInternal].result;
  if (theo != NULL)
    page->irr.generating = oBool(*theo);
  irr_pgb_remove(page);

  /* Private */
  if (fInitializePageDevice) {
    /* Private, for Colorants, ReservedColorants, ColorChannels and
       SeparationOrder (the private version which includes all
       colorants, none implied)

       SeparationDetails, for SeparationStyle, MinExtraSpotColorants,
       MaxExtraSpotColorants, SeparationOrdering and ColorantFamily */

    enum {
      privatematch_Colorants, privatematch_ReservedColorants,
      privatematch_ColorChannels, privatematch_SeparationOrder,
      privatematch_NumProcessColorants, privatematch_CustomConversions,
      privatematch_sRGB, privatematch_ProcessColorants,
      privatematch_ColorantPresence, privatematch_ProcessColorantBlack,
      privatematch_dummy
    } ;
    static NAMETYPEMATCH privatematch[privatematch_dummy + 1] = {
      /* Use the enum above to index */
      { NAME_Colorants, 1, { ODICTIONARY }},
      { NAME_ReservedColorants, 1, { ODICTIONARY }},
      { NAME_ColorChannels, 2, { OARRAY, OPACKEDARRAY }},
      { NAME_SeparationOrder, 2, { OARRAY, OPACKEDARRAY }},
      { NAME_NumProcessColorants, 1, { OINTEGER }},
      { NAME_CustomConversions, 2, { OARRAY, OPACKEDARRAY }},
      { NAME_sRGB, 1, { ODICTIONARY }},
      { NAME_ProcessColorants, 2, { OARRAY, OPACKEDARRAY }},
      { NAME_ColorantPresence, 1, { ODICTIONARY }},
      { NAME_ProcessColorantBlack | OOPTIONAL, 3, { ONULL, ONAME, OSTRING }},
      DUMMY_END_MATCH
    };

    enum {
      detailsmatch_SeparationStyle, detailsmatch_MinExtraSpotColorants,
      detailsmatch_MaxExtraSpotColorants, detailsmatch_SeparationOrdering,
      detailsmatch_dummy
    } ;
    static NAMETYPEMATCH detailsmatch[detailsmatch_dummy + 1] = {
      /* Use the enum above to index */
      { NAME_SeparationStyle, 1, { OINTEGER }},
      { NAME_MinExtraSpotColorants, 1, { OINTEGER }},
      { NAME_MaxExtraSpotColorants, 1, { OINTEGER }},
      { NAME_SeparationOrdering, 1, { OINTEGER }},
      DUMMY_END_MATCH
    };

    GUCR_RASTERSTYLE *hrNew;
    OBJECT colorantDetails = OBJECT_NOTVM_NULL ;

    if ( !dictmatch(pdevmatch[pdevmatch_Private].result, privatematch) ||
         !dictmatch(pdevmatch[pdevmatch_SeparationDetails].result,
                    detailsmatch) )
      goto cleanup;

    /* Grab the ColorantDetails dictionary from TrappingDetails, if present. */

    if ( (theo = pdevmatch[pdevmatch_TrappingDetails].result) != NULL ) {
      HQASSERT(oType(*theo) == ODICTIONARY,
               "TrappingDetails is not a dictionary") ;

      theo = fast_extract_hash_name( theo , NAME_ColorantDetails ) ;

      if ( theo )
        Copy( & colorantDetails , theo ) ;
    }

    if (! guc_setupRasterStyle(
            screening,
            pdevmatch[pdevmatch_ProcessColorModel].result,
            pdevmatch[pdevmatch_CalibrationColorModel].result,
            pdevmatch[pdevmatch_InterleavingStyle].result,
            pdevmatch[pdevmatch_ValuesPerComponent].result,
            detailsmatch[detailsmatch_SeparationStyle].result,
            privatematch[privatematch_NumProcessColorants].result,
            pdevmatch[pdevmatch_SeparationDetails].result,
            privatematch[privatematch_SeparationOrder].result,
            privatematch[privatematch_ColorChannels].result,
            privatematch[privatematch_Colorants].result,
            privatematch[privatematch_ReservedColorants].result,
            pdevmatch[pdevmatch_DefaultScreenAngles].result,
            privatematch[privatematch_CustomConversions].result,
            privatematch[privatematch_sRGB].result,
            privatematch[privatematch_ProcessColorants].result,
            privatematch[privatematch_ColorantPresence].result,
            privatematch[privatematch_ProcessColorantBlack].result,
            & colorantDetails,
            pdevmatch[pdevmatch_ObjectTypeMap].result,
            & hrNew) )
      goto cleanup;

    gsc_replaceRasterStyle(gstateptr->colorInfo, hrNew);

    /* gsc_replaceRasterStyle() will invalidate color chains, but the ChainCache
     * remains active because new raster styles as a result transparent makes it
     * useful. When called from setpagedevice, the ChainCache *may* still be
     * useful, but we won't take the risk.
     */
    gsc_markChainsInvalid(gstateptr->colorInfo) ;

    if ( !trapZoneReset(page, FALSE) )
      goto cleanup ;

    pgbproxy_reset(page) ;

    /** \todo ajcd 2011-03-04: 12145:
        target_change(new target) */
  } /** \todo ajcd 2011-03-04: 12145:
        else { target_activate(gstateptr->target) } */

  /* We may just have reset the rasterstyle, so pick up the new value. We do
     it in any case, because we may have been called from gs_setgstate()
     through call_resetpagedevice() because of a pagedevice reactivation
     around a (g)restore, and we want to clear the page's rasterstyle when
     deactivating the old pagedevice. */
  HQASSERT(page->hr == NULL, "Leaking rasterstyle handle") ;
  page->hr = gsc_getRS(gstateptr->colorInfo) ;
  guc_reserveRasterStyle(page->hr) ;

  /** \todo ajcd 2011-03-04: 12145:
      target activation would have called newpagedevice() here. */

  if ( rcbn_enable ) {
    if ( ! rcbn_start())
      goto cleanup ;
  }

  if (!gsc_pagedevice(context, page,
                      gstateptr->colorInfo,
                      &thegsDevicePageDict(*gstateptr)))
    goto cleanup;

  /* PDFOut */
  theo = pdevmatch[pdevmatch_PDFOut].result;
  if ( theo != NULL ) {
    if ( ! setDistillEnable( oBool( *theo )))
      goto cleanup;
  }

  /* PDFOutDetails */
  theo = pdevmatch[pdevmatch_PDFOutDetails].result;
  if ( pdfout_enabled() && !pdfout_setparams( context->pdfout_h , theo ))
    goto cleanup ;

  /** \todo ajcd 2011-01-10: 12145: Handled matrix here. */

  setHookStatus( MAX_HOOK , TRUE ) ;

  /* A friendly check for the dongle resolution limit being exceeded bearing in
   * mind any scaling and anti-aliasing that might be applied. The unfriendly
   * error occurs in the client function.
   * Also bearing in mind that resolution > 9000 means unlimited resolution
   */
  if ( maxResolution <= RESOLUTION_LIMIT ) {
    if ( page->xdpi > maxResolution || page->ydpi > maxResolution ) {
      (void)detail_error_handler(LIMITCHECK, "Resolution limit exceeded") ;
      goto cleanup;
    }
  }

  /* We've now set up the pagedevice dictionary. */
  result = TRUE ;

 cleanup:
  {
    const surface_set_t *surfaces = surface_set_select(&thegsDevicePageDict(*gstateptr)) ;
    surface_instance_t *sinstance ;
    /* Changing surface definition? */
    Bool continues = (page->surfaces == surfaces) && result ;

    if ( page->surfaces != NULL ) {
      /* Tidy up old surface definition. */
      if ( page->surfaces->deselect != NULL )
        (page->surfaces->deselect)(&page->sfc_inst, continues) ;
    }

    sinstance = continues ? page->sfc_inst : NULL ;
    page->surfaces = NULL ;
    page->sfc_inst = NULL ;

#undef return
    /* Having deselected the old surface, we can now quit if there was an
       error constructing the pagedevice. */
    if ( !result )
      return FALSE ;
    if ( surfaces == NULL )
      return detail_error_handler(CONFIGURATIONERROR,
                                  "No suitable surface definition for pagedevice") ;

    /* Initialise new surface definition. */
    if ( surfaces->select != NULL ) {
      sw_data_result result ;
      sw_datum pagedict ;
      if ( (result = swdatum_from_object(&pagedict, &thegsDevicePageDict(*gstateptr))) != SW_DATA_OK ) {
        return detail_error_handler(error_from_sw_data_result(result),
                                    "Surface set selection dictionary failed.");
      }
      if ( !(surfaces->select)(&sinstance, &pagedict, &sw_data_api_virtual,
                               continues) )
        return FALSE ;
    }
    page->surfaces = surfaces ;
    page->sfc_inst = sinstance ;
  }

  /** \todo ajcd 2011-03-10: Interacts with the pagebuffer device, including
      setting g_pgbbandmemory globals, asking the PGB device for stride and
      BandMemory/MaxBandMemory, and setting some height/band size PGB
      params. This also tests DOING_RUNLENGTH(), so the surface select has
      to set the RLE flags before this. */
  /* If constructing the new page device fails, deselect the surface we've
     just selected. */
  if ( !newpagedevice(page, w, h, ResamplingFactor, BandHeight) ) {
    result = FALSE ;
    goto cleanup ;
  }

  return dl_begin_page(page) ;
}

/* ---------------------------------------------------------------------- */
void deactivate_pagedevice( GSTATE *gs_old ,
                            GSTATE *gs_new ,
                            OBJECT *pagedevice ,
                            deactivate_pagedevice_t *dpd )
{
  HQASSERT( gs_old , "gs_old must always be non-null" ) ;
  HQASSERT( gs_new == NULL ||
            pagedevice == NULL ,
            "both gs_new and pagedevice can't be non-null" ) ;
  HQASSERT( dpd , "dpd must be non-null" ) ;

  dpd->new_is_pagedevice = FALSE ;
  dpd->old_is_pagedevice = FALSE ;
  dpd->different_devices = FALSE ;

  dpd->action = PAGEDEVICE_NO_ACTION ;

  if ( gs_new == NULL && pagedevice == NULL ) {
    /* Special case of reboot when both are NULL and we're forcing a deactivate.
     * Actually don't need to change any flags since the defaults above will suffice.
     */
    return ;
  }

  if ( gs_new != NULL ) {
    /*
     * For deactivation a device is considered a pagedevice if is is any
     * flavour of bandddevice, i.e. real banddevice, suppressed banddevice,
     * or errored banddevice. Then they are then different devices if either
     * theirs IDs are different of they are different flavours. [ Have to
     * do this second test as suppressed or errored banddevices do not have
     * new IDs assigned, and we want e.g. an errored banddevice and an ok
     * banddevice to qualify as different devices.
     */
    dpd->new_is_pagedevice = dev_is_bandtype(thegsDeviceType(*gs_new));
    dpd->old_is_pagedevice = dev_is_bandtype(thegsDeviceType(*gs_old));
    dpd->different_devices = ( thegsDeviceBandId(*gs_new) !=
                               thegsDeviceBandId(*gs_old) ||
                               thegsDeviceType(*gs_new) !=
                               thegsDeviceType(*gs_old) ) ;

    if ( dpd->new_is_pagedevice &&
         dpd->old_is_pagedevice &&
         dpd->different_devices )
      dpd->action = PAGEDEVICE_REACTIVATING ;
  }

  if ( pagedevice != NULL || dpd->action == PAGEDEVICE_REACTIVATING ) {
    OBJECT *tmpo ;
    int32 deact_old , deact_new ;

    oName( nnewobj ) = system_names + NAME_Deactivate ;
    tmpo = fast_extract_hash(&thegsDevicePageDict(*gs_old), &nnewobj) ;
    deact_old = -1 ;
    if ( tmpo != NULL && oType( *tmpo ) == OINTEGER )
      deact_old = oInteger( *tmpo ) ;

    if ( gs_new != NULL )
      tmpo = fast_extract_hash(&thegsDevicePageDict(*gs_new), &nnewobj) ;
    else if ( pagedevice != NULL ) {
      tmpo = fast_extract_hash( pagedevice , & nnewobj ) ;
      if ( tmpo == NULL )
        tmpo = fast_extract_hash(&thegsDevicePageDict(*gs_old), &nnewobj) ;
    }
    deact_new = -1 ;
    if ( tmpo != NULL && oType( *tmpo ) == OINTEGER )
      deact_new = oInteger( *tmpo ) ;

    /* Check if we want to delay deactivation and/or force deactivation with
       no output. If deactivation is delayed, then we cannot be reactivating
       this pagedevice. */
    if ( deact_new == -2 ) {
      dpd->action = PAGEDEVICE_FORCED_DEACTIVATE ;
    } else if ( deact_new >= deact_old && deact_new >= 0 ) {
      DL_STATE *page = get_core_context_interp()->page ;
      if ( page->rippedsomethingsignificant ||
           dlSignificantObjectsToRip(page) )
        dpd->action = PAGEDEVICE_DEFERRED_DEACTIVATE ;
    }
  }
}

/* ---------------------------------------------------------------------- */
Bool beginpage_(ps_context_t *pscontext)
{
  if ( deferred_setpagedevice )
    return TRUE ;

  return do_pagedevice_reactivate(pscontext, NULL) ;
}

/* ---------------------------------------------------------------------- */
static Bool gs_erasepage(ps_context_t *pscontext,
                         Bool fImposeErase, Bool fPageErase);


static Bool do_beginpage(ps_context_t *pscontext, int32 code, Bool fOutputPage)
{
  DL_STATE *page = ps_core_context(pscontext)->page;

  HQASSERT( code == PAGEDEVICE_DO_REACTIVATE ||
            code == PAGEDEVICE_DO_SHOWPAGE ||
            code == PAGEDEVICE_DO_COPYPAGE ,
            "do_beginpage: invalid pagedevice code" ) ;
  HQASSERT( gstateptr != NULL ,
            "do_beginpage: NULL pointer to gstate" ) ;

  if ( ! rcbn_enabled() || rcbn_do_beginpage() ) {
    if ( oType(thegsDevicePageDict(*gstateptr)) == ODICTIONARY ) {
      OBJECT *theo ;

      /* Got pagedevice dictionary - look for BeginPage procedure in it. */
      theo = fast_extract_hash_name(&thegsDevicePageDict(*gstateptr),
                                    NAME_BeginPage) ;

      if ( theo != NULL ) {
        OMATRIX oldPageBaseMatrix ;
        Bool result = TRUE ;
        Bool save_imposition;
        int32 oldPageBaseMatrixId = pageBaseMatrixId ;

        static Bool cleanup_imposition = FALSE;

        /* Disable imposition */
        save_imposition = doing_imposition;
        doing_imposition = FALSE;

        if ( save_imposition ) {
          ++pageBaseMatrixId ;
          thegsPageBaseID(gstateptr->thePDEVinfo) = pageBaseMatrixId ;
          thegsPageBaseID(thegsPageClip(*gstateptr)) = pageBaseMatrixId ;
          MATRIX_COPY( & oldPageBaseMatrix , & pageBaseMatrix ) ;
          if ( code == PAGEDEVICE_DO_REACTIVATE )
            result = initmatrix_(pscontext) ;
          result = result && initclip_(pscontext) ;
        }

        if ( result ) {
          uint8 pres_currentexflags = dl_currentexflags ;

          /* Set up matrix and clip ok - set up showpage count for BeginPage procedure */
          result = stack_push_integer( showpage_count, &operandstack ) ;

          /* Mark any dl objects created as being from BeginPage. */
          dl_currentexflags |= ( RENDER_BEGINPAGE | RENDER_UNTRAPPED ) ;
          disable_separation_detection() ;
          rcbn_disable_interception(gstateptr->colorInfo) ;
          result = result && setup_pending_exec( theo , TRUE /* do it now */) ;
          dl_currentexflags = pres_currentexflags ;
          enable_separation_detection() ;
          rcbn_enable_interception(gstateptr->colorInfo) ;
        }

        doing_imposition = save_imposition;

        if ( ! result )
          return FALSE ;

        if ( page->currentGroup ) {
          /* The BeginPage hook may include a setinterceptcolorspace which
             needs to taken account of in the page group's (and sub-groups')
             colorInfo. */
          if ( !groupResetColorInfo(page->currentGroup, gstateptr->colorInfo,
                                    page->colorState) )
            return FALSE ;
        }

        if ( doing_imposition ) {
          /* We can have impositionclipping set, if we do a setpagedevice within
           * the Imposed BeginPage. So free it here.
           */
          gs_freecliprec(&theClipRecord(impositionclipping)) ;

          /* Update clipping, setting special imposed clip marks */
          impositionclipping = thegsPageClip(*gstateptr) ;
          theClipStack(impositionclipping) = NULL ;
          theClipRecord(thegsPageClip(*gstateptr)) = NULL ;

          /* Clear the initclip record, because we've stolen it for
             impositionclipping. */
          gs_freecliprec(&gstateptr->thePDEVinfo.initcliprec) ;

          if ( !clip_device_new(gstateptr) )
            return FALSE ;

          /* Deduce new pagebase matrix from CTM after BeginPage. */
          if ( ! matrix_inverse(&thegsDevicePageCTM(*gstateptr),
                                & pageBaseMatrix ))
            return FALSE ;

          matrix_mult(&thegsPageCTM(*gstateptr), &pageBaseMatrix, &pageBaseMatrix) ;
          matrix_clean( & pageBaseMatrix ) ;

          if (!apply_pagebasematrix_to_all_gstates(&oldPageBaseMatrix,
                                                   &pageBaseMatrix,
                                                   oldPageBaseMatrixId))
            return FALSE;
          cleanup_imposition = TRUE ;

          /* Now make sure we do an erasepage for possibly moved imposition page. */
          if ( ! fOutputPage && doing_imposition ) {
            if ( ! dlreset_imposition(page) || /* Move pointers to end of imposition DL. */
                 ! gs_erasepage(pscontext, doing_imposition != 0, TRUE) )
              return FALSE ;
          }
        }
        else if ( cleanup_imposition ) {
          ++pageBaseMatrixId ;
          MATRIX_COPY( & oldPageBaseMatrix , & pageBaseMatrix ) ;
          MATRIX_COPY( & pageBaseMatrix , & identity_matrix ) ;
          if (!apply_pagebasematrix_to_all_gstates(&oldPageBaseMatrix,
                                                   &pageBaseMatrix,
                                                   oldPageBaseMatrixId))
            return FALSE;
          cleanup_imposition = FALSE ;
        }
      }
    }

    if ( fOutputPage && !trapZoneReset(page, TRUE) )
      return FALSE ;
  }

  return TRUE ;
}

/*
 * do_endpage() - try to execute the pagedevice EndPage procedure to decide
 * whether to render the page or not.  To try and gracefully handle errors
 * in the EndPage procedure the op and dict stacks are tidied up around it.
 * However, there is still enough rope for a Job to kill the RIP with (e.g.
 * we don't validate dict and op stacks around call to EndPage).
 */
static Bool do_endpage(ps_context_t *pscontext, int32 code, Bool *pfOutputPage)
{
  uint8   save_currentexflags;
  int32   dictstack_size;
  int32   opstack_size;
  int32   result;
  OBJECT* theo;
  int32   save_imposition;

  HQASSERT( code == PAGEDEVICE_DO_DEACTIVATE ||
            code == PAGEDEVICE_DO_SHOWPAGE ||
            code == PAGEDEVICE_DO_COPYPAGE ,
            "do_endpage: invalid pagedevice code" ) ;
  HQASSERT( gstateptr != NULL ,
            "do_endpage: NULL pointer to gstate" ) ;
  HQASSERT( pfOutputPage != NULL ,
            "do_endpage: NULL pointer to returned output page flag" ) ;

  *pfOutputPage = (code != PAGEDEVICE_DO_DEACTIVATE);

  /* Recombine skips EndPage proc until seen last separation */
  if ( rcbn_enabled() && !rcbn_do_endpage() ) {
    return TRUE ;
  }

  /* No worries if cannot find EndPage proc */
  if ( oType(thegsDevicePageDict(*gstateptr)) != ODICTIONARY ) {
    return TRUE ;
  }
  theo = fast_extract_hash_name(&thegsDevicePageDict(*gstateptr), NAME_EndPage);
  if ( theo == NULL ) {
    return TRUE ;
  }

  /* Remember dict and op stack size for later recovery */
  opstack_size = theStackSize(operandstack);
  dictstack_size = theStackSize(dictstack);

  /* Setup args for EndPage proc */
  result = (stack_push_integer(showpage_count, &operandstack) &&
            stack_push_integer(code, &operandstack));
  if ( result ) {
    /* Disable imposition */
    save_imposition = doing_imposition;
    doing_imposition = FALSE;

    /* Mark any dl objects created as being from EndPage and not trapped */
    save_currentexflags = dl_currentexflags;
    dl_currentexflags |= (RENDER_ENDPAGE|RENDER_UNTRAPPED);
    disable_separation_detection();
    rcbn_disable_interception(gstateptr->colorInfo);
    result = setup_pending_exec(theo, TRUE /* do it now */);
    dl_currentexflags = save_currentexflags;
    enable_separation_detection();
    rcbn_enable_interception(gstateptr->colorInfo);

    doing_imposition = save_imposition;

    if ( result ) {
      /* EndPage did not error - check it hasn't screwed up op and dict stacks */
      if ( (theStackSize(operandstack) > opstack_size) &&
           (theStackSize(dictstack) >= dictstack_size) ) {

        theo = theTop(operandstack);
        if ( oType(*theo) == OBOOLEAN ) {
          /* And verily EndPage hath spaketh ... */
            *pfOutputPage = oBool(*theo);
            pop(&operandstack);

        } else { /* EndPage left wrong type on op stack - tidy up time */
          result = error_handler(TYPECHECK);
        }

      } else { /* EndPage popped too much off op/dict stack - tidy up time */
        result = error_handler(UNDEFINED);
      }
    }
  }

  if ( !result ) {
    /* Pop off any gumph left on stacks. Note if EndPage executed OK
     * but added extra values then these extra values will be left there
     * on the basis that the Job knows what it is doing */
    if ( (theStackSize(dictstack) - dictstack_size) > 0 ) {
      dictstack_size = theStackSize(dictstack) - dictstack_size;
      do {
        EMPTY_STATEMENT();
      } while ( end_(pscontext) && (--dictstack_size > 0) );
    }

    if ( (theStackSize(operandstack) - opstack_size) > 0 ) {
      npop((theStackSize(operandstack) - opstack_size), &operandstack);
    }
  }

  return result ;
}

static int32 get_numcopies( void )
{
  int32 numcopies ;
  OBJECT *theo ;

  HQASSERT( gstateptr != NULL ,
            "get_numcopies: NULL pointer to gstate" ) ;

  numcopies = 0 ; /* Assume no copies */

  /* Look for NumCopies in pagedevice dictionary. */
  theo = fast_extract_hash_name( & thegsDevicePageDict(*gstateptr ) ,
                                 NAME_NumCopies ) ;
  if ( theo != NULL && oType( *theo ) == OINTEGER ) {
    /* Found NumCopies in pagedevice dictionary - use it for number of copies. */
    numcopies = oInteger( *theo ) ;
  }
  else { /* NumCopies not present or wrong type - look for #copies */
    int32 loop ;
    int32 dstacksize = theStackSize( dictstack ) ;
    theo = NULL ;
    for ( loop = 0 ; loop <= dstacksize ; ++loop )
      if (( theo = fast_extract_hash_name(stackindex( loop , & dictstack ) ,
                                          NAME_copies )) != NULL )
        break ;
    if ( theo && oType( *theo ) == OINTEGER )
      numcopies = oInteger( *theo ) ;
  }

  /* For safety range clip the value. */
  if ( numcopies < 0 )
    numcopies = 0 ;

  /* See if we need to override number of copies from recombine code. */
  rcbn_copies( & numcopies ) ;

  return numcopies ;

}

static int32  security_check_two = 1 ;
static int32  security_index_two = 0 ;
static Bool pass_security_check( void )
{
  ps_context_t *pscontext = get_core_context_interp()->pscontext ;
  Bool fCheckFail ;
  static uint8  security_checks[ 16 ] = {
    3,  7, 15,  2, 12,  6, 11,  9, 14,  1, 16,  4, 13,  8,  5, 10
  } ;

  if ((--security_check_two) == 0 ) {
    security_check_two = 2 * security_checks[ security_index_two ] ;
    if ((++security_index_two) == 16 )
      security_index_two = 0 ;
  }
  fCheckFail = ea_check_result;

  if ( fCheckFail ) {
    static uint8 *s = ( uint8 * )"(tmp/spi) (w) file closefile" ;
    OBJECT anobject = OBJECT_NOTVM_NOTHING ;

    security_check_two = 1 ;        /* So we always check next time round... */

    theTags( anobject ) = (OSTRING | EXECUTABLE);
    theLen( anobject ) = ( uint16 )strlen(( char * )s ) ;
    oString( anobject ) = s ;
    if (! push( & anobject , & executionstack))
      return FALSE ;
    execStackSizeNotChanged = FALSE ;

    MESS_WITH_CURRENT_DEVICE() ;
  }

  return  ! fCheckFail  ;
}


static Bool do_pagedevice_rendering(DL_STATE *page, int32 code,
                                    int32 numcopies, Bool *pfDidRender)
{
  Bool fDidRender ;

  HQASSERT(code == PAGEDEVICE_DO_DEACTIVATE ||
           code == PAGEDEVICE_DO_SHOWPAGE ||
           code == PAGEDEVICE_DO_COPYPAGE, "invalid pagedevice code");
  HQASSERT(pfDidRender != NULL , "NULL pointer to rendering flag");

  /* Say we have had a marking object in case this is a blank page */
  if ( !dlAddingObject(page, FALSE) )
    return FALSE;

  fDidRender = FALSE ;
  if ( ! rcbn_enabled() || rcbn_do_render()) {
    Bool result ;
    Bool mc;

    if ( !get_pagebuff_param(page, (uint8 *)"MultipleCopies", &mc,
                               ParamBoolean, TRUE) )
      return FALSE;

    /* Want to render - bump count of PS pages rendered. */
    ++showno ;

    fDidRender = TRUE ;

    if ( !push(mc ? &tnewobj : &fnewobj, &operandstack) )
      return FALSE;

    if ( ! stack_push_integer(numcopies, & operandstack) )
      return FALSE ;

    if ( ! push(&thegsDeviceShowProc(*gstateptr), &executionstack) )
      return FALSE ;

#ifdef METRICS_BUILD
    /* Walk the DL just before rendering to collect detailed
       per object transparency metrics. */
    if (! populate_dl_transparency_metrics_hashtable())
      return FALSE;
#endif

    /* Got pagedevice render proc on stack - invoke it. */
    HQASSERT(rendering_in_progress == PAGEDEVICE_NOT_ACTIVE,
             "Recursive call of render procedure") ;
    rendering_in_progress = code ;
    result = interpreter( 1 , NULL ) ;
    rendering_in_progress = PAGEDEVICE_NOT_ACTIVE ;
    if ( ! result )
      return FALSE;
  }

  *pfDidRender = fDidRender ;
  return TRUE ;
}

Bool do_pagedevice_reactivate(ps_context_t *pscontext,
                              deactivate_pagedevice_t *dpd)
{
  /* Always succeeds for the nulldevice */
  if ( CURRENT_DEVICE() == DEVICE_NULL )
    return TRUE ;

  if ( dpd != NULL && dpd->action != PAGEDEVICE_REACTIVATING )
    return TRUE ;

  /* Finally invoke BeginPage to start the next page. */
  return do_beginpage(pscontext, PAGEDEVICE_DO_REACTIVATE, FALSE) ;
}

/*
 * NOTE: if do_pagedevice_deactivate() is returning FALSE then there seems
 * to be a good chance that that the RIP will error out of the server loop
 * if finishing the job does not reset the problem. See 11171 for what
 * happened with a dodgy EndPage procedure.
 */
Bool do_pagedevice_deactivate( deactivate_pagedevice_t *dpd )
{
  corecontext_t *context = get_core_context_interp() ;
  DL_STATE *page = context->page ;
  Bool fAutoPage ;
  Bool fOutputPage ;
  Bool fDidRender = FALSE ;

  /* Always succeeds for the nulldevice */
  if ( CURRENT_DEVICE() == DEVICE_NULL || page->force_deactivate )
    return TRUE ;

  /* Finish off sep detection for this page */
  if ( ! finalise_sep_detection(gstateptr->colorInfo))
    return FALSE ;

  fAutoPage = FALSE ;
  if ( dpd->action != PAGEDEVICE_FORCED_DEACTIVATE &&
       context->systemparams->AutoShowpage )
    fAutoPage = ( page->rippedsomethingsignificant ||
                  dlSignificantObjectsToRip(page) ) ;

  if ( rcbn_enabled()) {
    /* Note that AutoShowpage is dealt with in the recombine code, as is all
     * other page output when recombine is on.
     */
    int32 numcopies = get_numcopies() ;
    if ( ! rcbn_register_deactivate(dpd->action == PAGEDEVICE_FORCED_DEACTIVATE ,
                                    fAutoPage, numcopies) )
      return FALSE ;
    fAutoPage = FALSE ;
  }

  /* Invoke the pagedevices EndPage procedure */
  if ( !do_endpage(context->pscontext, PAGEDEVICE_DO_DEACTIVATE, &fOutputPage) ) {
    error_clear_context(context->error); /* Ignore errors from do_endpage to avoid exiting server loop */
    monitorf(UVS("%%%%[ Warning: Error in EndPage procedure - assuming returned false ]%%%%\n"));
    fOutputPage = FALSE;
  }
  else {
    /*
     * Automatic showpage for end of job; note: the cancel_implicit_showpage flag
     * is reset by an interrupt, so we don't get interrupted pages automatically
     * output.
     */
    if ( ! fOutputPage && fAutoPage ) {
      fOutputPage = fAutoPage ;

      if ( pdfout_enabled() && ! pdfout_endpage(&context->pdfout_h , FALSE) )
        return FALSE ;

      if ( ! IDLOM_ENDPAGE( NAME_showpage ))
        return FALSE ;
    }
  }

#if 0
  /* item 2 at bottom of Page 253 of RB2: Jog, AdvanceMedia etc */
  theo = NULL ;
  if ( oType( thegsDevicePageDict(*gstateptr)) == ODICTIONARY ) {
    theo = fast_extract_hash_name( & internaldict , NAME_jugglepagedevice ) ;
  }
  if ( theo != NULL ) {
    if ( ! push(theo, &executionstack) )
      return FALSE ;
    if ( !interpreter(1, NULL) )
      return FALSE ;
  }
#endif

  if ( fOutputPage ) {
    int32 numcopies = get_numcopies() ;
    if ( pass_security_check() && numcopies > 0 ) {
      if ( ! do_pagedevice_rendering(page, PAGEDEVICE_DO_DEACTIVATE,
                                     numcopies, &fDidRender) ) {
        page->force_deactivate = TRUE ;
        return FALSE ;
      }
    }
  }

  if ( rcbn_do_render() ) {
    /* Must clear out this page if an attempt was made to render it
       (ie, regardless of whether it successfully rendered or not). */
    if ( ! rcbn_reset())
      return FALSE ;
  }

  return TRUE ;
}


/** \brief Perform a showpage, apart from the external callbacks (HDLT/PDFout).
 */
Bool do_pagedevice_showpage( Bool forced )
{
  corecontext_t *context ;
  ps_context_t *pscontext ;
  Bool result ;
  int32 numcopies ;
  Bool fOutputPage ;
  Bool fDidRender = FALSE ;
  Bool old_doing_imposition = FALSE ;

  /* Always succeeds for the nulldevice, don't try if we have dead pagedev */
  if ( CURRENT_DEVICE() == DEVICE_NULL || CURRENT_DEVICE() == DEVICE_ERRBAND )
    return TRUE ;

  /* Finish off sep detection for this page. */
  if ( ! finalise_sep_detection(gstateptr->colorInfo))
    return FALSE ;

  numcopies = get_numcopies() ;

  if ( ! forced )
    if ( rcbn_enabled())
      if ( ! rcbn_register_showpage( numcopies ))
        return FALSE ;

  context = get_core_context_interp() ;
  pscontext = context->pscontext ;

  /* Invoke the pagedevices EndPage procedure */
  if ( !do_endpage(pscontext, PAGEDEVICE_DO_SHOWPAGE, &fOutputPage) )
    return FALSE;

  if ( fOutputPage ) {
    if ( ! CURRENT_DEVICE_SUPPRESSES_STATE()) {
      if ( pass_security_check() && numcopies > 0 ) {
        if ( ! do_pagedevice_rendering(context->page, PAGEDEVICE_DO_SHOWPAGE,
                                       numcopies, &fDidRender) ) {
          /* Must do a reset to avoid recombine making further attempts to
             output the page. */
          if ( rcbn_do_render() )
            ( void ) rcbn_reset();
          return FALSE ;
        }
      }
    }
  }

  /* suppress the next logical page if not required */
  if (hPageRange != NULL) {
    Bool fThisPage, fMorePages;
    fThisPage = pageRangeStep(hPageRange, TRUE, & fMorePages);
    routedev_setDSCsuppression(! fThisPage);
    routedev_setDLsuppression(! fThisPage);
    if (! fMorePages) {
      if (!currfile_(pscontext) || !closefile_(pscontext))
        return FALSE;
    }
  }

  /* Do the initgraphics: in this context the page base is also reset. */
  old_doing_imposition = doing_imposition ;
  if ( fDidRender || ! rcbn_enabled() )
    doing_imposition = FALSE ;
  result = initgraphics_(pscontext) ;
  doing_imposition = old_doing_imposition ;
  if ( ! result )
    return FALSE ;

  if ( rcbn_do_render() ) {
    /* Must clear out this page if an attempt was made to render it
       (ie, regardless of whether it successfully rendered or not).
       Current page must be cleared before gs_erasepage as this may
       start telling the recombine control code information about the
       following page. */
    if ( ! rcbn_reset())
      return FALSE ;
  }

  if ( fOutputPage ) {
    /* Really do the erasepage irrespective of doing_imposition, but only
     * if we should have exposed a page.
     */
    if ( rcbn_enabled() )
      /* Move pointers back for recombine. */
      dlreset_recombine(context->page) ;

    /* If we got all of the way through the renderproc successfully, we'll
       have called renderbands_(), which calls spawn_all_passes_of_page(),
       which in turn ensures that the appropriate erase has been done. */
    if (
#if 0 /** \todo ajcd 2011-03-24: This doesn't work. Why not? */
        !fDidRender &&
#endif
        !gs_erasepage(pscontext, FALSE, TRUE) )
      return FALSE ;
  }

  if ( fDidRender || ! rcbn_enabled() )
    ++showpage_count ;

  /* Finally invoke BeginPage to start the next page. */
  return do_beginpage(pscontext, PAGEDEVICE_DO_SHOWPAGE, fOutputPage) ;
}


/** \brief Perform a copypage, apart from the external callbacks (HDLT/PDFout).
 */
static Bool do_pagedevice_copypage( ps_context_t *pscontext )
{
  corecontext_t *context = ps_core_context(pscontext);
  int32 code ;
  int32 numcopies ;
  Bool fOutputPage ;
  Bool fDidRender = FALSE ;

  /* Always succeeds for the nulldevice, don't try if we have dead pagedev */
  if ( CURRENT_DEVICE() == DEVICE_NULL || CURRENT_DEVICE() == DEVICE_ERRBAND )
    return TRUE ;

  /* Finish off sep detection for this page. */
  if ( ! finalise_sep_detection(gstateptr->colorInfo))
    return FALSE ;

  numcopies = get_numcopies() ;

  if ( rcbn_enabled())
    if ( ! rcbn_register_showpage( numcopies ))
      return FALSE ;

  /* Invoke the pagedevices EndPage procedure with appropriate code */
  code = theISaveLangLevel(workingsave) >= 3
    ? PAGEDEVICE_DO_SHOWPAGE
    : PAGEDEVICE_DO_COPYPAGE ;

  if ( !do_endpage(pscontext, code, &fOutputPage) )
    return FALSE;

  if ( fOutputPage ) {
    if ( pass_security_check() && numcopies > 0 )
      if ( !do_pagedevice_rendering(context->page,
                                    code, numcopies, &fDidRender) )
        return FALSE ;
  }

  if ( code == PAGEDEVICE_DO_SHOWPAGE ) {
    if ( rcbn_do_render() ) {
      /* Must clear out this page if an attempt was made to render it
         (ie, regardless of whether it successfully rendered or not).
         Current page must be cleared before gs_erasepage as this may
         start telling the recombine control code information about the
         following page. */
      if ( ! rcbn_reset())
        return FALSE ;
    }

    if ( fOutputPage ) {
      /* Really do the erasepage irrespective of doing_imposition, but only
         if we exposed a page. */
      if ( rcbn_enabled() )
        /* Move pointers back for recombine. */
        dlreset_recombine(context->page) ;

      /* If we got all of the way through the renderproc successfully, we'll
         have called renderbands_(), which calls spawn_all_passes_of_page(),
         which in turn ensures that the appropriate erase has been done. */
      if (
#if 0 /** \todo ajcd 2011-03-24: This doesn't work. Why not? */
          !fDidRender &&
#endif
          !gs_erasepage(pscontext, FALSE, TRUE) )
        return FALSE ;
    }

    if ( ! rcbn_enabled() || fDidRender )
      ++showpage_count ;
  }

  /* Finally invoke BeginPage to start the next page. */
  return do_beginpage(pscontext, code, fOutputPage) ;
}

/* ---------------------------------------------------------------------- */
Bool currentpagedevice_(ps_context_t *pscontext)
{
  Bool retval;
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (dev_is_bandtype(CURRENT_DEVICE())) {
    retval = push(&thegsDevicePageDict(*gstateptr), &operandstack);
  } else if ( oAccess(*oDict(thegsDevicePageDict(*gstateptr))) == UNLIMITED ) {
    /* We are in setpagedevice, but not gone back to set the device yet: we
       can tell by the protection on the dictionary. */
    retval = push(&thegsDevicePageDict(*gstateptr), &operandstack);
  } else {
  /* otherwise an empty dictionary */
    OBJECT thed = OBJECT_NOTVM_NOTHING ;
    if (! ps_dictionary(&thed, 0))
      return FALSE;
    SET_DICT_ACCESS(&thed, READ_ONLY) ;
    retval = push(& thed, &operandstack);
  }

  if (doing_setpagedevice || !retval)
    return retval;

  theo = fast_extract_hash_name(& internaldict, NAME_callbackcurrentpagedevice);
  if (theo) {
    if (! push(theo, & executionstack)) {
      npop(1, &operandstack);
      return FALSE;
    }
    SET_DICT_ACCESS(& thegsDevicePageDict(*gstateptr), UNLIMITED);
    if (! interpreter(1, NULL)) {
      SET_DICT_ACCESS(& thegsDevicePageDict(*gstateptr), READ_ONLY);
      return FALSE;
    }
    SET_DICT_ACCESS(& thegsDevicePageDict(*gstateptr), READ_ONLY);
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool showpage_(ps_context_t *pscontext)
{
  gc_safe_in_this_operator();

  reset_setsystemparam_separation() ;
  if ( ! flush_vignette( VD_Default ))
    return FALSE ;
  if ( pdfout_enabled() &&
       ! pdfout_endpage(&ps_core_context(pscontext)->pdfout_h, FALSE) )
    return FALSE ;
  if ( ! IDLOM_ENDPAGE( NAME_showpage ))
    return FALSE ;
  return do_pagedevice_showpage( FALSE );
}

/* ---------------------------------------------------------------------- */
Bool copypage_(ps_context_t *pscontext)
{
  Bool fClearPage = ( theISaveLangLevel( workingsave ) >= 3 ) ;

  gc_safe_in_this_operator();

  if ( fClearPage )
    reset_setsystemparam_separation() ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( pdfout_enabled() &&
       ! pdfout_endpage(&ps_core_context(pscontext)->pdfout_h, FALSE) )
    return FALSE ;

  if ( ! IDLOM_ENDPAGE( NAME_copypage ))
    return FALSE ;

  if ( rcbn_enabled() || doing_imposition ) {
    /* copypage does not sit well with imposition because we can't replicate
     * the display list. However, we can at least allow the matrix and clipping
     * to be adjusted properly. To do this, the BeginPage will have to do an
     * initgraphics, but copypage keeps the graphics state unchanged, so the
     * gsave and grestore will make sure that holds true, should the
     * application take advantage of that.
     * In fact only Aldus Preprint at the time of writing (Jan 93) is foolish
     * enough to use copypage, and then only to do its own #copies
     * implementation
     */
    Bool result ;
    int32 gid = gstateptr->gId ;
    if ( ! gs_gpush( GST_SHOWPAGE ))
      return FALSE ;
    result = do_pagedevice_showpage( FALSE ) ;
    return gs_cleargstates( gid, GST_SHOWPAGE, NULL ) && result ;
  }
  else {
    /* Really do a copypage.
     * Note that in LL3, this now only differs from showpage in that it
     * doesn't do an implicit initgraphics (i.e. now does an erasepage)
     */
    return do_pagedevice_copypage(pscontext);
  }
}

/* ---------------------------------------------------------------------- */
static Bool runEraseColorProc(DL_STATE *page, GSTATE *gs, Bool knockout)
{
  OBJECT * theo ;
  Bool fResult;
  int saved_contoneMask = page->colorPageParams.contoneMask;

  /* look up EraseColor. Whether in the page device or internaldict,
     this is a procedure which sets up the colorspace and background
     color; this is called from within an artificial gsave. The
     difference is that EraseColor in pagedevice is not executed in a
     context where the interception has been disabled temporarily */

  HQASSERT(oType(thegsDevicePageDict(*gs)) == ODICTIONARY,
            "invalid page device dictionary in erasepage");
  /* the page device would only not be a dictionary if we hadnt yet done a setpagedevice
     and we always do before any erasepage */

  theo = fast_extract_hash_name(& thegsDevicePageDict(*gs), NAME_EraseColor);
  HQASSERT(theo != NULL, "no EraseColor found in page device in erasepage");
  /* we should find this, it is added in pagedev.pss in the original
     page device dictionary, and the dictionary can only otherwise
     be empty for nulldevice which is otherwise dealt with above */

  if (theo == NULL || oType(*theo) == ONULL) {
    /* look up the default EraseColor in internaldict, which we require to be there */
    theo = fast_extract_hash_name(& internaldict , NAME_EraseColor);
    HQASSERT(theo != NULL && oType(*theo) == OARRAY,
             "can't find expected EraseColor in internaldict");
    if (theo == NULL)
      return error_handler( UNREGISTERED );
  }

  /* do what either EraseColor says */

  rcbn_disable_interception(gs->colorInfo) ;
  disable_separation_detection() ;
  if (!knockout) {
    /* The knockout color differs from the erase color iff a contone
       mask is being used. */
    page->colorPageParams.contoneMask = 0;
  }

  gsc_setConvertAllSeparation(gs->colorInfo, GSC_CONVERTALLSEPARATION_ALL);
  /* and invalidate the color chains + caches */
  gsc_markChainsInvalid(gs->colorInfo);

  /* must temporarily disable all overprinting (i.e. in the current gstate) */
  fResult = gsc_disableOverprint(gs->colorInfo);

  fResult = fResult &&
            (push( theo , & executionstack ) &&
             interpreter( 1, NULL ) &&
             gsc_invokeChainSingle( gs->colorInfo, GSC_FILL)) ;

  /* No need to re-enable gstate color attributes - the grestore will do it */

  page->colorPageParams.contoneMask = saved_contoneMask;
  enable_separation_detection() ;
  rcbn_enable_interception(gs->colorInfo) ;

  gsc_markChainsInvalid(gs->colorInfo);

  return fResult ;
}


static Bool applyEraseColor( corecontext_t *context,
                             Bool use_current_gs,
                             dl_color_t *eraseColor,
                             LateColorAttrib **lateColorAttribs,
                             Bool knockout )
{
  COLORVALUE colorValue;
  DL_STATE *page = context->page;
  COLOR_PAGE_PARAMS *colorPageParams = &page->colorPageParams;
  dl_color_t *dlc_current = dlc_currentcolor(page->dlc_context) ;
  GS_COLORinfo *old_info = NULL /* prevent compiler warning */;
  Bool replace_colorInfo = FALSE;
  GUCR_RASTERSTYLE *rs = gsc_getRS(gstateptr->colorInfo) ;
  Bool result = FALSE;

  if ( !use_current_gs ) {
    SPOTNO oldspot = gsc_getSpotno(gstateptr->colorInfo);

    /* Compute erase colour with colour settings from erasepage, but
       current device rasterstyle and updated page default screen. */
#define return DO_NOT_return_here /* not safe until colorInfo swapped back */
    replace_colorInfo = TRUE;
    old_info = gstateptr->colorInfo; /* must grestore this, so store it */
    gstateptr->colorInfo = gsc_eraseColorInfo();
    HQASSERT(!guc_backdropRasterStyle(gsc_getRS(gstateptr->colorInfo)),
             "Must use device RS");
    if ( !gsc_setSpotno(gstateptr->colorInfo, page->default_spot_no) ||
         !gsc_regeneratehalftoneinfo(gstateptr->colorInfo,
                                     page->default_spot_no, oldspot) )
      goto cleanup;
  }

  if ( !runEraseColorProc(page, gstateptr, knockout) )
    goto cleanup;

  if ( knockout && colorPageParams->contoneMask != 0 &&
       !guc_backdropRasterStyle(rs) && !gucr_halftoning(rs)) {
    GUCR_CHANNEL *hf;
    GUCR_COLORANT *hc;
    dl_color_t temp_color ;

    /* We're defining the knockout color with a contone mask. We
       must ensure that the knockout color contains every
       fully-fledged colorant so that the part of blit_color_unpack
       which infers positive/negative print from the nearness of the
       first color channel's value to 0 or 1 doesn't get used. In
       colorants which for whatever reason don't appear in the color
       resulting from running the erase color proc above, we need to
       have an explicit contone mask value in the knockout color. */

    dlc_clear(&temp_color);
    if ( !dlc_from_rs(page, rs, &temp_color, COLORVALUE_INVALID) )
      goto cleanup;

    for (hf = gucr_framesStart(rs); gucr_framesMore(hf); gucr_framesNext(&hf)) {
      COLORVALUE cv ;

      for ( hc = gucr_colorantsStart (hf);
            gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
            gucr_colorantsNext (& hc)) {
        const GUCR_COLORANT_INFO *colorantInfo ;

        if ( gucr_colorantDescription(hc, &colorantInfo)) {
          if ( dlc_get_indexed_colorant(dlc_current,
                                        colorantInfo->colorantIndex,
                                        &cv)) {
            /* We'll copy the explicit value for this colorant in
               the knockout color across. */
          }
          else {
            float clear = (float)ht_getClear(page->default_spot_no,
                                             REPRO_TYPE_OTHER,
                                             colorantInfo->colorantIndex, rs);

            if ( colorPageParams->contoneMask < 0 ||
                 colorPageParams->contoneMask > clear ) {
              (void)detail_error_handler(RANGECHECK,
                                         "ContoneMask out of range for "
                                         "this configuration");
              goto cleanup;
            }

            cv = FLOAT_TO_COLORVALUE(1.0f -
              ((float)colorPageParams->contoneMask / clear));
          }

          if ( !dlc_replace_indexed_colorant(page->dlc_context,
                                             &temp_color,
                                             colorantInfo->colorantIndex,
                                             cv) )
            goto cleanup;
        }
      }
    }

    if ( !dlc_copy(page->dlc_context, dlc_current, &temp_color) )
      goto cleanup;
  }

  /* Conditionally copy the erase color and the late color management state
   * to the output params */
  if (eraseColor != NULL &&
      ! dlc_copy(page->dlc_context, eraseColor, dlc_current))
    goto cleanup;
  if (lateColorAttribs != NULL &&
      ! getLateColorState(gstateptr, page, GSC_FILL, lateColorAttribs))
    goto cleanup;

  {
    dl_color_t *pdlc_colorCoherence;
    dl_color_t dlcWhite, dlcBlack;
    GUCR_CHANNEL *hf;
    GUCR_COLORANT *hc;

    pdlc_colorCoherence = NULL;
    dlc_clear(&dlcBlack);
    dlc_get_black(page->dlc_context, &dlcBlack);
    dlc_clear(&dlcWhite);
    dlc_get_white(page->dlc_context, &dlcWhite);

    /* look at what we got; if the colors in the resultant dlc
       are all solid or clear, then we can abbreviate the erase
       dlc. If a colorant is not represented in the dlc, we can
       assume it has the same coherence as the ones we can see;
       there ought to be at least one */
    for (hf = gucr_framesStart(gsc_getRS(gstateptr->colorInfo));
         gucr_framesMore(hf) && pdlc_colorCoherence != dlc_current;
         gucr_framesNext(&hf)) {
      for ( hc = gucr_colorantsStart (hf);
            gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED) &&
              pdlc_colorCoherence != dlc_current;
            gucr_colorantsNext (& hc)) {
        const GUCR_COLORANT_INFO *colorantInfo ;

        if ( gucr_colorantDescription(hc, &colorantInfo) &&
             dlc_get_indexed_colorant(dlc_current,
                                      colorantInfo->colorantIndex,
                                      &colorValue) ) {

          if ( ht_colorIsClear(gsc_getSpotno(gstateptr->colorInfo),
                               REPRO_TYPE_OTHER /* linework */,
                               colorantInfo->colorantIndex,
                               colorValue, gsc_getRS(gstateptr->colorInfo) ) ) {
            if ( pdlc_colorCoherence == NULL ) {
              pdlc_colorCoherence = &dlcWhite;

            } else if ( pdlc_colorCoherence != &dlcWhite ) {
              pdlc_colorCoherence = dlc_current;
            }

          } else if ( ht_colorIsSolid(gsc_getSpotno(gstateptr->colorInfo),
                                      REPRO_TYPE_OTHER /* linework */,
                                      colorantInfo->colorantIndex,
                                      colorValue, gsc_getRS(gstateptr->colorInfo)) ) {

            if ( pdlc_colorCoherence == NULL ) {
              pdlc_colorCoherence = &dlcBlack;

            } else if ( pdlc_colorCoherence != &dlcBlack ) {
              pdlc_colorCoherence = dlc_current;
            }

          } else {
            pdlc_colorCoherence = dlc_current;
          }
        }
      }
    }

    if ( pdlc_colorCoherence != NULL &&
         pdlc_colorCoherence != dlc_current ) {
      /* assign the simpler color to the global "current" color which will be
         assigned to the erase object - except for solid in color rle, which is too
         hot to handle */
      if ( !DOING_RUNLENGTH(page) ||
           gucr_interleavingStyle(gsc_getRS(gstateptr->colorInfo))
             != GUCR_INTERLEAVINGSTYLE_PIXEL ||
           pdlc_colorCoherence != &dlcBlack ) {
        dlc_release(page->dlc_context, dlc_current);
        dlc_copy_release(page->dlc_context, dlc_current, pdlc_colorCoherence);
        if (eraseColor != NULL) {
          dlc_release(page->dlc_context, eraseColor);
          if ( !dlc_copy(page->dlc_context, eraseColor, dlc_current) )
            goto cleanup;
        }
      }
    }
  }

  if ( !use_current_gs ) {
    SPOTNO oldspot = gsc_getSpotno(gstateptr->colorInfo);
    ht_change_non_purgable_screen(oldspot, gsc_getSpotno(old_info));
  }

  result = TRUE;

cleanup:
  if ( replace_colorInfo )
    gstateptr->colorInfo = old_info;
#undef return

  return result;
}

Bool gs_applyEraseColor( corecontext_t *context,
                         Bool use_current_gs,
                         dl_color_t *eraseColor,
                         LateColorAttrib **lateColorAttribs,
                         Bool knockout )
{
  if ( CURRENT_DEVICE() == DEVICE_NULL || CURRENT_DEVICE() == DEVICE_ERRBAND )
    return TRUE;

  if ( !gsave_(context->pscontext) )
    return FALSE;

  if ( !applyEraseColor(context, use_current_gs, eraseColor,
                        lateColorAttribs, knockout) )
    return FALSE;

  return grestore_(context->pscontext);
}


/* ---------------------------------------------------------------------- */
Bool erasepage_(ps_context_t *pscontext)
{
  return gs_erasepage(pscontext,
                      doing_setpagedevice == 0 && doing_imposition != 0,
                      doing_setpagedevice != 0 ) ;
}

static Bool gs_erasepage(ps_context_t *pscontext,
                         Bool fImposeErase, Bool fPageErase)
{
  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;

  /** \todo ajcd 2011-03-21: Why return FALSE on these? We're about to nuke
      the DL, it doesn't matter if we couldn't add pending objects to it. */
  if ( ! flush_vignette(VD_Default) || !finishaddchardisplay(page, 1) )
    return FALSE ;

  /* The following behaviour is similar to Distiller; erasepage in most
     caching contexts is an error (it would not be be guaranteed to be
     repeatable), except that it is ignored in character caching. Distiller
     disallows erasepage in Forms, however we do not have a test available to
     determine if we are inside a form (and haven't setgstated out to a page
     context). */
  switch ( CURRENT_DEVICE() ) {
  case DEVICE_NULL:
  case DEVICE_ERRBAND:
  case DEVICE_CHAR:
  case DEVICE_ILLEGAL:
    return TRUE;
  case DEVICE_PATTERN1:
  case DEVICE_PATTERN2:
    return error_handler(INVALIDACCESS) ; /* Yes, Distiller uses this error */
  }

  HQASSERT(page->im_imagesleft >= 0, "imagesleft gone below zero in erasepage");
  if ( page->im_imagesleft > 0 )
    return error_handler( UNDEFINEDRESULT ) ;

  if ( pdfout_enabled() &&
       !pdfout_endpage(&context->pdfout_h, TRUE) )
    return FALSE ;

  if ( ! IDLOM_ENDPAGE( NAME_erasepage ))
    return FALSE ;

  /* doing imposition test needs to take account of whether this really is
   * a new page or not. If it is a new page fail and call the new_page version
   * of reset_separation_detection. See test below for imposing after page 1.
   */
  if ( fPageErase ) {
    if ( rcbn_enabled() || fImposeErase ) {
      if ( ! reset_separation_detection_on_sub_page(gstateptr->colorInfo))
        return FALSE ;
    }
    else {
      if ( ! reset_separation_detection_on_new_page(gstateptr->colorInfo))
        return FALSE ;
    }
  }

  if ( !gsc_analyze_for_forcepositive(context,
                                      gstateptr->colorInfo, GSC_FILL,
                                      &page->forcepositive) )
    return FALSE ;

  if ( deferred_setpagedevice || fImposeErase ||
       (rcbn_enabled() && !rcbn_first_separation() && !rcbn_composite_page()) )
    return gs_pseudo_erasepage(pscontext) ;

  HQASSERT( ! rcbn_enabled() ||
            rcbn_first_separation() ||
            rcbn_composite_page() ,
            "gs_erasepage: erasing page in middle of page recombination" ) ;

  if ( !rcbn_reset() )
    return FALSE ;

  if ( ! gsc_loadEraseColorInfo(gstateptr->colorInfo))
    return FALSE ;

  /* Now do the erasepage, then start a new page, preserving the job and
     bumping the erase number. */
  dl_clear_page(page) ;
  return dl_begin_page(page) ;
}

/* ---------------------------------------------------------------------- */
/** Create and install a new pagedevice. This routine is called from the
    PostScript /resetpagedevice routine in internaldict. PostScript
    parameters are:

    bool   true if pagedevice is being initialised (the raster styles
           will be reset). false if updating pagedevice after running Install
           procedure.

    matrix Default pagedevice matrix.

    width  Width of pagedevice.

    height Height of pagedevice.

    proc   Procedure called to invoke rendering.

*/
Bool pagedevice_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;
  dcoord w, h ;

  OMATRIX matrix ;
  OBJECT *o1 , *o2 ;

  /* Play safe; flush any vignette */
  if ( ! deferred_setpagedevice ) {
    abort_vignette(page) ;
  }
  else {
    if ( !flush_vignette(VD_Default) )
      return FALSE ;
  }

  if ( theStackSize( operandstack ) < 4 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex(2, &operandstack) ; /* width of pagedevice */
  o2 = stackindex(1, &operandstack) ; /* height of pagedevice */
  if ( oType(*o1) != OINTEGER || oType(*o2) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  w = oInteger( *o1 ) ;
  h = oInteger( *o2 ) ;

  o1 = stackindex(3, &operandstack) ; /* pagedevice matrix */
  o2 = theTop(operandstack) ;         /* render proc */
  if ( ! is_matrix( o1 , & matrix ))
    return FALSE ;

  /* Clean up matrix so we don't get silly number like 1e-6... */
  matrix_clean( & matrix ) ;
  /* ...and round the translation components to the nearest integer. */
  MATRIX_CLEAN_EPSILON( matrix.matrix[ 2 ][ 0 ], 1, 1, 1.0 ) ;
  MATRIX_CLEAN_EPSILON( matrix.matrix[ 2 ][ 1 ], 1, 1, 1.0 ) ;

  switch ( oType( *o2 )) {
  case OARRAY :
  case OPACKEDARRAY :
    if ( oExecutable(*o2) )
      break ;
    /*@fallthrough@*/
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( (!oCanExec(*o2) && !object_access_override(o2)) ||
       (!oCanRead(*o1) && !object_access_override(o1)) )
    return error_handler( INVALIDACCESS ) ;

  if ( w <= 0 || h <= 0 )
    return error_handler( RANGECHECK ) ;

  if ( ! deferred_setpagedevice ) {
    Bool fInitializePageDevice;

    showpage_count = 0;

    o1 = stackindex(4, & operandstack); /* initialising page device? */
    if (oType(*o1) != OBOOLEAN)
      return error_handler(TYPECHECK);

    fInitializePageDevice = oBool(*o1);

    if ( !reset_pagedevice(context, page, fInitializePageDevice, w, h) )
      return FALSE ;

    thegsDeviceBandId( *gstateptr ) = ++bandid ;
  }

  o1 = ( & thegsDeviceShowProc( *gstateptr )) ;
  Copy( o1 , o2 ) ;

  /* either do all pages (normal case) or selectively */

  if (hPageRange == NULL) {
    SET_DEVICE(DEVICE_BAND);
  } else {
    Bool fThisPage, fMorePages;

    SET_DEVICE(DEVICE_SUPPRESS);

    fThisPage = pageRangeStep(hPageRange, FALSE, & fMorePages);
    routedev_setDSCsuppression(! fThisPage);
    routedev_setDLsuppression(! fThisPage);
  }

  if ( ! deferred_setpagedevice ) {
    SYSTEMVALUE screen_adjust ;
    SYSTEMVALUE screen_adjust_clean ;

    thegsDeviceW(*gstateptr) = w ;
    thegsDeviceH(*gstateptr) = h ;

    ( void )gs_setdefaultctm( & matrix , TRUE ) ;

    /* Calculate the Screen Rotate to apply to halftones. */
    screen_adjust = 90.0 -
        RAD_TO_DEG * myatan2( thegsDevicePageCTM( *gstateptr ).matrix[ 0 ][ 0 ] ,
                              thegsDevicePageCTM( *gstateptr ).matrix[ 0 ][ 1 ] ) ;
    screen_adjust_clean = ( SYSTEMVALUE )(( int32 )( screen_adjust >= 0.0 ?
        screen_adjust + 0.5 : screen_adjust - 0.5 )) ;
    if ( fabs( screen_adjust - screen_adjust_clean ) < EPSILON )
      screen_adjust = screen_adjust_clean ;

    /* Take a mirror of the ScreenRotate and keep it in colorInfo for use when
     * there isn't a gstate available */
    HQASSERT(gsc_getScreenRotate(gstateptr->colorInfo) == thegsDeviceScreenRotate( *gstateptr ),
             "Inconsistent ScreenRotate");
    thegsDeviceScreenRotate( *gstateptr ) = ( USERVALUE )screen_adjust ;
    gsc_setScreenRotate(gstateptr->colorInfo, (USERVALUE) screen_adjust);

    if ( gsc_getSpotno( gstateptr->colorInfo ) != 0 )  /* Only after we've installed a screen. */
      if ( ! gsc_redo_setscreen( gstateptr->colorInfo ))
        return FALSE ;
  }

  if ( ! initclip_(pscontext) )
    return FALSE ;

  /* We need to inform the PDF module of the presence of a new
     pagedevice. This is a bit grotty: it would be nice to sort out a
     way of avoiding having to do it. */

  if ( ! pdf_newpagedevice()) {
    return FALSE ;
  }

  npop( 5 , & operandstack ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* pagebbox is a harlequin extension which returns the extent of all the
   marks on the page so far, in default media space (i.e. user space adjusted
   according to imposition)
     pagebbox -> llx lly urx ury
*/

Bool pagebbox_(ps_context_t *pscontext)
{
  DL_STATE *page = ps_core_context(pscontext)->page;
  OMATRIX temp;
  SYSTEMVALUE x1, y1, x2, y2;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Flush the vignette pipeline so that any pending dl objects get flushed to the page. */
  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( bbox_is_empty(&page->page_bb) )
    return error_handler(NOCURRENTPOINT);

  if ( ! matrix_inverse( & thegsDevicePageCTM( *gstateptr ) , & temp ))
    return FALSE;

  /* remember the coordinates are inverted in y */
  MATRIX_TRANSFORM_XY( page->page_bb.x1 , page->page_bb.y2 + 1 ,
                       x1 , y1 , & temp ) ;

  MATRIX_TRANSFORM_XY( page->page_bb.x2 + 1 , page->page_bb.y1 ,
                       x2 , y2 , & temp ) ;

  return (stack_push_real(x1, &operandstack) &&
          stack_push_real(y1, &operandstack) &&
          stack_push_real(x2, &operandstack) &&
          stack_push_real(y2, &operandstack)) ;
}

/* ---------------------------------------------------------------------- */
Bool nulldevice_(ps_context_t *pscontext)
{
  SET_DEVICE(DEVICE_NULL);
  /* Don't update the page dimensions since we need it for deferred page
   * device deactivation.
   */
  /* Don't update the Default CTM since we need it for imposition.
   * Instead whenever we access Default CTM and we really do want
   * the [1 0 0 1 0 0] from the nulldevice, check the current device.
   * So only need to call initmatrix_ to update currentmatrix.
   */
  return initmatrix_(pscontext) && initclip_(pscontext) ;
}


/* ---------------------------------------------------------------------- */
/** \brief Create a new page device.

   \param page The DL page we're creating for a new device for.

   \param w The width of the output page.

   \param h The height of the output page.

   \param ResamplingFactor The anti-alias resampling factor, from the
   pagedevice dictionary. This is used to quantise the band height so the
   consumer doesn't have to store multiple bands.

   \param BandHeight The band height parameter from the pagedevice
   dictionary. This is used to request a specific band height.

   \return TRUE if the function succeeded, FALSE otherwise.
*/
static Bool newpagedevice(DL_STATE *page, int32 w, int32 h,
                          int32 ResamplingFactor, int32 BandHeight)
{
  int32 lines;
  int32 factor;
  GUCR_RASTERSTYLE *rs = gsc_getRS(gstateptr->colorInfo);

  /* This function sets the page_w, page_h and band_lines plus passes this
   * info to the pagebuffer device. */
  if ( ! determine_band_size(page, rs, w, h, ResamplingFactor, BandHeight))
    return FALSE ;

  lines = page->band_lines;
  /* lines is the number of lines in a single rendered band. We now need
     to make sure the DL we create has a large enough band size so that
     we don't add too many DLREFs per DL object. We do this using
     the fact that a normal distribution of the heights of objects is
     pretty narrow. (Just think of a page full of characters. In that
     case, most objects are of the same dimensions.)
  */
  if ( band_factor == 0.0f ) {
    factor = 1;
  } else {
    int32 obj_length = ( int32 )( band_factor * page->ydpi + 0.5 ) ;
    if ( obj_length <= 0 )
      obj_length = 1 ;
    factor = (int32)(( obj_length + ( lines - 1 )) / lines );
  }
  HQASSERT( factor > 0 , "factor should be > 0" ) ;

  if ( lines != 0 ) {
    page->sizedisplaylist = (h + lines - 1) / lines;
    page->sizedisplayfact = factor;

    page->sizefactdisplaylist = (page->sizedisplaylist + factor - 1) / factor;
    page->sizefactdisplayband = factor * lines;

    guc_setRasterStyleBandSize(page->hr, lines);
  } else {
    page->sizedisplaylist = 1;
    page->sizedisplayfact = 1;

    page->sizefactdisplaylist = 1;
    page->sizefactdisplayband = 1;

    guc_setRasterStyleBandSize(page->hr, 1);
  }
  return TRUE;
}


/* ---------------------------------------------------------------------- */

Bool sethalftonephase_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return do_sethalftonephase( & operandstack ) ;
}

Bool do_sethalftonephase( STACK *stack )
{
  OBJECT *o1 , *o2 ;

  if ( theIStackSize( stack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theITop( stack ) ;
  o1 = stackindex( 1 , stack ) ;

  if ( oType( *o1 ) != OINTEGER ||
       oType( *o2 ) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  HQASSERT(thegsDeviceHalftonePhaseX( *gstateptr ) == gsc_getHalftonePhaseX( gstateptr->colorInfo ) &&
           thegsDeviceHalftonePhaseY( *gstateptr ) == gsc_getHalftonePhaseY( gstateptr->colorInfo ),
           "Inconsistent halftone phase" ) ;

  if ( thegsDeviceHalftonePhaseX( *gstateptr ) == oInteger( *o1 ) &&
       thegsDeviceHalftonePhaseY( *gstateptr ) == oInteger( *o2 )) {
    npop( 2 , stack ) ;
    return TRUE ;
  }

  thegsDeviceHalftonePhaseX( *gstateptr ) = oInteger( *o1 ) ;
  thegsDeviceHalftonePhaseY( *gstateptr ) = oInteger( *o2 ) ;

  /* Take a mirror of these attributes and keep it in colorInfo for use when
   * there isn't a gstate available */
  gsc_setHalftonePhase(gstateptr->colorInfo, oInteger( *o1 ), oInteger( *o2 ));

  npop( 2 , stack ) ;

  return gsc_redo_setscreen( gstateptr->colorInfo ) ; /* Need to redo setscreen since h/t phase changed */
}

Bool currenthalftonephase_(ps_context_t *pscontext)
{
  OBJECT o1 = OBJECT_NOTVM_NOTHING, o2 = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  object_store_integer(&o1, thegsDeviceHalftonePhaseX(*gstateptr)) ;
  object_store_integer(&o1, thegsDeviceHalftonePhaseY(*gstateptr)) ;

  return push2( & o1 , & o2 , & operandstack ) ;
}



/* ====================================================================== */
/* PostScript operators to control dynamic separations (in addition to
   extensions to sethalftone and setcolorspace). The operators work
   thus:

        name      removefromseparationcolornames -

        name      addtoseparationcolornames -

        name      addtoseparationorder -
or      name dict addtoseparationorder -

        name      setbackgroundseparation -

These translate into the above calls. name can also be expressed equivalently as a
string (and can be /All in removefromseparationcolornames).

The optional dict parameter to addtoseparationorder takes the following keys

  x               As x in guc_newFrame; assumed 0 if omitted or no dictionary
  y               As y in guc_newFrame; assumed 0 if omitted or no dictionary
  Frame           Either a frame number for use as nRelativeToFrame in
                  guc_newFrame, or a colorant name or string, in which
                  case nRelativeToFrame is derived from it as described above.
  InsertBefore    true implies GUC_FRAMERELATION_BEFORE, false implies
                  GUC_FRAMERELATION_AFTER, absence or no dictionary implies
                  GUC_FRAMERELATION_END if Frame is not also given, or
                  GUC_FRAMERELATION_AT if Frame is given. There is no equivalent of
                  GUC_FRAMERELATION_START.

Therefore, with no dictionary, the named colorant would be added at the end in
position 0,0. This is the same operation as the automatic addition of a separation on
setcolorspace. */

/* ---------------------------------------------------------------------- */
Bool removefromseparationcolornames_(ps_context_t *pscontext)
{
  OBJECT * poColorant;
  NAMECACHE * pnmColorant;
  COLORANTINDEX ci;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (theStackSize(operandstack) < 0)
    return error_handler(STACKUNDERFLOW);

  poColorant = theTop(operandstack);

  if (oType(*poColorant) == OSTRING) {
    pnmColorant = cachename(oString(*poColorant), theLen(*poColorant));
  } else if (oType(*poColorant) == ONAME) {
    pnmColorant = oName(*poColorant);
  } else {
    return error_handler(TYPECHECK);
  }

  ci = guc_colorantIndex(gsc_getRS(gstateptr->colorInfo), pnmColorant);

  if (ci == COLORANTINDEX_UNKNOWN)
    return detail_error_handler(RANGECHECK, "Colorant is unknown.");
  else if (ci == COLORANTINDEX_NONE)
    return detail_error_handler(RANGECHECK, "None colorant is not allowed.");

  pop (& operandstack);

  guc_clearColorant(gsc_getRS(gstateptr->colorInfo), ci);

  /* Update equivalent colorants for current blend space target. */
  if (!guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo), ci))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool addtoseparationcolornames_(ps_context_t *pscontext)
{
  OBJECT * poColorant;
  NAMECACHE * pnmColorant;
  COLORANTINDEX ci;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (theStackSize(operandstack) < 0)
    return error_handler(STACKUNDERFLOW);

  poColorant = theTop(operandstack);

  if (oType(*poColorant) == OSTRING) {
    pnmColorant = cachename(oString(*poColorant), theLen(*poColorant));
  } else if (oType(*poColorant) == ONAME) {
    pnmColorant = oName(*poColorant);
  } else {
    return error_handler(TYPECHECK);
  }

  pop (& operandstack);

  if (!guc_colorantIndexPossiblyNewSeparation(gsc_getRS(gstateptr->colorInfo),
                                              pnmColorant, &ci))
    return FALSE;

  /* Update equivalent colorants for current blend space target. */
  if (!guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo), ci))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* This array MUST be in the same order as the dictmatch entries in
   addtoseparationorder. */
static const struct {
  uint16 name ;
  uint32 mask ;
} property_details[] = {
  { NAME_Picture,     RENDERING_PROPERTY_PICTURE },
  { NAME_Text,        RENDERING_PROPERTY_TEXT },
  { NAME_Vignette,    RENDERING_PROPERTY_VIGNETTE },
  { NAME_NamedColor,  RENDERING_PROPERTY_NAMEDCOLOR },
  { NAME_Other,       RENDERING_PROPERTY_LW },
  { NAME_Linework,    RENDERING_PROPERTY_LW },
#if defined(DEBUG_BUILD)
  { NAME_Composite,   RENDERING_PROPERTY_COMPOSITE },
#endif
} ;

Bool addtoseparationorder_(ps_context_t *pscontext)
{
  enum {
    paramsDictMatch_x, paramsDictMatch_y, paramsDictMatch_Frame,
    paramsDictMatch_InsertBefore,
    /* See comments above about ordering */
    paramsDictMatch_Picture, paramsDictMatch_Text,
    paramsDictMatch_Vignette, paramsDictMatch_NamedColor,
    paramsDictMatch_Other, paramsDictMatch_Linework,
#if defined(DEBUG_BUILD)
    paramsDictMatch_Composite,
#endif
    paramsDictMatch_Default,
    paramsDictMatch_ColorantType, paramsDictMatch_NeutralDensity,
    paramsDictMatch_Dynamic,
    paramsDictMatch_DefaultScreenAngle, paramsDictMatch_OverrideScreenAngle,
    paramsDictMatch_dummy
  } ;
  static NAMETYPEMATCH paramsDictMatch[paramsDictMatch_dummy + 1] = {
    /* Use the enum above to index this dictmatch */
    { NAME_x | OOPTIONAL,                   2, { OINTEGER, OREAL }},
    { NAME_y | OOPTIONAL,                   2, { OINTEGER, OREAL }},
    { NAME_Frame | OOPTIONAL,               3, { OSTRING, ONAME, OINTEGER }},
    { NAME_InsertBefore | OOPTIONAL,        1, { OBOOLEAN }},
    /* The following matches, from Picture to Default MUST be kept contiguous
       and in the same order in this dictmatch as the property_details
       table. */
    { NAME_Picture | OOPTIONAL,             2, { OBOOLEAN, ONAME }},
    { NAME_Text | OOPTIONAL,                2, { OBOOLEAN, ONAME }},
    { NAME_Vignette | OOPTIONAL,            2, { OBOOLEAN, ONAME }},
    { NAME_NamedColor | OOPTIONAL,          2, { OBOOLEAN, ONAME }},
    { NAME_Other | OOPTIONAL,               2, { OBOOLEAN, ONAME }},
    { NAME_Linework | OOPTIONAL,            2, { OBOOLEAN, ONAME }},
#if defined(DEBUG_BUILD) /* Allow specifying Composite as well */
    { NAME_Composite | OOPTIONAL,           2, { OBOOLEAN, ONAME }},
#endif
    { NAME_Default | OOPTIONAL,             2, { OBOOLEAN, ONAME }},
    /* OK, you can change the order again */
    { NAME_ColorantType | OOPTIONAL,        1, { ONAME }},
    { NAME_NeutralDensity | OOPTIONAL,      2, { OINTEGER, OREAL }},
    { NAME_Dynamic | OOPTIONAL,             1, { OBOOLEAN }},
    { NAME_DefaultScreenAngle | OOPTIONAL,  2, { OINTEGER, OREAL }},
    { NAME_OverrideScreenAngle | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  OBJECT * poColorant;
  OBJECT * poParamsDict;
  OBJECT * poParam;
  NAMECACHE * pnmColorant;
  NAMECACHE * pnmRelativeToFrame;

  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;
  SYSTEMVALUE x, y;
  uint32 nRenderingProperties, defaultRenderingProperties, defaultRenderingPropertiesMask ;
  int32 nRelativeToFrame;
  COLORANTINDEX ciRelativeToFrame;
  GUC_FRAMERELATION frameRelation;
  COLORANTINDEX ci;
  int32 specialHandling = -1; /* Rogue value to denote "not yet set" */
  Bool dynamic = FALSE;
  USERVALUE neutralDensity = -1.0f ;
  USERVALUE defaultScreenAngle = 0.0f;
  Bool fOverrideScreenAngle = FALSE;

  poParamsDict = NULL;
  x = y = 0.0;
  nRenderingProperties = 0;
  defaultRenderingPropertiesMask = (RENDERING_PROPERTY_RENDER_ALL|
                                    RENDERING_PROPERTY_MASK_ALL|
                                    RENDERING_PROPERTY_IGNORE_ALL|
                                    RENDERING_PROPERTY_KNOCKOUT_ALL) ;
  defaultRenderingProperties = RENDERING_PROPERTY_RENDER_ALL;
  nRelativeToFrame = GUC_RELATIVE_TO_FRAME_UNKNOWN;
  frameRelation = GUC_FRAMERELATION_END;

  if (theStackSize(operandstack) < 0)
    return error_handler(STACKUNDERFLOW);

  poColorant = theTop(operandstack);

  if (oType(*poColorant) == ODICTIONARY) {
    uint32 index ;

    if (theStackSize(operandstack) < 1)
      return error_handler(STACKUNDERFLOW);
    poParamsDict  = poColorant;
    poColorant = stackindex(1, & operandstack);

    /* decode the dictionary parameters as described above: x, y, Frame, InsertBefore */
    if (! dictmatch (poParamsDict, paramsDictMatch))
        return FALSE;

    /* x */
    poParam = paramsDictMatch[paramsDictMatch_x].result;
    if (poParam != NULL)
      x = object_numeric_value(poParam) ;

    /* y */
    poParam = paramsDictMatch[paramsDictMatch_y].result;
    if (poParam != NULL)
      y = object_numeric_value(poParam) ;

    /* Frame */
    poParam = paramsDictMatch[paramsDictMatch_Frame].result;
    if (poParam != NULL) {
      switch (oType(*poParam)) {
      case ONAME:
      case OSTRING:
        if (oType(*poParam) == ONAME)
          pnmRelativeToFrame = oName(*poParam);
        else
          pnmRelativeToFrame = cachename(oString(*poParam), theLen(*poParam));

        ciRelativeToFrame = guc_colorantIndex(gsc_getRS(gstateptr->colorInfo), pnmRelativeToFrame);
        if (ciRelativeToFrame == COLORANTINDEX_UNKNOWN ||
            ciRelativeToFrame == COLORANTINDEX_NONE ||
            ciRelativeToFrame == COLORANTINDEX_ALL)
          return detail_error_handler(RANGECHECK, "Frame name was not found.");

        nRelativeToFrame = guc_frameRelation(gsc_getRS(gstateptr->colorInfo), ciRelativeToFrame);
        if (nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN)
          return detail_error_handler(RANGECHECK, "Frame does not have a renderable colorant.");
        break;

      case OINTEGER:
        nRelativeToFrame = oInteger (*poParam);
        break;

      default:
        HQFAIL ("dictmatch let unexpected type through for x");
        break;
      }

      /* If Frame is given, but InsertBefore is not, we insert at
         given position, rather than defaulting to end */
      frameRelation = GUC_FRAMERELATION_AT;
    }

    /* InsertBefore */
    poParam = paramsDictMatch[paramsDictMatch_InsertBefore].result;
    if (poParam != NULL) {
      if (oBool(*poParam))
        frameRelation = GUC_FRAMERELATION_BEFORE;
      else
        frameRelation = GUC_FRAMERELATION_AFTER;

      if (nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN)
        return detail_error_handler(RANGECHECK, "InsertBefore given but not Frame.");
    }

    /* ColorantType */
    poParam = paramsDictMatch[paramsDictMatch_ColorantType].result ;
    if ( poParam ) {
      /* This needs careful documentation. The ColorantType in question here
       * is directly related to the ColorantType defined by Adobe in the RB3
       * as it appears in the ColorantDetails dictionary. We have extended it
       * to include some new values. Now, the GUC_COLORANT structure (and for
       * that matter the ColorantInfo structure) already has a colorantType
       * field which means something different. So from now on (including
       * the transfer of this information into the PageHeaderColorant) this
       * property of the colorant will be known as specialHandling.
       */
      switch ( oNameNumber(*poParam) ) {
      case NAME_Normal:
        specialHandling = SPECIALHANDLING_NONE ;
        break ;
      case NAME_Opaque:
        specialHandling = SPECIALHANDLING_OPAQUE ;
        break ;
      case NAME_OpaqueIgnore:
        specialHandling = SPECIALHANDLING_OPAQUEIGNORE ;
        break ;
      case NAME_Transparent:
        specialHandling = SPECIALHANDLING_TRANSPARENT ;
        break ;
      case NAME_TrapZones:
        specialHandling = SPECIALHANDLING_TRAPZONES ;
        break ;
      case NAME_TrapHighlights:
        specialHandling = SPECIALHANDLING_TRAPHIGHLIGHTS ;
        break ;
      default:
        return namedinfo_error_handler( RANGECHECK , NAME_ColorantType ,
                                        poParam ) ;
      }
    }

    /* NeutralDensity */
    poParam = paramsDictMatch[paramsDictMatch_NeutralDensity].result;
    if (poParam != NULL) {
      if ( !object_get_real(poParam, &neutralDensity) ) {
        HQFAIL("dictmatch let unexpected type through for NeutralDensity");
      }
    }

    /* Dynamic */
    poParam = paramsDictMatch[paramsDictMatch_Dynamic].result;
    if ( poParam != NULL ) {
      dynamic = oBool(*poParam);
    }

    /* DefaultScreenAngle */
    poParam = paramsDictMatch[paramsDictMatch_DefaultScreenAngle].result;
    if (poParam != NULL) {
      if ( !object_get_real(poParam, &defaultScreenAngle) ) {
        HQFAIL("dictmatch let unexpected type through for DefaultScreenAngle");
      }
    }

    /* OverrideScreenAngle */
    poParam = paramsDictMatch[paramsDictMatch_OverrideScreenAngle].result;
    if (poParam != NULL) {
      if (paramsDictMatch[paramsDictMatch_DefaultScreenAngle].result == NULL)
        /* Can't have OverrideScreenAngle without DefaultScreenAngle */
        return error_handler(UNDEFINED);
      fOverrideScreenAngle = oBool(*poParam);
    }

    /* Default. Sets default rendering properties */
    poParam = paramsDictMatch[paramsDictMatch_Default].result;
    if (poParam != NULL) {
      if (oType(*poParam) == ONAME) {
        if (oName(*poParam) == system_names + NAME_Ignore)
          defaultRenderingProperties = RENDERING_PROPERTY_IGNORE_ALL ;
        else if (oName(*poParam) == system_names + NAME_Render)
          defaultRenderingProperties = RENDERING_PROPERTY_RENDER_ALL ;
        else if (oName(*poParam) == system_names + NAME_Knockout)
          defaultRenderingProperties = RENDERING_PROPERTY_KNOCKOUT_ALL ;
        else if (oName(*poParam) == system_names + NAME_Mask) {
          defaultRenderingProperties = RENDERING_PROPERTY_MASK_ALL ;
        } else
          return error_handler(RANGECHECK) ;
      } else if (oBool(*poParam)) {
        defaultRenderingProperties = RENDERING_PROPERTY_RENDER_ALL ;
      } else { /* else false boolean implies knockout */
        defaultRenderingProperties = RENDERING_PROPERTY_KNOCKOUT_ALL ;
      }
    }

    /* Picture, Text, Vignette, NamedColor, Other, Linework, Composite. */
    for ( index = 0 ;
          index < paramsDictMatch_Default - paramsDictMatch_Picture ;
          ++index ) {
      HQASSERT(index < NUM_ARRAY_ITEMS(property_details),
               "Not enough property details") ;
      HQASSERT(property_details[index].name == (paramsDictMatch[index + paramsDictMatch_Picture].name & ~OOPTIONAL),
               "Property name does not match dictmatch name") ;

      poParam = paramsDictMatch[index + paramsDictMatch_Picture].result;
      if (poParam != NULL) {
        uint32 mask = property_details[index].mask ;

        HQASSERT((nRenderingProperties & mask) == 0,
                 "Somehow set rendering properties already") ;
        defaultRenderingPropertiesMask &= ~mask ; /* Clear default if set explicitly */
        if (oType(*poParam) == ONAME) {
          if (oName(*poParam) == system_names + NAME_Ignore)
            nRenderingProperties |= (mask & RENDERING_PROPERTY_IGNORE_ALL) ;
          else if (oName(*poParam) == system_names + NAME_Render)
            nRenderingProperties |= (mask & RENDERING_PROPERTY_RENDER_ALL) ;
          else if (oName(*poParam) == system_names + NAME_Knockout)
            nRenderingProperties |= (mask & RENDERING_PROPERTY_KNOCKOUT_ALL) ;
          else if (oName(*poParam) == system_names + NAME_Mask) {
            if ( paramsDictMatch[paramsDictMatch_Default].result == NULL )
              defaultRenderingProperties = RENDERING_PROPERTY_KNOCKOUT_ALL ;
            nRenderingProperties |= (mask & RENDERING_PROPERTY_MASK_ALL) ;
          } else
            return error_handler(RANGECHECK) ;
        } else if (oBool(*poParam)) {
          nRenderingProperties |= (mask & RENDERING_PROPERTY_RENDER_ALL) ;
        } else { /* else leave bits unset to knockout */
          nRenderingProperties |= (mask & RENDERING_PROPERTY_KNOCKOUT_ALL) ;
        }

        /* Indicate that this repro type was explicitly set. */
        nRenderingProperties |= (mask & RENDERING_PROPERTY_EXPLICIT_ALL) ;
      }
    }
  }

  /* Add in default rendering properties */
  nRenderingProperties |= (defaultRenderingProperties & defaultRenderingPropertiesMask) ;

  if ( RENDERING_PROPERTY_HAS_MASK(nRenderingProperties) &&
       RENDERING_PROPERTY_HAS_RENDER(nRenderingProperties) ) {
    return
      detail_error_handler(RANGECHECK, "Rendering and masking properties are not valid together.");
  }

  /* If Vignette disposition is different from LineWork, turn on Vignette
     detection */
  if ( context->userparams->VignetteDetect == VDL_None ) {
    uint32 vprop = (RENDERING_PROPERTY_VIGNETTE & nRenderingProperties) ;
    uint32 oprop = (RENDERING_PROPERTY_LW & nRenderingProperties) ;

    if ( RENDERING_PROPERTY_HAS_MASK(vprop) != RENDERING_PROPERTY_HAS_MASK(oprop) ||
         RENDERING_PROPERTY_HAS_RENDER(vprop) != RENDERING_PROPERTY_HAS_RENDER(oprop) ||
         RENDERING_PROPERTY_HAS_KNOCKOUT(vprop) != RENDERING_PROPERTY_HAS_KNOCKOUT(oprop) ) {
      context->userparams->VignetteDetect = VDL_Simple ;
    }
  }

  if (oType(*poColorant) == OSTRING) {
    pnmColorant = cachename(oString(*poColorant), theLen(*poColorant));
  } else if (oType(*poColorant) == ONAME) {
    pnmColorant = oName(*poColorant);
  } else {
    return detail_error_handler(TYPECHECK, "Colorant name is invalid.");
  }

  /* The specialHandling defaults to whatever's in ColorantDetails if one wasn't
   * provided in the (optional) params dictionary.
   */

  if ( specialHandling == -1 )
    specialHandling = guc_colorantSpecialHandling( gsc_getRS(gstateptr->colorInfo),
                                                   pnmColorant ) ;
  if ( neutralDensity <= 0.0f ) {
    neutralDensity = guc_colorantNeutralDensity( gsc_getRS(gstateptr->colorInfo),
                                                 pnmColorant ) ;
    /* ... and it may still be -1 after that if the colorant isn't in
       ColorantDetails or that doesn't contain NeutralDensity */
  }

  /* convert separation offsets to device space */
  x = x * page->xdpi / 72.0 + 0.5;
  if (x < 0.0) x -= 1.0;
  if ( doing_mirrorprint ) x = -x;

  y = y * page->ydpi / 72.0 + 0.5;
  if (y < 0.0) y -= 1.0;

  if (! guc_newFrame(gsc_getRS(gstateptr->colorInfo), pnmColorant,
                     (int32) x, (int32) y,
                     FALSE, /* not background */
                     dynamic,
                     nRenderingProperties,
                     frameRelation, nRelativeToFrame,
                     specialHandling, neutralDensity,
                     defaultScreenAngle, fOverrideScreenAngle,
                     DOING_RUNLENGTH(page),
                     & ci))
    return FALSE;

  if (ci == COLORANTINDEX_UNKNOWN)
    return detail_error_handler(RANGECHECK, "Colorant is unknown.");

  /* Update equivalent colorants for current blend space target. */
  if (!guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo), ci))
    return FALSE;

  /* Following the successful addition of a new colorant, we will patch
   * in a new screen into the page's default screen, updating page->
   * default_spot_no in the process. This is beneficial for composited
   * objects in the new colorant that would otherwise get their screen
   * angles from some other place. */
  invalidate_gstate_screens() ;
  if (!gsc_redo_setscreen(gstateptr->colorInfo))
    return FALSE;
  if (page->default_spot_no != gsc_getSpotno(gstateptr->colorInfo)) {
    SPOTNO newSpotno;
    newSpotno = ht_mergespotnoentry(page->default_spot_no,
                                    gsc_getSpotno(gstateptr->colorInfo),
                                    HTTYPE_DEFAULT, ci,
                                    page->eraseno);
    if ( newSpotno > 0 ) {
      page->default_spot_no = newSpotno;
      ht_set_page_default(newSpotno);
    } else {
      if (newSpotno != 0)
        return FALSE;
    }
  }

  npop (poParamsDict == NULL ? 1 : 2, & operandstack);
  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool setbackgroundseparation_(ps_context_t *pscontext)
{
  OBJECT * poColorant;
  NAMECACHE * pnmColorant;
  COLORANTINDEX ci;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (theStackSize(operandstack) < 0)
    return error_handler(STACKUNDERFLOW);

  poColorant = theTop(operandstack);

  if (oType(*poColorant) == OSTRING) {
    pnmColorant = cachename(oString(*poColorant), theLen(*poColorant));
  } else if (oType(*poColorant) == ONAME) {
    pnmColorant = oName(*poColorant);
  } else {
    return error_handler(TYPECHECK);
  }

  ci = guc_colorantIndex(gsc_getRS(gstateptr->colorInfo), pnmColorant);

  if (ci == COLORANTINDEX_UNKNOWN)
    return detail_error_handler(RANGECHECK, "Colorant is unknown.");
  else if (ci == COLORANTINDEX_NONE)
    return detail_error_handler(RANGECHECK, "None colorant is not allowed.");
  else if (ci == COLORANTINDEX_ALL)
    return error_handler(RANGECHECK);

  pop (& operandstack);

  return guc_colorantSetBackgroundSeparation(gsc_getRS(gstateptr->colorInfo), ci);
}

/* -------------------------------------------------------------------------- */
/* Implements the operator setcolormapping. This is of the form:
 *   << /Cyan [ (Photo Light Cyan) (Photo Light Cyan) ] >> setcolormapping
 *   ...
 *   << /Black [ (Photo Black) ] >> setcolormapping
 *   << (Pantone 123) [ (Pantone 123: Cyan) ... (Pantone 123: Black) ] >> setcolormapping
 *   << /Cyan null >> setcolormapping
 * A -null- value indicates remove any mappings for that key.
 * Note that multiple mappings may be set inside a single dictionary.
 */
static Bool set_colorantmappings( OBJECT *key , OBJECT *val , void *argBlockPtr )
{
  int32 i ;
  int32 type ;
  int32 length ;
  int32 verify ;
  OBJECT *olist ;
  NAMECACHE *name ;

  HQASSERT( key != NULL , "key NULL in set_colorantmappings" ) ;
  HQASSERT( val != NULL , "val NULL in set_colorantmappings" ) ;
  HQASSERT( argBlockPtr != NULL , "argBlockPtr NULL in set_colorantmappings" ) ;

  verify = * ( int32 * )argBlockPtr ;

  type = oType( *key ) ;
  if ( type == OSTRING ) {
    name = cachename(oString(*key), theLen(*key)) ;
    if ( name == NULL ) {
      HQASSERT( verify , "Should have cached cachename in verification pass" ) ;
      return FALSE ;
    }
  }
  else {
    if ( type != ONAME ) {
      HQASSERT( verify , "Should have checked type in verification pass" ) ;
      return error_handler( TYPECHECK ) ;
    }
    name = oName( *key ) ;
    HQASSERT( name != NULL , "name NULL in set_colorantmappings" ) ;
  }

  if ( verify ) {
    if ( name == system_names + NAME_All ||
         name == system_names + NAME_None )
      return error_handler( TYPECHECK ) ;
  }
  else {
    HQASSERT( name != system_names + NAME_All &&
              name != system_names + NAME_None ,
              "Should have checked name in verification pass" ) ;
  }

  type = oType( *val ) ;
  if ( type != OARRAY && type != OPACKEDARRAY &&
       type != ONULL )
    return error_handler( TYPECHECK ) ;

  if ( type == ONULL ) {
    /* A -null- OBJECT means remove that mapping. */
    if ( ! verify ) {
      COLORANTINDEX ci ;
      ci = guc_colorantIndexReserved( gsc_getRS(gstateptr->colorInfo), name ) ;
      HQASSERT( ci != COLORANTINDEX_ALL &&
                ci != COLORANTINDEX_NONE ,
                "set_colorantmappings: ci either all or none" ) ;
      if ( ci != COLORANTINDEX_UNKNOWN ) {
        if ( ! guc_setColorantMapping(gsc_getRS(gstateptr->colorInfo), ci, NULL, 0) )
          return FALSE ;
      }
    }
  }
  else {
    COLORANTINDEX ci ;
    COLORANTINDEX *ci_map ;

    ci = COLORANTINDEX_UNKNOWN ;  /* Be quiet, oh ye compiler of little faith. */
    ci_map = NULL ;
    if ( ! verify ) {
      if ( !guc_colorantIndexPossiblyNewName( gsc_getRS(gstateptr->colorInfo), name, &ci ))
        return FALSE;
      HQASSERT( ci != COLORANTINDEX_ALL &&
                ci != COLORANTINDEX_NONE &&
                ci != COLORANTINDEX_UNKNOWN ,
                "set_colorantmappings: ci either all, none or unknown" ) ;

      length = theLen(*val) ;
      HQASSERT( length >= 0 , "length < 0 in set_colorantmappings" ) ;
      ci_map = mm_alloc_with_header( mm_pool_temp ,
                                     length * sizeof( COLORANTINDEX ) ,
                                     MM_ALLOC_CLASS_NCOLOR ) ;
      if ( ci_map == NULL )
        return error_handler( VMERROR ) ;
    }

    length = theLen(*val) ;
    HQASSERT( length >= 0 , "length < 0 in set_colorantmappings" ) ;
    olist = oArray( *val ) ;
    HQASSERT( ( olist == NULL && length == 0 ) ||
              ( olist != NULL && length != 0 ) ,
              "olist & length illegal values in set_colorantmappings" ) ;
    for ( i = 0 ; i < length ; ++i , ++olist ) {
      type = oType( *olist ) ;
      if ( type == OSTRING ) {
        name = cachename(oString(*olist), theLen(*olist)) ;
        if ( name == NULL ) {
          HQASSERT( verify , "Should have cached cachename in verification pass" ) ;
          return FALSE ;
        }
      }
      else {
        if ( type != ONAME ) {
          HQASSERT( verify , "Should have checked type in verification pass" ) ;
          return error_handler( TYPECHECK ) ;
        }
        name = oName( *olist ) ;
        HQASSERT( name != NULL , "name NULL in set_colorantmappings" ) ;
      }

      if ( verify ) {
        if ( name == system_names + NAME_All ||
             name == system_names + NAME_None )
          return error_handler( TYPECHECK ) ;
      }
      else {
        COLORANTINDEX cit ;
        HQASSERT( name != system_names + NAME_All &&
                  name != system_names + NAME_None ,
                  "Should have checked name in verification pass" ) ;
        if ( !guc_colorantIndexPossiblyNewName( gsc_getRS(gstateptr->colorInfo), name, &cit ))
          return FALSE;
        HQASSERT( cit != COLORANTINDEX_ALL &&
                  cit != COLORANTINDEX_NONE &&
                  cit != COLORANTINDEX_UNKNOWN ,
                  "set_colorantmappings: cit either all, none or unknown" ) ;
        ci_map[ i ] = cit ;
      }
    }
    if ( ! verify ) {
      Bool result ;

      result = guc_setColorantMapping(gsc_getRS(gstateptr->colorInfo), ci, ci_map, length) ;
      mm_free_with_header( mm_pool_temp , ci_map ) ;
      if ( ! result )
        return FALSE ;
    }
  }

  /* Update equivalent colorants for current blend space target. */
  return guc_updateEquivalentColorants(gsc_getTargetRS(gstateptr->colorInfo),
                                       COLORANTINDEX_ALL) ;
}

Bool setcolorantmapping_(ps_context_t *pscontext)
{
  Bool verify ;
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  HQASSERT( theo != NULL , "theo NULL in setcolorantmapping_" ) ;
  if ( oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  verify = TRUE ;
  if ( ! walk_dictionary( theo , set_colorantmappings , & verify ))
    return FALSE ;

  verify = FALSE ;
  if ( ! walk_dictionary( theo , set_colorantmappings , & verify ))
    return FALSE ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
enum {
  csdm_Properties, csdm_X, csdm_Y, csdm_Omit, csdm_Name,
  csdm_Separation, csdm_ColorantIndex, csdm_sRGB, csdm_CMYK,
  csdm_ColorantType, csdm_Background,
  csdm_dummy
} ;

static NAMETYPEMATCH currentseparationdictmatch[csdm_dummy + 1] = {
  { NAME_Properties | OOPTIONAL,     2, { ODICTIONARY, ONULL }},
  { NAME_X | OOPTIONAL,              2, { OINTEGER, ONULL }},
  { NAME_Y | OOPTIONAL,              2, { OINTEGER, ONULL }},
  { NAME_Omit | OOPTIONAL,           2, { OBOOLEAN, ONULL }},
  { NAME_Name | OOPTIONAL,           3, { ONAME, OSTRING, ONULL }},
  { NAME_Separation | OOPTIONAL,     3, { ONAME, OSTRING, ONULL }},
  { NAME_ColorantIndex | OOPTIONAL,  2, { OINTEGER, ONULL }},
  { NAME_sRGB | OOPTIONAL,           2, { OARRAY, ONULL }},
  { NAME_CMYK | OOPTIONAL,           2, { OARRAY, ONULL }},
  { NAME_ColorantType | OOPTIONAL,   2, { ONAME, ONULL }},
  { NAME_Background | OOPTIONAL,     2, { OBOOLEAN, ONULL }},
  DUMMY_END_MATCH
};

typedef struct currsepinfo_args {
  GUCR_RASTERSTYLE *hr ;
  GUCR_COLORANT* hc ;
  STACK *stack ;
  int32 dictsize ;
  int32 omitpresep ;
  int32 nstack ;
} currsepinfo_args ;

static Bool currentseparationinfo(currsepinfo_args *args)
{
  OBJECT thed = OBJECT_NOTVM_NOTHING ;
  int32 i, j ;
  Bool check = TRUE ;

  const GUCR_COLORANT_INFO *colorantInfo;

  if ( !gucr_colorantDescription(args->hc, &colorantInfo) )
    return TRUE ;

  /* Make two passes over info; first time (check == TRUE) just match and
     typecheck the dictionary entries. Second time create the sub-dictionaries
     and insert the hash values */
  do {

    if ( !check &&
         !ps_dictionary(&thed, args->dictsize) )
      return FALSE ;

    for ( i = 0 ; theISomeLeft(&currentseparationdictmatch[i]) ; ++i ) {
      OBJECT *matched = currentseparationdictmatch[i].result ;
      if ( matched ) {
        OBJECT value = OBJECT_NOTVM_NOTHING ;
        int32 nameindex = (currentseparationdictmatch[i].name & ~OOPTIONAL) ;

        switch ( nameindex ) {
        case NAME_Omit:
          theTags(value) = OBOOLEAN | LITERAL ;
          /* This is the same condition that is used in guc_omitSeparations in
             gu_chan.c to decide whether to omit separations when printing a
             gray or preseparate job. */
          if ( args->omitpresep ) {
            if ( (colorantInfo->originalName == NULL &&
                  colorantInfo->name != &system_names[NAME_Black]) ||
                 (colorantInfo->originalName != NULL &&
                  colorantInfo->originalName != &system_names[NAME_Black]) )
              oBool(value) = TRUE ;
            else
              oBool(value) = guc_fOmittingSeparation(args->hr, colorantInfo->colorantIndex) ;
          } else
            oBool(value) = guc_fOmittingSeparation(args->hr, colorantInfo->colorantIndex) ;

          if ( check &&
               oType(*matched) == OBOOLEAN &&
               oBool(value) != oBool(*matched) )
            return TRUE ;

          break ;
        case NAME_Name:
          theTags(value) = ONAME | LITERAL ;
          oName(value) = colorantInfo->name ;

          if ( check )
            if ( (oType(*matched) == ONAME &&
                  oName(value) != oName(*matched)) ||
                 (oType(*matched) == OSTRING &&
                  oName(value) != cachename(oString(*matched), theLen(*matched))) )
              return TRUE ;

          break ;
        case NAME_Separation: {
          NAMECACHE *sepname = (colorantInfo->originalName ?
                                colorantInfo->originalName :
                                colorantInfo->name) ;

          theTags(value) = ONAME | LITERAL ;
          oName(value) = sepname ;

          if ( check )
            if ( (oType(*matched) == ONAME && sepname != oName(*matched)) ||
                 (oType(*matched) == OSTRING &&
                  sepname != cachename(oString(*matched), theLen(*matched))) )
              return TRUE ;

          break ;
        }
        case NAME_Properties:
          if ( !check &&
               !ps_dictionary(&value, NUM_ARRAY_ITEMS(property_details)) )
            return FALSE ;

          for ( j = 0 ; j < NUM_ARRAY_ITEMS(property_details) ; ++j ) {
            OBJECT subname = OBJECT_NOTVM_NOTHING,
              subvalue = OBJECT_NOTVM_NOTHING ;
            int32 name ;
            uint32 masked ;

            masked = (colorantInfo->nRenderingProperties &
                      property_details[j].mask) ;

            /* If the intent is not explicitly set, use Default instead. */
            name = property_details[j].name ;
            if ( (colorantInfo->nRenderingProperties &
                  property_details[j].mask &
                  RENDERING_PROPERTY_EXPLICIT_ALL) == 0 )
              name = NAME_Default ;

            object_store_name(&subname, name, LITERAL) ;

            theTags(subvalue) = ONAME | LITERAL ;
            if ( RENDERING_PROPERTY_HAS_RENDER(masked) )
              oName(subvalue) = &system_names[NAME_Render] ;
            else if ( RENDERING_PROPERTY_HAS_MASK(masked) )
              oName(subvalue) = &system_names[NAME_Mask] ;
            else if ( RENDERING_PROPERTY_HAS_KNOCKOUT(masked) )
              oName(subvalue) = &system_names[NAME_Knockout] ;
            else
              oName(subvalue) = &system_names[NAME_Ignore] ;

            if ( check ) { /* First pass, typecheck and match */
              OBJECT *submatch ;

              if ( oType(*matched) == ODICTIONARY &&
                   (submatch = fast_extract_hash(matched, &subname)) != NULL ) {
                if ( oType(*submatch) == ONAME ) {
                  if ( oName(*submatch) != oName(subvalue) )
                    return TRUE ;
                } else if ( oType(*submatch) != ONULL )
                  return error_handler(TYPECHECK) ;
              }
            } else { /* Second pass, do insertion */
              if ( !insert_hash(&value, &subname, &subvalue) )
                return FALSE ;
            }
          }

          break ;
        case NAME_X:
          object_store_integer(&value, colorantInfo->offsetX) ;

          if ( check &&
               oType(*matched) == OINTEGER &&
               oInteger(value) != oInteger(*matched) )
            return TRUE ;

          break ;
        case NAME_Y:
          object_store_integer(&value, colorantInfo->offsetY) ;

          if ( check &&
               oType(*matched) == OINTEGER &&
               oInteger(value) != oInteger(*matched) )
            return TRUE ;

          break ;
        case NAME_ColorantIndex:
          object_store_integer(&value, colorantInfo->colorantIndex) ;

          if ( check &&
               oType(*matched) == OINTEGER &&
               oInteger(value) != oInteger(*matched) )
            return TRUE ;

          break ;
        case NAME_sRGB: {

          if ( check ) {
            if ( oType(*matched) == OARRAY ) {
              OBJECT *matchlist = oArray(*matched) ;

              if ( theLen(*matched) != 3 )
                return error_handler(RANGECHECK) ;

              for ( j = 0 ; j < 3 ; ++j ) {
                USERVALUE rgb ;

                if ( !object_get_real(&matchlist[j], &rgb) )
                  return error_handler(TYPECHECK) ;

                if ( rgb != colorantInfo->sRGB[j] )
                  return TRUE ;
              }
            }
          } else {
            if ( !ps_array(&value, 3) )
              return FALSE ;

            for ( j = 0 ; j < 3 ; ++j ) {
              object_store_real(&oArray(value)[j], colorantInfo->sRGB[j]) ;
            }
          }

          break ;
        }
        case NAME_CMYK: {

          if ( check ) {
            if ( oType(*matched) == OARRAY ) {
              OBJECT *matchlist = oArray(*matched) ;

              if ( theLen(*matched) != 4 )
                return error_handler(RANGECHECK) ;

              for ( j = 0 ; j < 4 ; ++j ) {
                USERVALUE cmyk ;

                if ( !object_get_real(&matchlist[j], &cmyk) )
                  return error_handler(TYPECHECK) ;

                if ( cmyk != colorantInfo->CMYK[j] )
                  return TRUE ;
              }
            }
          } else {
            if ( !ps_array(&value, 4) )
              return FALSE ;

            for ( j = 0 ; j < 4 ; ++j ) {
              object_store_real(&oArray(value)[j], colorantInfo->CMYK[j]) ;
            }
          }

          break ;
        }
        case NAME_ColorantType:
          theTags(value) = ONAME | LITERAL ;
          switch ( colorantInfo->colorantType ) {
          case COLORANTTYPE_PROCESS:
            oName(value) = &system_names[NAME_Process] ;
            break ;
          case COLORANTTYPE_SPOT:
            oName(value) = &system_names[NAME_Spot] ;
            break ;
          case COLORANTTYPE_EXTRASPOT:
            oName(value) = &system_names[NAME_ExtraSpot] ;
            break ;
          default:
            HQFAIL("Unknown colorant type") ; /* FALLTHRU */
          case COLORANTTYPE_UNKNOWN:
            oName(value) = &system_names[NAME_Unknown] ;
            break ;
          }

          if ( check &&
               oType(*matched) == ONAME &&
               oName(value) != oName(*matched) )
            return TRUE ;

          break ;
        case NAME_Background:
          object_store_bool(&value, colorantInfo->fBackground) ;

          if ( check &&
               oType(*matched) == OBOOLEAN &&
               oBool(value) != oBool(*matched) )
            return TRUE ;

          break ;
        default:
          HQFAIL("Unknown dictmatch name in currentseparationinfo") ;
        }

        if ( !check ) {
          oName(nnewobj) = &system_names[nameindex] ;
          if ( !insert_hash(&thed, &nnewobj, &value) )
            return FALSE ;
        }
      }
    }
    check = !check ; /* Check cycles TRUE->FALSE->TRUE then loop exits */
  } while ( !check ) ;

  if ( !push(&thed, args->stack) )
    return FALSE ;

  ++args->nstack ;

  return TRUE ;
}

Bool currentseparationorder_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;
  currsepinfo_args args ;
  GUCR_CHANNEL* hf ;
  int32 nSheets = 0 ;
  OBJECT *topo ;
  int32 i ;
  Bool saved = FALSE, result = FALSE ;
  GUCR_COLORANTSET* save_cs = NULL ;

  args.hr = gsc_getRS(gstateptr->colorInfo);
  /* args.hc set in loop/callback later */
  args.stack = &operandstack ;
  args.dictsize = 0 ;
  args.nstack = 0 ;
  args.omitpresep = FALSE ;

  if ( EmptyStack(theIStackSize(args.stack)) )
    return error_handler(STACKUNDERFLOW) ;

  topo = theITop(args.stack) ;

  if ( oType(*topo) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  if ( !dictmatch(topo, currentseparationdictmatch) )
    return FALSE ;

  /* Count how many matches we made. Don't care about actual values. */
  for ( i = 0 ; theISomeLeft(&currentseparationdictmatch[i]) ; ++i )
    if ( currentseparationdictmatch[i].result != NULL )
      ++args.dictsize ;

  if ( args.dictsize == 0 )
    return error_handler(RANGECHECK) ;

  /* After this point, you MUST cleanup properly, or you may leak memory. */
#define return DO_NOT_RETURN_-_GO_TO_revert_separations_INSTEAD!

  /* Omit specified? */
  if ( currentseparationdictmatch[csdm_Omit].result ) {

    /* Check for invalid combinations of color jobs & output color mode.
       Invalid combinations have already been checked for recombining work.
       This is the same set of conditions that are used to decide to call
       guc_omitSeparations from render.c. */
    if ( context->systemparams->DetectSeparation &&
         !rcbn_enabled() &&
         !page_is_composite() &&
         gucr_separationStyle(args.hr) == GUCR_SEPARATIONSTYLE_SEPARATIONS ) {
      int32 nColorants ;
      DEVICESPACEID deviceSpace ;

        guc_deviceColorSpace(args.hr, &deviceSpace, &nColorants) ;

        if ( deviceSpace == DEVICESPACE_CMYK ||
             deviceSpace == DEVICESPACE_RGBK )
          args.omitpresep = TRUE ;
      }

    if ( !update_erase(page) ) /* Must have up-to-date erase color */
      goto revert_separations;

    if ( !guc_saveOmitSeparations(args.hr, &save_cs) )
      goto revert_separations ;
    saved = TRUE ;

    if ( !omit_blank_separations(page) )
      goto revert_separations ;
  }

  for ( hf = gucr_framesStart(args.hr) ; /* returns handle */
        gucr_framesMore(hf) ;
        gucr_framesNext(&hf)) {

    for ( args.hc = gucr_colorantsStart(hf) ;
          gucr_colorantsMore(args.hc, GUCR_INCLUDING_PIXEL_INTERLEAVED) ;
          gucr_colorantsNext(&args.hc) ) {
      if ( !currentseparationinfo(&args) )
        goto revert_separations ;
    }

    if ( args.nstack > 0 && gucr_framesEndOfSheet(hf) ) {
      oInteger(inewobj) = args.nstack ;

      if ( !push(&inewobj, args.stack) ||
           !array_(pscontext) ||
           !astore_(pscontext) )
        goto revert_separations ;

      ++nSheets ;
      args.nstack = 0 ;
    }
  }

  HQASSERT(args.nstack == 0, "Ended frames but last wasn't end of sheet") ;

  oInteger(inewobj) = nSheets ;

  if ( !push(&inewobj, args.stack) ||
       !array_(pscontext) ||
       (nSheets != 0 && !astore_(pscontext)) ||
       !exch_(pscontext) )
    goto revert_separations ;

  pop(args.stack) ;

  result = TRUE ;

revert_separations:
  if ( saved ) { /* revert saved separations */
    (void)guc_restoreOmitSeparations(args.hr, save_cs, TRUE) ;
  } else {
    HQASSERT(save_cs == NULL, "save handle not NULL but not saved?");
  }
#undef return

  return result ;
}

/* ---------------------------------------------------------------------- */
/* currentseparationcolornames merely returns the private Colorants
   dictionary. */
Bool currentseparationcolornames_(ps_context_t *pscontext)
{
  OBJECT *ffcdict = gucs_fullyFledgedColorants(gsc_getRS(gstateptr->colorInfo)) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  HQASSERT(ffcdict, "No colorants dictionary in currentseparationcolornames") ;

  return push(ffcdict, &operandstack) ;
}

/* ---------------------------------------------------------------------- */
Bool setstrokeadjust_(ps_context_t *pscontext)
{
  int32 new;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (! get1B(& new))
    return FALSE;
  thegsDeviceStrokeAdjust(*gstateptr) = (int8)new;
  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool currentstrokeadjust_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push(thegsDeviceStrokeAdjust(*gstateptr)
              ? &tnewobj : &fnewobj,
              &operandstack);
}

/* ---------------------------------------------------------------------- */

/* returns a bool depending on whether backdropAutoSeparations is effective */
Bool autoseparationstatus_(ps_context_t *pscontext)
{
  DL_STATE *page = ps_core_context(pscontext)->page;
  Bool autoseps =
    page->backdropAutoSeparations &&
    page->currentGroup != NULL &&
    guc_backdropRasterStyle(groupInputRasterStyle(groupBase(page->currentGroup)));

  return push(autoseps ? &tnewobj : &fnewobj, &operandstack);
}


void init_C_globals_devops(void)
{
  static OMATRIX init_pageBaseMatrix = { 1.0 , 0.0 , 0.0 , 1.0 , 0.0 , 0.0 , MATRIX_OPT_0011 } ;
  static uint32 init_securityArray[8] = { 0 };

  doing_imposition = FALSE;
  doing_mirrorprint = FALSE;
  showno = 0;
  bandid = 0;
  pageBaseMatrixId = PAGEBASEID_INITIAL ;
  pageBaseMatrix = init_pageBaseMatrix ;
  HqMemCpy(securityArray, init_securityArray, sizeof(init_securityArray)) ;
  rendering_in_progress = PAGEDEVICE_NOT_ACTIVE;
  ea_check_result = FALSE;
  doing_security = FALSE;
  lastcode = CODEINITIALISER;
  security = 0 ;
  showpage_count = 0;
  doing_setpagedevice = 0;
  deferred_setpagedevice = FALSE;
  resolutionExceeded = FALSE ;
  gfPseudoErasepage = FALSE ;
  hPageRange = NULL ;
  Rb = 0 ;
  security_check = 1 ;
  security_index = 0 ;
  security_check_two = 1 ;
  security_index_two = 0 ;
}


/* Old tickle function period was at least 5 seconds.  The issue of security
 * checks causing unpredictable extra processing was more to do with the
 * security checks in setpagedevice and show/copypage which are now not done.
 * The check period may need to be dynamic based on the type of security device
 * in which case the skin needs to be able to configure this value dynamically.
 *
 * Grist for the mill.
 */
#define CHECK_PERIOD  (5) /* seconds */

hqn_timer_t *ea_checks_timer;

static void HQNCALL ea_checks_do(hqn_timer_t *timer, void *data)
{
  UNUSED_PARAM(hqn_timer_t*, timer);
  UNUSED_PARAM(void*, data);

  security = doSecurityCheck(lastcode, securityArray);
  lastcode = randomiseInRIP(lastcode);
  securityArray[0] ^= lastcode;
  securityArray[1] ^= lastcode;
  securityArray[2] ^= lastcode;
  securityArray[3] ^= lastcode;
  securityArray[4] ^= lastcode;
  securityArray[5] ^= lastcode;
  securityArray[6] ^= lastcode;
  securityArray[7] ^= lastcode;
  lastcode = randomiseInRIP(lastcode);
  ea_check_result = (security^lastcode) != 0 || securityArray[1] != 1;
}

static
Bool ea_checks_swstart(
  struct SWSTART* params)
{
  UNUSED_PARAM(struct SWSTART*, params);

  /* A short time to the first timer callback should ensure an initial security
   * check relatively ASAP */
  ea_checks_timer = hqn_timer_create(50, CHECK_PERIOD*1000, ea_checks_do, NULL);
  if (ea_checks_timer == NULL) {
    return(FALSE);
  }

  return(TRUE);
}

static
void ea_checks_finish(void)
{
  /* Release resources in reverse order to which they were acquired */
  if (ea_checks_timer != NULL) {
    hqn_timer_destroy(ea_checks_timer);
    ea_checks_timer = NULL;
  }
}

void ea_checks_C_globals(
  core_init_fns* fns)
{
  fns->swstart = ea_checks_swstart;
  fns->finish = ea_checks_finish;
}


/*
Log stripped */
