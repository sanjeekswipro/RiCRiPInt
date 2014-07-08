/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbvmerg.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine vignette merging
 */

#include "core.h"
#include "swerrors.h"

#include "display.h"
#include "vnobj.h"
#include "stacks.h"
#include "mm.h"
#include "control.h"
#include "pathops.h"
#include "panalyze.h"
#include "dlstate.h"
#include "gu_chan.h"
#include "gstack.h"
#include "dl_free.h"

#include "recomb.h"
#include "rcbdl.h"
#include "rcbcomp.h"
#include "rcbvmerg.h"

static DLREF *dl_step_n(DLREF *dl, int32 n)
{
  while ( n-- > 0 )
    dl = dlref_next(dl);
  return dl;
}

static void rcbv_remove_from_bands(DL_STATE *page, LISTOBJECT *lobj)
{
  VIGNETTEOBJECT *vigobj;
  int32 b1, b2, last_band;

  HQASSERT(lobj != NULL, "lobj null");
  vigobj = lobj->dldata.vignette;
  HQASSERT(vigobj != NULL, "vigobj null");

  RCB_BBOX_TO_BANDS(page, lobj->bbox.y1, lobj->bbox.y2, b1, b2,
                    "rcbv_remove_from_bands");

  last_band = page->sizefactdisplaylist;

  rcb_remove_from_some_bands(page, lobj, b1, b2);
  rcb_remove_from_some_bands(page, lobj, last_band, last_band);
}

static Bool rcbv_make_white(DL_STATE *page,
                            LISTOBJECT *sublobj, p_ncolor_t *pnc_white)
{
  dl_color_t dlc_white, dlc_color;
  COLORVALUE *colorvalues, scolorvalue;
  int32 ccolorants, i, result;

  HQASSERT(sublobj != NULL, "sublobj null");
  HQASSERT(pnc_white != NULL, "pnc_white null");

  ccolorants = dl_num_channels(sublobj->p_ncolor);

  if ( ccolorants > 1 ) {
    colorvalues = (COLORVALUE *)mm_alloc(mm_pool_temp,
                                         sizeof(COLORVALUE) * ccolorants,
                                         MM_ALLOC_CLASS_GENERAL);
    if ( colorvalues == NULL )
      return error_handler(VMERROR);
  } else
    colorvalues = &scolorvalue;

  for ( i = 0; i < ccolorants; ++i )
    colorvalues[i] = COLORVALUE_PRESEP_WHITE;

  dlc_from_dl_weak(sublobj->p_ncolor, &dlc_color);

  dlc_clear(&dlc_white);
  result = dlc_alloc_fillin_template(page->dlc_context, &dlc_white, &dlc_color,
                                     colorvalues, ccolorants);

  if ( ccolorants > 1 )
    mm_free(mm_pool_temp, colorvalues, sizeof(COLORVALUE) * ccolorants);

  if ( !result )
    return FALSE;

  dlc_to_dl_weak(pnc_white, &dlc_white);

  return TRUE;
}

/**
 * Add a white LISTOBJECT to the vignette chain
 */
static Bool rcbv_include_whitelobj(DL_STATE *page, LISTOBJECT *vig_lobj)
{
  LISTOBJECT *nbour, *white;
  VIGNETTEOBJECT *vigobj;

  HQASSERT(vig_lobj != NULL, "Vignette listobject null");
  vigobj = vig_lobj->dldata.vignette;
  HQASSERT(vigobj != NULL, "vigobj null");
  if ( vigobj->white.used )
    return TRUE;

  vigobj->white.used = TRUE;
  white = vigobj->white.lobj;
  vigobj->white.lobj = NULL;   /* So we only do once. */

  if ( vigobj->outlines.nfillh ) { /* Merge in at head. */
    vigobj->outlines.nfillo = vigobj->outlines.nfillm = vigobj->outlines.nfillh;
    nbour = vn_white_insert_vignette(page, vig_lobj, TRUE, white);
  }
  else { /* Merge in at tail. */
    HQASSERT(vigobj->outlines.nfillt, "somehow lost overprint outline" );

    vigobj->outlines.nfillo = vigobj->outlines.nfillm = vigobj->outlines.nfillt;
    nbour = vn_white_insert_vignette(page, vig_lobj, FALSE, white);
  }
  if ( nbour == NULL )
    return FALSE;

  if ( !dl_equal_colorants(white->p_ncolor, nbour->p_ncolor) ) {
    /* White object does not have the full set of colorants required, so add
     * any missing colorants from adjacent object.
     */
    if ( !dl_merge_extra(page->dlc_context, &white->p_ncolor,
                         &nbour->p_ncolor) )
      return FALSE;
  }
  return TRUE;
}

