/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:vndetect.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Vignette detection
 *
 * Comments here on things that have not been done:
 * a) adjacent multi-segment lines done by stroke.
 *      (probably never done so don't bother).
 * b) rings going out.
 *      (probably never done so don't bother, but note that if it were done,
 *       vn_maxblt_clipping would need to not reverse order of vignette
 *       elements in this case).
 * c) ring case of succesive paths inside paths (where rings overlap).
 *      (probably never done so don't bother).
 * d) fuzzy match on outline for rings (et al) using same_nfill.
 *      (possible improvement that probably don't need).
 * e) merge based on bbox of objects as opposed to assuming linear(?).
 *      (would have to be a bizare job that requires this).
 * f) adjacent strokes but with different thicknesses.
 *      (probably never done so don't bother).
 * g) vignette detection for userpaths.
 *      (simply more coding that hasn't been done yet).
 * h) multi-path stroked reducing vignettes.
 *      (simply needs extension to cope with multiple dl objects per
 *       stroke/fill object).
 */

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "monitor.h"
#include "objects.h"
#include "swpdfout.h"
#include "namedef_.h"

#include "graphics.h"
#include "pathops.h"
#include "routedev.h"
#include "dlstate.h"
#include "dl_free.h"
#include "gstack.h"
#include "params.h"
#include "gu_fills.h"
#include "control.h"
#include "stacks.h"
#include "gu_path.h"
#include "pathcons.h"
#include "system.h"
#include "dl_bres.h"
#include "trap.h"
#include "psvm.h"
#include "gstate.h"
#include "gu_ctm.h"
#include "dicthash.h"
#include "display.h"
#include "gu_chan.h"

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
#include "fcache.h"
#include "spdetect.h"
#endif

#include "gschead.h"
#include "gscsmplk.h"
#include "gscdevci.h"
#include "gschtone.h"
#include "gschcms.h"
#include "shadex.h"
#include "idlom.h"

#include "panalyze.h"
#include "vnargs.h"
#include "vnobj.h"
#include "vndetect.h"

#include "rcbcntrl.h"

#include "plotops.h" /* for setup_vignette_clipping */
#include "genhook.h"
#include "gs_tag.h"
#include "dl_store.h"
#include "hdlPrivate.h"

#if defined( DEBUG_BUILD )
#include "ripdebug.h"
#endif

#if defined( DEBUG_BUILD )
static int32 debug_vo = FALSE;
static int32 debug_vr = 0;
#endif

#if defined( ASSERT_BUILD )
static Bool trace_vf = FALSE;
static Bool trace_va = FALSE;

char *vd_type_names[] = { /* Path Type */
  "VDT_Unknown", "VDT_Complex", "VDT_Simple", "VDT_Circle",
  "VDT_RectangleDevice", "VDT_RectangleUser", "VDT_Stroke", "VDT_Line"
};

char *vd_match_names[] = { /* Path Match */
  "VDM_Unknown", "VDM_Exact", "VDM_Translated", "VDM_Scaled", "VDM_Rotated"
};

char *vd_style_names[] = { /* Style */
  "VDS_Unknown", "VDS_StrongContained", "VDS_Contained", "VDS_Adjacent",
  "VDS_Overlapped", "VDS_Spaced"
};
#endif /* defined( ASSERT_BUILD ) */


/* color accuracy */
#define CL_EPSILON      (0.000001f)  /* One millionth */
#define CL_EPSILON_HIGH (0.01f)      /* One hundredth */

static Bool analyze_vignette(DL_STATE *page, PATHINFO *path,
                             int32 pathtype, int32 filltype,
                             int32 copythepath, STROKE_PARAMS *sparams,
                             int32 colorType );
static Bool analyze_pathtopath_s(VIGNETTEARGS *currobj, VIGNETTEARGS *prevobj,
                                 int32 pathtests );
static Bool analyze_pathtopath_f(VIGNETTEARGS *currobj, VIGNETTEARGS *prevobj,
                                 int32 pathtests);
static Bool analyze_pathtorect_f(VIGNETTEARGS *currobj, VIGNETTEARGS *prevobj);
static Bool analyze_1stpath_s(VIGNETTEARGS *currobj);
static Bool analyze_2ndpath_f(VIGNETTEARGS *currobj);
static Bool analyze_1stpath_f(VIGNETTEARGS *currobj);

static int8 map_vignette_kind(VIGNETTEARGS *tailobj) ;
static int8 map_vignette_curve(VIGNETTEARGS *headobj);

static Bool build_pathnfill_outline(VIGNETTEARGS *headobj,
                                    VIGNETTEARGS *tailobj,
                                    PATHLIST **thepath, Bool *freepath,
                                    NFILLOBJECT **pnfill,
                                    Bool *iscurved) ;

static Bool vn_maxblt_clipping(VIGNETTEOBJECT * pVignette,
                               LISTOBJECT * pVignetteLobj);

static Bool abort_vignette_chain(DL_STATE *page, VIGNETTEARGS *vobj);
static Bool flush_vignette_chain(DL_STATE *page);

static Bool is_path_inside_extra_cliprect(VIGNETTEARGS *currobj);

typedef struct vn_gs_overrides {
  int32  blackgenerationid;
  OBJECT blackgeneration;

  int32  undercolorremovalid;
  OBJECT undercolorremoval;

  uint8  overprint;
} VN_GS_OVERRIDES;

/* Put these few globals in a structure if this ever needs to be threaded. */
static int32 vd_countfill = 0;
static int32 vd_countlobj = 0;
static Bool vd_forcesplit = FALSE;
static int32 vd_device = DEVICE_ILLEGAL;
static int32 vd_saveddevice = DEVICE_ILLEGAL;
static STATEOBJECT *vd_objectstate[2] = { NULL, NULL };
static SYSTEMVALUE vd_size[2] = { 0.0, 0.0 };
static SYSTEMVALUE vd_disp[2] = { 0.0, 0.0 };
static Bool vd_gotcomposite = FALSE; /* Treat vignette as presep/composite */

static int32 vd_ncolormonotonic = 0;
static uint8 *vd_colormonotonic = NULL;
static int32 vd_ncolorants = 0;
static COLORANTINDEX *vd_colorants = NULL;
static USERVALUE* vd_colorvalues = NULL;
static int32 vd_ncolorvalues = 0;

static int32 vd_flushing = 0; /* Are we flushing a vignette chain? */

/* Tracks BG/UCR, as changes to these may cause a flush_vignette */
static VN_GS_OVERRIDES vd_gsoverrides = { 0 };

static VIGNETTEARGS *vd_headobj = NULL;
static VIGNETTEARGS *vd_tailobj = NULL;

/* For clippath change special cases. */
static int32 vd_cause = VD_Default;
static CLIPPATH vd_clippath = { 0 };
static int32 vd_cliptype = VDT_Unknown;
static PATHINFO vd_clippathinfo = PATHINFO_STATIC(NULL,NULL,NULL);
static Bool vd_initmatrix = FALSE;
static OMATRIX vd_clipinvmatrix = { 1.0, 0.0, 0.0, 1.0,
                                    0.0, 0.0, MATRIX_OPT_0011 };

/* For Quark outer most rolled rect special case. */
static int32 vd_rolledrect = VDR_Unknown;

static Bool vd_flushed_lobj = FALSE;
static LISTOBJECT *vd_lobj = NULL;

static dl_color_t vd_dlc_overprints = { 0 };

static int vd_disposition_flags;

#if defined( DEBUG_BUILD )
static PATHINFO pathoutline = PATHINFO_STATIC(NULL,NULL,NULL);

enum {
  DEBUG_VR_PRINT = 1  /**< Print vignette details */
} ;

void init_vignette_detection_debug( void )
{
#if defined( ASSERT_BUILD )
  register_ripvar(NAME_trace_vf, OBOOLEAN, &trace_vf);
  register_ripvar(NAME_trace_va, OBOOLEAN, &trace_va);
#endif
  register_ripvar(NAME_debug_vr, OINTEGER, &debug_vr);
  register_ripvar(NAME_debug_vo, OBOOLEAN, &debug_vo);
}
#endif

/**
 * This routine (and these statics) is (are) used to capture a dl element
 * that is about to be added to the (a) display list. analyze_vignette..
 * picks up the object and either adds it to the vignette chain, or, will
 * flush it out to its intended display list. If no object is added (because
 * its colour is /None, or it is clipped out) then vd_lobj will be NULL after
 * this routine.
 *
 * Code did have an assert that lobj->opcode could only be rect or fill.
 * This was broken by addition of triangle primitive, and then found to
 * already be broken by promotion of images in patterns and multiple
 * rectangle enhancements. It has also had the comment that multiple
 * sequential calls to this function could only happen because of the
 * stroke code breaking up a path into pieces. This is also no longer true.
 * (stroke code no longer does that, and other bits of code can generate
 * multiple calls).
 * So change the logic of this function so that if it gets something its
 * not expecting to flush the vignette queue and pass the item straight
 * through. Not how it was designed, but will work for now...
 * \todo BMJ 12-Jun-08 :  fix this properly.
 */
static int32 addobjecttovignettedl(DL_STATE *page, LISTOBJECT *lobj)
{
  dbbox_t *bbox;

  HQASSERT(lobj, "lobj NULL");
  bbox = &(lobj->bbox);

  if ( dl_is_none(lobj->p_ncolor) )
    return DL_NotAdded;

  /* If we already have one queued up OR its not one of the types we want */
  if ( vd_lobj != NULL ||
       (lobj->opcode != RENDER_fill &&
        lobj->opcode != RENDER_quad &&
        lobj->opcode != RENDER_rect) ) {
    LISTOBJECT *lobj_p = vd_lobj;

    /* Don't keep anything queued up */
    vd_lobj = NULL;
    vd_flushed_lobj = TRUE;

    reset_analyze_vignette(); /* Turn off vignette recognition */

    if ( !flush_vignette(VD_Default) ) /* Flush anything we have waiting */
      return DL_Error;

    if ( lobj_p ) { /* If we still have one in our hand, add that to the DL */
      if ( !add_listobject(page, lobj_p, NULL) )
        return DL_Error;
    }
    /* And then just add the object we were passed to the DL */
    if ( !add_listobject(page, lobj, NULL) )
      return DL_Error;
    return DL_Added;
  }
  /* Otherwise just remember the object for later processing... */
  vd_lobj = lobj;
  return DL_Added;
}

static void vd_free_colorvalues(void)
{
  if ( vd_colorvalues ) {
    mm_free(mm_pool_temp, vd_colorvalues,
            vd_ncolorvalues * sizeof(USERVALUE));
    vd_colorvalues = NULL;
  }
  vd_ncolorvalues = 0;
}

/**
 * Must store values from the color chain whilst analysing the vignette
 * (it is not safe to hold references on arrays in the color chain!!!).
 */
static Bool vd_store_colorvalues(USERVALUE** pcolorvalues, int32 ncomps)
{
  int32 i;

  if ( vd_colorvalues && vd_ncolorvalues < ncomps )
    vd_free_colorvalues();

  if ( vd_colorvalues == NULL ) {
    int32 ncolorvalues = (ncomps < 4 ? 4 : ncomps);
    vd_colorvalues = mm_alloc(mm_pool_temp,
                              ncolorvalues * sizeof(USERVALUE),
                              MM_ALLOC_CLASS_VIGNETTEARGS);
    if ( vd_colorvalues == NULL )
      return error_handler(VMERROR);
    vd_ncolorvalues = ncolorvalues;
  }

  for (i = 0; i < ncomps; ++i)
    vd_colorvalues[i] = (*pcolorvalues)[i];

  *pcolorvalues = vd_colorvalues;

  return TRUE;
}

/**
 * Preserve all stateobjects in use by the current vignette candidate.
 */
void vn_preservestate(DL_STATE *page)
{
  HQASSERT(vd_headobj != NULL, "Should be a vignette in progress");
  HQASSERT(vd_objectstate[0] != NULL, "First vd_objectstate cannot be NULL");

  dlSSPreserve(page->stores.state, &vd_objectstate[0]->storeEntry, TRUE);
  if ( vd_objectstate[1] != NULL )
    dlSSPreserve(page->stores.state, &vd_objectstate[1]->storeEntry, TRUE);
}

/**
 * This routine is used to re-route the dl addition to addobjecttovignettedl.
 * Called when we know we want to look at a dl object for vignette analysis.
 * Could be made a macro if speed turns out to be vital.
 */
void setup_analyze_vignette( void )
{
  vd_lobj = NULL;
  vd_flushed_lobj = FALSE;
  device_current_addtodl = addobjecttovignettedl;
}

/**
 * This routine is used to re-set the dl addition routine.
 * Called when we know we want to look at a dl object for vignette analysis.
 * Could be made a macro if speed turns out to be vital.
 */
void reset_analyze_vignette( void )
{
  device_current_addtodl = device_table_addtodl[thegsDeviceType(*gstateptr)];
}

/**
 * This routine returns TRUE if we're currently analyzing a vignette.
 */
Bool analyzing_vignette( void )
{
  return (vd_headobj != NULL);
}

Bool check_analyze_vignette_s( STROKE_PARAMS *sparams )
{
  HQASSERT(sparams, "NULL sparams");

  if ( SystemParams.PoorStrokepath )
    return FALSE;
  if ( sparams->linestyle.dashlistlen != 0 ) /* Is there dashing ? */
    return FALSE;
  if ( path_count_subpaths(sparams->thepath) > 1 )
    return FALSE;
  return TRUE;
}

/**
 * Deals with stroke. For now does not deal with rectstroke, ustroke et al.
 *
 * Note that the stroke case does some simple filtering of unsupported cases.
 */
Bool analyze_vignette_s(STROKE_PARAMS *sparams, int32 colorType)
{
  HQASSERT(sparams, "NULL sparams");
  HQASSERT(sparams->strokedpath == NULL, "no vignettes in strokepath...");
  HQASSERT(!isHDLTEnabled(*gstateptr) ||
           !vd_flushed_lobj, "vd_flushed_lobj MUST be FALSE");

#if PS2_PDFOUT
  /** \todo @@@ TODO FIXME: ??? MarkJ: I don't understand how or why about
   * this yet
   */
  /* See if we have already dealt with this object
   * (due to dashes lines for example).
   */
  if ( vd_flushed_lobj ) {
    if ( pdfout_enabled() )
      return pdfout_dostroke( get_core_context_interp()->pdfout_h, sparams,
                              colorType, NULL );
    else
      return TRUE;
  }
#endif
  return analyze_vignette(sparams->page, sparams->thepath, ISSTRK, EOFILL_TYPE,
                          sparams->copypath, sparams, colorType );
}

/**
 * Deals with fill, eofill & rectfill. For now does not deal with ufill et al.
 */
Bool analyze_vignette_f(DL_STATE *page, PATHINFO *path,
                        int32 pathtype, int32 filltype,
                        Bool copythepath, int32 colorType)
{
  HQASSERT(!isHDLTEnabled( *gstateptr ) ||
           !vd_flushed_lobj, "vd_flushed_lobj MUST be FALSE");

  return analyze_vignette(page, path, pathtype, filltype, copythepath, NULL,
                          colorType );
}

/**
 * This routine does the main analysis work of seeing if the current object
 * is a continuation of a potential vignette.
 */