static Bool rcbv_merge_elements_spread(DL_STATE *page,
                                       LISTOBJECT *old_lobj,
                                       LISTOBJECT *new_lobj,
                                       int32 matchtype, Bool fmixedmode)
{
  int32 rolledrect;
  int32 count1, count2;
  int32 inccount, sumcount, maxcount;
  DLREF *old_dlobj, *new_dlobj;
  STATEOBJECT *newstate;
  VIGNETTEOBJECT *old_vigobj, *new_vigobj;

  HQASSERT(old_lobj != NULL, "old_lobj null");
  HQASSERT(new_lobj != NULL, "new_lobj null");

  inccount = sumcount = maxcount = 0; /* Keep compiler happy... */

  /* Pick up possibly new object state since this will be inherited by the
   * vignette.
   */
  newstate = old_lobj->objectstate;

  old_vigobj = old_lobj->dldata.vignette;
  new_vigobj = new_lobj->dldata.vignette;

  HQASSERT(old_vigobj && new_vigobj, "lost VIGNETTEOBJECTs");

  old_dlobj = vig_dlhead(old_lobj);
  new_dlobj = vig_dlhead(new_lobj);
  HQASSERT(old_dlobj && new_dlobj, "vignette with no sub-objects");

  HQASSERT(old_vigobj->rolledrect == new_vigobj->rolledrect,
           "vignettes should both be rolled the same");
  rolledrect = old_vigobj->rolledrect;

  if ( matchtype == MERGE_FUZZY || matchtype == (MERGE_EXACT|MERGE_FUZZY)) {
    count1 = vig_len(old_vigobj) - ( rolledrect != VDR_Unknown ? 1 : 0 );
    count2 = vig_len(new_vigobj) - ( rolledrect != VDR_Unknown ? 1 : 0 );
    if ( count1 >= count2 || fmixedmode ) {
      maxcount = count1;
      inccount = count2;
    } else { /* New vignette has more steps. Keep this one. */
      uint8 old_rec;
      rcbv_compare_t * compareinfo;

      /* This step ensures that the correct bands are selected for each
         object on the DL */
      rcb_reassign_swapped_bands(page, old_lobj, new_lobj);

      old_lobj->dldata.vignette = new_vigobj;
      new_lobj->dldata.vignette = old_vigobj;

      /* the new vignette must hang onto the compare info structure */
      compareinfo = new_vigobj->compareinfo;
      new_vigobj->compareinfo = old_vigobj->compareinfo;
      old_vigobj->compareinfo = compareinfo;
      HQASSERT(old_vigobj->compareinfo != NULL, "compareinfo null");
      HQASSERT(new_vigobj->compareinfo == NULL,"compareinfo non null.");

      old_vigobj = old_lobj->dldata.vignette;
      new_vigobj = new_lobj->dldata.vignette;

      old_rec    = old_vigobj->recurse;
      old_vigobj->recurse = new_vigobj->recurse;
      new_vigobj->recurse = old_rec;

      old_dlobj = vig_dlhead(old_lobj);
      new_dlobj = vig_dlhead(new_lobj);

      maxcount = count2;
      inccount = count1;
    }
    sumcount = inccount / 2;
  }

  while ( old_dlobj && new_dlobj ) {
    old_lobj = dlref_lobj(old_dlobj);
    new_lobj = dlref_lobj(new_dlobj);
    HQASSERT(old_lobj && new_lobj, "vignette link with no object");

    /* If the spots numbers are not the same then merge the list
     * entries so the screen is preserved in the merged object.
     * This may require that we create a new cloned spotno.
     */
    old_lobj->objectstate = newstate;

    if ( !dl_merge_extra(page->dlc_context, &old_lobj->p_ncolor,
                         &new_lobj->p_ncolor) )
      return FALSE;

    if ( rolledrect == VDR_Unknown &&
         ( matchtype == MERGE_FUZZY || matchtype == (MERGE_EXACT|MERGE_FUZZY)) ) {
      sumcount += inccount;
      if ( sumcount >= maxcount ) {
        sumcount -= maxcount;
        new_dlobj = dlref_next(new_dlobj);
      }
      old_dlobj = dlref_next(old_dlobj);
    } else {
      new_dlobj = dlref_next(new_dlobj);
      old_dlobj = dlref_next(old_dlobj);
    }
    rolledrect = VDR_Unknown;
  }

  HQASSERT((!old_dlobj) && (matchtype == MERGE_FUZZY ||
             matchtype == (MERGE_EXACT|MERGE_FUZZY) ||
             !new_dlobj), "vignette out of sync");
  return TRUE;
}

static Bool rcbv_merge(DL_STATE *page, int32 cextendh, int32 cextendt,
                       LISTOBJECT *lobjd, LISTOBJECT *lobjs,
                       p_ncolor_t *pnc_white)
{
  DLREF *linkd, *links;
  LISTOBJECT *slobj;

  HQASSERT((cextendh == 0 && cextendt >= 1) || (cextendh >= 1 && cextendt == 0),
            "cxtendh/cextendt unexpected" );
  HQASSERT(lobjd != NULL, "lobjd null");
  HQASSERT(lobjs != NULL, "lobjs null");
  HQASSERT(pnc_white != NULL, "pnc_white null");

  linkd = vig_dlhead(lobjd);
  links = vig_dlhead(lobjs);
  HQASSERT(linkd != NULL, "linkd null");
  HQASSERT(links != NULL, "links null");

  while ( cextendh-- >= 1 ) {
    HQASSERT(linkd, "linkd NULL");
    slobj = dlref_lobj(linkd);

    if ( !dl_merge_extra(page->dlc_context, &slobj->p_ncolor, pnc_white) )
      return FALSE;
    linkd = dlref_next(linkd);
  }
  while ( linkd && links ) {
    LISTOBJECT *slobj1, *slobj2;

    slobj1 = dlref_lobj(linkd);
    slobj2 = dlref_lobj(links);

    if ( !dl_merge_extra(page->dlc_context, &slobj1->p_ncolor,
                         &slobj2->p_ncolor) )
      return FALSE;

    linkd = dlref_next(linkd);
    links = dlref_next(links);
  }
  while ( cextendt-- >= 1 ) {
    HQASSERT(linkd, "linkd NULL");
    slobj = dlref_lobj(linkd);

    if ( !dl_merge_extra(page->dlc_context, &slobj->p_ncolor, pnc_white) )
      return FALSE;
    linkd = dlref_next(linkd);
  }
  HQASSERT(linkd == NULL && links == NULL, "linkd and links must be null");
  return TRUE;
}

static Bool rcbv_merge_white_with_dest(DL_STATE *page,
                                       int32 cextend, DLREF **plinks,
                                       p_ncolor_t *pnc_white)
{
  DLREF *links;
  Bool added;

  links = *plinks;

  while ( cextend-- >= 1 ) {
    LISTOBJECT *slobj;

    HQASSERT(links != NULL, "links null");

    slobj = dlref_lobj(links);

    if ( !dl_merge_extra(page->dlc_context, &slobj->p_ncolor, pnc_white) )
      return FALSE;

    if ( !add_listobject(page, slobj, &added) )
      return FALSE;
    dlref_assign(links, NULL);
    links = dlref_next(links);
  }
  *plinks = links;
  return TRUE;
}