static Bool analyze_vignette(DL_STATE *page, PATHINFO *path,
                             int32 pathtype, int32 filltype, Bool copythepath,
                             STROKE_PARAMS *sparams, int32 colorType )
{
  corecontext_t *context = get_core_context_interp();
  int32 ncolorants = 0;
  COLORANTINDEX *colorants = NULL;

  Bool begincandidate = TRUE;
  Bool flushcandidate = FALSE;
  Bool endedcandidate = FALSE;
  Bool swapvdobjectstates = FALSE;
  Bool check_clip_differences = FALSE;
  Bool check_old_paths_inside = FALSE;
  Bool check_new_paths_inside = FALSE;

  STATEOBJECT *currentdlstate;
  int curr_disposition_flags;
  PATHLIST *p1 = NULL, *p2 = NULL, *pt;
  PATHINFO *path1 = NULL, *path2 = NULL;
  VIGNETTEARGS *currobj;
  VIGNETTEARGS *prevobj;
  VIGNETTEARGS *newobj;
  VIGNETTEARGS vd_newobj = {0};

  /* Used for Gray to CMYK, Gray to RGB promotion */
  USERVALUE vd_color4[4];
  Bool fPromotePrevColor = FALSE;
  Bool fPromoteCurrColor = FALSE;

  /* Used when original colorvalues needed for shfill chain */
  USERVALUE vd_color_orig4[4];
  Bool fCurrColorConvToRGB = FALSE;

#if PS2_PDFOUT
  int32 needs_colorinfo = 0;
#endif
  uint32 allocsize = 0;

  HQASSERT(path, "path NULL");
  HQASSERT(path->firstpath, "path->firstpath NULL");

  /* Order of vignette detection is:
   * a) method (stroke or not stroke) is same
   * b) device is same
   * c) knockout/overprint, ucr or bg change test.
   * d) clipping path change test.
   * e) STATEOBJECTs check
   * e2) Disposition flags check
   * f) bbox check of dl object (containing, intersecting, and-or adjacency).
   * g) color space similarity.
   * h) (optional?) monotonicity of color check.
   * i) as f) [bbox check] but with original object.
   * j) "centre of gravity" displacement & size test.
   * k) check object inside original cliprect.
   * l) full path consistency check (containing and-or adjacency).
   * m) drifting scaled vignette test.
   */

  /* state: flush == F, begin == T, ended == F ** these are the defaults ** */

  prevobj = vd_tailobj;

  /* If object was clipped out, or was dropped because its DL colour was None
   * (this can happen because of overprint reduction), then flush existing
   * candidates, and note that this object is not the start of a new vignette.
   * This shouldn't cause problems splitting existing vignettes, since such
   * objects should be at the ends of the vignette.
   */
  if ( vd_lobj == NULL ) {
    flushcandidate = TRUE;
    begincandidate = FALSE;
    HQTRACE(trace_vf,("flushcandidate due to clipped/none color object"));
  }

  /* Order of vignette detection is:
   * a) method (stroke or not stroke) is same
   */
  if ( prevobj ) {
    if ( (sparams && !prevobj->vd_sparams) ||
         (!sparams && prevobj->vd_sparams) ) {
      flushcandidate = TRUE;
      HQTRACE(trace_vf,("flushcandidate due to change in [non]stroke"));
    }
    else if ( sparams && prevobj->vd_sparams ) {
      LINESTYLE *oldls = &(sparams->linestyle);
      LINESTYLE *newls = &(prevobj->vd_sparams->linestyle);

      if ( oldls->startlinecap != newls->startlinecap ||
           oldls->endlinecap   != newls->endlinecap   ||
           oldls->dashlinecap  != newls->dashlinecap  ||
           oldls->linejoin     != newls->linejoin     ||
           !MATRIX_REQ(&sparams->orig_ctm, &prevobj->vd_sparams->orig_ctm) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to linecap/linejoin/matrix"));
      }
    }
  }

  if ( sparams ) {
    if ( sparams->linestyle.dashlistlen != 0 ) { /* Is there dashing ? */
      flushcandidate = TRUE;
      begincandidate = FALSE;
      HQTRACE(trace_vf,("flushcandidate due to dashed line"));
    }
  }

  /* vignette detection:
   * b) device is same
   */
  if ( begincandidate && !flushcandidate &&
       CURRENT_DEVICE() != vd_device &&
       vd_device != DEVICE_ILLEGAL ) {
    flushcandidate = TRUE;
    HQTRACE(trace_vf,("flushcandidate due to change in device"));
  }
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * c) overprint/knockout, ucr or bg change test.
   */
  if ( begincandidate && !flushcandidate && prevobj ) {
    GS_COLORinfo *colorInfo = gstateptr->colorInfo;
    if ( vd_gsoverrides.overprint != gsc_getoverprint(gstateptr->colorInfo,
                                                      colorType )) {
      flushcandidate = TRUE;
      HQTRACE(trace_vf,("flushcandidate due to change in overprint/knockout"));
    }
    if ( !flushcandidate && vd_gsoverrides.undercolorremovalid !=
                             gsc_getundercolorremovalid( colorInfo )) {
      /* ids have changed, but the proc may still be the same. */
      OBJECT *poUCR = gsc_getundercolorremovalobject( colorInfo );
      if ( !OBJECTS_IDENTICAL(vd_gsoverrides.undercolorremoval, *poUCR) &&
           !compare_objects(&vd_gsoverrides.undercolorremoval, poUCR) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to change in ucr"));
      }
      else
        /* Update id to avoid doing the same test again. */
        vd_gsoverrides.undercolorremovalid =
          gsc_getundercolorremovalid(colorInfo);
    }
    if ( !flushcandidate && vd_gsoverrides.blackgenerationid !=
          gsc_getblackgenerationid(colorInfo) ) {
      /* ids have changed, but the proc may still be the same. */
      OBJECT *poBG = gsc_getblackgenerationobject( colorInfo );
      if ( !OBJECTS_IDENTICAL(vd_gsoverrides.blackgeneration, *poBG) &&
           !compare_objects(&vd_gsoverrides.blackgeneration, poBG) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to change in black generation"));
      }
      else
        /* Update id to avoid doing the same test again. */
        vd_gsoverrides.blackgenerationid =
          gsc_getblackgenerationid(colorInfo);
    }
  }
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * d) clipping path change test.
   */
  if ( begincandidate && !flushcandidate ) {
    if ( vd_cause == VD_GRestore ) {
      if ( vd_objectstate[1] == NULL )
        check_clip_differences = TRUE;
      check_new_paths_inside = TRUE;
    }
    if ( vd_cause == VD_AddClip ) {
      if ( vd_objectstate[1] == NULL ) {
        check_clip_differences = TRUE;
        check_old_paths_inside = TRUE;
      }
    }
    if ( check_clip_differences ) {
      CLIPRECORD *clip_old = theClipRecord(vd_clippath);
      CLIPRECORD *clip_new = theClipRecord(thegsPageClip(*gstateptr));
      int32 degenerate, orien;

      if ( vd_cause == VD_GRestore ) {
        /* Swap over since VD_GRestore means old has 'gained' clip. */
        CLIPRECORD *clip_tmp = clip_old;
        clip_old = clip_new; clip_new = clip_tmp;
      }
      else
        swapvdobjectstates = TRUE;

      if ( clip_new ) {
        CLIPRECORD *extra_clip;
        /* If cliprect has been added, it will be at the start of the
         * current pages clip. The rest of the records must match.
         */
        extra_clip = clip_new;
        clip_new = clip_new->next;
        /* The two clip record lists must match exactly from now on. */
        while ( clip_old && clip_new ) {
          if ( clip_old->clipno != clip_new->clipno ) {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to clip change"));
            break;
          }
          clip_old = clip_old->next;
          clip_new = clip_new->next;
        }
        /* Only deal with the case where the extra clip is a rectangle. */
        if ( clip_old || clip_new ||
             !pathisarectangle(&theClipPath(*extra_clip),
                               &degenerate, &orien, &vd_cliptype,
                               NULL ) ||
             degenerate ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to clip change"));
        }
        else {
          vd_initmatrix = TRUE;
          vd_clippathinfo = theClipPath(*extra_clip);
        }
      }
      else {
        /* New clip record is empty so cannot possible added a rectangle. */
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to clip path change test(3)"));
      }
    }
  }
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * e) STATEOBJECTs check
   */
  currentdlstate = page->currentdlstate;
  if ( begincandidate && !flushcandidate && vd_headobj != NULL ) {
    if ( currentdlstate != vd_objectstate[0] ) {
      if ( currentdlstate != vd_objectstate[1] ) {
        if ( check_clip_differences ) {
          /* Check that halftones and patterns have not changed from the
           * previous state.
           */
          if ( currentdlstate->patternstate != vd_objectstate[0]->patternstate
              || currentdlstate->spotno != vd_objectstate[0]->spotno )
          {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to change in dlstate(1)"));
          }
        }
        else {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to change in dlstate(2)"));
        }
      }
    }
  }
  currobj = &vd_newobj;
  currobj->vd_sparams = sparams;
  HQASSERT((pathtype & (~(ISSTRK|ISRECT|ISFILL))) == 0, "unknown pathtype");
  currobj->vd_pathtype = pathtype;
  HQASSERT(filltype == NZFILL_TYPE || filltype == EOFILL_TYPE,
           "unknown filltype" );
  currobj->vd_filltype = filltype;
  currobj->vd_ftol = fl_getflat();

  /* Fill in currobj (hacks for now). */
  currobj->vd_lobj = vd_lobj;
  vd_lobj = NULL;
  dlc_clear( &currobj->vdl_color );
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * e2) Disposition flags check
   */
  curr_disposition_flags = gstateptr->user_label ? DISPOSITION_FLAG_USER : 0;
  if ( begincandidate && !flushcandidate && vd_headobj != NULL ) {
    if ( curr_disposition_flags != vd_disposition_flags ) {
      flushcandidate = TRUE;
      HQTRACE(trace_vf,("flushcandidate due to change in disposition"));
    }
  }
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * f) bbox check of dl object (containing, intersecting, and-or adjacency).
   */
  if ( begincandidate && !flushcandidate ) {
    if ( prevobj ) {
      dbbox_t *ibboxp = prevobj->vd_lobj ? &prevobj->vd_lobj->bbox : NULL;
      dbbox_t *ibboxc = currobj->vd_lobj ? &currobj->vd_lobj->bbox : NULL;
      int32 eix = gstateptr->pa_eps.eix;
      int32 eiy = gstateptr->pa_eps.eiy;
      if ( ibboxp && ibboxc ) {
        /* Integer bboxes of both objects must intersect to be a vignette
         * (2 pixel slop).
         * AC (2/12/96) allow 2 'pixel' gaps.
         */
        if ( !bbox_intersects_epsilon(ibboxc, ibboxp, eix, eiy) ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to non-intersecting ibbox"));
        }
      }
    }
  }
  /* state: flush ?= T, begin == T, ended == F */

  /* vignette detection:
   * g) color space similarity.
   */
  { /* Must always do this since we need to store color[space] */
    int32 i;
    uint8 icolorp_space = SPACE_notset; /* Shut up compiler */
    uint8 icolorc_space;
    int32 icolorp_dims = 0;             /* Shut up compiler */
    int32 icolorc_dims;
    USERVALUE *icolorp_values = NULL;
    USERVALUE *icolorc_values = NULL;

    /* When looking at color being monotonically increasing, we really need
     * to be careful. That's becase the input color may be monotonic in it's
     * input color space (e.g. RGB), but not in its output color space
     * (e.g. Gray).
     * That implies we need to check the input color space however we then
     * have a problem with mixed color spaces whereby some parts of a vignette
     * are done with RGB and some parts with Gray. Resolve that by converting
     * to RGB first before converting to the larger of the color spaces.
     * We also have a problem with input color space that don't make sense;
     * e.g. Indexed.  In that case we convert the color space to RGB.
     */
    icolorc_space = currobj->vd_icolor_space =
                    gsc_getcolorspace(gstateptr->colorInfo, colorType);
    /* Store the original colorspace that will not get promoted */
    currobj->vd_icolor_space_original = icolorc_space;

    gsc_getcolorvalues(gstateptr->colorInfo, colorType, &icolorc_values,
                       &icolorc_dims);

    /* Copy colorvalues into a buffer to ensure values are preserved during the
       vignette analysis (color chain may get rebuilt before we're done). */
    if ( !vd_store_colorvalues(&icolorc_values, icolorc_dims) )
      return FALSE;

    switch ( icolorc_space ) {
    case SPACE_DeviceRGB:
      theTags(currobj->vd_icolor_object) = ONAME | LITERAL;
      oName(currobj->vd_icolor_object) = system_names + NAME_DeviceRGB;
      break;
    case SPACE_DeviceCMYK:
      theTags(currobj->vd_icolor_object) = ONAME | LITERAL;
      oName(currobj->vd_icolor_object) = system_names + NAME_DeviceCMYK;
      break;
    case SPACE_DeviceGray:
      theTags(currobj->vd_icolor_object) = ONAME | LITERAL;
      oName(currobj->vd_icolor_object) = system_names + NAME_DeviceGray;
      break;
    default:
      currobj->vd_icolor_object =
        *gsc_getcolorspaceobject(gstateptr->colorInfo, colorType);
    }

    currobj->vd_icolor_dims = icolorc_dims;
    currobj->vd_icolor_values = icolorc_values;
#if PS2_PDFOUT
    for (i = 0; i < 4; i++)
       currobj->orig_color.orig_icolor[i] = icolorc[i];
    currobj->basecolorspace = icolorc_space;
#endif

    if ( !flushcandidate && prevobj ) {
      icolorp_dims = prevobj->vd_icolor_dims;
      icolorp_space = prevobj->vd_icolor_space;
      icolorp_values = prevobj->vd_icolor_values;
    }

    switch ( icolorc_space ) {
    case SPACE_DeviceGray:
      if ( begincandidate && !flushcandidate &&
           prevobj != NULL && icolorc_space != icolorp_space ) {

        /* Allow color space to switch from CMYK to Gray, or RGB to Gray */
        if ( icolorp_space != SPACE_DeviceRGB &&
             icolorp_space != SPACE_DeviceCMYK ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to color space change(1)"));
        } else {
          /* Save the original gray value in case this turns out not to be
             part of the current vignette candidate */
          currobj->vd_icolor_value_orig = icolorc_values[0];

          /* Convert input color values of current Gray to equivalent
           * RGB or CMYK
           */
          if ( !gsc_invokeChainTransform( gstateptr->colorInfo, colorType,
                                           icolorp_space, FALSE, vd_color4 ))
            return FALSE;

          icolorc_space = currobj->vd_icolor_space = icolorp_space;
          theTags(currobj->vd_icolor_object) = ONAME | LITERAL;
          if ( icolorp_space == SPACE_DeviceRGB ) {
            oName(currobj->vd_icolor_object) =
              system_names + NAME_DeviceRGB;
            icolorc_dims = currobj->vd_icolor_dims = 3;
          } else {
            oName(currobj->vd_icolor_object) =
              system_names + NAME_DeviceCMYK;
            icolorc_dims = currobj->vd_icolor_dims = 4;
          }
          icolorc_values = currobj->vd_icolor_values = vd_color4;

          /* The current element must use the promoted color space
             and values for the shfill chain equivalent color */
          fPromoteCurrColor = TRUE;
        }
      }
      break;
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMYK:
      if ( begincandidate && !flushcandidate &&
           prevobj != NULL && icolorc_space != icolorp_space ) {
        VIGNETTEARGS * pPromote;

        /* Allow color space to switch from Gray to CMYK */
        if ( icolorp_space != SPACE_DeviceGray ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to color space change(2)"));
        } else {
          /* Convert input color values of all previous Grays to equivalent
             RGB or CMYK. May as well use the shfill chain (rather than using
             colorType chain and doing gsave/grestore) */
          if ( !gsc_setcolorspacedirect( gstateptr->colorInfo, GSC_VIGNETTE,
                                          icolorp_space ))
            return FALSE;

          for ( pPromote = vd_headobj; pPromote != NULL;
                pPromote = pPromote->vd_next ) {
            if ( !gsc_setcolordirect(gstateptr->colorInfo, GSC_VIGNETTE,
                                     pPromote->vd_icolor_values ) ||
                 !gsc_invokeChainTransform(gstateptr->colorInfo, GSC_VIGNETTE,
                                           icolorc_space, FALSE,
                                           pPromote->vd_icolor_values ))
              return FALSE;

            pPromote->vd_icolor_space = icolorc_space;
            theTags(pPromote->vd_icolor_object) = ONAME | LITERAL;
            if ( icolorc_space == SPACE_DeviceRGB ) {
              oName(pPromote->vd_icolor_object) =
                system_names + NAME_DeviceRGB;
              pPromote->vd_icolor_dims = 3;
            } else {
              oName(prevobj->vd_icolor_object) =
                system_names + NAME_DeviceCMYK;
              pPromote->vd_icolor_dims = 4;
            }
          }

          icolorp_space = prevobj->vd_icolor_space;
          icolorp_dims = prevobj->vd_icolor_dims;
          icolorp_values = prevobj->vd_icolor_values;

          /* Update the monotonic flags from the
             previous color space to the current */

          if ( icolorc_space == SPACE_DeviceCMYK ) {
            /* Gray to CMYK (additive to subtractive) */
            if ( vd_colormonotonic[0] == VDC_Increasing )
              vd_colormonotonic[3] = VDC_Decreasing;
            else if ( vd_colormonotonic[0] == VDC_Decreasing )
              vd_colormonotonic[3] = VDC_Increasing;
            vd_colormonotonic[0] =
              vd_colormonotonic[1] =
                vd_colormonotonic[2] = VDC_Neutral;
          } else {
            /* Gray to RGB */
            vd_colormonotonic[1] =
              vd_colormonotonic[2] = vd_colormonotonic[0];
          }

          /* All the previous elements of the vignette have a Gray shfill chain
           * color. These colors will need to be replaced with an equivalent
           * RGB/CMYK color if the current element passes the remaining tests
           */
          fPromotePrevColor = TRUE;
        }
      }
      break;
    case SPACE_Pattern:
      flushcandidate = TRUE;
      HQTRACE(trace_vf,("flushcandidate due to pattern color space"));
      begincandidate = FALSE;
      break;
    case SPACE_DeviceN:
    case SPACE_Separation:
#if PS2_PDFOUT
      if ( pdfout_enabled() ) {
        uint8 bcspace = theIgsBaseColorSpace( colinfo );
        if ( bcspace == SPACE_DeviceCMYK ||
             bcspace == SPACE_DeviceRGB ||
             bcspace == SPACE_DeviceGray ) {
          currobj->basecolorspace = theIgsBaseColorSpace( colinfo );
          currobj->orig_color.sepcolor.septinttransform =
            theIgsSepTintTransform(colinfo);
          currobj->orig_color.sepcolor.sepname = theIgsSepName(colinfo);
        }
        else
          needs_colorinfo = TRUE;
      }
#endif
      if ( prevobj ) {
        if ( !gsc_getNSeparationColorants(gstateptr->colorInfo, colorType,
              &ncolorants, &colorants) )
          return FALSE;
        if ( ncolorants != vd_ncolorants ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to ncolorants difference"));
        }
        else {
          int32 i;
          HQASSERT(vd_colorants != NULL, "vd_colorants should not be NULL");
          for ( i = 0; i < ncolorants; ++i ) {
            if ( colorants[i] != vd_colorants[i] ) {
              flushcandidate = TRUE;
              HQTRACE(trace_vf,("flushcandidate due to colorants difference"));
              break;
            }
          }
        }
      }
      break;
    default:
      /* This case handles all the device independent colour spaces */
#if PS2_PDFOUT
      needs_colorinfo = pdfout_enabled();
#endif
      /* Note that here we used to call transform color whereas now we always
       * pick RGB. [If we wanted to do the same thing then it would be
       * equivalent to calling gsc_getChainOutputColors and using the result.]
       * This is simpler.
       */
      if ( prevobj != NULL ) {
        if ( currobj->vd_icolor_space_original !=
             prevobj->vd_icolor_space_original ) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("flushcandidate due to color space difference"));
        }
      }
      if ( begincandidate ) {

        /* Bail out if there are more than 4 colorants for pragmatic reasons.
         * This should only happen for ICCBased colorspaces.
         */
        if ( icolorc_dims > 4) {
          HQASSERT(icolorc_space == SPACE_ICCBased,
                  "Only ICCBased should have more than 4 components");
          flushcandidate = TRUE;
          begincandidate = FALSE;
          break;
        }

        /* Store the original colorvalues for use in the shfill chain,
           ( currently only expecting up to 4) */
        for ( i = 0; i < icolorc_dims; ++i ) {
          vd_color_orig4[i] = currobj->vd_icolor_values[i];
        }

        if ( !gsc_invokeChainTransform( gstateptr->colorInfo, colorType,
                                         SPACE_DeviceRGB, FALSE,
                                         vd_color4 )) {
          flushcandidate = TRUE;
          HQTRACE(trace_vf,("transform_color(1) produced error"));
          begincandidate = FALSE;
        }
        icolorc_space = currobj->vd_icolor_space = SPACE_DeviceRGB;
        theTags(currobj->vd_icolor_object) = ONAME | LITERAL;
        oName(currobj->vd_icolor_object) = system_names + NAME_DeviceRGB;
        icolorc_dims = currobj->vd_icolor_dims = 3;
        icolorc_values = currobj->vd_icolor_values = vd_color4;
        fCurrColorConvToRGB = TRUE;
      }
      break;

    } /* end of the switch on the colorspace */

    /* part (e) not closed yet 'cos we want i, icolorp, icolorc &c still */

    /* state: flush ?= T, begin ?= F, ended == F */

    /* vignette detection:
     * h) (optional?) monotonicity of color check.
     */
    if ( !flushcandidate && prevobj != NULL ) {
      Bool gotcolorchange = FALSE;
      HQASSERT(prevobj != NULL, "somehow lost prevobj");
      HQASSERT(vd_colormonotonic != NULL, "Somehow lost vd_colormonotonic");
      HQASSERT(icolorc_dims <= vd_ncolormonotonic, "colormonotonic too small");
      for ( i = 0; i < icolorc_dims; ++i ) {
        if ( icolorc_values[i] < icolorp_values[i] ) {
          gotcolorchange = TRUE;
          if ( vd_colormonotonic[i] != VDC_Increasing )
            vd_colormonotonic[i] = VDC_Decreasing;
          else {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to color monoto change"));
            break;
          }
        }
        else if ( icolorc_values[i] > icolorp_values[i] ) {
          gotcolorchange = TRUE;
          if ( vd_colormonotonic[i] != VDC_Decreasing )
            vd_colormonotonic[i] = VDC_Increasing;
          else {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to color monoto change"));
            break;
          }
        }
        else {
          if ( vd_colormonotonic[i] == VDC_Unknown )
            vd_colormonotonic[i] = VDC_Neutral;
        }
      }
#ifdef NEUTRAL_COLOR_FLUSHES
      /* Don't do this; misses out on blends... */
      if ( !gotcolorchange ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to no color change"));
      }
#endif
    }
  }
  /* end of if not flush for part (h) */
  /* state: flush ?= T, begin ?= F, ended == F */

  /* vignette detection:
   * i) as f) [bbox check] but with original object.
   */
  if ( begincandidate ) {
    currobj->vd_numpaths = path_count_subpaths( path );
    /* if the path does not have a subpath then it can not be a candidate */
    if ( (!currobj->vd_numpaths) ||
         (currobj->vd_numpaths == 2 && sparams) ||
         (currobj->vd_numpaths > 2) ) {
      begincandidate = FALSE;
      flushcandidate = TRUE;
      HQTRACE(trace_vf,("flushcandidate due to not enough sub-paths"));
    }
    else
      /* A ring vignette is allowed to (should) finish with a circle. */
      if ( prevobj && (currobj->vd_numpaths > prevobj->vd_numpaths) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to too many sub-paths"));
      }
  }

  if ( begincandidate ) {
    /* We create a single path that is the original (or copied) path,
     * and up to two paths that contain the subpaths individually.
     */
    currobj->vd_origpath = (*path);
    p1 = path->firstpath;
    path1 = &currobj->vd_path[0];
    path_init( path1 );
    pt = &currobj->vd_plst[0];
    path1->firstpath = pt;
    pt->subpath = p1->subpath;
    pt->systemalloc = PATHTYPE_STRUCT;
    pt->next = NULL;

    /* Set global bounding box and cache path's bbox */
    (void)path_bbox( path1, &currobj->vd_gbbox, BBOX_IGNORE_LEVEL2|BBOX_SAVE);

    if ( currobj->vd_numpaths == 2 ) {
      HQASSERT(!sparams, "can only have two subpaths with fill variant");
      p2 = p1->next;

      path2 = &currobj->vd_path[1];
      path_init( path2 );
      pt = &currobj->vd_plst[1];
      path2->firstpath = pt;
      pt->subpath = p2->subpath;
      pt->systemalloc = PATHTYPE_STRUCT;
      pt->next = NULL;
      (void)path_bbox(path2, NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);

      /* To be a vignette with two subpaths per path, one must be contained
       * in the other.
       */
      if ( bbox_contains_epsilon(&path2->bbox, &path1->bbox,
                                 gstateptr->pa_eps.ex,
                                 gstateptr->pa_eps.ey) ) {
        /* p1 contained inside p2; swap over. */
        PATHINFO tmppath;
        tmppath = (*path1); (*path1) = (*path2); (*path2) = tmppath;
        p1 = path1->firstpath;
        p2 = path2->firstpath;
        /* RESET global bounding box. */
        currobj->vd_gbbox = path1->bbox;
      }
      else if ( !bbox_contains_epsilon(&path1->bbox, &path2->bbox,
                                       gstateptr->pa_eps.ex,
                                       gstateptr->pa_eps.ey) ) {
        /* curr object not acceptable, 2 bboxes are identical(ish) */
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to identical bboxes"));
        begincandidate = FALSE;
      }
      /* else p2 contained inside p1. */

      if ( begincandidate ) {
        /* If everything touches, they are identical and so no vignette. */
        if ( bbox_equal(&path1->bbox, &path2->bbox) ) {
#ifdef SAME_BBOX_FLUSHES
          /* Only true if boths shapes are the same... */
          /* Still, don't need to enlarge bbox though... */
          /* Consider a square rotated 45 degrees and scaled inside itself.. */
          flushcandidate = TRUE;
          begincandidate = FALSE;
#endif
        }
        else {
          sbbox_t *bboxn = &path2->bbox;
          sbbox_t *bboxc = &currobj->vd_gbbox;
          /* Enlarge global bounding box. */
          bbox_union(bboxc, bboxn, bboxc);
        }
      }
    }
    /* else only 1 path, so the bbox is set up */
    /* now see how it matches the previous stuff, if any and still plausible */
    if ( !flushcandidate && prevobj && !sparams ) {
      sbbox_t *bboxp = &prevobj->vd_gbbox;
      sbbox_t *bboxc = &currobj->vd_gbbox;
      /* FP bboxes of both objects must intersect (or touch) to be a vig . */
      /* Note this disallows VDS_Spaced style vignettes. */
      /* AC (2/12/96) allow 2 'pixel' gaps. */
      if ( !bbox_intersects_epsilon(bboxc, bboxp,
                                    3.0 + gstateptr->pa_eps.ex,
                                    3.0 + gstateptr->pa_eps.ey) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to non-intersecting bbox"));
      }
    }
  } /* end of if begincandidate */
  /* state: flush ?= T, begin ?= F, ended == F */

  /* vignette detection:
   * j) "centre of gravity" displacement & size test.
   */
  if ( begincandidate && !flushcandidate && prevobj ) {
    if ( prevobj->vd_prev == NULL ) {
      /* first pair defines required displacement */
      sbbox_t *bboxp = &prevobj->vd_gbbox;
      sbbox_t *bboxc = &currobj->vd_gbbox;
      vd_disp[0] = 0.5 * ((bboxc->x1 + bboxc->x2) - (bboxp->x1 + bboxp->x2));
      vd_disp[1] = 0.5 * ((bboxc->y1 + bboxc->y2) - (bboxp->y1 + bboxp->y2));
    }
    else {
     /* vd_diplacements are already set up; check if change at all */
      sbbox_t *bboxp = &prevobj->vd_gbbox;
      sbbox_t *bboxc = &currobj->vd_gbbox;
      SYSTEMVALUE disp[2];
      disp[0] = 0.5 * ((bboxc->x1 + bboxc->x2) - (bboxp->x1 + bboxp->x2));
      disp[1] = 0.5 * ((bboxc->y1 + bboxc->y2) - (bboxp->y1 + bboxp->y2));
      if ( fabs( vd_disp[0] - disp[0] ) >= gstateptr->pa_eps.e2dx ||
           fabs( vd_disp[1] - disp[1] ) >= gstateptr->pa_eps.e2dy ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to displacement test"));
      }
    }
  }
  if ( begincandidate && !flushcandidate && prevobj ) {
    if ( prevobj->vd_prev == NULL || (prevobj->vd_prev->vd_prev == NULL &&
           vd_rolledrect != VDR_Unknown) )
    {
      /* first pair defines required size, or, second pair for rolled rects */
      sbbox_t *bboxp = &prevobj->vd_gbbox;
      sbbox_t *bboxc = &currobj->vd_gbbox;
      vd_size[0] = ((bboxc->x2 - bboxc->x1) - (bboxp->x2 - bboxp->x1));
      vd_size[1] = ((bboxc->y2 - bboxc->y1) - (bboxp->y2 - bboxp->y1));
    }
    else {
      /* vd_sizes are already set up; check if change at all */
      sbbox_t *bboxp = &prevobj->vd_gbbox;
      sbbox_t *bboxc = &currobj->vd_gbbox;
      SYSTEMVALUE size[2];
      size[0] = ((bboxc->x2 - bboxc->x1) - (bboxp->x2 - bboxp->x1));
      size[1] = ((bboxc->y2 - bboxc->y1) - (bboxp->y2 - bboxp->y1));
      if ( fabs( vd_size[0] - size[0] ) >= gstateptr->pa_eps.e2x ||
           fabs( vd_size[1] - size[1] ) >= gstateptr->pa_eps.e2y ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to size test"));
      }
    }
  }
  /* state: flush ?= T, begin ?= F, ended == F */
  /* vignette detection:
   * k) check object inside original cliprect.
   */
  if ( begincandidate && !flushcandidate && prevobj ) {
    if ( check_new_paths_inside ) {
      if ( !is_path_inside_extra_cliprect( currobj )) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to object not inside cliprect"));
      }
    }
    if ( check_old_paths_inside ) {
      /* Must check that the previous elements are inside the new
       * cliprect to continue with the vignette.
       */
      if ( !is_path_inside_extra_cliprect(vd_headobj) || (vd_headobj !=
            prevobj && !is_path_inside_extra_cliprect(prevobj)) ) {
        flushcandidate = TRUE;
        HQTRACE(trace_vf,("flushcandidate due to object not inside cliprect"));
      }
#if defined( ASSERT_BUILD )
      if ( !flushcandidate ) {
        VIGNETTEARGS *vobj;
        for ( vobj = vd_headobj; vobj != NULL; vobj = vobj->vd_next )
          HQASSERT(is_path_inside_extra_cliprect(vobj),
              "not inside but !flushcandidate");
      }
#endif
    }
  }
  /* state: flush ?= T, begin ?= F, ended == F */
  /* vignette detection:
   * l) full path consistency check (containing and-or adjacency).
   */
  currobj->vd_rcbtrap.type = RCBTRAP_NOTTESTED;

  if ( begincandidate ) {
    currobj->vd_style = VDS_Unknown;
    currobj->vd_match = VDM_Unknown;
    currobj->vd_type[0] = VDT_Unknown;
    currobj->vd_type[1] = VDT_Unknown;
    if ( ( !flushcandidate ) && prevobj ) {
      HQASSERT(currobj->vd_numpaths <= prevobj->vd_numpaths,
          "but I checked this..." );
      /* Delayed analysis of very first path. */
      if ( vd_headobj == vd_tailobj ) {
        if ( prevobj->vd_sparams ) {
          HQASSERT(sparams, "sparams somehow NULL(1)");
          if ( !( analyze_1stpath_s( prevobj ) &&
                 ( prevobj->vd_numpaths == 1 ))) {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to analyze_1stpath_s"));
          }
        }
        else {
          if ( !( analyze_1stpath_f( prevobj ) &&
                 ( prevobj->vd_numpaths == 1 ||
                   analyze_2ndpath_f( prevobj )))) {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to analyzepath"));
          }
        }
      }
      if ( !flushcandidate ) {
        int32 pathtests;

        /* Constrain future path tests to matches allowed with prevobj,
           unless on second element or just recognised a rolled up shape */
        pathtests = ((vd_rolledrect == VDR_Unknown && prevobj == vd_headobj) ||
                     (vd_rolledrect != VDR_Unknown &&
                      prevobj->vd_prev == vd_headobj )
                      ? VDM_All : prevobj->vd_match );
        HQASSERT(pathtests != VDM_Unknown, "no matches to test for");

        if ( prevobj->vd_sparams ) {
          int32 pathtests;
          HQASSERT(sparams, "sparams somehow NULL(2)");
          /* Constrain future path tests to matches allowed with prevobj.
             No rolled rects for stroked objects */
          pathtests = ((prevobj == vd_headobj) ? VDM_All : prevobj->vd_match);
          if ( !analyze_pathtopath_s( currobj, prevobj, pathtests )) {
            flushcandidate = TRUE;
            HQTRACE(trace_vf,("flushcandidate due to analyze_pathtopath_s"));
          }
        }
        else {
          int32 pathtests;

          /* Constrain future path tests to matches allowed with prevobj,
             unless on second element or just recognised a rolled up shape */
          if ( prevobj == vd_headobj )
            pathtests = VDM_All;
          else if ( vd_rolledrect != VDR_Unknown && prevobj->vd_prev ==
                                                    vd_headobj )
            pathtests = VDM_Scaled; /* Only allow scaled matches
                                       after a rolled rect */
          else
            pathtests = prevobj->vd_match;

          if ( !analyze_pathtopath_f( currobj, prevobj, pathtests )) {
            /* If this is the first object, then we need to check for a
             * rectangle rolled up at the front of a vignette
             * (which Quark does).
             */
            if ( vd_headobj == vd_tailobj &&
                 analyze_pathtorect_f( currobj, prevobj )) {
              HQASSERT(vd_rolledrect != VDR_Unknown, "Not rolled rect");
            }
            else {
              flushcandidate = TRUE;
              HQTRACE(trace_vf,("flushcandidate due to analyze_pathtopath_f"));
            }
          }
        }
      }
      if ( !flushcandidate && begincandidate ) {
        if ( prevobj->vd_numpaths == 2 &&
             currobj->vd_numpaths == 1 )
          /* we have a single path object inside a sequence of 2-pathed
           * ones, and we are happy about them all being a vignette, so
           * this implies the END of the sequence.
           */
          endedcandidate = TRUE;
      }
    }
  }
  /* state: flush ?= T, begin ?= F, ended ?= T */

  /* vignette detection:
   * m) drifting scaled vignette test.
   * Accummulation of errors in the circle (or ring) centres over a
   * number of object may cause a scaled vignette to degenerate into a
   * translation.
   * In this case since all the other relative translations have been
   * consistent, we consider that we've got an off-centered vignette
   * and so flush the whole thing as not a vignette.
   */
  if ( begincandidate && !flushcandidate && prevobj &&
       currobj->vd_style == VDS_StrongContained &&
       currobj->vd_match == VDM_Scaled ) {
    sbbox_t *bboxh = &vd_headobj->vd_gbbox;
    sbbox_t *bboxc = &currobj->vd_gbbox;
    if ( fabs( 0.5 * ((bboxh->x1 + bboxh->x2) - (bboxc->x1 + bboxc->x2)) >=
          gstateptr->pa_eps.e2dx )|| fabs( 0.5 * ((bboxh->y1 + bboxh->y2) -
          ( bboxc->y1 + bboxc->y2 )) >= gstateptr->pa_eps.e2dy )) {
      vd_forcesplit = TRUE;
      begincandidate = FALSE;
      flushcandidate = TRUE;
      endedcandidate = FALSE;
      HQTRACE(trace_vf,("flushcandidate due to drifting vignette test"));
    }
  }
  /* state: flush ?= T, begin ?= F, ended ?= T */

  /* we can now act on those flags to:
        flush   begin  ended
          F       F       F    do nothing, was no vig, curr is unsuitable
          F       F       T    N/A (=> curr obj bad && curr obj completes vig)
          F       T       F    continue with current object, or start anew
          F       T       T    continue with current object then flush.
          T       F       F    flush, curr obj unsuitable
          T       F       T    N/A (=> curr obj bad && curr obj completes vig)
          T       T       F    flush, start anew with current object
          T       T       T    N/A (=> curr obj is complete vig on its own!)
  */
  HQASSERT('o' == "oNoooNoN" [
    (flushcandidate ? 4 : 0) + (begincandidate ? 2 : 0) +
    (endedcandidate ? 1 : 0) ],
           "Inconsistent state flags after vignette analysis");
  /* so sue me... */
  if ( flushcandidate ) {
    if ( !flush_vignette( VD_Default ))
      return FALSE;
  }

  if ( !begincandidate ) {
    /* Then it's no good, output it as usual... */

    if ( currobj->vd_lobj == NULL )
      return TRUE; /* clipped out or has a none color, nothing more to do */

    if ( isHDLTEnabled( *gstateptr )) {
      int32 rval;
      /* Maybe need a DEVICE_SETG here, but don't think so... */
      if ( sparams ) {
        /* need to account for any stroke structure specified matrix adjust. */
        OMATRIX savedCTM;
        MATRIX_COPY(&savedCTM, &thegsPageCTM(*gstateptr));
        if ( sparams->usematrix )
          MATRIX_COPY(&thegsPageCTM(*gstateptr), &sparams->orig_ctm);
        rval = IDLOM_STROKE(colorType, sparams, NULL);
        MATRIX_COPY(&thegsPageCTM(*gstateptr), &savedCTM );
      }
      else {
        rval = IDLOM_FILL(colorType, filltype, path, NULL);
      }
      switch ( rval ) {
      case NAME_false:  /* PS error in callbacks */
        free_dl_object( currobj->vd_lobj, page );
        return FALSE;
      case NAME_Discard:        /* just pretending */
        free_dl_object( currobj->vd_lobj, page );
        return TRUE;
      default:          /* only add, for now */
        break;
      }
    }

    if ( pdfout_enabled() ) {
      Bool result;
      if ( sparams != NULL ) {
        result = pdfout_dostroke(context->pdfout_h, sparams, colorType);
      } else {
        result = pdfout_dofill(context->pdfout_h, path, filltype, 0 ,colorType);
      }
      if ( !result ) {
        free_dl_object( currobj->vd_lobj, page );
        return FALSE;
      }
    }

    return add_listobject(page, currobj->vd_lobj, NULL);
  }

  vd_device = CURRENT_DEVICE();
  allocsize  = sizeof( VIGNETTEARGS ) +
    ( sparams ? sizeof( STROKE_PARAMS )   : 0 ) +
#if PS2_PDFOUT
    ( needs_colorinfo ? sizeof(COLORinfo) : 0 ) +
#endif
    ( currobj->vd_icolor_dims < 4 ? 4 : currobj->vd_icolor_dims ) *
      sizeof( currobj->vd_icolor_values[0] );
  if ( (newobj = ( VIGNETTEARGS * )mm_alloc( mm_pool_temp,
                                             allocsize,
                                             MM_ALLOC_CLASS_VIGNETTEARGS) )
      != NULL ) {
    int32 i;
    Bool pathsgotswapped = ( p1 != path->firstpath);

    /* Copy over everything we've accumulated so far. */
    (*newobj) = (*currobj);

    /* Setup pointer into the color values */
    newobj->vd_icolor_values = (USERVALUE *)((uint8 *)newobj + allocsize ) -
      ( currobj->vd_icolor_dims < 4 ? 4 : currobj->vd_icolor_dims );

    if ( flushcandidate && fPromoteCurrColor) {
      /* Had promoted the Gray to RGB or CMYK but now we know this object
         does not continue the vignette we had better put it back to gray */
      HQASSERT(currobj->vd_icolor_space_original == SPACE_DeviceGray,
               "Expected to only promote Gray to RGB or Gray to CMYK");
      newobj->vd_icolor_space_original = currobj->vd_icolor_space_original;
      newobj->vd_icolor_space = currobj->vd_icolor_space_original;
      newobj->vd_icolor_values[0] = currobj->vd_icolor_value_orig;
      newobj->vd_icolor_dims = 1;
      oName(newobj->vd_icolor_object) = system_names + NAME_DeviceGray;
      fPromoteCurrColor = FALSE;
    }
    else {
      /* Copy over color values */
      for ( i = 0; i < newobj->vd_icolor_dims; ++i )
        newobj->vd_icolor_values[i] = currobj->vd_icolor_values[i];
    }
    if ( flushcandidate && fPromotePrevColor ) {
      /* dl colors have not been mapped yet, so nothing more to be done */
      fPromotePrevColor = FALSE;
    }

    path1 = &newobj->vd_path[0];
    pt = &newobj->vd_plst[0];
    path1->firstpath = pt;
    if ( newobj->vd_numpaths == 2 ) {
      path2 = &newobj->vd_path[1];
      pt = &newobj->vd_plst[1];
      path2->firstpath = pt;
      if ( pathsgotswapped ) {
        pt = &newobj->vd_plst[1];
        path1->firstpath = pt;
        pt = &newobj->vd_plst[0];
        path2->firstpath = pt;
      }
    }
    newobj->size = ( uint16 )allocsize;
#if PS2_PDFOUT
    if ( needs_colorinfo )
       {
          newobj->orig_color.orig_colinfo = (COLORinfo *)(((uint8 *)newobj) +
              allocsize -  sizeof(COLORinfo));
          *newobj->orig_color.orig_colinfo = *colinfo;
       }
#endif

    newobj->vd_sparams = NULL;
    if ( sparams ) {
      newobj->vd_sparams = ( STROKE_PARAMS * )( newobj + 1 );
      (*newobj->vd_sparams) = (*sparams);
      newobj->vd_sparams->usematrix = TRUE; /* protect against deferring */
    }

    /* [11690] Can't steal the path if it is shared of course */
    if (path && path->firstpath && path->firstpath->shared > 0)
      copythepath = TRUE ;

    if ( copythepath ) {
      Bool result;

      /* It's possible that the old path (copied from currobj) will be leaked:
       * I'm basing my assumption that it's not on the fact that we haven't had
       * any leaks reported in vignette detection recently.
       */
      path_init( &newobj->vd_origpath );

      result = path_copy( &newobj->vd_origpath, path, mm_pool_temp );
      if ( !result ) {
        mm_free( mm_pool_temp, ( mm_addr_t )newobj, newobj->size);
        return error_handler( VMERROR );
      }
      p1 = newobj->vd_origpath.firstpath;
      pt = &newobj->vd_plst[0];
      pt->subpath = p1->subpath;
      if ( newobj->vd_numpaths == 2 ) {
        p2 = p1->next;
        pt = &newobj->vd_plst[1];
        pt->subpath = p2->subpath;
      }
    }
    else {
      /* Steal path (normally from gstate); means clear out old path having
       * stolen it..
       */
      path_init( path );
    }

    { /* Update rest of structure. */
      LINELIST *l1, *l2;

      path = &newobj->vd_origpath;
      path1->lastpath = p1;
      l1 = p1->subpath;
      while ( l1->next != NULL )
        l1 = l1->next;
      path1->lastline = l1;
      path1->curved = path->curved;
      path1->protection = path->protection;
      if ( newobj->vd_numpaths == 2 ) {
        path2->lastpath = p2;
        l2 = p2->subpath;
        while ( l2->next != NULL )
          l2 = l2->next;
        path2->lastline = l2;
        path2->curved = path->curved;
        path2->protection = path->protection;
      }
    }
    if ( sparams ) {
      /* Link up pointer to new PATHINFO. */
      HQASSERT(newobj->vd_numpaths == 1, "can only deal with 1 sub-path");
      newobj->vd_sparams->thepath = ( &newobj->vd_origpath );
    }
    if ( newobj->vd_lobj != NULL ) {
      /* For vignettes we may use a completely different color chain; for
       * example if we have a different intercept for a vignette. In
       * addition, certain controls like 100% Black Intercept don't apply
       * for such objects (even if use no intercept).
       * So, since we don't know until later on if an object is part of a
       * vignette or not, we need to transform the color twice; once with
       * its own colorType and once with colorType set to GSC_VIGNETTE.
       * However, don't do this when inside a pattern et al.
       */
      if ( DEVICE_INVALID_CONTEXT()) {
        HQASSERT(!vd_gotcomposite,
          "vd_gotcomposite should be FALSE in chars/patterns");
        dlc_get_black(page->dlc_context, &newobj->vdl_color);
      }
      else {
        COLORSPACE_ID ispace;
        Bool result;

        if ( fPromoteCurrColor ) {
          ispace = (COLORSPACE_ID) newobj->vd_icolor_space;
          HQASSERT(ispace == SPACE_DeviceRGB || ispace == SPACE_DeviceCMYK,
                   "Promoted space should only be RGB or CMYK");
        } else {
          ispace = (COLORSPACE_ID) newobj->vd_icolor_space_original;
        }

        if ( ispace == SPACE_DeviceRGB ||
             ispace == SPACE_DeviceCMYK ||
             ispace == SPACE_DeviceGray )
          result = gsc_setcolorspacedirect(gstateptr->colorInfo,
                                           GSC_VIGNETTE, ispace);
        else {
          OBJECT *ocolorspace = gsc_getcolorspaceobject(gstateptr->colorInfo,
                                                        colorType);
          HQASSERT(!fPromoteCurrColor,
                   "Cannot use gstate color space when promoted currobj");
          result = push( ocolorspace, &operandstack ) &&
                   gsc_setcolorspace(gstateptr->colorInfo,&operandstack,
                                     GSC_VIGNETTE );
        }

        if ( result ) {
          dl_color_t saved_dlc_currentcolor;
          VIGNETTEARGS *vd_saveheadobj = vd_headobj;

          /**
           * Invoking color chains may require execution of tint functions,
           * which mean a recursive invocation of the interpreter.
           * It is possible for the recursive interpreter to end-up back here
           * in the vignette code, which is not designed to be re-enterant.
           * So to avoid this turn off vignette detection, invoke the color
           * chains, then turn it back on again. Inspection has shown just
           * nulling out vd_headobj is sufficient.
           * \todo BMJ 07-Aug-09 : Vignette code needs cleaner context -
           * clean up all the static/global variable usage.
           */
          vd_headobj = NULL; /* save vignette detection */

          /* Save dlc_currentcolor. */
          dlc_copy_release(page->dlc_context, &saved_dlc_currentcolor,
                           dlc_currentcolor(page->dlc_context));

          /* Obtain the shfill chain equivalent color for the current object */
          if ( !fCurrColorConvToRGB ) {
            result =
              gsc_setcolordirect(gstateptr->colorInfo, GSC_VIGNETTE,
                                 newobj->vd_icolor_values ) &&
              gsc_invokeChainSingle(gstateptr->colorInfo, GSC_VIGNETTE);
          }
          else {
            /* If the color was converted to RGB for the purpose of a
             * monotonicity check, revert to using the original values at
             * this stage
             */
            result =
              gsc_setcolordirect(gstateptr->colorInfo, GSC_VIGNETTE,
                                 vd_color_orig4 ) &&
              gsc_invokeChainSingle( gstateptr->colorInfo, GSC_VIGNETTE );
          }

          if ( result )
            dlc_copy_release(page->dlc_context, &newobj->vdl_color,
                             dlc_currentcolor(page->dlc_context));

          /* Re-convert previous Grays now they have been converted to
             RGB or CMYK input space. This ensures the vignette is
             treated consistently across the elements (i.e., same
             intercept is applied etc.) */
          if ( result && fPromotePrevColor ) {
            VIGNETTEARGS * nextobj;
            for ( nextobj = vd_saveheadobj;
                  result && nextobj != NULL;
                  nextobj = nextobj->vd_next ) {
              HQASSERT(nextobj->vd_icolor_space == newobj->vd_icolor_space,
                       "nextobj color space is inconsistent with newobj");
              result =
                gsc_setcolordirect(gstateptr->colorInfo, GSC_VIGNETTE,
                                   nextobj->vd_icolor_values ) &&
                gsc_invokeChainSingle( gstateptr->colorInfo, GSC_VIGNETTE );
              if ( result ) {
                dlc_release(page->dlc_context, &nextobj->vdl_color);
                dlc_copy_release(page->dlc_context, &nextobj->vdl_color,
                                 dlc_currentcolor(page->dlc_context));
              }
            }
          }
          vd_headobj = vd_saveheadobj; /* restore vignette detection */

          if ( result ) {
            vd_gotcomposite |= ((newobj->vd_lobj->spflags & RENDER_RECOMBINE)
                                                                         == 0);
            /* Restore dlc_currentcolor */
            dlc_copy_release(page->dlc_context, dlc_currentcolor(page->dlc_context),
                             &saved_dlc_currentcolor);
          }
        }

        if ( !result ) {
          /* Oops, something failed... */
          path_free_list(newobj->vd_origpath.firstpath, mm_pool_temp);
          mm_free( mm_pool_temp, ( mm_addr_t )newobj, newobj->size);
          return FALSE;
        }
      }
    }
    /* Before we link in to our current candidate, allocate hdlt bits... */
    newobj->vd_hdlt = NULL;
    if ( isHDLTEnabled( *gstateptr )) {
      /* Maybe need a DEVICE_SETG here, but don't think so... */
      IDLOMARGS *prevargs = vd_tailobj ? vd_tailobj->vd_hdlt : NULL;
      if ( sparams ) {
        /* need to account for any stroke structure specified matrix adjust. */
        OMATRIX savedCTM;
        MATRIX_COPY(&savedCTM, &thegsPageCTM(*gstateptr));
        if ( sparams->usematrix )
          MATRIX_COPY(&thegsPageCTM(*gstateptr), &sparams->orig_ctm);
        newobj->vd_hdlt = IDLOM_LATCH_STROKE(colorType, newobj->vd_sparams,
                                             prevargs);
        MATRIX_COPY(&thegsPageCTM(*gstateptr), &savedCTM);
      }
      else {
        newobj->vd_hdlt = IDLOM_LATCH_FILL(colorType, filltype,
                                           &newobj->vd_origpath, prevargs);
      }

      /* Did we succeed. */
      if ( newobj->vd_hdlt == NULL ) {
        path_free_list(newobj->vd_origpath.firstpath, mm_pool_temp);
        mm_free( mm_pool_temp, ( mm_addr_t )newobj, newobj->size);
        return error_handler( VMERROR );;
      }
    }

    if ( pdfout_enabled() ) {
      Bool result;
      if ( sparams != NULL ) {
        result = pdfout_vignettestroke(context->pdfout_h, newobj->vd_sparams,
                                       colorType);
      } else {
        result = pdfout_vignettefill(context->pdfout_h, path, filltype, 0,
                                     colorType);
      }
      if ( !result ) {
        path_free_list(newobj->vd_origpath.firstpath, mm_pool_temp);
        mm_free( mm_pool_temp, ( mm_addr_t )newobj, newobj->size);
        return FALSE;
      }
    }
    if ( vd_headobj == NULL ) {
      int32 n = ( newobj->vd_icolor_dims < 4 ? 4 : newobj->vd_icolor_dims );
      GS_COLORinfo *colorInfo = gstateptr->colorInfo;

      vd_headobj = newobj;
      vd_objectstate[0] = currentdlstate;
      vd_disposition_flags = gstateptr->user_label ? DISPOSITION_FLAG_USER : 0;

      /* Reset these here in case anything left over from analysis to
       * previous object. */
      newobj->vd_type[0] = VDT_Unknown;
      newobj->vd_type[1] = VDT_Unknown;

      /* The knockout/overprint, under color removal and black
       * generation procedures must remain consistent throughout the
       * vignette. Can rely on the procs staying around because
       * restore_ calls flush_vignette.
       */
      vd_gsoverrides.overprint = (uint8)gsc_getoverprint(colorInfo, colorType);
      vd_gsoverrides.undercolorremovalid =
        gsc_getundercolorremovalid(colorInfo);
      Copy(&vd_gsoverrides.undercolorremoval,
           gsc_getundercolorremovalobject(colorInfo));
      vd_gsoverrides.blackgenerationid = gsc_getblackgenerationid( colorInfo );
      Copy(&vd_gsoverrides.blackgeneration,
           gsc_getblackgenerationobject(colorInfo));

      /* Copy clipinfo structure and increment the reference counts of
       * the cliprecords to stop them being freed.
       */
      vd_clippath = thegsPageClip(*gstateptr); /* structure copy */
      gs_reservecliprec(theClipRecord(vd_clippath));
      theClipStack(vd_clippath) = NULL;

      HQASSERT(ncolorants >= 0, "ncolorants should never be -ve");
      HQASSERT(currobj->vd_icolor_dims > 0,
               "should be some colors in vignette object" );

      if ( ncolorants > 0 ) {
        HQASSERT(vd_colorants == NULL, "vd_colorants should have been freed");
        vd_colorants = mm_alloc( mm_pool_temp,
                                 ncolorants * sizeof( colorants[0] ),
                                 MM_ALLOC_CLASS_VIGNETTEARGS );
        if ( vd_colorants == NULL )
          return error_handler( VMERROR );
        vd_ncolorants = ncolorants;
        for ( i = 0; i < ncolorants; ++i )
          vd_colorants[i] = colorants[i];
      }
      HQASSERT(vd_colormonotonic == NULL,
               "vd_colormonotonic should have been freed");
      vd_colormonotonic = mm_alloc( mm_pool_temp,
                                    n * sizeof( vd_colormonotonic[0] ),
                                    MM_ALLOC_CLASS_VIGNETTEARGS );
      if ( vd_colormonotonic == NULL )
        return error_handler( VMERROR );
       vd_ncolormonotonic = n;
       while ((--n) >= 0 )
         vd_colormonotonic[n] = VDC_Unknown;
    }

    if ( !flushcandidate && check_clip_differences ) {
      if ( swapvdobjectstates ) {
        vd_objectstate[1] = vd_objectstate[0];
        vd_objectstate[0] = currentdlstate;
        gs_freecliprec(&theClipRecord(vd_clippath));

        /* Copy clipinfo structure and increment the reference counts of
         * the cliprecords to stop them being freed.
         */
        vd_clippath = thegsPageClip(*gstateptr); /* structure copy */
        gs_reservecliprec(theClipRecord(vd_clippath));
        theClipStack(vd_clippath) = NULL;
      }
      else {
        vd_objectstate[1] = currentdlstate;
      }
    }

    if ( vd_tailobj != NULL )
      vd_tailobj->vd_next = newobj;

    newobj->vd_next = NULL;
    newobj->vd_prev = vd_tailobj;
    vd_tailobj = newobj;

    ++vd_countfill;
    if ( newobj->vd_lobj )
      ++vd_countlobj;

    if ( endedcandidate ) {
      /* This element terminates the sequence. */
      return flush_vignette( VD_Default );
    }
  }
  else {
    /* first alloc failed */
    return error_handler( VMERROR );
  }
  return TRUE;
}

static Bool analyze_pathtopath_s( VIGNETTEARGS *currobj,
                                  VIGNETTEARGS *prevobj,
                                  int32 pathtests )
{
  PATHINFO *path1, *path2;

  HQASSERT(currobj != NULL, "currobj null");
  HQASSERT(prevobj != NULL, "prevobj null");

  HQASSERT(currobj->vd_sparams != NULL, "currobj->vd_sparams null");
  HQASSERT(prevobj->vd_sparams != NULL, "prevobj->vd_sparams null");

  HQASSERT(currobj->vd_numpaths == 1, "currobj should only have 1 sub-path");
  HQASSERT(prevobj->vd_numpaths == 1, "prevobj should only have 1 sub-path");

  HQASSERT(prevobj->vd_style == VDS_Unknown ||
           (prevobj->vd_style == VDS_StrongContained &&
            (pathtests & VDM_Exact)) || (prevobj->vd_style == VDS_Adjacent &&
            (pathtests & VDM_Translated)),
            "prevobj->style inconsistent with pathtests");

  path1 = &currobj->vd_path[0];
  path2 = &prevobj->vd_path[0];

  /* Only interested in these kinds of matches */
  pathtests &= ( VDM_Translated | VDM_Exact );

  if ( !strokedpathsaresimilar( path1, path2,
                                 prevobj->vd_type[0] == VDT_Circle,
                                 pathtests, VDM_Unknown /* hint */,
                                 &currobj->vd_match ))
    return FALSE;

  /* Only consider these kinds of matches now */
  currobj->vd_match &= ( VDM_Translated | VDM_Exact );

  if ( ( currobj->vd_match & VDM_Translated ) != 0 ) {
#if defined( ASSERT_BUILD )
    SYSTEMVALUE dx, dy;
    dx = currobj->vd_path[0].bbox.x1 - prevobj->vd_path[0].bbox.x1;
    dy = currobj->vd_path[0].bbox.y1 - prevobj->vd_path[0].bbox.y1;
    HQASSERT(fabs( dx - vd_disp[0] ) < gstateptr->pa_eps.e2dx &&
              fabs( dy - vd_disp[1] ) < gstateptr->pa_eps.e2dy,
              "dx,dy should be eq to vd_disp[..]");
#endif
    if ( prevobj->vd_type[0] != VDT_Line ||
         !strokedpathsareadjacent( path1,
                                    vd_disp[0],
                                    vd_disp[1],
                                    currobj->vd_sparams )) {

      currobj->vd_match &= ~VDM_Translated;
    }
  }

  if ( ((currobj->vd_match & VDM_Exact) != 0) &&
       (currobj->vd_sparams->linestyle.linewidth >=
        prevobj->vd_sparams->linestyle.linewidth) )
    currobj->vd_match &= ~VDM_Exact;

  if ( currobj->vd_match == VDM_Translated ) {
    if ( prevobj == vd_headobj )
      prevobj->vd_match = currobj->vd_match;
    HQASSERT(prevobj->vd_style == VDS_Unknown ||
             prevobj->vd_style == VDS_Adjacent,
             "prevobj->vd_style unexpected(0)");
    currobj->vd_style = prevobj->vd_style = VDS_Adjacent;
    currobj->vd_type[0] = prevobj->vd_type[0];
    return TRUE;
  }
  else if ( currobj->vd_match == VDM_Exact ) {
    if ( prevobj == vd_headobj )
      prevobj->vd_match = currobj->vd_match;
    HQASSERT(prevobj->vd_style == VDS_Unknown ||
             prevobj->vd_style == VDS_StrongContained,
             "prevobj->vd_style unexpected(1)");
    currobj->vd_style = prevobj->vd_style = VDS_StrongContained;
    currobj->vd_type[0] = prevobj->vd_type[0];
    return TRUE;
  }

  return FALSE;
}

static Bool analyze_single_pathtopath_f( VIGNETTEARGS *currobj,
                                          VIGNETTEARGS *prevobj,
                                          int32 pathtests )
{
  PATHINFO *path1, *path2;

  HQASSERT(currobj != NULL, "currobj null");
  HQASSERT(prevobj != NULL, "prevobj null");
  HQASSERT(prevobj->vd_numpaths == 1 && currobj->vd_numpaths == 1,
            "currobj and prevobj must have one path");

  path1 = &currobj->vd_path[0];
  path2 = &prevobj->vd_path[0];

  if ( !pathsaresimilar( path1, path2,
                          prevobj->vd_type[0] == VDT_Circle,
                          pathtests, VDM_Unknown /* hint */,
                          &currobj->vd_match ))
    return FALSE;

  /* Only interested in these kinds of matches */
  currobj->vd_match &= ( VDM_Exact | VDM_Translated | VDM_Scaled );

  /* Allow exact match for specific case of a rolled up rectangle only */
  if ( ( currobj->vd_match & VDM_Exact ) != 0 &&
       prevobj->vd_type[0] != VDT_RectangleDevice &&
       prevobj->vd_type[0] != VDT_RectangleUser &&
       prevobj != vd_headobj ) {
    currobj->vd_match &= ~VDM_Exact;
  }

  if ( ( currobj->vd_match & VDM_Translated ) != 0 ) {
#if defined( ASSERT_BUILD )
    SYSTEMVALUE dx, dy;
    dx = currobj->vd_path[0].bbox.x1 - prevobj->vd_path[0].bbox.x1;
    dy = currobj->vd_path[0].bbox.y1 - prevobj->vd_path[0].bbox.y1;
    HQASSERT(fabs( dx - vd_disp[0] ) < gstateptr->pa_eps.e2dx &&
              fabs( dy - vd_disp[1] ) < gstateptr->pa_eps.e2dy,
              "dx,dy should be eq to vd_disp[..]");
#endif
    if ( ( prevobj->vd_type[0] != VDT_RectangleDevice &&
           prevobj->vd_type[0] != VDT_RectangleUser ) ||
         !pathsareadjacent( path1, vd_disp[0], vd_disp[1] )) {

      currobj->vd_match &= ~VDM_Translated;
    }
  }

  if ( ( currobj->vd_match & VDM_Scaled ) != 0 ) {

    Bool fscaled = FALSE;

    /* Check that style is one of VDS_Contained, VDS_Overlapped, VDS_Spaced */
    if ( prevobj->vd_type[0] == VDT_RectangleDevice ||
         prevobj->vd_type[0] == VDT_RectangleUser ||
         prevobj->vd_type[0] == VDT_Circle ||
         prevobj->vd_type[0] == VDT_Simple ) {
      if ( fabs( vd_disp[0] ) < gstateptr->pa_eps.e2dx &&
           fabs( vd_disp[1] ) < gstateptr->pa_eps.e2dy ) {
        sbbox_t *bboxp = &prevobj->vd_gbbox;
        sbbox_t *bboxc = &currobj->vd_gbbox;
        /* Paths similar, simple and same position. So don't intersect. */
        if ( bbox_contains(bboxp, bboxc) ) {
          fscaled = TRUE;
        }
      }
    }
    if ( !fscaled )
      currobj->vd_match &= ~VDM_Scaled;
  }

  switch ( currobj->vd_match ) {
  case VDM_Translated :       /* insist on translated only */
    if ( prevobj == vd_headobj )
      prevobj->vd_match = currobj->vd_match;
    HQASSERT(prevobj->vd_style == VDS_Unknown ||
             prevobj->vd_style == VDS_Adjacent,
             "prevobj->vd_style unexpected(0)");
    currobj->vd_style = prevobj->vd_style = VDS_Adjacent;
    currobj->vd_type[0] = prevobj->vd_type[0];
    return TRUE;
  case VDM_Scaled :           /* insist on scaled only */
    if ( prevobj == vd_headobj )
      prevobj->vd_match = currobj->vd_match;
    HQASSERT(prevobj->vd_style == VDS_Unknown ||
             prevobj->vd_style == VDS_Contained ||
             prevobj->vd_style == VDS_StrongContained,
             "prevobj->vd_style unexpected(1)");
    currobj->vd_style = prevobj->vd_style =
      ( prevobj->vd_style == VDS_Contained ? VDS_Contained :
                                             VDS_StrongContained );
    currobj->vd_type[0] = prevobj->vd_type[0];
    return TRUE;
  default :
    if ( (currobj->vd_match & VDM_Exact) != 0 ) {
      /* Rolled rect case for Quark;
         allows other matches besides exact */
      currobj->vd_match = prevobj->vd_match = VDM_Exact;
      currobj->vd_style = prevobj->vd_style;
      currobj->vd_type[0] = prevobj->vd_type[0];
      return FALSE;    /* false to invoke rolled rect handling */
    }
  }

  /* Did not find a suitable disambiguated match */
  currobj->vd_match = VDM_Unknown;
  return FALSE;
}