/**
 * Have two vignettes whose bodies match, but one has extra white objects at
 * the start and the other has extra white objects at the end. Need to merge
 * the two into one, adding extra white objects so that result is the union
 * of the two of them.
 *
 * Have created a vignette HDL via malloc function, so should just need
 * to read the two source vignettes and add the appropriate elements
 * sequentially. Then close the HDL when done.
 *
 * \todo BMJ 29-Oct-08 : Not sure exactly what this code is doing and if it
 * is working in all cases, but need to alter it to support DL purging.
 * Struggling to find test cases that stress this code, so not convinced my
 * changes are 100% correct. Needs further investigation.
 */
static Bool rcbv_merge_with_dest(DL_STATE *page, rcbv_compare_t *rcbv_compare,
                                 LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                 p_ncolor_t *pnc_white1, p_ncolor_t *pnc_white2)
{
  DLREF *link1, *link2;
  Bool added;

  HQASSERT(rcbv_compare != NULL, "rcbv_compare");
  HQASSERT(lobj1 != NULL, "lobj1 null");
  HQASSERT(lobj2 != NULL, "lobj2 null");
  HQASSERT(pnc_white1 != NULL, "pnc_white1 null");
  HQASSERT(pnc_white2 != NULL, "pnc_white2 null");

  link1 = vig_dlhead(lobj1);
  link2 = vig_dlhead(lobj2);
  HQASSERT(link1 != NULL, "link1 null");
  HQASSERT(link2 != NULL, "link2 null");

  if ( rcbv_compare->cextend1h >= 1 ) {
    if ( !rcbv_merge_white_with_dest(page, rcbv_compare->cextend1h, &link2,
                                     pnc_white1) )
      return FALSE;
  } else {
    HQASSERT(rcbv_compare->cextend2h >= 1, "expected cextend2h >= 1" );

    if ( !rcbv_merge_white_with_dest(page, rcbv_compare->cextend2h,
                                     &link1, pnc_white2) )
      return FALSE;
  }

  while ( link1 != NULL && link2 != NULL ) { /* merge common with dest */
    LISTOBJECT *slobj1 = dlref_lobj(link1), *slobj2 = dlref_lobj(link2);

    if ( !dl_merge_extra(page->dlc_context, &slobj1->p_ncolor,
                         &slobj2->p_ncolor) )
      return FALSE;

    if ( !add_listobject(page, slobj1, &added) )
      return FALSE;
    dlref_assign(link1, NULL);
    link1 = dlref_next(link1);
    link2 = dlref_next(link2);
  }

  if ( rcbv_compare->cextend1t >= 1 ) {
    if ( !rcbv_merge_white_with_dest(page, rcbv_compare->cextend1t, &link2,
                                     pnc_white1) )
      return FALSE;
  } else {
    HQASSERT( rcbv_compare->cextend2t >= 1, "expected cextend2t >= 1");

    if ( !rcbv_merge_white_with_dest(page, rcbv_compare->cextend2t, &link1,
                                     pnc_white2 ))
      return FALSE;
  }

  HQASSERT(link1 == NULL && link2 == NULL, "link1, link2 must be null" );

  return TRUE;
}

/* Only need to check the marker if not extending at that end. Otherwise
 * will be extending with white so the marker value does not change */