static Bool analyze_double_pathtopath_f( VIGNETTEARGS *currobj,
                                          VIGNETTEARGS *prevobj )
{
  PATHINFO *path1, *path2;

  HQASSERT(currobj != NULL, "currobj null");
  HQASSERT(prevobj != NULL, "prevobj null");
  HQASSERT(prevobj->vd_numpaths == 2,
           "prevobj must have two paths");
  HQASSERT(currobj->vd_numpaths == 1 || currobj->vd_numpaths == 2,
           "must have one or two paths");

  path1 = &prevobj->vd_path[1];
  path2 = &currobj->vd_path[0];

  /* When we've got two sub-paths then either:
   *  1. The inner shape of the previous object must EQ the outer shape of the
   *     current object.
   *  2. The outer shape of the current object must be INSIDE the outer shape
   *     of the previous object.
   *     AND
   *     The inner shape of the previous object must be INSIDE the outer shape
   *     of the current object.
   *     AND
   *     The inner shape of the current object must be INSIDE the inner shape
   *     of the previous object.
   * For now we will only cope with case 1.
   */

  /* FALSE arg means use displacement epsilons to decide if exact or
   * translated. */
  if ( pathsaresimilar( path1, path2,
                        prevobj->vd_type[1] == VDT_Circle,
                        VDM_Exact /* tests */, VDM_Exact /* hint */,
                        &currobj->vd_match ) &&
       ( currobj->vd_match & VDM_Exact ) != 0 ) {
    currobj->vd_type[0] = prevobj->vd_type[1];
    currobj->vd_orient[0] = prevobj->vd_orient[1];
    if ( currobj->vd_numpaths == 1 ) {
      currobj->vd_style = prevobj->vd_style;
      currobj->vd_match = prevobj->vd_match;
      return TRUE;
    }
  }
  else {
    path_reverse_linelists( path2->firstpath, NULL );
    if ( pathsaresimilar( path1, path2,
                          prevobj->vd_type[1] == VDT_Circle,
                          VDM_Exact /* tests */, VDM_Exact /* hint */,
                          &currobj->vd_match  ) &&
         ( currobj->vd_match & VDM_Exact ) != 0 ) {
      path_reverse_linelists( path2->firstpath, NULL );
      currobj->vd_type[0] = prevobj->vd_type[1];
      HQASSERT(prevobj->vd_orient[1] != VDO_Unknown, "should be known");
      currobj->vd_orient[0] = ( prevobj->vd_orient[1] == VDO_ClockWise ?
          VDO_AntiClockWise : VDO_ClockWise );
      if ( currobj->vd_numpaths == 1 ) {
        currobj->vd_style = prevobj->vd_style;
        currobj->vd_match = prevobj->vd_match;
        return TRUE;
      }
    }
    /*
     * Ditto for rings going out, but not for now...
     * else (...) {
     * }
     */
    else { /* Failed to continue vignette, but see if can start one. */
      path_reverse_linelists( path2->firstpath, NULL );
      return FALSE;
    }
  }

  /* Now check that fill is indeed a ring and that second path contained
   * in first. */
  if ( !analyze_2ndpath_f( currobj )) {
    currobj->vd_type[0] = VDT_Unknown;
    return FALSE;
  }
  if ( currobj->vd_style != VDS_StrongContained ||
       prevobj->vd_style != VDS_StrongContained )
    prevobj->vd_style = currobj->vd_style = VDS_Contained;
  return TRUE;
}

static Bool analyze_pathtopath_f( VIGNETTEARGS *currobj,
                                   VIGNETTEARGS *prevobj,
                                   int32 pathtests )
{
  if ( prevobj->vd_numpaths == 1 && currobj->vd_numpaths == 1 )
    return analyze_single_pathtopath_f( currobj, prevobj, pathtests );
  else
    /* Only need pathtests for single path case. If have two paths, by
       definition, must be looking for a strong contained vignette (scaled) */
    return analyze_double_pathtopath_f( currobj, prevobj );
}

static Bool analyze_pathtorect_f( VIGNETTEARGS *currobj,
                                   VIGNETTEARGS *prevobj )
{
  HQASSERT(currobj, "currobj NULL");
  HQASSERT(prevobj, "prevobj NULL");

  if ( (currobj->vd_numpaths == 1) &&
       (prevobj->vd_numpaths == 1) &&
       (prevobj->vd_type[0] == VDT_RectangleDevice ||
        prevobj->vd_type[0] == VDT_RectangleUser) &&
       (fabs( vd_disp[0] ) < gstateptr->pa_eps.e2dx) &&
       (fabs( vd_disp[1] ) < gstateptr->pa_eps.e2dy) ) {
    if ( currobj->vd_match == VDM_Unknown )
      if ( !analyze_1stpath_f( currobj ))
        return FALSE;

    HQASSERT(currobj->vd_type[0] != VDT_Unknown, "type should be VDT_Unknown");

    switch ( currobj->vd_match ) {
    case VDM_Rotated:   /* Rolled up [full-]diamond. */
      HQASSERT(currobj->vd_type[0] == VDT_RectangleDevice ||
               currobj->vd_type[0] == VDT_RectangleUser,
               "should be a rectangle if VDM_Rotated match");
      /* FALL THROUGH */
    case VDM_Unknown:   /* Rolled up [full-]circle or [full-]diamond. */
      /* Now check that one end of the [full-]{circle,diamond} splits
       * the preceding rectangle. If it does then it is a [full-]circle
       * or [full-]diamond vignette.
       */
      { PATHINFO *path1 = &currobj->vd_path[0];
        PATHINFO *path2 = &prevobj->vd_path[0];
        if ( pathsplitsrect(path1, path2, currobj->vd_type[0], &vd_rolledrect))
        {
          currobj->vd_style = prevobj->vd_style = VDS_StrongContained;
          return TRUE;
        }
      }
      break;
    case VDM_Exact:
      /* Rolled up [mid]-linear. */
      HQASSERT(currobj->vd_type[0] == VDT_RectangleDevice ||
                currobj->vd_type[0] == VDT_RectangleUser,
                "should be a rectangle if Exact match");
      currobj->vd_style = prevobj->vd_style = VDS_StrongContained;
      vd_rolledrect = VDR_MidLinear;
      return TRUE;
    case VDM_Translated:
    case VDM_Scaled:
      /* nowt. */
      break;
    default:
      HQFAIL( "Uknown vd_match" );
      break;
    }
  }
  return FALSE;
}

static Bool analyze_2ndpath_f( VIGNETTEARGS *currobj )
{
  PATHINFO *path1 = &currobj->vd_path[0];
  PATHINFO *path2 = &currobj->vd_path[1];

  HQASSERT(currobj->vd_type[0] != VDT_Unknown,
    "vd_type should never be VDT_Unknown");
  HQASSERT(currobj->vd_numpaths == 2, "Wrong number of paths");

  if ( pathsaresimilar( path1, path2,
                        currobj->vd_type[0] == VDT_Circle,
                        VDM_Scaled /* tests */, VDM_Unknown /* hint */,
                        &currobj->vd_match  )){
    if ( ( currobj->vd_match & VDM_Scaled ) == 0 )
      return FALSE; /* Note paths must differ in size... */
    currobj->vd_type[1] = currobj->vd_type[0];
    currobj->vd_orient[1] = currobj->vd_orient[0];
    HQASSERT(currobj->vd_orient[1] != VDO_Unknown, "should be known");
  }
  else {
    path_reverse_linelists( path2->firstpath, NULL );
    if ( pathsaresimilar( path1, path2,
                          currobj->vd_type[0] == VDT_Circle,
                          VDM_Scaled /* tests */, VDM_Unknown /* hint */,
                          &currobj->vd_match )) {
      path_reverse_linelists( path2->firstpath, NULL );
      if ( ( currobj->vd_match & VDM_Scaled ) == 0 )
        return FALSE; /* Note paths must differ in size... */
      currobj->vd_type[1] = currobj->vd_type[0];
      HQASSERT(currobj->vd_orient[0] != VDO_Unknown, "should be known");
      currobj->vd_orient[1] =
        ( currobj->vd_orient[0] == VDO_ClockWise ? VDO_AntiClockWise :
                                   VDO_ClockWise );
    }
    else {
      path_reverse_linelists( path2->firstpath, NULL );
      return FALSE;
    }
  }
  HQASSERT(currobj->vd_match == VDM_Scaled, "vd_match not scaled only");

  /* Check shapes are well orientated and properly contained... */
  if ( currobj->vd_filltype == NZFILL_TYPE &&
       currobj->vd_orient[0] == currobj->vd_orient[1] )
    return FALSE;

  if ( (currobj->vd_type[0] == VDT_RectangleDevice &&
        currobj->vd_type[1] == VDT_RectangleDevice) ||
       (currobj->vd_type[0] == VDT_RectangleUser &&
        currobj->vd_type[1] == VDT_RectangleUser) ||
       (currobj->vd_type[0] == VDT_Circle &&
        currobj->vd_type[1] == VDT_Circle) ||
       (currobj->vd_type[0] == VDT_Simple &&
        currobj->vd_type[1] == VDT_Simple) ) {
    sbbox_t *bbox0 = &currobj->vd_path[0].bbox;
    sbbox_t *bbox1 = &currobj->vd_path[1].bbox;
    if ( fabs( 0.5 * ( bbox0->x1 + bbox0->x2 ) -
               0.5 * ( bbox1->x1 + bbox1->x2 )) < gstateptr->pa_eps.e2dx &&
         fabs( 0.5 * ( bbox0->y1 + bbox0->y2 ) -
               0.5 * ( bbox1->y1 + bbox1->y2 )) < gstateptr->pa_eps.e2dy ) {
      /* Paths similar, simple and same position. Therefore don't intersect. */
      currobj->vd_style = VDS_StrongContained;
      return TRUE;
    }
  }
  return FALSE;
}

static Bool analyze_1stpath_s( VIGNETTEARGS *currobj )
{
  Bool closed;
  Bool degenerate;
  PATHINFO *path1;

  HQASSERT(currobj, "currobj NULL");
  HQASSERT(currobj->vd_sparams, "currobj->vd_sparams NULL");

  HQASSERT(currobj->vd_type[0] == VDT_Unknown, "type should be VDT_Unknown");

  /* Analyze the current path(s). */
  path1 = &currobj->vd_path[0];
  if ( pathisaline( path1, &degenerate, &closed ) ) {
    if ( closed && currobj->vd_sparams->linestyle.linejoin != 1 ) {
      currobj->vd_type[0] = VDT_Line;
      currobj->vd_orient[0] = VDO_Unknown;
      return TRUE;
    }
    if ( !closed &&
         ( currobj->vd_sparams->linestyle.startlinecap != 1 ||
           currobj->vd_sparams->linestyle.endlinecap != 1 ||
           currobj->vd_sparams->linestyle.dashlinecap != 1 )) {
      currobj->vd_type[0] = VDT_Line;
      currobj->vd_orient[0] = VDO_Unknown;
      return TRUE;
    }
  }
  if ( degenerate ) {
    if ( !closed &&
         ( currobj->vd_sparams->linestyle.startlinecap != 1 ||
           currobj->vd_sparams->linestyle.endlinecap != 1 ||
           currobj->vd_sparams->linestyle.dashlinecap != 1 )) {
      return FALSE;
    }
  }
  currobj->vd_type[0] = VDT_Stroke;
  currobj->vd_orient[0] = VDO_Unknown;
  return TRUE;
}

static Bool analyze_1stpath_f( VIGNETTEARGS *currobj )
{
  Bool degenerate;
  PATHINFO *path1;

  HQASSERT(currobj->vd_type[0] == VDT_Unknown, "type should be VDT_Unknown");

  /* Analyze the current path(s). */
  path1 = &currobj->vd_path[0];
  if ( currobj->vd_pathtype == ISRECT ) {
    HQASSERT(currobj->vd_numpaths == 1, "more than one path can't be ISRECT");
    currobj->vd_type[0] = VDT_RectangleDevice;
    currobj->vd_orient[0] = VDO_Unknown;
    return TRUE;
  }
  if ( currobj->vd_pathtype == (ISRECT|ISFILL) ) {
    HQASSERT(currobj->vd_numpaths == 1, "more than one and ISRECT|ISFILL");
    currobj->vd_type[0] = VDT_RectangleUser;
    currobj->vd_orient[0] = VDO_Unknown;
    return TRUE;
  }
  if ( pathisacircle( path1, &degenerate, &currobj->vd_orient[0],
                      &currobj->vd_type[0], &currobj->vd_rcbtrap ))
    return TRUE;
  if ( degenerate )
    return FALSE;
  if ( pathisarectangle( path1, &degenerate, &currobj->vd_orient[0],
                         &currobj->vd_type[0], &currobj->vd_rcbtrap ))
    return TRUE;
  if ( degenerate )
    return FALSE;
  return FALSE;
}

static Bool pointsintersect(FPOINT pts1[4], int32 li1, FPOINT pts2[4],
                            int32 li2, int32 c1, int32 c2)
{
  int32 sign;
  SYSTEMVALUE r12;
  SYSTEMVALUE det;
  SYSTEMVALUE dx0, dy0, dx1, dy1;
  SYSTEMVALUE dxt, dyt;

  HQASSERT(pts1, "pts1 NULL");
  HQASSERT(pts2, "pts2 NULL");

  dx0 = pts1[li1].x - pts2[li2].x;
  dy0 = pts1[li1].y - pts2[li2].y;
  dx1 = pts1[c1].x - pts1[c2].x;
  dy1 = pts1[c1].y - pts1[c2].y;
  det = dx0 * dy1 - dy0 * dx1;
  sign = 1;
  if ( det < 0.0 ) {
    sign = -1;
    det = -det;
  }
  if ( det < PA_EPSILON )       /* Lines are parallel. */
    return FALSE;

  dxt = pts2[li2].x - pts1[c2].x;
  dyt = pts2[li2].y - pts1[c2].y;
  r12 = dyt * dx1 - dxt * dy1;
  if ( sign < 0 )
    r12 = -r12;
  det += PA_EPSILON;
  if ( r12 < -PA_EPSILON || r12 > det )
    return FALSE;
  r12 = dyt * dx0 - dxt * dy0;
  if ( sign < 0 )
    r12 = -r12;
  if ( r12 < -PA_EPSILON || r12 > det )
    return FALSE;
  return TRUE;
}

static Bool getstrokeoutline( PATHINFO *path, STROKE_PARAMS *sparams )
{
  Bool result;
  uint8 sadjust = sparams->strokeadjust;

  HQASSERT(path, "path NULL");
  HQASSERT(sparams, "sparams NULL");

  path_init( path );
  sparams->strokedpath = path;
  sparams->usematrix = TRUE; /* we may be delayed, so don't trust CTM */
  sparams->strokeadjust = FALSE;
  result = dostroke(sparams, GSC_ILLEGAL, STROKE_NOT_VIGNETTE);
  sparams->strokeadjust = sadjust;
  sparams->strokedpath = NULL;
  if ( !result )
    return FALSE;
  if ( !path->firstpath)
    return FALSE;
  return TRUE;
}

static int32 getrectstrokepoints( FPOINT pts[4], STROKE_PARAMS *sparams )
{
  int32 n;
  PATHINFO *path;
  PATHINFO spath;

  path = &spath;
  if ( !getstrokeoutline( path, sparams ))
    return 0;
  n = getpathrectanglepoints( path, pts );
  HQASSERT(n == 2 || n == 4, "funny looking rectangle");
  path_free_list( path->firstpath, mm_pool_temp);
  return ( n );
}

static Bool build_pathnfill_outline(VIGNETTEARGS *headobj,
                                    VIGNETTEARGS *tailobj,
                                    PATHLIST **thepath, Bool *free_path,
                                    NFILLOBJECT **pnfill,
                                    Bool *iscurved)
{
  HQASSERT(headobj  != NULL, "headobj NULL");
  HQASSERT(tailobj  != NULL, "tailobj NULL");
  HQASSERT(thepath  != NULL, "thepath NULL");
  HQASSERT(free_path != NULL, "free_path NULL");
  HQASSERT(pnfill   != NULL, "pnfill NULL");
  HQASSERT(iscurved != NULL, "iscurved NULL");

  (*thepath)  = NULL;
  (*free_path) = FALSE;
  (*pnfill)   = NULL;
  (*iscurved) = FALSE;

#if defined( DEBUG_BUILD )
  if ( pathoutline.firstpath != NULL ) {
    path_free_list(pathoutline.firstpath, mm_pool_temp);
    path_init( &pathoutline );
  }
#endif

  if ( headobj->vd_style == VDS_Contained ||
       headobj->vd_style == VDS_StrongContained ) {
    LISTOBJECT *lobj = headobj->vd_lobj;

    HQASSERT(tailobj->vd_style == VDS_Contained ||
             tailobj->vd_style == VDS_StrongContained,
             "tailobj should be VDS_[Strong]Contained too");

    if ( headobj->vd_numpaths == 1 ) {
      /* If no dl object exists then all objects MUST be clipped out. */
      if ( lobj == NULL )
        return TRUE;
      else if ( lobj->opcode == RENDER_fill ) {
#if defined( DEBUG_BUILD )
        if ( debug_vo ) {
          if ( headobj->vd_sparams ) {
            PATHINFO lpath;
            if ( !getstrokeoutline( &lpath, headobj->vd_sparams ))
              return FALSE;
            pathoutline = lpath;
          }
          else {
            if ( !path_copy(&pathoutline, &headobj->vd_path[0],
                            mm_pool_temp) )
              return FALSE;
          }
        }
#endif
        (*pnfill)  = lobj->dldata.nfill;
        (*thepath) = headobj->vd_path[0].firstpath;
        return TRUE;
      } else if ( lobj->opcode == RENDER_quad ) {
        /* There is no NFILL object, but we should have a path.
           build_nfill_outline will turn this into an NFILL. */
        HQASSERT(headobj->vd_path[0].firstpath,
                 "Quad object does not have path") ;
#if defined( DEBUG_BUILD )
        if ( debug_vo ) {
          if ( headobj->vd_sparams ) {
            PATHINFO lpath;
            if ( !getstrokeoutline( &lpath, headobj->vd_sparams ))
              return FALSE;
            pathoutline = lpath;
          }
          else {
            if ( !path_copy(&pathoutline, &headobj->vd_path[0],
                            mm_pool_temp) )
              return FALSE;
          }
        }
#endif
        /* Leave *pnfill as NULL. */
        *thepath = headobj->vd_path[0].firstpath;
        return TRUE;
      } else {
        HQASSERT(lobj->opcode == RENDER_rect, "unknown opcode");
        /* For stroke we need to extract the strokepath. */
        if ( headobj->vd_sparams ) {
          PATHINFO lpath;
          if ( !getstrokeoutline( &lpath, headobj->vd_sparams ))
            return FALSE;
          (*thepath) = lpath.firstpath;
#if defined( DEBUG_BUILD )
          if ( debug_vo )
            (*iscurved) = lpath.curved;
#endif
          (*free_path) = TRUE;
        }
      }
    }
    /* By definition, the first path is always the outer one. */
    if ( !(*thepath) ) {
      (*thepath) = headobj->vd_path[0].firstpath;
#if defined( DEBUG_BUILD )
      if ( debug_vo )
        (*iscurved) = ( headobj->vd_path[0] ).curved;
#endif
      HQASSERT((*thepath), "lost path of vignette shape");
    }
    /* Add special case code for when we have rings ending in a ring. */
    if ( headobj->vd_numpaths == 2 && tailobj->vd_numpaths == 2 ) {
      PATHLIST *thepath2;
      thepath2 = tailobj->vd_path[1].firstpath;
      HQASSERT(thepath2, "lost path of vignette shape");
      (*thepath)->next = thepath2;
#if defined( DEBUG_BUILD )
      if ( debug_vo )
        (*iscurved) |= ( tailobj->vd_path[1] ).curved;
#endif
    }
  }
  else {
    int32 index;
    LINELIST *tmpline;
    LINELIST *theline;
    int32 n1, n2;
    int32 li1, li2, li3, li4;
    FPOINT pts1[4], pts2[4];
    FPOINT *points[4];

    HQASSERT(headobj->vd_style == VDS_Adjacent,
        "headobj should be VDS_Adjacent");
    HQASSERT(tailobj->vd_style == VDS_Adjacent,
        "tailobj should be VDS_Adjacent too");
    HQASSERT(headobj->vd_numpaths == 1,
        "headobj can't be VDS_Adjacent with two paths");
    HQASSERT(tailobj->vd_numpaths == 1,
        "tailobj can't be VDS_Adjacent with two paths");
    HQASSERT(headobj->vd_type[0] == VDT_RectangleDevice ||
             headobj->vd_type[0] == VDT_RectangleUser ||
             headobj->vd_type[0] == VDT_Line,
             "headobj type must be VDT_Rectangle...");
    HQASSERT(tailobj->vd_type[0] == VDT_RectangleDevice ||
             tailobj->vd_type[0] == VDT_RectangleUser ||
             tailobj->vd_type[0] == VDT_Line,
             "tailobj type must be VDT_Rectangle...");

    if ( headobj->vd_sparams ) {
      /* For stroke case get stroked outline so we get correct
         stroke-adjusted 4 corners. */
      if ( (n1 = getrectstrokepoints(pts1, headobj->vd_sparams)) == 0 ||
           (n2 = getrectstrokepoints(pts2, tailobj->vd_sparams)) == 0 )
        return TRUE;
    }
    else {
      PATHINFO *path;
      path = &headobj->vd_path[0];
      n1 = getpathrectanglepoints( path, pts1 );
      HQASSERT(n1 == 2 || n1 == 4, "funny looking rectangle(1)");
      path = &tailobj->vd_path[0];
      n2 = getpathrectanglepoints( path, pts2 );
      HQASSERT(n2 == 2 || n2 == 4, "funny looking rectangle(2)");
    }
    if ( n1 != n2 ) {   /* Punt on this... */
      HQFAIL( "should never get this; mix of 2 & 4 line segments" );
      return FALSE;
    }
    /* Case of linewidth ending up as 0.0. */
    if ( n1 == 2 ) {
      li1 = 0;
      li2 = 1;
      li3 = 1;
      li4 = 0;
    }
    else {
      /* To find corners take line segment that is longest. */
      int32 i1, i3;
      SYSTEMVALUE ndist;
      SYSTEMVALUE ldist = 0.0;
      li1 = li3 = -1;
      for ( i1 = 0; i1 < 4; ++i1 ) {
        SYSTEMVALUE xi1 = pts1[i1].x;
        SYSTEMVALUE yi1 = pts1[i1].y;
        for ( i3 = 0; i3 < 4; ++i3 ) {
          SYSTEMVALUE dx = xi1 - pts2[i3].x;
          SYSTEMVALUE dy = yi1 - pts2[i3].y;
          ndist = dx * dx + dy * dy;
          if ( ndist > ldist ) {
            ldist = ndist;
            li1 = i1;
            li3 = i3;
          }
        }
      }
      HQASSERT(li1 >= 0 && li3 >= 0, "couldn't find largest");

      /* To find adjacent edges, check intersection of (li1,li3)) with
       * ((li1+2)%4,((li1+1)%4) & ((li1+2)%4,((li1+3)%4). If intersects
       * with ((li1+2)%4,((li1+1)%4) then other point, (li1+3). Ditto
       * other way round.
       */
      if ( pointsintersect(pts1, li1, pts2, li3, (li1+1)&3, (li1+2)&3) )
        li2 = ( li1 + 3 ) & 3;
      else if ( pointsintersect(pts1, li1, pts2, li3, (li1+2)&3, (li1+3)&3) )
        li2 = ( li1 + 1 ) & 3;
      else {
        HQFAIL("couldn't find intersected li2");
        li2 = ( li1 + 1 ) & 3;;
      }
      if ( pointsintersect(pts2, li3, pts1, li1, (li3+1)&3, (li3+2)&3) )
        li4 = ( li3 + 3 ) & 3;
      else if ( pointsintersect(pts2, li3, pts1, li1, (li3+2)&3, (li3+3)&3) )
        li4 = ( li3 + 1 ) & 3;
      else {
        HQFAIL("couldn't find intersected li4");
        li4 = ( li3 + 1 ) & 3;;
      }
    }
    points[0] = &pts1[li1];
    points[1] = &pts1[li2];
    points[2] = &pts2[li3];
    points[3] = &pts2[li4];

    (*thepath) = &p4cpath;
    theline = (*thepath)->subpath;
    for ( index = 0; index < 4; ++index ) {
      theline->point.x = points[index]->x;
      theline->point.y = points[index]->y;
      theline = theline->next;
      HQASSERT(theline, "somehow lost our 4 lines");
    }
    tmpline = (*thepath)->subpath;
    theline->point.x = tmpline->point.x;
    theline->point.y = tmpline->point.y;
    theline = theline->next;
    HQASSERT(!theline, "somehow didn't end our 4 lines");

#if defined( DEBUG_BUILD )
    if ( debug_vo )
      (*iscurved) = FALSE;
#endif
  }
  return TRUE;
}