static void rcbv_merge_whiteobject(DL_STATE *page,
                                   rcbv_compare_t *rcbv_compare,
                                   LISTOBJECT *lobj_old,
                                   LISTOBJECT *lobj_new,
                                   VIGNETTEOBJECT *vigobj_old,
                                   VIGNETTEOBJECT *vigobj_new)
{
  DLREF *link_old, *link_new;

  link_old = vig_dlhead(lobj_old);
  link_new = vig_dlhead(lobj_new);

  if ( rcbv_compare->cextend1h == 0 && rcbv_compare->cextend2h == 0 ) {
    LISTOBJECT *slobj_old, *slobj_new;
    slobj_old = dlref_lobj(link_old);
    slobj_new = dlref_lobj(link_new);
    if ((( slobj_old->marker & MARKER_VN_WHITEOBJECT ) == 0 ) ||
        (( slobj_new->marker & MARKER_VN_WHITEOBJECT ) == 0 )) {
      slobj_old->marker &= (~MARKER_VN_WHITEOBJECT);
      slobj_new->marker &= (~MARKER_VN_WHITEOBJECT);
      dl_release(page->dlc_context, &vigobj_old->partialcolors);
      dl_release(page->dlc_context, &vigobj_new->partialcolors);
    }
  }
  if ( rcbv_compare->cextend1t == 0 && rcbv_compare->cextend2t == 0 ) {
    LISTOBJECT *slobj_old, *slobj_new;
    link_old = dl_step_n(link_old, vig_len(vigobj_old) - 1);
    link_new = dl_step_n(link_new, vig_len(vigobj_new) - 1);
    slobj_old = dlref_lobj(link_old);
    slobj_new = dlref_lobj(link_new);
    if ((( slobj_old->marker & MARKER_VN_WHITEOBJECT ) == 0 ) ||
        (( slobj_new->marker & MARKER_VN_WHITEOBJECT ) == 0 )) {
      slobj_old->marker &= (~MARKER_VN_WHITEOBJECT);
      slobj_new->marker &= (~MARKER_VN_WHITEOBJECT);
      dl_release(page->dlc_context, &vigobj_old->partialcolors);
      dl_release(page->dlc_context, &vigobj_new->partialcolors);
    }
  }
}

static Bool rcbv_merge_partialcolors(DL_STATE *page, p_ncolor_t *pncolorl,
                                     p_ncolor_t *pncolorr)
{
  HQASSERT(pncolorl != NULL, "pncolorl null");
  HQASSERT(pncolorr != NULL, "pncolorr null");

  if ( *pncolorr != NULL ) {
    if ( *pncolorl != NULL )
      return dl_merge(page->dlc_context, pncolorl, pncolorr);
    else
      dl_copy_release(pncolorl, pncolorr);
  }
  return TRUE;
}

/**
 * Partial match and both vignettes need extending (at opposite ends)
 */
static Bool rcbv_double_extend(DL_STATE *page, rcbv_compare_t *rcbv_compare,
                               LISTOBJECT *lobj_old, LISTOBJECT *lobj_new,
                               VIGNETTEOBJECT *vigobj_old,
                               VIGNETTEOBJECT *vigobj_new,
                               Bool fvigko, LISTOBJECT **lobj_ret,
                               Bool *faddnewtodl)
{
  LISTOBJECT *lobj_merge, *sub_lobj, slobj_temp;
  DLREF *link1, *link2;
  p_ncolor_t nc_white1, nc_white2;
  int32 count, result = TRUE;

  HQASSERT(rcbv_compare != NULL, "rcbv_compare null");
  HQASSERT(lobj_old != NULL, "lobj_old null");
  HQASSERT(lobj_new != NULL, "lobj_new null");
  HQASSERT(vigobj_old != NULL, "vigobj_old null");
  HQASSERT(vigobj_new != NULL, "vigobj_new null");
  HQASSERT(faddnewtodl != NULL, "faddnewtodl null");

  count = vig_len(vigobj_old) + rcbv_compare->cextend1h +
                              rcbv_compare->cextend1t;
  HQASSERT(count == vig_len(vigobj_new) + rcbv_compare->cextend2h +
           rcbv_compare->cextend2t, "mismatch between new and old vignette");

  /* Allocate the listobject, vignetteobject and all the dl links */
  if ( !make_listobject_copy(page, lobj_old, &lobj_merge) /* RENDER_vignette */
       || !vn_alloc_vignette(page, lobj_merge, count) )
    return FALSE;

  /* override with merge info */
  lobj_merge->p_ncolor = (p_ncolor_t)0;
  HQASSERT(lobj_old->attr.planes == NULL, "Have fuzzy planes in a vignette");

  /* Create white color for missing elements for each vignette */
  link1 = vig_dlhead(lobj_old);
  link2 = vig_dlhead(lobj_new);
  if ( !rcbv_make_white(page, dlref_lobj(link1), &nc_white1 ) ||
       !rcbv_make_white(page, dlref_lobj(link2), &nc_white2 ))
    result = FALSE;

  /* Merge all the elements together filling in missing colorants with white */
  if ( result ) {
    result = rcbv_merge_with_dest(page, rcbv_compare, lobj_old, lobj_new,
                                  &nc_white1, &nc_white2);
    dl_release(page->dlc_context, &nc_white1);
    dl_release(page->dlc_context, &nc_white2);
  }
  if ( !vn_complete_vignette() || !result ) {
    free_dl_object(lobj_merge, page);
    return FALSE;
  }

  /* Successfully merged elements, now merge vignetteobjects and create
   * outlines
   */
  vn_merge_vignetteobject(lobj_merge, lobj_old, lobj_new);

  /* Merge partialcolors for end white object */
  if ( !rcbv_merge_partialcolors(page, &vigobj_new->partialcolors,
                                 &vigobj_old->partialcolors))
    return FALSE;
  if ( vigobj_new->partialcolors != NULL )
    dl_copy_release(&lobj_merge->dldata.vignette->partialcolors,
                    &vigobj_new->partialcolors);

  /* Swap over merge and new listobjects (lobj_merge becomes lobj_new) */
  slobj_temp  = *lobj_new;
  *lobj_new   = *lobj_merge;
  *lobj_merge = slobj_temp;

  /* Remove the original dl objects from the dl (if they were on the dl)
     and free all their remaining objects */
  rcbv_remove_from_bands(page, lobj_old);
  if ( fvigko )
    rcbv_remove_from_bands(page, lobj_merge);
  free_dl_object(lobj_old, page);
  free_dl_object(lobj_merge, page);
  *faddnewtodl = TRUE;

  /* Indicate which dl object will be used for the merged vignette */
  *lobj_ret = lobj_new;

  /* Make the first element's color also the top level vignette color */
  link1 = vig_dlhead(lobj_new);
  sub_lobj = dlref_lobj(link1);
  return dl_copy(page->dlc_context, &lobj_new->p_ncolor, &sub_lobj->p_ncolor);
}