static Bool build_nfill_outline( DL_STATE *page,
                                 VIGNETTEARGS *headobj,
                                 VIGNETTEARGS *tailobj,
                                 NFILLOBJECT **pnfill,
                                 uint8 *freefill )
{
  Bool result;
  Bool iscurved = FALSE;
  Bool freethepath = FALSE;
  PATHLIST *thepath = NULL;
  NFILLOBJECT *nfill = NULL;
  CLIPOBJECT *theclip;
  dbbox_t save_clip;
  USERVALUE flat;

  HQASSERT(headobj, "headobj NULL");
  HQASSERT(tailobj, "tailobj NULL");
  HQASSERT(pnfill, "pnfill NULL");
  HQASSERT(freefill, "freefill NULL");

  if ( !build_pathnfill_outline( headobj, tailobj,
                                 &thepath, &freethepath,
                                 &nfill, &iscurved ))
    return FALSE;

  /* If we either created the nfill, or one never existed, then return. */
  if ( nfill != NULL || thepath == NULL ) {
    (*pnfill) = nfill;
    (*freefill) = FALSE;
    return TRUE;
  }

  /* Save current clipping and set to objects so we correctly clip outline. */
  HQASSERT(vd_objectstate[0], "vd state should not have been cleared yet");
  theclip = vd_objectstate[0]->clipstate;
  save_clip = cclip_bbox;
  cclip_bbox = theclip->bounds;

  flat = fl_getflat();
  fl_setflat( headobj->vd_ftol );

  nfill = NULL;
  result = make_nfill(page, thepath, NFILL_ISFILL, &nfill);
  if ( result && nfill ) {
    dbbox_t bbox;
    Bool clipped;

    bbox_nfill(nfill, &cclip_bbox, &bbox, &clipped);
    if ( clipped ) {
      free_fill(nfill, page);
      nfill = NULL;
    }
  }
  fl_setflat( flat );
  cclip_bbox = save_clip;

  /* Add recombine trap info. */
  if ( rcbn_enabled() ) {
    if ( nfill != NULL && thepath->next == NULL ) {
      PATHINFO tpathinfo;
      path_init( &tpathinfo );
      tpathinfo.firstpath = thepath;
      /* Only do do-nuts if they end with a circle */
      HQASSERT(tailobj->vd_numpaths == 1,
               "Should only be 1 path in created outline");
      (void)path_bbox(&tpathinfo, NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);
      if ( headobj->vd_style == VDS_Adjacent ) {
        /* HACK, HACK, HACK; see task 20034. */
        gstateptr->pa_eps.e2x += 1.0;
        gstateptr->pa_eps.e2y += 1.0;
      }
      if ( !rcbt_addtrap(page->dlpools, nfill, &tpathinfo, FALSE /* fDonut */, NULL) )
        result = FALSE;
      if ( headobj->vd_style == VDS_Adjacent ) {
        /* HACK, HACK, HACK; see task 20034. */
        gstateptr->pa_eps.e2x -= 1.0;
        gstateptr->pa_eps.e2y -= 1.0;
      }
      HQASSERT(nfill->rcbtrap != NULL, "Somehow didn't generate trap");
    }
  }
#if defined( DEBUG_BUILD )
  if ( debug_vo ) {
    PATHINFO frompath;
    frompath.firstpath = thepath;
    frompath.curved = ( uint8 )iscurved;
    if ( !path_copy( &pathoutline, &frompath, mm_pool_temp ))
      return FALSE;
  }
#endif
  if ( freethepath )
    path_free_list( thepath, mm_pool_temp);

  /* Add special case code for when we have rings ending in a ring. */
  if ( headobj->vd_style == VDS_Contained ||
       headobj->vd_style == VDS_StrongContained ) {
    if ( headobj->vd_numpaths == 2 && tailobj->vd_numpaths == 2 ) {
      thepath->next = NULL;    /* reset. */
    }
  }

  (*pnfill)   = nfill;
  (*freefill) = ( uint8 )( nfill ? TRUE : FALSE );
  return result;
}

static Bool vn_iswhiteobject( VIGNETTEARGS *anyobj )
{
  int32 i, n;
  USERVALUE whcol;
  USERVALUE *icolor;

  HQASSERT(anyobj, "anyobj NULL");
  icolor = anyobj->vd_icolor_values;
  whcol = 0.0f;
  switch ( anyobj->vd_icolor_space ) {
  case SPACE_DeviceGray:
  case SPACE_DeviceRGB:
    whcol = 1.0f;
    /* fall through */
  case SPACE_DeviceCMYK:
  case SPACE_DeviceN:
  case SPACE_Separation:
    n = anyobj->vd_icolor_dims;
    for ( i = 0; i < n; ++i ) {
      USERVALUE tmp = ( USERVALUE )fabs( icolor[i] - whcol );
      if ( tmp >= CL_EPSILON )
        return FALSE;
    }
    return TRUE;
  default:
    HQFAIL( "shouldn't get any other color spaces here" );
    return FALSE;
  }
  /* not reached */
}

/**
 * Determines if the vignette is tending to a missing white end (but not if
 * the vignette goes white to white). The while loop allows us to deal with
 * repeated colors for elements in separations with a low number of levels.
 * Assuming the color is changing lineararly means we need a generous
 * epsilon (it is not worth doing curve fitting for this).
 */
static Bool vn_tending_to_white(VIGNETTEARGS *vargs_ht, Bool fhead)
{
  VIGNETTEARGS *vargs_np;
  USERVALUE *icolorht = vargs_ht->vd_icolor_values;

  HQASSERT(vargs_ht != NULL, "vargs_ht null");
  HQASSERT(rcbn_enabled(), "recombine not on");

  vargs_np = ( fhead ? vargs_ht->vd_next : vargs_ht->vd_prev );

  while ( vargs_np != NULL ) {
    if ( vargs_ht->vd_icolor_space == vargs_np->vd_icolor_space ) {
      int32 i, n;
      USERVALUE sum   = 0.0f;
      USERVALUE whcol = 0.0f;
      USERVALUE *icolornp = vargs_np->vd_icolor_values;
      switch ( vargs_ht->vd_icolor_space ) {
      case SPACE_DeviceGray:
      case SPACE_DeviceRGB:
        whcol = 1.0f;
        /* FALL THROUGH */
      case SPACE_DeviceCMYK:
      case SPACE_DeviceN:
      case SPACE_Separation:
        n = vargs_np->vd_icolor_dims;
        for ( i = 0; i < n; ++i ) {
          USERVALUE diff = icolorht[i] - icolornp[i];
          USERVALUE tmp = ( USERVALUE )fabs( icolorht[i] + diff - whcol );
          if ( tmp >= CL_EPSILON_HIGH )
            return FALSE;
          sum += ( USERVALUE )fabs( diff );
        }
        /* At least one component must have some difference. */
        if ( sum >= CL_EPSILON )
          return TRUE;
        break;
      default:
        HQFAIL("unexpected colorspace");
        return FALSE;
      }
    }
    else
      return FALSE;
    vargs_np = ( fhead ? vargs_np->vd_next : vargs_np->vd_prev );
  }
  return FALSE;
}

/**
 * build_paths_overprint is used by build_nfill_overprint to construct
 * new paths for the new whiteobj by extrapolating extendobj. Currently
 * deals with VDS_Adjacent and VDS_StrongContained (scaled) vignettes
 */
static Bool build_paths_overprint(VIGNETTEARGS *whiteobj,
                                  VIGNETTEARGS *extendobj,
                                  Bool fextendhead)
{
  OMATRIX t_matrix;

  HQASSERT(whiteobj != NULL, "build_paths_overprint: whiteobj null");
  HQASSERT(extendobj != NULL, "build_paths_overprint: extendobj null");
  HQASSERT(extendobj->vd_style == VDS_Adjacent ||
           extendobj->vd_style == VDS_StrongContained,
           "currently only deal with VDS_Adjacent or VDS_StrongContained");

  if ( extendobj->vd_numpaths == 1 ) {

    if ( !path_copy(&whiteobj->vd_path[0], &extendobj->vd_path[0],
                    mm_pool_temp ))
      return FALSE;

    if ( extendobj->vd_style == VDS_Adjacent ) {

      if ( fextendhead )
        path_translate( &whiteobj->vd_path[0],
                        -vd_disp[0], -vd_disp[1] );
      else
        path_translate( &whiteobj->vd_path[0],
                        vd_disp[0],  vd_disp[1] );
    } else { /* VDS_StrongContained */

      if ( fextendhead ) {
        if ( !pathsmatrixscale( &extendobj->vd_path[0],
                                 &extendobj->vd_next->vd_path[0],
                                 &t_matrix )) {
          path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
          return FALSE;
        }
      }
      else {
        if ( !pathsmatrixscale( &extendobj->vd_path[0],
                                 &extendobj->vd_prev->vd_path[0],
                                 &t_matrix )) {
          path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
          return FALSE;
        }
      }
      path_transform( &whiteobj->vd_path[0], &t_matrix );
    }
    (void)path_bbox(&whiteobj->vd_path[0], NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);
  }
  else {
    HQASSERT(extendobj->vd_numpaths == 2,
             "vd_num_paths must be either 1 or 2");
    HQASSERT(extendobj->vd_style == VDS_StrongContained,
             "Must a strong contained to have two paths");

    /* Can optimise do-nuts so only need to transform one of the paths.
     * Either extending before the head so the inner path comes from
     * the outer path of the current head object and the new outer is
     * a transformation, or extending after the tail so the outer path
     * comes from the inner of the current tail object and the new inner
     * is a transformation
     */

    if ( fextendhead ) {

      if ( !path_copy( &whiteobj->vd_path[0], &extendobj->vd_path[0],
                      mm_pool_temp ))
        return FALSE;

      if ( !path_copy( &whiteobj->vd_path[1], &extendobj->vd_path[0],
                        mm_pool_temp )) {
        path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
        return FALSE;
      }

      if ( !pathsmatrixscale( &extendobj->vd_path[0],
                               &extendobj->vd_next->vd_path[0],
                               &t_matrix ) ) {
        path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
        path_free_list(whiteobj->vd_path[1].firstpath, mm_pool_temp);
        return FALSE;
      }
      path_transform( &whiteobj->vd_path[0], &t_matrix );
    }
    else {

      if ( !path_copy( &whiteobj->vd_path[0], &extendobj->vd_path[1],
                        mm_pool_temp ))
        return FALSE;

      if ( !path_copy( &whiteobj->vd_path[1], &extendobj->vd_path[1],
                        mm_pool_temp )) {
        path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
        return FALSE;
      }

      if ( !pathsmatrixscale( &extendobj->vd_path[1],
                               &extendobj->vd_prev->vd_path[1],
                               &t_matrix ) ) {
        path_free_list(whiteobj->vd_path[0].firstpath, mm_pool_temp);
        path_free_list(whiteobj->vd_path[1].firstpath, mm_pool_temp);
        return FALSE;
      }
      path_transform( &whiteobj->vd_path[1], &t_matrix );
    }
    (void)path_bbox(&whiteobj->vd_path[0], NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);
    (void)path_bbox(&whiteobj->vd_path[1], NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);
  }
  return TRUE;
}

/**
 * This routine is used to calculate the outline of the extended vignette iff
 * it may have been in fact bigger. This can occur when the end of the
 * vignette is missed due to it being white and overprinting being on.
 * Set up a faked head/tail VIGNETTEARGS and call build_nfill_outline.
 */
static Bool build_nfill_overprint(DL_STATE *page,
                                  VIGNETTEARGS *extendobj,
                                  VIGNETTEARGS *headobj,
                                  VIGNETTEARGS *tailobj,
                                  NFILLOBJECT **pnfill,
                                  vig_whiteinfo_t *pwhite,
                                  uint8 *freefill)
{
  Bool result;
  STROKE_PARAMS sparams;
  VIGNETTEARGS vigextend;
  NFILLOBJECT *nfill;

  HQASSERT(extendobj == headobj || extendobj == tailobj,
           "extendobj MUST be head or tail");
  HQASSERT(extendobj->vd_style == VDS_Adjacent ||
           extendobj->vd_style == VDS_StrongContained,
           "currently only deal with VDS_Adjacent or VDS_StrongContained");

  vigextend.vd_lobj     = extendobj->vd_lobj;
  vigextend.vd_ftol     = extendobj->vd_ftol;
  vigextend.vd_pathtype = extendobj->vd_pathtype;
  vigextend.vd_filltype = extendobj->vd_filltype;
  vigextend.vd_numpaths = extendobj->vd_numpaths;

  path_init( &vigextend.vd_path[0] );
  path_init( &vigextend.vd_path[1] );

  if ( !build_paths_overprint(&vigextend, extendobj, (extendobj == headobj)) )
    return FALSE;

  vigextend.vd_gbbox = vigextend.vd_path[0].bbox;
  vigextend.vd_style = extendobj->vd_style;
  vigextend.vd_match = extendobj->vd_match;
  vigextend.vd_type[0] = extendobj->vd_type[0];
  vigextend.vd_type[1] = extendobj->vd_type[1];
  vigextend.vd_orient[0] = extendobj->vd_orient[0];
  vigextend.vd_orient[1] = extendobj->vd_orient[1];
  vigextend.vd_icolor_space = extendobj->vd_icolor_space;
  vigextend.vd_icolor_object = extendobj->vd_icolor_object;
  vigextend.vd_icolor_values = NULL;
  vigextend.vd_sparams = NULL;
  if ( extendobj->vd_sparams ) {
    vigextend.vd_sparams = &sparams;
    sparams = (*extendobj->vd_sparams);
    sparams.usematrix = TRUE; /* we may be delayed, so don't trust CTM */
    sparams.thepath = &(vigextend.vd_path[0]);
  }

  if ( extendobj == headobj ) {
    headobj->vd_prev = &vigextend;
    vigextend.vd_prev = NULL;
    vigextend.vd_next = headobj;
    result = build_nfill_outline( page, &vigextend, tailobj, &nfill, freefill );
    headobj->vd_prev = NULL;
  }
  else {
    tailobj->vd_next = &vigextend;
    vigextend.vd_prev = tailobj;
    vigextend.vd_next = NULL;
    result = build_nfill_outline( page, headobj, &vigextend, &nfill, freefill );
    tailobj->vd_next = NULL;
  }

  /* Also capture a DL object that we can chain if necessary.
   * Things we need to set up are:
   *  a) clipping
   *  b) halftoning
   */

  /* Save current clipping and set to objects so we correctly clip outline. */
  if ( result ) {
    dbbox_t save_clip;
    USERVALUE flat;
    CLIPOBJECT *theclip;
    STATEOBJECT *dlstate;

    HQASSERT(vd_objectstate[0], "vd tstate should not have been cleared yet");
    theclip = vd_objectstate[0]->clipstate;
    save_clip = cclip_bbox;
    cclip_bbox = theclip->bounds;

    flat = fl_getflat();
    fl_setflat( headobj->vd_ftol );

    /* Flush vignette may occur in a context when a DEVICE_SETG hasn't been
       done. Use saved state so that DL creation will work properly. */
    dlstate = page->currentdlstate;
    page->currentdlstate = vd_objectstate[0];
    setup_analyze_vignette();
    if ( vigextend.vd_sparams )
      result = dostroke_draw(&sparams);
    else
      result = dofill(&vigextend.vd_path[0], vigextend.vd_filltype,
                      GSC_FILL, FILL_VIA_VIG|FILL_NOT_ERASE);
    reset_analyze_vignette();
    page->currentdlstate = dlstate;

    /* Now to finish off getting the extra object, we simply need to
       fix up the state & color. */
    /* Ultimately, set object type and ALL planes to overprint. */
    if ( result &&
         vd_lobj != NULL ) {
      LISTOBJECT *lobj = vd_lobj;
      dl_color_t dlcWhite;
      dlc_clear( &dlcWhite );
      lobj->objectstate = vd_objectstate[0];
      if ( vd_gotcomposite ) {
        /* For non-recombine we can simply use the special white dl
         * color.  Must also clear the recombine bit, since may be
         * incorrectly inherited.
         */
        dlc_get_white(page->dlc_context, &dlcWhite);
        dl_release(page->dlc_context, &lobj->p_ncolor);
        dlc_to_lobj_release( lobj, &dlcWhite );
        lobj->spflags &= ~RENDER_RECOMBINE;
      }
      else {
        /* For recombine we must generate a non-special dl color,
         * since we may need to merge colorants into it (amongst other
         * operations).  Must also set the recombine bit, since may be
         * incorrectly inherited.
         */
        COLORVALUE    cv = COLORVALUE_PRESEP_WHITE;
        COLORANTINDEX ci = rcbn_current_colorant();
        if ( dlc_alloc_fillin(page->dlc_context, 1, &ci, &cv, &dlcWhite) ) {
          dl_release(page->dlc_context, &lobj->p_ncolor);
          dlc_to_lobj_release( lobj, &dlcWhite );
          lobj->spflags |= RENDER_RECOMBINE;
        }
        else {
          result = FALSE;
        }
      }
    }
    fl_setflat( flat );
    cclip_bbox = save_clip;
  }

  /* Free up the structures we created. */
  path_free_list(vigextend.vd_path[0].firstpath, mm_pool_temp);
  if ( extendobj->vd_numpaths == 2 )
    path_free_list(vigextend.vd_path[1].firstpath, mm_pool_temp);

  (*pnfill) = nfill;
  pwhite->lobj  = vd_lobj;
  pwhite->dropped = (vd_lobj == NULL);
  pwhite->used = pwhite->dropped;
  vd_lobj = NULL;
  return result;
}

/**
 * This routine is used to calculate the approximate circle that represents
 * the circular vignette. This is because we can't always use the outline
 * for our matching since Freehand draws this type of blend with different
 * outer circles. We therefore take the middle of the outer two circles as
 * our second potential match criteria.
 */
static Bool build_nfill_circle_outline( DL_STATE *page, VIGNETTEARGS *headobj,
                                        NFILLOBJECT **pnfill )
{
  int32 index;
  Bool result;
  LINELIST *tmpline;
  LINELIST *theline;
  PATHLIST *thepath;
  NFILLOBJECT *nfill;
  FPOINT pts1[12], pts2[12];
  dbbox_t save_clip;
  USERVALUE flat;
  CLIPOBJECT *theclip;

  HQASSERT(headobj, "headobj NULL");
  HQASSERT(pnfill , "pnfill NULL");
  HQASSERT(!headobj->vd_sparams, "headobj->vd_sparams not NULL");

  ( void )getpathcirclepoints( &headobj->vd_path[0], pts1 );

  /* May need to reverse second path if goes in wrong direction. */
  HQASSERT(headobj->vd_orient[0] != VDO_Unknown,
      "can't deal with unknown orient(1)" );
  HQASSERT(headobj->vd_orient[1] != VDO_Unknown,
      "can't deal with unknown orient(2)" );
  if ( headobj->vd_orient[0] != headobj->vd_orient[1] )
    path_reverse_linelists(headobj->vd_path[1].firstpath, NULL );
  ( void )getpathcirclepoints( &headobj->vd_path[1], pts2 );
  if ( headobj->vd_orient[0] != headobj->vd_orient[1] )
    path_reverse_linelists(headobj->vd_path[1].firstpath, NULL );

  thepath = &p4curve;
  theline = thepath->subpath;
  for ( index = 0; index < 12; ++index ) {
    theline->point.x = 0.5 * (pts1[index].x + pts2[index].x);
    theline->point.y = 0.5 * (pts1[index].y + pts2[index].y);
    theline = theline->next;
    HQASSERT(theline, "somehow lost our 4 curves");
  }
  tmpline = thepath->subpath;
  theline->point.x = tmpline->point.x;
  theline->point.y = tmpline->point.y;
  theline = theline->next;
  HQASSERT(theline, "somehow lost our 4 curves");
  theline->point.x = tmpline->point.x;
  theline->point.y = tmpline->point.y;
  theline = theline->next;
  HQASSERT(!theline, "somehow didn't end our 4 curves");

  /* Save current clipping and set to objects so we correctly clip outline. */
  theclip = vd_objectstate[0]->clipstate;
  save_clip = cclip_bbox;
  cclip_bbox = theclip->bounds;

  flat = fl_getflat();
  fl_setflat( headobj->vd_ftol );

  nfill = NULL;
  result = make_nfill(page, &p4curve, NFILL_ISFILL, &nfill);
  if ( result && nfill ) {
    dbbox_t bbox;
    Bool clipped;

    bbox_nfill(nfill, &cclip_bbox, &bbox, &clipped);

    if ( clipped ) {
      free_fill(nfill, page);
      nfill = NULL;
    }
  }
  /* Add recombine trap info. */
  if ( rcbn_enabled()) {
    if ( nfill != NULL ) {
      PATHINFO tpathinfo;
      path_init( &tpathinfo );
      tpathinfo.firstpath = &p4curve;
      (void)path_bbox(&tpathinfo, NULL, BBOX_IGNORE_LEVEL2|BBOX_SAVE);
      if ( !rcbt_addtrap(page->dlpools, nfill, &tpathinfo, TRUE /* fDonut */, NULL) )
        result = FALSE;
      HQASSERT(nfill->rcbtrap != NULL, "Somehow didn't generate trap");
    }
  }
  fl_setflat( flat );
  cclip_bbox = save_clip;
  (*pnfill) = nfill;
  return result;
}

#ifdef VIGNETTE_REPLACEMENT
Bool setup_axial_shfill(VIGNETTEARGS *headobj, VIGNETTEARGS *tailobj,
                        OMATRIX *shmat, OBJECT *thed, OBJECT *thef,
                        int32 *orientation)
{
  PATHLIST *thepath = NULL;
  LINELIST *theline = NULL;
  NFILLOBJECT *nfill = NULL;
  Bool freethepath = FALSE;
  Bool iscurved = FALSE;

  static OBJECT coords_olist[4] = { /* Will fill in coord values later */
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT coords = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 4 }, {{ (void *)coords_olist }}
  };
  static OBJECT domain_olist[2] = { /* Will fill in domain values later */
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT domain = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 2 }, {{ (void *)domain_olist }}
  };
  static OBJECT bbox_olist[4] = { /* Will fill in bbox values later */
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT bbox = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 4 }, {{ (void *)bbox_olist }}
  };

  HQASSERT(headobj, "Head object should not be null");
  HQASSERT(tailobj, "Tail object should not be null");
  HQASSERT(shmat, "Matrix pointer should not be null");
  HQASSERT(thed, "Dictionary pointer should not be null");
  HQASSERT(tailobj->vd_type[0] == VDT_RectangleDevice ||
           tailobj->vd_type[0] == VDT_RectangleUser ||
           tailobj->vd_type[0] == VDT_Line,
           "Inappropriate type of vignette object");

  /* Matrix to map axial fill with BBox [0 0 1 1] onto vignette */
  if ( !build_pathnfill_outline( headobj, tailobj,
                                  &thepath, &freethepath,
                                  &nfill, &iscurved ))
    return FALSE;

  if ( thepath == NULL )
    return FALSE;

  theline = thepath->subpath;
  HQASSERT(theline != NULL, "Outline has no 1st point");
  HQASSERT(theline->type == MOVETO, "No moveto at start");
  shmat->matrix[2][0] = theline->point.x;
  shmat->matrix[2][1] = theline->point.y;
  theline = theline->next;
  HQASSERT(theline != NULL, "Outline has no 2nd point");
  HQASSERT(theline->type == LINETO, "Outline 2nd point isn't lineto");
  shmat->matrix[1][0] = theline->point.x - shmat->matrix[2][0];
  shmat->matrix[1][1] = theline->point.y - shmat->matrix[2][1];
  theline = theline->next;
  HQASSERT(theline != NULL, "Outline has no 3rd point");
  HQASSERT(theline->type == LINETO, "Outline 3rd point isn't lineto");
  theline = theline->next;
  HQASSERT(theline != NULL, "Outline has no 4th point");
  HQASSERT(theline->type == LINETO, "Outline 4th point isn't lineto");
  shmat->matrix[0][0] = theline->point.x - shmat->matrix[2][0];
  shmat->matrix[0][1] = theline->point.y - shmat->matrix[2][1];

  if ( tailobj->vd_style == VDS_StrongContained ||
       tailobj->vd_style == VDS_Contained ) {
    SYSTEMVALUE sx, sy;
    Bool x_same = FALSE, y_same = FALSE;

    if ( headobj->vd_numpaths != 1 || tailobj->vd_numpaths != 1 )
      return FALSE;

    theline = tailobj->vd_path[0].firstpath->subpath;
    HQASSERT(theline, "Outline start point missing");
    HQASSERT(theline->type == MOVETO, "Outline start not MOVETO");

    sx = theline->point.x;
    sy = theline->point.y;

    theline = theline->next;
    HQASSERT(theline != NULL, "Outline has no 2nd point");
    HQASSERT(theline->type == LINETO, "2nd point isn't lineto");

    if ( fabs(theline->point.x - sx - shmat->matrix[1][0]) <
         gstateptr->pa_eps.ex &&
         fabs(theline->point.y - sy - shmat->matrix[1][1]) <
         gstateptr->pa_eps.ey )
      x_same = TRUE;

    theline = theline->next;
    HQASSERT(theline != NULL, "Outline has no 3rd point");
    HQASSERT(theline->type == LINETO, "3rd point isn't lineto");
    theline = theline->next;
    HQASSERT(theline != NULL, "Outline has no 4th point");
    HQASSERT(theline->type == LINETO, "4th point isn't lineto");

    if ( fabs(theline->point.x - sx - shmat->matrix[0][0]) <
         gstateptr->pa_eps.ex &&
         fabs(theline->point.y - sy - shmat->matrix[0][1]) <
         gstateptr->pa_eps.ey )
      y_same = TRUE;

    /* Can't do contained vignette if wholly contained or if identical */
    if ( (x_same && y_same) || (!x_same && !y_same) )
      return FALSE;

    /* Flip axis if wrong direction of color change */
    if ( y_same ) {
      sx = shmat->matrix[0][0];
      sy = shmat->matrix[0][1];
      shmat->matrix[0][0] = shmat->matrix[1][0];
      shmat->matrix[0][1] = shmat->matrix[1][1];
      shmat->matrix[1][0] = sx;
      shmat->matrix[1][1] = sy;
      *orientation = 1;
    } else
      *orientation = 3;
  }

  MATRIX_SET_OPT_BOTH(shmat);
  if ( freethepath )
    path_free_list( thepath, mm_pool_temp);

  /* Create a dictionary for the shfill */
  if ( !ps_dictionary(thed, 7) )
    return FALSE;

  oName(nnewobj) = system_names + NAME_ShadingType;
  oInteger(inewobj) = 2;
  if ( !insert_hash(thed, &nnewobj, &inewobj))
    return FALSE;

  oName(nnewobj) = system_names + NAME_AntiAlias;
  if ( !insert_hash(thed, &nnewobj, &tnewobj))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Coords;
  oReal(coords_olist[0]) = 0.0f;
  oReal(coords_olist[1]) = 0.0f;
  oReal(coords_olist[2]) = 1.0f;
  oReal(coords_olist[3]) = 0.0f;
  if ( !insert_hash(thed, &nnewobj, &coords))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Domain;
  oReal(domain_olist[0]) = 0.0f;
  oReal(domain_olist[1]) = 1.0f;
  if ( !insert_hash(thed, &nnewobj, &domain))
    return FALSE;

  oName(nnewobj) = system_names + NAME_BBox;
  oReal(bbox_olist[0]) = 0.0f;
  oReal(bbox_olist[1]) = 0.0f;
  oReal(bbox_olist[2]) = 1.0f;
  oReal(bbox_olist[3]) = 1.0f;
  if ( !insert_hash(thed, &nnewobj, &bbox))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Function;
  if ( !ps_array(thef, tailobj->vd_icolor_dims) ||
       !insert_hash(thed, &nnewobj, thef) )
    return FALSE;

  return TRUE;
}