Bool rcbv_merge_vignettes(DL_STATE *page, rcbv_compare_t *rcbv_compare,
                          LISTOBJECT *lobj_old, LISTOBJECT *lobj_new,
                          LISTOBJECT **lobj_ret, Bool *faddnewtodl)
{
  VIGNETTEOBJECT *vigobj_old, *vigobj_new;
  int32 fmixedmode;

  HQASSERT(rcbv_compare != NULL, "rcbv_compare null" );
  HQASSERT((rcbv_compare->cextend1h == 0 || rcbv_compare->cextend2h == 0 ) &&
            (rcbv_compare->cextend1t == 0 || rcbv_compare->cextend2t == 0 ),
            "unexpected set of extends");
  HQASSERT(lobj_old != NULL, "lobj_old null");
  HQASSERT(lobj_new != NULL, "lobj_new null");

  *faddnewtodl = FALSE;

  vigobj_old = lobj_old->dldata.vignette;
  vigobj_new = lobj_new->dldata.vignette;
  HQASSERT(vigobj_old != NULL, "vigobj_old null");
  HQASSERT(vigobj_new != NULL, "vigobj_new null");

  HQASSERT((vigobj_old->colormonotonic == VDC_Neutral &&
            vigobj_new->colormonotonic == VDC_Neutral ) ||
            ( vigobj_old->colormonotonic != VDC_Neutral &&
              vigobj_new->colormonotonic != VDC_Neutral ) ||
            ( vigobj_old->colormonotonic != VDC_Neutral &&
              vigobj_new->colormonotonic == VDC_Neutral ),
            "Unexpected set of colormonotonic values");

  /* Are we merging a vignette and a knockout? */
  fmixedmode = (vigobj_old->colormonotonic != VDC_Neutral &&
                vigobj_new->colormonotonic == VDC_Neutral);

  /* Merge the vignettes sense of 'colormonotonic' (for vignette splitting) */
  if ( vigobj_old->colormonotonic == VDC_Neutral ||
       vigobj_new->colormonotonic == VDC_Neutral )
    vigobj_new->colormonotonic = vigobj_old->colormonotonic = VDC_Neutral;

  /* Merge the vignettes confidence level */
  if ( vigobj_old->confidence == VDC_High ||
       vigobj_new->confidence == VDC_High ) {
    HQTRACE(debug_vignette && (vigobj_old->confidence == VDC_Low ||
            vigobj_new->confidence == VDC_Low),
             ("promoted vignette to VDC_High"));
    vigobj_new->confidence = vigobj_old->confidence = VDC_High;
  }

  /* Merge the whiteobject markers */
  rcbv_merge_whiteobject(page, rcbv_compare, lobj_old, lobj_new,
                         vigobj_old, vigobj_new);

  /* If possible, use hidden white dl object to extend at head */
  if ( ((rcbv_compare->cextend1h == 1 && vigobj_old->outlines.nfillh != NULL) ||
       (rcbv_compare->cextend1t == 1 && vigobj_old->outlines.nfillt != NULL)) &&
       !vigobj_old->white.used ) {
    if ( !rcbv_include_whitelobj(page, lobj_old) )
      return FALSE;

    rcbv_compare->cextend1h = rcbv_compare->cextend1t = 0;
  }

  /* If possible, use hidden white dl object to extend at tail */
  if ( ((rcbv_compare->cextend2h == 1 && vigobj_new->outlines.nfillh != NULL) ||
       (rcbv_compare->cextend2t == 1 && vigobj_new->outlines.nfillt != NULL)) &&
       !vigobj_new->white.used ) {
    if ( !rcbv_include_whitelobj(page, lobj_new) )
      return FALSE;

    rcbv_compare->cextend2h = rcbv_compare->cextend2t = 0;
  }

  /* Now merge all the elements together.
   * At this point should have one of four basic cases:
   * 1. Complete match between vignettes OR outline match and number of
   *    steps differs
   * 2. New vignette matches part of old vignette
   * 3. Old vignette matches part of new vignette
   * 4. Partial match and both vignettes need extending (at opposite ends)
   */
  if ( rcbv_compare->matchstyle == RCBV_OUTLINE_MATCH ||
       ( rcbv_compare->cextend1h + rcbv_compare->cextend1t == 0 &&
         rcbv_compare->cextend2h + rcbv_compare->cextend2t == 0 )) {

    /* 1. Complete match between vignettes OR outline match and number of
     *    steps differs */
    if ( !rcbv_merge_elements_spread(page, lobj_old, lobj_new,
                                     rcbv_compare->matchtype, fmixedmode) )
      return FALSE;

    /* Merge partialcolors for end white object. The LISTOBJECTs may be
       modified by rcbv_merge_elements_spread, so we have to retrieve the
       new values in order to get the correct destination object. */
    vigobj_old = lobj_old->dldata.vignette;
    vigobj_new = lobj_new->dldata.vignette;
    if ( !rcbv_merge_partialcolors(page, &vigobj_old->partialcolors,
                                   &vigobj_new->partialcolors) )
      return FALSE;

    HQASSERT(!fmixedmode == rcb_assert_not_on_dl(page, lobj_new, &lobj_new->bbox),
             "Merging knockout not on DL or not knockout and on DL");
    if ( fmixedmode ) {
      /* Merging a vignette and a knockout therefore new_lobj will already
         be on the display list; remove and free the knockout lobj */
      rcbv_remove_from_bands(page, lobj_new);
      free_dl_object(lobj_new, page);
    }

    /* Indicate which dl object will be used for the merged vignette */
    *lobj_ret = lobj_old;
    return TRUE;
  }
  else if ( rcbv_compare->cextend1h + rcbv_compare->cextend1t == 0 ) {
    /* 2. New vignette matches part of old vignette */
    DLREF *link = vig_dlhead(lobj_new);
    p_ncolor_t nc_white;
    int32 result;

    /* Create a white color for missing elements from new vignette */
    if ( !rcbv_make_white(page, dlref_lobj(link), &nc_white) )
      return FALSE;

    /* Merge all the elements, filling in missing colorants with white */
    result = rcbv_merge(page, rcbv_compare->cextend2h, rcbv_compare->cextend2t,
                        lobj_old, lobj_new, &nc_white);

    dl_release(page->dlc_context, &nc_white);

    if ( !result )
      return FALSE;

    /* Merge partialcolors for end white object */
    if ( !rcbv_merge_partialcolors(page, &vigobj_old->partialcolors,
                                   &vigobj_new->partialcolors) )
      return FALSE;

    if ( fmixedmode ) {
      /* Merging a vignette and a knockout therefore new_lobj will already
         be on the display list; remove and free the knockout lobj */
      rcbv_remove_from_bands(page, lobj_new);
      free_dl_object(lobj_new, page);
    }

    /* Indicate which dl object will be used for the merged vignette */
    *lobj_ret = lobj_old;
    return TRUE;
  }
  else if ( rcbv_compare->cextend2h + rcbv_compare->cextend2t == 0 ) {
    /* 3. Old vignette matches part of new vignette */
    DLREF *link = vig_dlhead(lobj_old);
    LISTOBJECT *sub_lobj;
    p_ncolor_t nc_white;
    int32 result;

    /* Create a white color for missing elements from old vignette */
    if ( !rcbv_make_white(page, dlref_lobj(link), &nc_white) )
      return FALSE;

    /* Merge all the elements, filling in missing colorants with white */
    result = rcbv_merge(page, rcbv_compare->cextend1h, rcbv_compare->cextend1t,
                        lobj_new, lobj_old, &nc_white);

    dl_release(page->dlc_context, &nc_white);

    if ( !result )
      return FALSE;

    /* Merge partialcolors for end white object */
    if ( !rcbv_merge_partialcolors(page, &vigobj_new->partialcolors,
                                   &vigobj_old->partialcolors) )
      return FALSE;

    /* Merged old into new, so remove old and add new to the dl */
    rcbv_remove_from_bands(page, lobj_old);
    free_dl_object(lobj_old, page);
    /* Only need to add new to display list if not merging a vignette
       and a knockout; otherwise new will already exist on the dl */
    if ( !fmixedmode )
      *faddnewtodl = TRUE;

    /* Indicate which dl object will be used for the merged vignette */
    *lobj_ret = lobj_new;

    /* Since keeping new need to update the top level vignette color
       to contain the full merged set of colorants */
    link = vig_dlhead(lobj_new);
    sub_lobj = dlref_lobj(link);
    dl_release(page->dlc_context, &lobj_new->p_ncolor);
    return dl_copy(page->dlc_context, &lobj_new->p_ncolor, &sub_lobj->p_ncolor);
  }
  else {
    /* 4. Partial match and both vignettes need extending (at opposite ends) */
    HQASSERT((rcbv_compare->cextend1h == 0 && rcbv_compare->cextend1t >= 1 &&
              rcbv_compare->cextend2h >= 1 && rcbv_compare->cextend2t == 0) ||
             (rcbv_compare->cextend1h >= 0 && rcbv_compare->cextend1t == 0 &&
              rcbv_compare->cextend2h == 0 && rcbv_compare->cextend2t >= 1 ),
              "unexpected set of extends");
    return rcbv_double_extend(page, rcbv_compare,
                              lobj_old, lobj_new, vigobj_old,
                              vigobj_new, fmixedmode, lobj_ret, faddnewtodl);
  }
  /* NOT REACHED */
}

/* Log stripped */