/**
 * Create radial shading dictionary
 */
Bool setup_radial_shfill(VIGNETTEARGS *headobj, VIGNETTEARGS *tailobj,
                          OMATRIX *shmat, OBJECT *thed, OBJECT *thef)
{
  OMATRIX shinv;
  PATHLIST *thepath = NULL;
  LINELIST *theline = NULL;
  NFILLOBJECT *nfill = NULL;
  SYSTEMVALUE ax, ay, bx, by, cx, cy, r1s, r2s;
  Bool freethepath = FALSE;
  int32 index;
  Bool iscurved = FALSE;

  static OBJECT coords_olist[6] = { /* Will fill in coord values later */
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT coords = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 6 }, {{ (void *)coords_olist }}
  };
  static OBJECT domain_olist[2] = { /* Will fill in domain values later */
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT domain = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 2 }, {{ (void *)domain_olist }}
  };
  static OBJECT extend_olist[2] = { /* Will fill in extend values later */
    {{ OBOOLEAN|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
    {{ OBOOLEAN|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0 }, {{ NULL }}},
  };
  static OBJECT extend = {
    { OARRAY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 2 }, {{ (void *)extend_olist }}
  };

  HQASSERT(headobj, "Head object should not be null");
  HQASSERT(tailobj, "Tail object should not be null");
  HQASSERT(shmat, "Matrix pointer should not be null");
  HQASSERT(thed, "Dictionary pointer should not be null");
  HQASSERT(tailobj->vd_type[0] == VDT_Circle,
           "Inappropriate type of vignette object");

  /* Matrix to map outer circle to centre at (0,0), radii (1,0), (0,1) */
  if ( !build_pathnfill_outline( headobj, tailobj,
                                  &thepath, &freethepath,
                                  &nfill, &iscurved ))
    return FALSE;

  if ( thepath == NULL )
    return FALSE;

  theline = thepath->subpath;

  HQASSERT(theline != NULL, "Outline has no 1st point");
  HQASSERT(theline->type == MOVETO, "No moveto at start");

  ax = theline->point.x;
  ay = theline->point.y;

  for ( index = 0; index < 3;) { /* Ignore degenerates, find end of curve */
    LINELIST *lastline = theline;
    theline = theline->next;
    if ( !theline )
      return FALSE;

    if ( fabs(theline->point.x - lastline->point.x) >=
         gstateptr->pa_eps.ex ||
         fabs(theline->point.y - lastline->point.y) >=
         gstateptr->pa_eps.ey ) {
      HQASSERT(theline->type == CURVETO, "point isn't curveto");
      ++index;
    } else {
      HQASSERT(theline->type == MOVETO || theline->type == LINETO,
               "Degenerate isn't moveto or lineto");
    }
  }

  bx = theline->point.x;
  by = theline->point.y;

  for ( index = 0; index < 3; ++index ) {
    theline = theline->next;
    HQASSERT(theline != NULL, "Outline point missing");
    HQASSERT(theline->type == CURVETO, "Outline point isn't curveto");
  }

  /* Centre is halfway between ax,ay and here */
  shmat->matrix[2][0] = (theline->point.x + ax) * 0.5;
  shmat->matrix[2][1] = (theline->point.y + ay) * 0.5;
  shmat->matrix[0][0] = ax - shmat->matrix[2][0];
  shmat->matrix[0][1] = ay - shmat->matrix[2][1];
  shmat->matrix[1][0] = bx - shmat->matrix[2][0];
  shmat->matrix[1][1] = by - shmat->matrix[2][1];
  MATRIX_SET_OPT_BOTH(shmat);

  if ( freethepath )
    path_free_list( thepath, mm_pool_temp);

  /* Now get radius and centre of inner circle */
  if ( tailobj->vd_numpaths == 2 )
    theline = tailobj->vd_path[1].firstpath->subpath;
  else
    theline = tailobj->vd_path[0].firstpath->subpath;

  ax = theline->point.x;
  ay = theline->point.y;

  for ( index = 0; index < 6;) { /* Ignore degenerates, find end of curve */
    LINELIST *lastline = theline;
    theline = theline->next;
    if ( !theline )
      return FALSE;

    if ( fabs(theline->point.x - lastline->point.x) >=
         gstateptr->pa_eps.ex ||
         fabs(theline->point.y - lastline->point.y) >=
         gstateptr->pa_eps.ey ) {
      HQASSERT(theline->type == CURVETO, "point isn't curveto");
      ++index;
    } else {
      HQASSERT(theline->type == MOVETO || theline->type == LINETO,
               "Degenerate isn't moveto or lineto");
    }
  }

  /* Centre is halfway between ax and here */
  bx = (theline->point.x + ax) * 0.5;
  by = (theline->point.y + ay) * 0.5;

  r1s = shmat->matrix[0][0] * shmat->matrix[0][0] +
    shmat->matrix[0][1] * shmat->matrix[0][1];
  if ( tailobj->vd_numpaths == 2 ) { /* Annular vignette */
    cx = ax - bx;
    cy = ay - by;
    r2s = cx * cx + cy * cy; /* Inner radius */
  } else { /* Radial vignette */
    r2s = 0.0;
  }

  if ( !matrix_inverse(shmat, &shinv) )
    return error_handler(UNDEFINED);

  /* Find equivalent centre point of inner circle */
  MATRIX_TRANSFORM_XY(bx, by, cx, cy, &shinv);

  /* Create a dictionary for the shfill */
  if ( !ps_dictionary(thed, 6) )
    return FALSE;

  oName(nnewobj) = system_names + NAME_ShadingType;
  oInteger(inewobj) = 3;
  if ( !insert_hash(thed, &nnewobj, &inewobj))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Coords;
  oReal(coords_olist[0]) = 0.0f;
  oReal(coords_olist[1]) = 0.0f;
  oReal(coords_olist[2]) = 1.0f;
  oReal(coords_olist[3]) = (USERVALUE)cx;
  oReal(coords_olist[4]) = (USERVALUE)cy;
  oReal(coords_olist[5]) = (USERVALUE)sqrt(fabs(r2s / r1s));
  if ( !insert_hash(thed, &nnewobj, &coords))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Domain;
  oReal(domain_olist[0]) = 0.0f;
  oReal(domain_olist[1]) = 1.0f;
  if ( !insert_hash(thed, &nnewobj, &domain))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Extend;
  oBool(extend_olist[0]) = FALSE;
  oBool(extend_olist[1]) = (tailobj->vd_numpaths != 2);
  if ( !insert_hash(thed, &nnewobj, &extend))
    return FALSE;

  oName(nnewobj) = system_names + NAME_Function;
  if ( !ps_array(thef, tailobj->vd_icolor_dims) ||
       !insert_hash(thed, &nnewobj, thef) )
    return FALSE;

  return TRUE;
}
#endif

static int8 map_vignette_kind( VIGNETTEARGS *tailobj )
{
  HQASSERT(tailobj, "tailobj NULL");

  switch ( tailobj->vd_type[0] ) {
  case VDT_Circle:
    return VK_Circle;

  case VDT_RectangleDevice:
  case VDT_RectangleUser:
  case VDT_Line:
    return VK_Rectangle;

  case VDT_Complex:
  case VDT_Simple:
  case VDT_Stroke:
    return VK_Unknown;

  default:
    HQFAIL( "unknown vd_type[0]" );
    break;
  }
  return VK_Unknown;
}

static int8 map_vignette_curve( VIGNETTEARGS *headobj )
{
  int32 i, n;
  int32 curve = VK_Unknown;
  int32 curves[4];
  VIGNETTEARGS *nextobj;
  USERVALUE linDiff = 0.0f;
  USERVALUE logDiff = 0.0f;

  HQASSERT(headobj, "headobj NULL");
  HQASSERT(headobj->vd_next, "headobj->vd_next NULL");

  for ( i = 0; i < 4; ++i )
    curves[i] = VK_Unknown;

  while (( nextobj = headobj->vd_next ) != NULL ) {
    USERVALUE *icolorh, *icolorn;

    if ( headobj->vd_icolor_space != nextobj->vd_icolor_space )
      return VK_Unknown;

    icolorh = headobj->vd_icolor_values;
    icolorn = nextobj->vd_icolor_values;
    n = headobj->vd_icolor_dims;
    for ( i = 0; i < n; ++i ) {
      USERVALUE tlinDiff = icolorn[i] - icolorh[i];

      if ( fabs( tlinDiff ) < CL_EPSILON ) {
        if ( curves[i] != VK_Unknown )
          return VK_Unknown;
      }
      else {
        if ( curve == VK_Unknown ) {
          curves[i] = curve = VK_Linear;
          linDiff = tlinDiff;
        }
        else {
          if ( fabs( tlinDiff - linDiff ) < CL_EPSILON ) {
            if ( curve == VK_Logarithmic )
              return VK_Unknown;
            else
              HQASSERT(curve == VK_Linear, "curve should be VK_Linear");
          }
          else {
            if ( curve == VK_Linear ) {
              curves[i] = curve = VK_Logarithmic;
              logDiff = linDiff / tlinDiff;
              linDiff = tlinDiff;
            }
            else {
              if ( fabs( linDiff / tlinDiff - logDiff ) < CL_EPSILON ) {
                HQASSERT(curve == VK_Logarithmic,
                         "curve should be VK_Logarithmic");
                logDiff = linDiff / tlinDiff;
                linDiff = tlinDiff;
              }
              else
                return VK_Unknown;
            }
          }
        }
      }
    }
    headobj = nextobj;
  }
  return ( int8 )curve;
}

#if defined( DEBUG_BUILD )

/**
 * Test the given object to see if it a vignette. If so return a DL object
 * representing the outline of the vignette painted red. This can then be
 * rendered for debug purposes.
 */
LISTOBJECT *debug_vignette_red_outline(LISTOBJECT *lobj)
{
  HQASSERT(lobj, "DL object NULL");

  if ( lobj->opcode == RENDER_vignette ) {
    VIGNETTEOBJECT *vigobj = lobj->dldata.vignette;

    HQASSERT(vigobj, "somehow lost VIGNETTEOBJECT");

    if ( vigobj->outlines.outline_lobj )
      return vigobj->outlines.outline_lobj;
  }
  return NULL;
}

static Bool create_vignette_red_outline( VIGNETTEOBJECT *vigobj )
{
  Bool result = TRUE;
  HQASSERT(vigobj, "vigobj NULL");
  if ( pathoutline.firstpath ) {
    ps_context_t *pscontext = get_core_context_interp()->pscontext ;
    uint8 *outlines = ( uint8 * )"systemdict begin\
                                    initgraphics\
                                    {} settransfer\
                                    {} setblackgeneration\
                                    {} setundercolorremoval\
                                    1 setflat 2 setlinewidth 1 0 0 setrgbcolor\
                                  end";
    STROKE_PARAMS params;

    /* Protect everything with a gsave/grestore. */
    result = save_(pscontext);
    if ( result ) {
      /* Setup environment for doing a red stroke. */
      theLen( snewobj ) = ( uint16 )strlen(( char * )outlines );
      oString(snewobj ) = outlines;
      result = push( &snewobj, &executionstack );
      if ( result && !interpreter( 1, NULL )) {
        result = FALSE;
      }

      /* Create the DL object and capture it. */
      disable_separation_detection();
      setup_analyze_vignette();
      result = result && path_close( CLOSEPATH, &pathoutline );
      set_gstate_stroke( &params, &pathoutline, NULL, FALSE );
      result = result && dostroke( &params, GSC_FILL, STROKE_NOT_VIGNETTE );
      reset_analyze_vignette();
      enable_separation_detection();

      if ( vd_lobj )
      {
        vigobj->outlines.outline_lobj = vd_lobj;
        vd_lobj = NULL;
      }

      HQASSERT(!vd_flushed_lobj, "otherwise get colors on the DL");

      /* We don't want to restore if there was an error in the interpreter, as
      this would restore away the $error dictionary setup by handleerror() in
      the interpreter call. The server loop will automatically restore to the
      server level. */
      if ( result )
        result = restore_(pscontext);
    }

    /* Free path and add the DL object to the vignette candidate. */
    path_free_list(pathoutline.firstpath, mm_pool_temp);
    path_init( &pathoutline );
  }
  return result;
}
#endif /* defined( DEBUG_BUILD ) */

#if PS2_PDFOUT
/**
 * This one is for the pdfout sake. It takes the currobj,
 * and returns a colinfo.
 * The argument colinfo is a pointer to an empty structure, that this function
 * can return (and at the moment, does).
 */
COLORinfo *build_colinfo_from_vinobj(VIGNETTEARGS *currobj, COLORinfo *colinfo)
{

   uint8 cspace = currobj->vd_icolor_space;
   uint8 bcspace = currobj->basecolorspace;
   theIgsColorSpaceGen(colinfo) = (uint32)-1; /* make sure nobody is confuse
                                                 this with areal colinfo */


   if ( cspace == SPACE_Separation && bcspace != SPACE_Separation ) {
     theIgsColorSpace(colinfo) = cspace;
     theIgsBaseColorSpace(colinfo) = bcspace;
     theIgsSepTintTransform(colinfo) =
         currobj->orig_color.sepcolor.septinttransform;
     theIgsSepName(colinfo) = currobj->orig_color.sepcolor.sepname;
   }
   else switch (bcspace) {
    case SPACE_DeviceRGB:
    case SPACE_DeviceGray:
    case SPACE_DeviceCMYK :
        theIgsColorSpace(colinfo) = bcspace;
        theIgsInputColor(colinfo)[0] = currobj->orig_color.orig_icolor[0];
        theIgsInputColor(colinfo)[1] = currobj->orig_color.orig_icolor[1];
        theIgsInputColor(colinfo)[2] = currobj->orig_color.orig_icolor[2];
        theIgsInputColor(colinfo)[3] = currobj->orig_color.orig_icolor[3];
        break;
   case SPACE_Pattern:
      HQFAIL("Pattern colorspace in a vignette");
      break;
   default:
       /* currently, the orig_colinfo will allocated only if pdfout_enabled. */
     HQASSERT(pdfout_enabled(),
         "Not in pdfout but calling with complex colorspace");
     return currobj->orig_color.orig_colinfo;
     break;
    }

  return colinfo;
}
#endif

/**
 * Work-around for potential clipping rect 'optimisation' (MarkJ).
 *
 * Previously, if the clipping path changed then the chain of objects
 * would be flushed. This is not suitable for jobs where a clipping
 * rectangle applies to some objects, but has been optimised away for
 * the objects which are known to be inside the clipping
 * rectangle. For example, if the clipping rect overlaps two ends of a
 * rectangular vignette, the clipping rect may apply to the first
 * rectangle, be optimised away for the intermediate objects, and
 * reapplied for the last object.
 *
 * In this case an unnecessary flush would occur, which may
 * potentially cause problems in trapping and recombination. To deal
 * with this optimisation it is necessary to add two new tests in
 * analyze_vignette. If the clipping path is known to have changed
 * then the first test is to check only a clip rect has been lost. The
 * new clipping path must still however be equivalent to the original
 * path, therefore the second test checks all the objects are inside
 * the original clip rect.
 *
 * If the clipping path changes a second time, it must revert to the
 * original clipping path for the vignette to continue. In which case
 * neither of the two test apply, and because of the uniqueness of
 * clipping paths and stateobjects, the stateobject test will
 * succeed. No further changes in the clipping path are allowed if the
 * vignette is to be continued.
 */
Bool flush_vignette( int32 cause )
{
  Bool result;

  if ( vd_headobj == NULL )
    return TRUE;

  switch ( cause ) {
  case VD_GRestore :
    if ( vd_cause == VD_Default ) {
      /* Second stateobject is null, therefore this is the first
       * change, only allow the removal of a cliprect (this is checked
       * in the next call to analyze_vignette).
       */
      HQASSERT(vd_objectstate[1] == NULL, "objectstates got out of sync(1)");
      vd_cause = VD_GRestore;
      return TRUE;
    }
    break;
  case VD_AddClip :
    if ( vd_cause == VD_Default ) {
      /* Second stateobject is null, therefore this is the first
       * change, only allow the addition of a cliprect (this is
       * checked in the next call to analyze_vignette).
       */
      HQASSERT(vd_objectstate[1] == NULL, "objectstates got out of sync(2)");
      vd_cause = VD_AddClip;
      return TRUE;
    }
    else if ( vd_cause == VD_GRestore &&
              vd_objectstate[1] != NULL ) {
      /* Second stateobject is not null, therefore this is the second
       * change (after having transitioned from VD_Default to VD_GRestore,
       * only allow the addition of a cliprect (this is checked in the
       * next call to analyze_vignette).
       */
      vd_cause = VD_AddClip;
      return TRUE;
    }
    break;
  case VD_Default :
    break;
  default :
    HQFAIL( "Unrecognised cause for flush_vignette" );
    return error_handler( UNREGISTERED );
  }

  ++vd_flushing;
  result = flush_vignette_chain(get_core_context_interp()->page);
  --vd_flushing;
  return result;
}

void abort_vignette(DL_STATE *page)
{
  if ( vd_headobj != NULL )
    (void)abort_vignette_chain(page, vd_headobj);
}

/**
 * This routine builds all the required outlines. Amongst others it creates:
 * 1. A basic outline which is the outer most shape of the vignette.
 *    Used for trapping outline and recombine merging.
 * 2. A special outline for recombining of Freehand circular vignettes.
 *    FH uses doughnuts to form the vignette, but the size of doughnuts depends
 *    on the number of steps in it. We therefore need to create a circle shape
 *    that represents a mid circle of the outer most doughnut to use for
 *    recombine.
 * 3. A rolled rect outline for recombining of Quark special effect
 *    vignettes.  Quark pre-pends an outer most rectangle to the
 *    vignette. We need to attach this outer most rectangle to the vignette
 *    so that we can correctly identify knockouts for recombine. This shape
 *    is therefore that of the vignette inside the rolled rect.
 * 4. Extended outlines for recombining of vignettes with overprinted ends.
 *    A number of applications end up loosing the ends of a vignette if the
 *    color in a partiuclar channel at the end point is 0.
 *    We need to create these extended outlines in case we later determine
 *    that we need to add back in a missing shape, and so hence extend the
 *    basic outline as described in 1.
 */
static Bool vn_buildoutlines(DL_STATE *page, VIGNETTEOBJECT *vigobj,
                             VIGNETTEARGS *headobj, VIGNETTEARGS *tailobj)
{
  vn_outlines_t *outlines;
  NFILLOBJECT *nfillb = NULL;
  NFILLOBJECT *nfillr = NULL;
  NFILLOBJECT *nfills = NULL;
  NFILLOBJECT *nfillh = NULL;
  NFILLOBJECT *nfillt = NULL;
  NFILLOBJECT *nfillo = NULL;
  NFILLOBJECT *nfillm = NULL;

  HQASSERT(vigobj, "vigobj NULL");
  HQASSERT(headobj, "headobj NULL");
  HQASSERT(tailobj, "tailobj NULL");

  outlines = &vigobj->outlines;

  outlines->freenfillb = FALSE;
  outlines->freenfillr = FALSE;
  outlines->freenfillh = FALSE;
  outlines->freenfillt = FALSE;

  vigobj->white.h = FALSE;
  vigobj->white.t = FALSE;
  vigobj->white.lobj = NULL;
  vigobj->white.used = TRUE;

#if defined( DEBUG_BUILD )
  vigobj->outlines.outline_lobj = NULL;
#endif
  vigobj->rolledrect = ( uint8 )vd_rolledrect;

  if ( rcbn_enabled()
#if defined( DEBUG_BUILD )
       || debug_vo
#endif
       ) {
    if ( tailobj->vd_style == VDS_StrongContained ||
         tailobj->vd_style == VDS_Contained ||
         tailobj->vd_style == VDS_Adjacent ) {
      if ( !build_nfill_outline( page, headobj, tailobj,
                                 &nfillb, &outlines->freenfillb ))
        return FALSE;
      nfillo = nfillm = nfillb;
    }
#if defined( DEBUG_BUILD )
    if ( debug_vo ) {
      if ( !create_vignette_red_outline( vigobj ))
        return FALSE;
    }
#endif
    if ( rcbn_enabled()) {
      if ( vd_rolledrect != VDR_Unknown ) {
        HQASSERT(tailobj->vd_style == VDS_StrongContained ||
                 tailobj->vd_style == VDS_Contained,
                 "rolled rects should be contained");
        if ( !build_nfill_outline( page, headobj->vd_next, tailobj,
                                   &nfillr, &outlines->freenfillr ))
          return FALSE;
        if ( vd_rolledrect == VDR_FullDiamond ||
             vd_rolledrect == VDR_FullCircular )
          nfillo = nfillr;
      }
      else if ( tailobj->vd_style == VDS_StrongContained &&
                tailobj->vd_type[0] == VDT_Circle ) {

        if ( headobj->vd_numpaths == 2 )
          if ( !build_nfill_circle_outline(page, headobj, &nfills) )
            return FALSE;

        if ( vn_tending_to_white( headobj, TRUE )) {
          if ( !build_nfill_overprint( page, headobj, headobj, tailobj,
                                       &nfillh,
                                       &vigobj->white,
                                       &outlines->freenfillh ))
            return FALSE;
          vigobj->white.h = TRUE;
        }
        else if ( vn_tending_to_white( tailobj, FALSE )) {
          if ( !build_nfill_overprint( page, tailobj, headobj, tailobj,
                                       &nfillt,
                                       &vigobj->white,
                                       &outlines->freenfillt ))
            return FALSE;
          vigobj->white.t = TRUE;
        }
      }
      else if ( (tailobj->vd_style == VDS_Adjacent) &&
                (tailobj->vd_type[0] == VDT_Line ||
                 tailobj->vd_type[0] == VDT_RectangleDevice ||
                 tailobj->vd_type[0] == VDT_RectangleUser) ) {

        if ( vn_tending_to_white( headobj, TRUE )) {
          if ( !build_nfill_overprint( page, headobj, headobj, tailobj,
                                       &nfillh,
                                       &vigobj->white,
                                       &outlines->freenfillh ))
            return FALSE;
          vigobj->white.h = TRUE;
        }
        else if ( vn_tending_to_white( tailobj, FALSE )) {
          if ( !build_nfill_overprint( page, tailobj, headobj, tailobj,
                                       &nfillt,
                                       &vigobj->white,
                                       &outlines->freenfillt ))
            return FALSE;
          vigobj->white.t = TRUE;
        }
      }
    }
  }
  outlines->nfillb = nfillb;
  outlines->nfills = nfills;
  outlines->nfillr = nfillr;
  outlines->nfillh = nfillh;
  outlines->nfillt = nfillt;
  outlines->nfillo = nfillo;
  outlines->nfillm = nfillm;
  return TRUE;
}

/**
 * This routine should be called to reset the globals used in detecting
 * vignettes. When we switch to using private structures, this routine will
 * fill in the private structure instead.
 */
static void vn_reset_detection(DL_STATE *page)
{
  vd_countfill = 0;
  vd_countlobj = 0;
  vd_forcesplit = FALSE;
  vd_device = DEVICE_ILLEGAL;
  vd_objectstate[0] = NULL;
  vd_objectstate[1] = NULL;
  vd_disp[0] = 0.0;
  vd_disp[1] = 0.0;
  vd_size[0] = 0.0;
  vd_size[1] = 0.0;
  vd_gotcomposite = FALSE;

  if ( vd_colormonotonic != NULL ) {
    HQASSERT(vd_ncolormonotonic > 0,
        "vd_ncolormonotonic should be > 0 if allocated" );
    mm_free( mm_pool_temp,
             ( mm_addr_t )vd_colormonotonic,
             vd_ncolormonotonic * sizeof( vd_colormonotonic[0] ));
    vd_ncolormonotonic = 0;
    vd_colormonotonic = NULL;
  }
  else
    HQASSERT(vd_ncolormonotonic == 0,
        "vd_ncolormonotonic should be == 0 if not allocated");
  if ( vd_colorants != NULL ) {
    HQASSERT(vd_ncolorants > 0, "vd_ncolorants should be > 0 if allocated");
    mm_free( mm_pool_temp,
             ( mm_addr_t )vd_colorants,
             vd_ncolorants * sizeof( vd_colorants[0] ));
    vd_ncolorants = 0;
    vd_colorants = NULL;
  }
  else
    HQASSERT(vd_ncolorants == 0,
        "vd_ncolorants should be == 0 if not allocated");

  vd_gsoverrides.overprint = FALSE;
  vd_gsoverrides.undercolorremovalid = 0;
  vd_gsoverrides.undercolorremoval = onull; /* Struct copy to set properties */
  vd_gsoverrides.blackgenerationid = 0;
  vd_gsoverrides.blackgeneration = onull; /* Struct copy to set properties */

  vd_headobj = NULL;
  vd_tailobj = NULL;

  vd_cause = VD_Default;
  /* vd_clippath; old code didn't set this */
  vd_cliptype = VDT_Unknown;
  /* vd_clippathinfo; old code didn't set this */
  vd_initmatrix = FALSE;
  /* vd_clipinvmatrix; old code didn't set this */
  vd_rolledrect = VDR_Unknown;

  /* Finished with the clip records, so decrement the reference
   * counters so they will get freed eventually.
   */
  gs_freecliprec(&theClipRecord(vd_clippath));

  if ( vd_saveddevice != DEVICE_ILLEGAL ) {
    SET_DEVICE( vd_saveddevice );
    vd_saveddevice = DEVICE_ILLEGAL;
  }
  dlc_release(page->dlc_context, &vd_dlc_overprints);
}

/**
 * This routine should be called when we've got a fatal error and simply want
 * to free all the memory and data structures associated with a vignette chain.
 */
static Bool abort_vignette_chain(DL_STATE *page, VIGNETTEARGS *vobj)
{
  (void)vn_complete_vignette(); /* close any partially started vignette */
  while ( vobj ) {
    LISTOBJECT *lobj = vobj->vd_lobj;
    VIGNETTEARGS *tobj;
    if ( lobj )
      free_dl_object(lobj, page);
    if ( vobj->vd_hdlt )
      freeIdlomArgs(vobj->vd_hdlt);

    path_free_list(vobj->vd_origpath.firstpath, mm_pool_temp);
    dlc_release(page->dlc_context, &vobj->vdl_color);
    tobj = vobj;
    vobj = vobj->vd_next;
    mm_free( mm_pool_temp, (mm_addr_t)tobj, tobj->size);
  }
  vn_reset_detection(page);
  return FALSE;
}


/**
 * This routine is used to finalize if what we've collected together should
 * really be considered as a vignette or not. Amongst other things it looks
 * at the style of the collected objects, along with how many there are and
 * also tries to eliminate color bars (unclipped rectangles).
 */
static int32 vn_addasvignette(void)
{
  int32 countfill;

  /* We add a vignette object if:
   *  a) At least two sub-elements,
   *     match of {VDS_Adjacent, VDS_StrongContained}.
   *  b) At least six sub-elements,
   *     match of {VDS_Adjacent, VDS_Contained, VDS_StrongContained}.
   */
  if ( vd_forcesplit )
    return 0;

  countfill = vd_countfill - ( vd_rolledrect != VDR_Unknown ? 1 : 0 );
  if ( countfill >= UserParams.VignetteMinFills ) {
    /* All elements in a vignette must be the same style, apart from a
     * VDS_StrongContained which can degenerate into a VDS_Contained.
     * Therefore only need to check the tail to find overall style.
     */
    if ( !( vd_tailobj->vd_style == VDS_Adjacent ||
             vd_tailobj->vd_style == VDS_Contained ||
             vd_tailobj->vd_style == VDS_StrongContained ))
      return 0;

    /* If the head and tail elements are the same color (and therefore
       all the intermediate elements as well) then it is not much of a
       vignette and more likely to be a color bar. Vignettes like this
       can happen for recombine (eg, KOs) but in that case we have to
       maintain it as a vignette in case it should merge with another */
    if ( !rcbn_enabled() && dl_equal(vd_tailobj->vd_lobj->p_ncolor,
                                      vd_headobj->vd_lobj->p_ncolor))
      return 0;
  }
  else {
    HQASSERT(countfill < UserParams.VignetteMinFills,
             "should be < UserParams.VignetteMinFills");
    if ( countfill <= 1 )
      return 0;
    else {
      HQASSERT(countfill >= 2, "should be >= 2");
      if ( !( vd_tailobj->vd_style == VDS_Adjacent ||
               vd_tailobj->vd_style == VDS_StrongContained ))
        return 0;

      /* We only use VignetteMinFills to determine a vignette or otherwise if:
       * a) The (potential) vignette contains device aligned rectangles.
       * and
       * b) There is no complex clipping for the (potential) vignette.
       * This copes (fairly) well with the case of color bars and the
       * case where a user puts two rectangles side by side to obtain a
       * special effect.  Note that if any parts of the vignette are
       * clipped out then it's unlikely to be either color bars or a
       * users special effects, so we use that too.
       */
      if ( !rcbn_enabled()) {
        if ( vd_countfill == vd_countlobj &&
             vd_tailobj->vd_type[0] == VDT_RectangleDevice ) {
          CLIPOBJECT *complex = vd_objectstate[0]->clipstate;
          HQASSERT(complex, "should always be a CLIPOBJECT");
          if ( complex->ncomplex == 0 )
            return 0;
        }
      }

      /* be a bit stricter about 2 element vignettes */
      if ( countfill == 2) {
        int32 worry = 0;
        CLIPOBJECT *complex = vd_objectstate[0]->clipstate;

        HQASSERT(complex, "should always be a CLIPOBJECT");

        /* are the elements the same color? */
        if ( dl_equal(vd_tailobj->vd_lobj->p_ncolor,
                     vd_headobj->vd_lobj->p_ncolor) )
          worry++;

        /* clip should be complex or at least nono-rectangluar */
        if ( complex->ncomplex == 0 ) {
          worry+=2;
          if ( vd_tailobj->vd_type[0] == VDT_RectangleDevice )
            worry++;
        }

        /* They should be fills */
        if ( (vd_headobj->vd_lobj->opcode != RENDER_fill) ||
             (vd_tailobj->vd_lobj->opcode != RENDER_fill) )
          worry++;
        /* worry is a lack of confidency stat. May put a higher threshold
         * on this */
        if ( worry > 1 )
          return 0;
      }
    }
  }
  return vd_countlobj;
}

/**
 * This routine is used to determine the confidence we have about a given
 * vignette. See comments in routine vn_addasvignette relating to this.
 */
static uint8 vn_determineconfidence( VIGNETTEARGS *tailobj )
{
  int32 countfill = vd_countfill - ( vd_rolledrect != VDR_Unknown ? 1 : 0 );
  if ( countfill < UserParams.VignetteMinFills &&
       vd_countfill == vd_countlobj &&
       tailobj->vd_type[0] == VDT_RectangleDevice ) {
    CLIPOBJECT *complex = vd_objectstate[0]->clipstate;
    HQASSERT(complex, "should always be a CLIPOBJECT");
    if ( complex->ncomplex == 0 )
      return VDC_Low;
  }
  return VDC_High;
}

/**
 * This routine is used to update the trap information for recombine to do
 * with fuzzy matches. For now we deal with simple strokes and fills
 * (rects & ovals).
 * Also used for merging donut vignettes and knockouts for recombine.
 */
static Bool vn_update_rcbtrap(mm_pool_t *pools, VIGNETTEARGS *vobj,
                              LISTOBJECT *lobj )
{
  Bool result = TRUE;

  HQASSERT(vobj != NULL, "vobj NULL");
  HQASSERT(lobj != NULL, "lobj NULL");
  HQASSERT(vobj->vd_numpaths == 1 || vobj->vd_numpaths == 2,
            "vn_update_rcbtrap: expected numpaths to be 1 or 2" );

  if ( lobj->opcode == RENDER_fill ) {
    if ( vobj->vd_numpaths == 1 &&
         vobj->vd_pathtype == ISSTRK ) {
      if ( vobj->vd_type[0] == VDT_Unknown )
        ( void )analyze_1stpath_s( vobj );
      if ( vobj->vd_type[0] == VDT_Line ) {
        PATHINFO *path;
        PATHINFO spath;
        path = &spath;
        if ( getstrokeoutline( path, vobj->vd_sparams )) {
          int32 degenerate, orient, type;
          if ( pathisarectangle( path, &degenerate, &orient, &type,
                                 &vobj->vd_rcbtrap )) {
            NFILLOBJECT *nfill = lobj->dldata.nfill;
            vobj->vd_rcbtrap.type = RCBTRAP_LINE;
            if ( !rcbt_addtrap(pools, nfill, &vobj->vd_path[0], FALSE /* fDonut */,
                               &vobj->vd_rcbtrap) )
              result = FALSE;
            HQASSERT(nfill->rcbtrap != NULL, "Somehow didn't generate trap");
          }
          path_free_list( path->firstpath, mm_pool_temp);
        }
      }
    }
    else {
      NFILLOBJECT *nfill = lobj->dldata.nfill;
      if ( !rcbt_addtrap(pools, nfill, &vobj->vd_path[0],
                         (vobj->vd_numpaths == 2) /* fDonut */,
                         &vobj->vd_rcbtrap ))
        result = FALSE;
    }
  }
  return result;
}

/**
 * This routine is used to produce a consistent set of colorants from all the
 * given dl colors in the vignette. Unification of said colorants in said
 * vignette is done at a later stage.
 */
static Bool vn_accumulatecolorants(DL_STATE *page,
                                   VIGNETTEARGS *headobj,
                                   dl_color_t *pdlc)
{
  VIGNETTEARGS *mergeobj;

  HQASSERT(headobj != NULL, "headobj NULL");
  HQASSERT(pdlc != NULL, "pdlc NULL");

  for ( mergeobj = headobj; mergeobj != NULL; mergeobj = mergeobj->vd_next ) {
    if ( mergeobj->vd_lobj != NULL ) {
      if ( !dlc_copy(page->dlc_context, pdlc, &mergeobj->vdl_color) )
        return FALSE;
      mergeobj = mergeobj->vd_next;
      break;
    }
  }
  for (; mergeobj != NULL; mergeobj = mergeobj->vd_next ) {
    if ( mergeobj->vd_lobj != NULL ) {
      if ( !dlc_merge_overprints(page->dlc_context, pdlc,
                                 &mergeobj->vdl_color) )
        return FALSE;
    }
  }
  return TRUE;
}

/**
 * This routine is used to reduce the colorant set in a vignettes colors
 * according to the consistent set of colorants across all vignette
 * sub objects.
 */
static Bool vn_reducecolorants(DL_STATE*page, VIGNETTEARGS *vobj,
                               LISTOBJECT *lobj)
{
  HQASSERT(vobj != NULL, "vobj NULL");
  HQASSERT(lobj != NULL, "lobj NULL");

  /* Having computed vd_color to have overprint bits according to the
   * intersection of the overprints of all the colors in the vignette, make
   * this color use only those overprints by intersecting that minimal
   * subset with this color's overprints; this is cheap, it only cancels bits.
   */
  if ( !dlc_merge_overprints(page->dlc_context, &vobj->vdl_color,
                             &vd_dlc_overprints) )
    return FALSE;

  /* If appropriate, we now apply the overprint bits to reduce the
   * colorants present in the object (which will be the same for all the
   * objects in the vignette, by virtue of the merge above). It is not
   * appropriate to do this if we apply overprints by the "maxblt"
   * technique, but the function can work that out for itself.
   */
  if ( !dlc_reduce_overprints(page->dlc_context, &vobj->vdl_color) )
    return FALSE;

  /* We aren't interested in the single color computed for the object any
   * more, only its color as part of the vignette, so release the plain
   * color.
   */
  dl_release(page->dlc_context, &lobj->p_ncolor);
  dlc_to_lobj_release(lobj, &vobj->vdl_color);
  return TRUE;
}

/**
 * The vignette's dl color is derived from all of the subobjects' dl colors.
 * A vignette dl color component has a value of COLORVALUE_HALF where the
 * component varies through the subobjects; if a component has a common value
 * through all the subobjects then this value is stored in the vignette dl
 * color (eg, a knockout).
 */
static Bool vn_makevignettecolor(DL_STATE *page, LISTOBJECT* lobj)
{
  dl_color_t dlc_vignette, dlc_loop;
  dl_color_iter_t iterator;
  dlc_iter_result_t more;
  COLORANTINDEX ci;
  COLORVALUE cv;

  /* dlc_loop is used just to iterate over the colorants.  dlc_vignette is the
     dl color we're building for the vignette lobj and may get reallocated by
     dlc_replace_indexed_colorant. */
  dlc_from_dl_weak(lobj->p_ncolor, &dlc_loop);
  if ( !dlc_from_dl(page->dlc_context, &lobj->p_ncolor, &dlc_vignette) )
    return FALSE;

  for (more = dlc_first_colorant(&dlc_loop, &iterator, &ci, &cv);
       more == DLC_ITER_COLORANT;
       more = dlc_next_colorant(&dlc_loop, &iterator, &ci, &cv)) {
    DLREF *subdl;

    for (subdl = vig_dlhead(lobj); subdl; subdl = dlref_next(subdl) ) {
      dl_color_t dlc_sub;
      COLORVALUE cv_sub;

      dlc_from_dl_weak(dlref_lobj(subdl)->p_ncolor, &dlc_sub);

      if ( !dlc_set_indexed_colorant(&dlc_sub, ci) ||
           !dlc_get_indexed_colorant(&dlc_sub, ci, &cv_sub) ||
          cv != cv_sub) {
        /* This color component varies through the vignette subobjects. */
        if ( !dlc_replace_indexed_colorant(page->dlc_context, &dlc_vignette,
                                           ci, COLORVALUE_HALF)) {
          dlc_release(page->dlc_context, &dlc_vignette);
          return FALSE;
        }
        break;
      }
    }
  }

  /* Assign the new color to the vignette lobj. */
  dl_release(page->dlc_context, &lobj->p_ncolor);
  dlc_to_dl_weak(&lobj->p_ncolor, &dlc_vignette);

  return TRUE;
}

static struct {
  LISTOBJECT *lobj;
  Bool (*func)(DL_STATE *page, LISTOBJECT *lobj);
  uint32 depth;
} vig_hdl;

static int32 addobjecttovigdl(DL_STATE *page, LISTOBJECT *lobj)
{
  HDL *hdl;

  UNUSED_PARAM(DL_STATE*, page);
  HQASSERT(vig_hdl.lobj, "No saved vig listobject");
  hdl = vig_hdl.lobj->dldata.vignette->vhdl;
  HQASSERT(hdl, "No HDL in saved vig listobject");

  if ( !hdlAdd(hdl, lobj) )
    return DL_Error;

  return DL_Added;
}

/**
 * Allocate a DL vignette object and create and open the associated HDL
 */
Bool vn_alloc_vignette(DL_STATE *page, LISTOBJECT *lobj, int32 count)
{
  VIGNETTEOBJECT *vig;

  HQASSERT(lobj->opcode == RENDER_vignette, "unsuitable listobject");
  lobj->dldata.vignette = NULL;
  HQASSERT(count <= 258, "Limited to 256+2 elements in a vignette");
  vig = (VIGNETTEOBJECT *)dl_alloc(page->dlpools, sizeof(VIGNETTEOBJECT),
                                   MM_ALLOC_CLASS_VIGNETTEOBJECT);
  if ( vig == NULL )
    return error_handler(VMERROR);

  HqMemZero(vig, sizeof(VIGNETTEOBJECT));
  vig->vcount = count;
  vig->vhdl = NULL;
  lobj->dldata.vignette = vig;

  if ( !hdlOpen(page, FALSE, HDL_VIGNETTE, &lobj->dldata.vignette->vhdl) )
    return FALSE;

  vig_hdl.lobj = lobj;
  vig_hdl.func = device_current_addtodl;
  HQASSERT(vig_hdl.depth == 0," Gone recursive with vignette DL addition");
  vig_hdl.depth++;
  device_current_addtodl = addobjecttovigdl;

  return TRUE;
}

/**
 * Finished with current vignette so close associated hdl.
 * Called in clean up when vignette may have not yet been started, so need
 * to ensure it is a no-op in such cases.
 */
Bool vn_complete_vignette(void)
{
  if ( vig_hdl.lobj ) { /* nothing to do if no open vignette */
    HDL **hdlptr;
    device_current_addtodl = vig_hdl.func;
    hdlptr = &vig_hdl.lobj->dldata.vignette->vhdl;
    vig_hdl.lobj = NULL;
    vig_hdl.func = NULL;
    vig_hdl.depth--;
    HQASSERT(vig_hdl.depth == 0," Gone recursive with vignette DL addition");
    return hdlClose(hdlptr, TRUE);
  }
  else
    return TRUE;
}

/**
 * Insert a white object into the head or tail of the vignette HDL
 * and return the nearest neighbour (one before or one after inserted item)
 */
LISTOBJECT *vn_white_insert_vignette(DL_STATE *page,
                                     LISTOBJECT *vig_lobj, Bool atHead,
                                     LISTOBJECT *white_lobj)
{
  VIGNETTEOBJECT *vigobj;
  DLREF *dlobj, *nbour;
  HDL *hdl = vig_lobj->dldata.vignette->vhdl;

  HQASSERT(vig_lobj != NULL, "Vignette listobject null");
  vigobj = vig_lobj->dldata.vignette;
  HQASSERT(vigobj != NULL, "vigobj null");

  /* Need a hdl-insert type function. One does not exist, so roll our
   * own for now.
   */
  if ( (dlobj = alloc_n_dlrefs(1, page)) == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  dlref_assign(dlobj, white_lobj);
  vigobj->vcount++;
  if ( atHead ) {
    DLREF **bands = hdlBands(hdl);
    uint32 orderi = hdlExtentOnParent(hdl).length;

    nbour = bands[orderi];
    dlref_setnext(dlobj, nbour);
    bands[orderi] = dlobj;
  } else {
    nbour = vig_dlhead(vig_lobj);
    while ( dlref_next(nbour) )
      nbour = dlref_next(nbour);
    dlref_setnext(nbour, dlobj);
  }
  return dlref_lobj(nbour);
}


/**
 * 1. Check if the vignette is really a colorbar;
 * 2. Check if the single fill matches a vignette already on the dl
 *    indicating that vignette is really a colorbar;
 * 3. Check if the single fill is part of a knocked out white end of
 *    an overprinted vignette.
 */
static Bool vn_check_colorbar_or_whiteend( DL_STATE *page,
                                           VIGNETTEARGS *vargs,
                                           VIGNETTEOBJECT *vobj,
                                           LISTOBJECT *lobj)
{
  Bool fCallSplitter = FALSE;

  if ( vobj != NULL ) {
    /* Only consider adjacent vignettes for colorbars and KO whiteends.
     * A vignette with more than nMinElements probably is a vignette
     */
    int32 nMinElements;
#define COLORBAR_MIN_FACTOR 4
    nMinElements = ( COLORBAR_MIN_FACTOR *
                     UserParams.VignetteMinFills );

    fCallSplitter = ( vobj->style == VDS_Adjacent &&
                      vd_countfill < nMinElements );
  } else if ( vargs->vd_numpaths == 1 ) {
    /* Probably hit a showpage before this object
     * could be analysed so do it now */
    if ( vargs->vd_type[0] == VDT_Unknown ) {
      if ( vargs->vd_sparams ) {
        if ( !analyze_1stpath_s( vargs ))
          return TRUE; /* not a recognisable shape; not an error */
      } else {
        if ( !analyze_1stpath_f( vargs ))
          return TRUE; /* not a recognisable shape; not an error */
      }
    }
    fCallSplitter = ( vargs->vd_type[0] == VDT_RectangleUser ||
                      vargs->vd_type[0] == VDT_RectangleDevice );
  }
  if ( fCallSplitter )
    return merge_dl_objects(page, lobj, compare_vignette_splitter,
                            MERGE_SPLIT | MERGE_FUZZY) != MERGE_ERROR;
  return TRUE;
}

/**
 * For recombine to merge two vignetteobjects, the merging of the sub-objects
 * is done in rcbvmerg
 */
void vn_merge_vignetteobject(LISTOBJECT *lobj_merge, LISTOBJECT *lobj_old,
                             LISTOBJECT *lobj_new)
{
  vn_outlines_t blank = { 0 };
  VIGNETTEOBJECT *vigobj_merge = lobj_merge->dldata.vignette;
  VIGNETTEOBJECT *vigobj_old =  lobj_old->dldata.vignette;
  VIGNETTEOBJECT *vigobj_new =  lobj_new->dldata.vignette;

  HQASSERT(vigobj_old->style == VDS_StrongContained,
           "can only deal with strong contained vignette at the moment");

  bbox_union(&lobj_old->bbox, &lobj_new->bbox, &lobj_merge->bbox);

  if ( vigobj_old->style == VDS_StrongContained ) {
    if ( vigobj_old->white.t ) {
      vn_outlines_t temp;
      /* Extended old at the tail so the merged outline is the same as
       * vigobj_old */
      HQASSERT(vigobj_old->outlines.nfillh == NULL,
               "nfillh != null");
      HQASSERT(!vigobj_old->white.h, "vigobj_old->white.h true" );
      /* Move all but nfillt outline and flag, otherwise the free_dl_vignette
         will think a white_lobj extend has been done */
      vigobj_merge->outlines = temp = vigobj_old->outlines;
      vigobj_merge->outlines.nfillt = NULL;
      vigobj_merge->outlines.freenfillt = FALSE;
      vigobj_old->outlines = blank;
      vigobj_old->outlines.nfillt = temp.nfillt;
      vigobj_old->outlines.freenfillt = temp.freenfillt;

#if defined( DEBUG_BUILD )
      vigobj_merge->outlines.outline_lobj = vigobj_old->outlines.outline_lobj;
#endif
    }
    else {
      vn_outlines_t temp;
      HQASSERT(vigobj_new->white.t, "vigobj_new->white.t is false");
      HQASSERT(!vigobj_new->white.h, "vigobj_new->white.h is true");
      /* Extended new at the tail so the merged outline is the same as
       * vigobj_new */
      HQASSERT(vigobj_new->outlines.nfillh == NULL, "nfillh != null");
      /* Move all but nfillt outline and flag, otherwise the free_dl_vignette
         will think a white_lobj extend has been done */
      vigobj_merge->outlines = temp = vigobj_new->outlines;
      vigobj_merge->outlines.nfillt = NULL;
      vigobj_merge->outlines.freenfillt = FALSE;
      vigobj_new->outlines = blank;
      vigobj_new->outlines.nfillt = temp.nfillt;
      vigobj_new->outlines.freenfillt = temp.freenfillt;

#if defined( DEBUG_BUILD )
      vigobj_merge->outlines.outline_lobj = vigobj_new->outlines.outline_lobj;
#endif
    }
  }
  else {
    /* @@@ NYI */
    HQFAIL("NYI creating an outline for dynamically extended vignette");
    vigobj_merge->outlines = blank;

#if defined( DEBUG_BUILD )
    vigobj_merge->outlines.outline_lobj = NULL;
#endif
  }
  vigobj_merge->compareinfo = NULL;
  vigobj_merge->partialcolors = 0;
  vigobj_merge->style = vigobj_old->style;
  vigobj_merge->confidence = vigobj_old->confidence;
  vigobj_merge->rolledrect = vigobj_old->rolledrect;
  vigobj_merge->recurse = (vigobj_old->recurse | vigobj_new->recurse);
  vigobj_merge->rollover = FALSE;
  vigobj_merge->colormonotonic = vigobj_old->colormonotonic;
  /* Should never have to extend this vignette,
     otherwise sequence points of vignette are probably wrong */
  vigobj_merge->white.h = FALSE;
  vigobj_merge->white.t = FALSE;
  vigobj_merge->white.lobj = NULL;
  vigobj_merge->white.used = FALSE;
}

#if defined( DEBUG_BUILD )

static void print_simple_color( p_ncolor_t pNColor )
{
  dl_color_t        dlc;
  dl_color_iter_t   dlci;
  dlc_iter_result_t iter_res;
  COLORANTINDEX     ci;
  COLORVALUE        cv;

  dlc_from_dl_weak(pNColor, &dlc);

  iter_res = dlc_first_colorant(&dlc, &dlci, &ci, &cv);
  while ( iter_res == DLC_ITER_COLORANT ) {
    monitorf((uint8*)" %d: %X; ",ci,cv);
    iter_res = dlc_next_colorant(&dlc, &dlci, &ci, &cv);
  }
}

static int32 fMaxPrint = 1; /* print only nShow x 2 elements */
static int32 nShow = 10;
void debug_print_vignette( LISTOBJECT * pVLobj )
{
  if ( pVLobj->opcode == RENDER_vignette ) {
    int32 i;
    DLREF *pLink;
    VIGNETTEOBJECT *vigobj;

    vigobj = pVLobj->dldata.vignette;

    monitorf((uint8*)"Start of vignette of %d elements (0x%X)\n",
             vig_len(vigobj), pVLobj);

    pLink = vig_dlhead(pVLobj);

    for ( i = 0; i < vd_countlobj; ++i, pLink = dlref_next(pLink) ) {
      LISTOBJECT *pELobj = dlref_lobj(pLink);

      /* Only print nShow x 2 elements at most (nShow first and nShow last) */
      if ( !fMaxPrint || i < nShow || (vd_countlobj - i) <= nShow ) {
        monitorf((uint8*)"  bbox: %d %d %d %d; color: ",
                 pELobj->bbox.x1, pELobj->bbox.y1, pELobj->bbox.x2, pELobj->bbox.y2);
        print_simple_color(pELobj->p_ncolor);
        monitorf((uint8*)"\n");
      } else {
        monitorf((uint8*)"  ...\n");
        i = vd_countlobj - (nShow + 1);
      }
    }
    monitorf((uint8*)"End of vignette\n");
  }
}
#endif /* defined( DEBUG_BUILD ) */

/**
 * Add the given listobject to the currently open vignette chain.
 *
 * Note when recombine is on cannot just call add_listobject as this would
 * recombine merging on individual elements rather than wait and do it on
 * the entire chain.
 */
static Bool vig_hdladd(VIGNETTEOBJECT *vigobj, LISTOBJECT *lobj)
{
  return hdlAdd(vigobj->vhdl, lobj);
}

/**
 * Populate the given vignette object
 */
static Bool build_vignette(DL_STATE *page, VIGNETTEARGS *currobj,
                           int32 nvigobjs, LISTOBJECT *v_lobj)
{
#if defined( ASSERT_BUILD )
  Bool recombinemarker = FALSE;
#endif
  Bool addasseparate = (nvigobjs == 0);
  VIGNETTEOBJECT *vigobj = v_lobj->dldata.vignette;
  LISTOBJECT *lobj = NULL;
  LISTOBJECT *subdl_lobj;
  Bool fTagBytesMayHaveChanged = (nvigobjs != 0);
  dbbox_t bbox = { MAXDCOORD, MAXDCOORD, MINDCOORD, MINDCOORD };
  dbbox_t subdl_bbox;
  dbbox_t split_bbox;

  if ( vigobj != NULL )
    vigobj->recurse = FALSE;

  while ( currobj ) {
    VIGNETTEARGS *nextobj = currobj->vd_next;

    subdl_lobj = currobj->vd_lobj;

#if PS2_PDFOUT
    if ( pdfout_enabled() ) {
      COLORinfo dummy;
      COLORinfo *colinfo = &dummy;

      colinfo = build_colinfo_from_vinobj( currobj, colinfo );
      if ( currobj->vd_sparams != NULL ) {
        if ( !pdfout_dostroke( get_core_context_interp()->pdfout_h,
                                currobj->vd_sparams,
                                addasseparate ? GSC_FILL : GSC_VIGNETTE,
                                &vd_gsoverrides ))
          return abort_vignette_chain(page, currobj);
      }
      else {
        if ( !pdfout_dofill( get_core_context_interp()->pdfout_h,
                              &currobj->vd_origpath,
                              currobj->vd_filltype, 0,
                              addasseparate ? GSC_FILL : GSC_VIGNETTE,
                              &vd_gsoverrides ))
          return abort_vignette_chain(page, currobj);
      }
    }
#endif

    /* If we didn't add as vignette, do hdlt stuff. */
    if ( addasseparate && currobj->vd_hdlt && isHDLTEnabled(*gstateptr) ) {
      switch ( IDLOM_DO_LATCHED( currobj->vd_hdlt )) {
        case NAME_false: /* PS error in IDLOM callbacks */
          return abort_vignette_chain(page, currobj);
        case NAME_Discard: /* just pretending */
          /* Get rid of DL object so we effectively don't add to DL. */
          if ( subdl_lobj ) {
            free_dl_object(subdl_lobj, page);
            subdl_lobj = NULL;
          }
          break;
        default: /* only add, for now */
          break;
      }
    }

    /* If there is an object, then add it to the DL. When we're doing a
     * vignette, each time round this loop builds up one more part of
     * the vignette chain until we're on the last time round in which case
     * we add the vignette.
     */
    if ( subdl_lobj ) {
      if ( rcbn_enabled() &&
           !vn_update_rcbtrap(page->dlpools, currobj, subdl_lobj) )
        return abort_vignette_chain(page, currobj) ;

      /* Calculate and store some flags used by the vignette splitting code */
      subdl_lobj->marker |= MARKER_VN_VNCANDIDATE;
      if ( vn_iswhiteobject(currobj) )
        subdl_lobj->marker |= MARKER_VN_WHITEOBJECT;
      subdl_bbox = split_bbox = currobj->vd_lobj->bbox;

      if ( nvigobjs != 0 ) {
        /* Each element of the vignette must have the same
         * stateobject and clipping path; therefore uprate objects
         * not already using the final stateobject (the final
         * stateobject being the state with potentially an extra
         * clipprect).
         */
        subdl_lobj->objectstate = vd_objectstate[0];
        DISPOSITION_STORE(subdl_lobj->disposition, REPRO_TYPE_VIGNETTE,
                          GSC_VIGNETTE, vd_disposition_flags);

        /* May have a vignette with mixed presep/composite flags;
           Unify the recombine/composite flags through the vignette */
        if ( vd_gotcomposite )
          subdl_lobj->spflags &= ~RENDER_RECOMBINE;
        else
          subdl_lobj->spflags |= RENDER_RECOMBINE;

        /* If the knockout flags are inconsistent we'll have to render
           the vignette elements with the slower recurse method. */
        if ( (v_lobj->spflags & RENDER_KNOCKOUT)
             != (subdl_lobj->spflags & RENDER_KNOCKOUT) )
          vigobj->recurse = TRUE;

        if ( fTagBytesMayHaveChanged ) {
          int32 tag_bytes = TAG_BYTES( subdl_lobj->objectstate );
          if ( tag_bytes > 0 )
            HqMemCpy( (uint8 *) subdl_lobj - tag_bytes,
                      gstateptr->theGSTAGinfo.data + 1,
                      tag_bytes );
        }

        /* Fill in LISTOBJECT when we have the first one. */
        if ( nvigobjs == vd_countlobj ) {
          bbox = subdl_bbox;
          if ( !vigobj->white.used )
            bbox = vigobj->white.lobj->bbox;
          if ( !make_listobject_copy(page, subdl_lobj, &lobj) ) /* fill/rect->vig */
            return abort_vignette_chain(page,  currobj);

          /* Now overwrite some fields */
          lobj->opcode = RENDER_vignette;
          DISPOSITION_STORE(lobj->disposition, REPRO_TYPE_VIGNETTE,
                            GSC_VIGNETTE, 0);
#if defined( ASSERT_BUILD )
          recombinemarker = FALSE;
          if ( (subdl_lobj->spflags & RENDER_RECOMBINE) != 0 )
            recombinemarker = TRUE;
#endif
          lobj->dldata.vignette = vigobj;
          lobj->dldata.vignette->rollover = FALSE;
          if ( (subdl_lobj->spflags & RENDER_RECOMBINE) != 0 )
            lobj->attr.planes = NULL;
          else
            lobj->attr.rollover = DL_NO_ROLL;
          lobj->marker   = MARKER_VN_FIXKNOCKOUT;
          if ( !guc_backdropRasterStyle(gsc_getTargetRS(gstateptr->colorInfo)) )
            lobj->marker |= MARKER_DEVICECOLOR;
        }
        HQASSERT(((subdl_lobj->spflags & RENDER_RECOMBINE) != 0) ==
                recombinemarker, "recombine markers out of sync");

        /* Store bbox of current sub-object. */
        lobj->bbox = subdl_bbox;
        /* Enlarge bbox of vignette object. */
        bbox_union(&bbox, &subdl_bbox, &bbox);

        HQASSERT(!dlc_is_clear(&currobj->vdl_color), "vdl_color empty");

        if ( vd_gotcomposite ) {
          /* Use the vignette color calculated using the shfill chain instead
           * of the fill chain (see earlier comment). Must also unify the set
           * of colorants painted throughout the vignette.
           */
          if ( !vn_reducecolorants(page, currobj, subdl_lobj) )
            return abort_vignette_chain(page,  currobj);
        }

        if ( nvigobjs == vd_countlobj ) {
          /* Setup the top level vignette dl object with the first
           * subobjects color and spflags.
           */
          dl_release(page->dlc_context, &lobj->p_ncolor);
          if ( !dl_copy(page->dlc_context, &lobj->p_ncolor,
                        &subdl_lobj->p_ncolor) )
            return abort_vignette_chain(page, currobj);
          lobj->spflags = subdl_lobj->spflags;
        }

        /* Link next vignette item to chain. */
        if ( !vig_hdladd(vigobj, subdl_lobj) )
          return FALSE;

        --nvigobjs;
        if ( nvigobjs == 0 ) {
          lobj->bbox = bbox;

          /* Sneaky add of vignette listobject. */
          subdl_lobj = lobj;
          subdl_bbox = split_bbox = bbox;
          bbox_intersection(&bbox, &(lobj->objectstate->clipstate->bounds),
                            &bbox);
          lobj->bbox = bbox;

          /* The vignette has been built and overprint unification has been
             done.  Derive a new dl color for the vignette lobj. */
          if ( !vn_makevignettecolor(page, lobj) )
            return abort_vignette_chain(page, currobj);
#if defined( DEBUG_BUILD )
          if ( vigobj->outlines.outline_lobj ) {
            /* Enlarge bbox of vignette object with red outline bbox. */
            bbox_union(&subdl_bbox, &vigobj->outlines.outline_lobj->bbox,
                       &subdl_bbox);
            /* Update bbox since it's got larger. */
            lobj->bbox = subdl_bbox;
          }
#endif
          if ( !vn_complete_vignette() )
            return FALSE;
        }
      }
      if ( nvigobjs == 0 ) {
        Bool added;

#if defined( DEBUG_BUILD )
        /* Enumerates the vignette elements, giving bbox and color info.
           This is useful for debugging recombine merging of vignettes */
        if ( debug_vr )
          debug_print_vignette(subdl_lobj);
#endif
        if ( !add_listobject(page, subdl_lobj, &added) )
          return abort_vignette_chain(page, currobj);
        if ( added ) {
          if ( rcbn_enabled() && rcbn_merge_required(subdl_lobj->opcode) &&
               ( subdl_lobj->spflags & RENDER_RECOMBINE ) != 0 ) {
            /* Consider colorbars and knocked out white ends */
            if ( !vn_check_colorbar_or_whiteend(page, currobj, vigobj,
                                                subdl_lobj) ) {
              /* Must nuke the DL object since added to the DL. */
              currobj->vd_lobj = NULL;
              return abort_vignette_chain(page, currobj);
            }
          }
        }
      }
    }

    /* Get hdlt to free it's bits. */
    if ( currobj->vd_hdlt )
      freeIdlomArgs( currobj->vd_hdlt );

    path_free_list(currobj->vd_origpath.firstpath, mm_pool_temp);
    dlc_release(page->dlc_context, &currobj->vdl_color);
    mm_free( mm_pool_temp, ( mm_addr_t )currobj ,currobj->size);
    currobj = nextobj;
  }

  /* clip the vignette objects to cope with maxblt problems if necessary (for
     recombine, this is done after we have recombined everything and done color
     management */

  if ( vd_gotcomposite && lobj != NULL )
    if ( !vn_maxblt_clipping(vigobj, lobj))
      return FALSE;

  return TRUE;
}

/**
 * This routine is only called from one place, and the caller encapsulates it
 * in vd_flushing increment and decrement to prevent recursive vignette
 * detection.
 *
 * \todo BMJ 28-Oct-08 :  Try and simplify this function and its children
 */
static Bool flush_vignette_chain(DL_STATE *page)
{
  Bool neutral = TRUE;
  VIGNETTEARGS *currobj, *headobj, *tailobj;
  int32 nvigobjs = vn_addasvignette();
  LISTOBJECT v_lobj; /* local dl object in which vignette is built-up */

  HQASSERT(vd_lobj == NULL, "vd_lobj should have been cleared");
  HQASSERT(vd_headobj, "vd_headobj is null");
  HQASSERT(vd_objectstate[0], "vignette objectstate not set up");

  /* Reset global state used for tracking vignettes; here we merely need
   * to set the head/tail pointers so that analyzing_vignette returns FALSE.
   */
  currobj = vd_headobj;
  headobj = vd_headobj;
  tailobj = vd_tailobj;
  vd_headobj = NULL;
  vd_tailobj = NULL;
  init_listobject(&v_lobj, RENDER_vignette, NULL);

  /* Switch device back to what it was at the time of the vignette. */
  vd_saveddevice = CURRENT_DEVICE();
  HQASSERT(vd_device >= DEVICE_MIN && vd_device <= DEVICE_MAX,
            "device out of range");
  SET_DEVICE(vd_device);

  if ( pdfout_enabled() ) {
    if ( !pdfout_vignetteend(get_core_context_interp()->pdfout_h, nvigobjs != 0))
      return abort_vignette_chain(page, headobj);
  }

  /* Having decided if it's a vignette, do the hdlt stuff...
     Don't know why we use GSC_UNDEFINED colour chain. */
  if ( nvigobjs != 0 ) {
    VIGNETTEOBJECT *vigobj;

    if ( isHDLTEnabled(*gstateptr) ) {
      int8 kindVC  = map_vignette_kind(tailobj);
      int8 curveVC = map_vignette_curve(vd_rolledrect != VDR_Unknown ?
                                        headobj->vd_next : headobj);
      switch ( IDLOM_VIGNETTE( GSC_UNDEFINED, kindVC, curveVC,
                               tailobj->vd_hdlt, &vd_clippath )) {
        case NAME_false: /* PS error in IDLOM callbacks */
          return abort_vignette_chain(page, headobj);
        case NAME_Discard:
          (void)abort_vignette_chain(page, headobj);
          return TRUE;
        default: /* add */
          break;
      }
    }

    if ( !vn_alloc_vignette(page, &v_lobj, vd_countlobj) )
      return abort_vignette_chain(page, headobj);

    /* Purely used with recombine for vignette splitting. */
    vigobj = v_lobj.dldata.vignette;
    vigobj->style = ( uint8 )headobj->vd_style;
    vigobj->confidence = vn_determineconfidence( tailobj );
    vigobj->colormonotonic = vd_colormonotonic[0];
    vigobj->partialcolors = NULL;
    /* Pointer to info used for recombine compare and merging only */
    vigobj->compareinfo = NULL;

    /* Build the various outlines we need for recombine matching et al. */
    if ( !vn_buildoutlines(page, vigobj, headobj, tailobj) )
      return abort_vignette_chain(page,  headobj);
  }
#if defined( ASSERT_BUILD )
  if ( trace_va && ( nvigobjs != 0 ) )
    monitorf(( uint8 * )"flush" );
#endif
  dlc_clear(&vd_dlc_overprints);
  if ( vd_gotcomposite ) {
    if ( !vn_accumulatecolorants(page, headobj, &vd_dlc_overprints) )
      return abort_vignette_chain(page, headobj);
  }

  if ( nvigobjs != 0 ) {
    int32 n;

    for (n = vd_ncolormonotonic-1; n >= 0; n--) {
      if ( vd_colormonotonic[n] != VDC_Neutral ) {
        neutral = FALSE;
        break;
      }
    }

    if ( !neutral )
      if ( !runHooks(&gstateptr->thePDEVinfo.pagedevicedict,
                     GENHOOK_StartVignette) )
        return abort_vignette_chain(page, headobj);

    /* this may have changed the gstate tags structure and hence the
       object state - so create a new one if need be */

    if (vd_objectstate[0]->gstagstructure != gstateptr->theGSTAGinfo.structure) {
      STATEOBJECT newstate, *stateptr;

      newstate = *(vd_objectstate[0]);
      newstate.gstagstructure = gstateptr->theGSTAGinfo.structure;

      stateptr = (STATEOBJECT *)dlSSInsert(page->stores.state,
                                           &newstate.storeEntry, TRUE);
      if ( stateptr == NULL ) {
        (void)abort_vignette_chain(page, headobj);
        return error_handler(VMERROR);
      }
      vd_objectstate[0] = stateptr;
    }
  }
  if ( !build_vignette(page, currobj, nvigobjs, &v_lobj) )
    return FALSE;

  if ( nvigobjs != 0 ) {
    if ( !neutral )
      if ( !runHooks(&gstateptr->thePDEVinfo.pagedevicedict,
                     GENHOOK_EndVignette))
        return FALSE;
  }

#if defined( ASSERT_BUILD )
  if ( trace_va && (nvigobjs != 0) )
    monitorf(( uint8 * )"%svignette of %d (%d) sub-elements\n",
      currobj->vd_sparams ? "stroked-" : "", vd_countfill, vd_countlobj);
#endif
  vn_reset_detection(page); /* Reset (global) state used for tracking vignettes. */
  return TRUE;
}

/**
 * Reverse the order of rendering the objects in the vignette and clear
 * rollovers at the same time.
 *
 * NOTE: adjacent (linear) vignettes also suffer from the same
 * problem, though to a much lesser extent, because each rectangle
 * overlaps the next along their common edge. This means that if two
 * of the colorants run in opposite directions, the common edge will
 * be a bit darker than it should be, since the color in the overlap
 * will be made up of the darker tints of each colorant, according
 * to maxblt rules. However, we think we can ignore this case for
 * now, hence the exception above. It should be sufficient to remove
 * that exception, at the cost of a decrease in efficiency and
 * memory usage, if it does turn out to matter.
 */
static void reverse_vig_clear_rollovers(LISTOBJECT *lobj)
{
  LISTOBJECT *lobjs[256+2]; /* maximum lobjs in a vigenette */
  DLREF *dl;
  int32 i, maxi;

  for ( maxi = 0, dl = vig_dlhead(lobj); dl != NULL; dl = dlref_next(dl) )
    maxi++;
  HQASSERT(maxi <= NUM_ARRAY_ITEMS(lobjs), "Too many vignette objs");
  for ( i = 0, dl = vig_dlhead(lobj); dl != NULL; dl = dlref_next(dl), i++ ) {
    lobjs[i] = dlref_lobj(dl);
    lobjs[i]->attr.rollover = DL_NO_ROLL; /* kill any rollovers */
  }
  for ( i = 0, dl = vig_dlhead(lobj); dl != NULL; dl = dlref_next(dl), i++ )
    dlref_assign(dl, lobjs[maxi - 1 - i]);
}

/**
 * If a vignette is overprinted and color managed, it will use maxblts
 * to render the vignette. This means that dark colors from the
 * background show through lighter colors in the overprinted vignette,
 * which is a pragmatic solution to dealing with the small amounts of
 * colorant generated by color management in otherwise overprinted
 * channels of a vignette.
 *
 * However, this breaks the opaque painting model, and introduces an
 * unwanted side effect for contained vignettes (including radial and
 * cylindrical vignettes) where the outer (first drawn) elements of
 * the vignette can end up with darker colors than the central later
 * drawn elements and these components then show through.
 *
 * This function
 *   - tests whether this is going to happen,
 *   - reverses the order of vignette elements if it does
 *     (to make the next step more efficient)
 *   - uses inverse clipping on each with respect of its neighbours so that
 *     the rearmost elements aren't drawn in areas where they would cause a
 *     problem.
 */
static Bool vn_maxblt_clipping(VIGNETTEOBJECT *pVignette, LISTOBJECT *lobj)
{
  dl_color_t dlc;

  HQASSERT(pVignette != NULL, "NULL Vignette");
  HQASSERT(lobj != NULL, "NULL LISTOBJECT");

  if ( lobj->p_ncolor == NULL ) {
    HQFAIL("null color pointer - why? (safe to ignore)");
    return TRUE;
  }

  dlc_from_dl_weak(lobj->p_ncolor, &dlc);
  if ( !dlc_doing_maxblt_overprints(&dlc) )
    return TRUE;

  if ( pVignette->style != VDS_Spaced && pVignette->style != VDS_Adjacent ) {
    HQASSERT(pVignette->style == VDS_StrongContained ||
             pVignette->style == VDS_Contained ||
             pVignette->style == VDS_Overlapped,
             "unexpected vignette style");

    /*
     * Reverse the vignette sub-dl.
     *
     * Note : Have added vignette to main DL and calculated rollovers at
     * that time. Now we are modifying the DL and invalidating the conditions
     * under which the original rollover calculations were done. So really need
     * to re-do the rollover calculations. But in fact we are also about to
     * alter the clipping on each object in the vignette as well. Having
     * different clipping on each object will mean rollovers are not possible.
     * So the easiest thing to do is just turn off rollovers for this entire
     * vignette chain. Do this at the same time as we do the reversal for
     * ease of processing.
     */
    reverse_vig_clear_rollovers(lobj);

    /* Now process the list to set up clips for each object in turn with
       respect to the next and all its successors (formerly predecessors) */
    if ( !setup_vignette_clipping(CoreContext.page, vig_dlhead(lobj)) )
      return FALSE;

    /* tell rendering we need to do clipping for each element independently: */
    lobj->dldata.vignette->recurse = TRUE;
  }
  return TRUE;
}

static Bool is_path_inside_extra_cliprect( VIGNETTEARGS *currobj )
{
  int32 i;

  HQASSERT(currobj, "currobj NULL");

  if ( vd_cliptype == VDT_RectangleDevice ) {
    /* Rectangle is device aligned, just use the bbox. */
    sbbox_t *clipbbox = &vd_clippathinfo.bbox;
    HQASSERT(vd_clippathinfo.bboxtype != BBOX_NOT_SET,
              "using vd_clippathinfo bbox but not set" );
    for ( i = 0; i < currobj->vd_numpaths; ++i ) {
      sbbox_t *pathbbox = &currobj->vd_path[i].bbox;
      HQASSERT((currobj->vd_path[i]).bboxtype != BBOX_NOT_SET,
                "using vd_path bbox but not set");
      if ( !bbox_contains_epsilon(clipbbox, pathbbox, PA_EPSILON, PA_EPSILON) )
        return FALSE;
    }
  }
  else {
    HQASSERT(vd_cliptype == VDT_RectangleUser, "type should rect user");
    /* Not device aligned, find the matrix inverse and the bbox is the unit
     * square. */
    if ( vd_initmatrix ) {
      SYSTEMVALUE length[2];
      /* Calculate the inverse matrix to map clip rect to the unit square. */
      get_path_matrix(&vd_clippathinfo, &vd_clipinvmatrix, length);
      vd_initmatrix = FALSE;
    }
    for ( i = 0; i < currobj->vd_numpaths; ++i ) {
      PATHLIST *path = currobj->vd_path[i].firstpath;
      while ( path ) {
        LINELIST *line = path->subpath;
        while ( line ) {
          SYSTEMVALUE x = line->point.x;
          SYSTEMVALUE y = line->point.y;
          SYSTEMVALUE tx, ty;
          /* Apply matrix to each point in the line. */
          MATRIX_TRANSFORM_XY( x, y, tx, ty, &vd_clipinvmatrix);
          /* Check it is within the unit square (the cliprect). */
          if ( tx < 0.0 - PA_EPSILON ||
               ty < 0.0 - PA_EPSILON ||
               tx > 1.0 + PA_EPSILON ||
               ty > 1.0 + PA_EPSILON )
            return FALSE;
          line = line->next;
        }
        path = path->next;
      }
    }
  }
  return TRUE;
}

/**
 * Return a pointer to the head of the given vignette DL
 */
DLREF *vig_dlhead(LISTOBJECT *lobj)
{
  VIGNETTEOBJECT *vigobj;
  DLREF *head;

  HQASSERT(lobj && lobj->dldata.vignette, "Lost DL Vignette data");
  vigobj = lobj->dldata.vignette;

  head = hdlOrderList(vigobj->vhdl);
  HQASSERT(head, "Lost DL Vignette hdl");
  return head;
}

/**
 * Return the object length of the given vignette
 */
int32 vig_len(VIGNETTEOBJECT *vigobj)
{
  int32 vc;

  HQASSERT(vigobj, "Lost DL Vignette data");
  vc = vigobj->vcount;
  return vc;
}

Bool vd_detect(void)
{
  corecontext_t *context = get_core_context_interp();
  return vd_flushing == 0
    && (context->userparams->VignetteDetect ||
        rcbn_intercepting() ||
        isTrappingActive(context->page)) ;
}

void init_C_globals_vndetect(void)
{
#if defined( ASSERT_BUILD )
  trace_vf = FALSE;
  trace_va = FALSE;
#endif
#if defined( DEBUG_BUILD )
  fMaxPrint = 1;
  nShow = 10;
  debug_vr = 0;
  debug_vo = FALSE;
  HqMemZero(&pathoutline, sizeof(pathoutline));
#endif
  vd_countfill = 0;
  vd_countlobj = 0;
  vd_forcesplit = FALSE;
  vd_device = DEVICE_ILLEGAL;
  vd_saveddevice = DEVICE_ILLEGAL;
  HqMemZero(&vd_objectstate, sizeof(vd_objectstate));
  HqMemZero(&vd_size, sizeof(vd_size));
  HqMemZero(&vd_disp, sizeof(vd_disp));
  vd_gotcomposite = FALSE;
  vd_ncolormonotonic = 0;
  vd_colormonotonic = NULL;
  vd_ncolorants = 0;
  vd_colorants = NULL;
  vd_colorvalues = NULL;
  vd_ncolorvalues = 0;
  vd_flushing = 0;
  HqMemZero(&vd_gsoverrides, sizeof(vd_gsoverrides));
  vd_headobj = NULL;
  vd_tailobj = NULL;
  vd_cause = VD_Default;
  HqMemZero(&vd_clippath, sizeof(vd_clippath));
  vd_cliptype = VDT_Unknown;
  HqMemZero(&vd_clippathinfo, sizeof(vd_clippathinfo));
  vd_initmatrix = FALSE;
  vd_rolledrect = VDR_Unknown;
  vd_flushed_lobj = FALSE;
  vd_lobj = NULL;
  HqMemZero(&vd_dlc_overprints, sizeof(vd_dlc_overprints));
  vd_disposition_flags = 0;
  vig_hdl.lobj = NULL;
  vig_hdl.func = NULL;
  vig_hdl.depth = 0;
}

/* Log stripped */
