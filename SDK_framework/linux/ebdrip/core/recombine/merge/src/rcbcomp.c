/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbcomp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine compare functions
 */

#include "core.h"
#include "rcbcomp.h"

#include "objects.h"
#include "display.h"
#include "imageo.h" /* IMAGEOBJECT */
#include "stacks.h"
#include "vnobj.h"
#include "dl_store.h"
#include "graphics.h"
#include "pathops.h"
#include "panalyze.h"
#include "gs_color.h"
#include "swerrors.h"
#include "control.h"
#include "gstack.h"
#include "constant.h" /* EPSILON */
#include "shadex.h"  /* SHADINGinfo */
#include "hdl.h" /* HDL operations */
#include "imexpand.h" /* im_expandformat et. al. */
#include "imstore.h"

#include "recomb.h"
#include "rcbcntrl.h"
#include "rcbvigko.h"
#include "rcbdl.h"
#include "rcbshfil.h" /* rcbs_compare_* */
#include "rcbtrap.h"


static rcb_merge_t compare_patch_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                         rcb_merge_t test_type);
static rcb_merge_t compare_shfill_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                          rcb_merge_t test_type);
static rcb_merge_t compare_vignette_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                            rcb_merge_t test_type);
static rcb_merge_t compare_char_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type);
static rcb_merge_t compare_image_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                         rcb_merge_t test_type);
static rcb_merge_t compare_mask_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type);
static rcb_merge_t compare_fill_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type);

/* For object types which should never exist or never be matched. */
static rcb_merge_t compare_invalid_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                           rcb_merge_t test_type);
/* For object types which can exist, but never match. */
static rcb_merge_t compare_reject_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                          rcb_merge_t test_type);

COMPARE_LOBJS rcb_comparefn(int32 opcode)
{
  static COMPARE_LOBJS dl_comparefn[] = {
    compare_invalid_objects,    /* RENDER_void */
    compare_invalid_objects,    /* RENDER_erase */
    compare_char_objects,       /* RENDER_char */
    compare_invalid_objects,    /* RENDER_rect */
    compare_invalid_objects,    /* RENDER_quad */
    compare_fill_objects,       /* RENDER_fill */
    compare_mask_objects,       /* RENDER_mask */
    compare_image_objects,      /* RENDER_image */
    compare_vignette_objects,   /* RENDER_vignette */
    compare_invalid_objects,    /* RENDER_gouraud */
    compare_shfill_objects,     /* RENDER_shfill */
    compare_patch_objects,      /* RENDER_shfill_patch */
    compare_reject_objects,     /* RENDER_hdl */
    compare_reject_objects,     /* RENDER_group */
    compare_invalid_objects,    /* RENDER_backdrop */
    compare_invalid_objects     /* RENDER_cell */
  };

  HQASSERT(NUM_ARRAY_ITEMS(dl_comparefn) == N_RENDER_OPCODES,
           "Render opcode table changed, dl_comparefn needs updating");
  HQASSERT(opcode < N_RENDER_OPCODES, "Not a valid render opcode");
  HQASSERT(dl_comparefn[opcode] != NULL,
           "No recombine object compare routine for this opcode");

  return dl_comparefn[opcode];
}

uint32 rcb_compareop(int32 opcode)
{
  /* For DL object matching based recombine. */
  static int32 dl_compareop[N_RENDER_OPCODES] = {
    MERGE_NONE,                 /* RENDER_void */
    MERGE_NONE,                 /* RENDER_erase */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_char */
    MERGE_NONE,                 /* RENDER_rect */
    MERGE_NONE,                 /* RENDER_quad */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_fill */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_mask */
    MERGE_EXACT,                /* RENDER_image */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_vignette */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_gouraud */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_shfill */
    MERGE_EXACT|MERGE_FUZZY,    /* RENDER_shfill_patch */
    MERGE_NONE,                 /* RENDER_hdl */
    MERGE_NONE,                 /* RENDER_group */
    MERGE_NONE,                 /* RENDER_backdrop */
    MERGE_NONE                  /* RENDER_cell */
  };

  HQASSERT(NUM_ARRAY_ITEMS(dl_compareop) == N_RENDER_OPCODES,
           "Render opcode table changed, dl_compareop needs updating");
  HQASSERT(opcode < N_RENDER_OPCODES, "Not a valid render opcode");
  HQASSERT(dl_compareop[opcode] != MERGE_NONE,
           "No recombine object compare op for this opcode");

  return dl_compareop[opcode];
}

static Bool rcbv_compare_elements_sequence(DLREF *link1,
                                           DLREF **plink2,
                                           int32 fallowextend,
                                           int32 *pextend)
{
  DLREF *link2;
  LISTOBJECT *lobj1, *lobj2;
  int32 extend;

  HQASSERT(link1 != NULL, "link1 null");
  HQASSERT(plink2 != NULL, "plink2 null");
  HQASSERT(pextend != NULL, "pextend null");

  link2 = *plink2;
  HQASSERT(link2 != NULL, "link2 null");

  lobj1 = dlref_lobj(link1);
  HQASSERT(lobj1 != NULL, "lobj1 null");
  HQASSERT(lobj1->opcode == RENDER_fill, "vignette sub-object not fill");

  extend = *pextend = 0;

  while (link2 != NULL) {

    lobj2 = dlref_lobj(link2);
    HQASSERT(lobj2 != NULL, "lobj2 null");
    HQASSERT(lobj2->opcode == RENDER_fill, "vignette sub-object not fill");

    if ( compare_fill_objects(lobj1, lobj2, MERGE_EXACT) == MERGE_EXACT ) {
      *pextend = extend;
      *plink2  = link2;
      return TRUE;
    }

    if ( !fallowextend )
      return FALSE;

    ++extend;
    link2 = dlref_next(link2);
  }

  return FALSE;
}

static Bool rcbv_compare_elements(DLREF **plink1, DLREF **plink2)
{
  DLREF *link1, *link2;

  HQASSERT(plink1 != NULL, "plink1 null");
  HQASSERT(plink2 != NULL, "plink2 null");

  link1 = *plink1;
  link2 = *plink2;

  while ( link1 != NULL && link2 != NULL ) {
    LISTOBJECT *tlobj1, *tlobj2;

    tlobj1 = dlref_lobj(link1);
    tlobj2 = dlref_lobj(link2);
    HQASSERT(tlobj1 != NULL, "tlobj1 null");
    HQASSERT(tlobj2 != NULL, "tlobj2 null");

    HQASSERT(tlobj2->opcode == RENDER_fill, "bad object type in vignette");

    if ( compare_fill_objects(tlobj1, tlobj2, MERGE_EXACT) == MERGE_NONE )
      return FALSE;

    link1 = dlref_next(link1);
    link2 = dlref_next(link2);
  }
  *plink1 = link1;
  *plink2 = link2;
  return TRUE;
}

static Bool rcbv_compare_vignette_elements(rcbv_compare_t *rcbv_compare,
                                           LISTOBJECT *lobj1,
                                           LISTOBJECT *lobj2,
                                           VIGNETTEOBJECT *vigobj1,
                                           VIGNETTEOBJECT *vigobj2)
{
  DLREF *link1, *link2;
  int32 vc1, vc2;

  HQASSERT(rcbv_compare != NULL, "rcbv_compare null");
  HQASSERT(lobj1 != NULL, "lobj1 null");
  HQASSERT(lobj2 != NULL, "lobj2 null");
  HQASSERT(vigobj1 != NULL, "vigobj1 null");
  HQASSERT(vigobj2 != NULL, "vigobj2 null");

  vc1 = vig_len(vigobj1);
  vc2 = vig_len(vigobj2);
  link1 = vig_dlhead(lobj1);
  link2 = vig_dlhead(lobj2);
  HQASSERT(link1 != NULL, "link1 null");
  HQASSERT(link2 != NULL, "link2 null");

  if ( rcbv_compare_elements_sequence(link1,  &link2,
                                      vigobj1->white.h,
                                      &rcbv_compare->cextend1h) ||
       rcbv_compare_elements_sequence(link2,  &link1,
                                      vigobj2->white.h,
                                      &rcbv_compare->cextend2h) ) {
    /* Already successfully compared the current two sub-objects */
    link1 = dlref_next(link1);
    link2 = dlref_next(link2);

    /* Compare all subsequent objects */
    if ( rcbv_compare_elements(&link1, &link2)) {
      int32 cextend1h, cextend1t, cextend2h, cextend2t;

      cextend1h = rcbv_compare->cextend1h;
      cextend2h = rcbv_compare->cextend2h;
      cextend1t = rcbv_compare->cextend1t =
        ((link1 == NULL && link2 != NULL) ? cextend2h + vc2 - vc1 : 0);
      cextend2t = rcbv_compare->cextend2t =
        ((link1 != NULL && link2 == NULL) ? cextend1h + vc1 - vc2 : 0);

      HQASSERT(!cextend1h || !cextend2h, "extend both heads ?");
      HQASSERT((rcbv_compare->cextend1h + rcbv_compare->cextend1t +
                vc1) == (rcbv_compare->cextend2h +
                rcbv_compare->cextend2t + vc2),
                "extend counts inconsistent");

      if ( !((cextend1t && cextend2t) ||
              (cextend1h && cextend1t) ||
              (cextend2h && cextend2t) ||
              (cextend1h && !vigobj1->white.h) ||
              (cextend1t && !vigobj1->white.t) ||
              (cextend2h && !vigobj2->white.h) ||
              (cextend2t && !vigobj2->white.t)) ) {
        /* @@@ Temporaily switch off matching vignettes that both require
         * extending by more than one element and not strong contained until
         * rcbv_merge_vignettes is completed */
        if ( ((rcbv_compare->cextend1h + rcbv_compare->cextend1t > 1) ||
              (rcbv_compare->cextend1h == 1 &&
              (vigobj1->outlines.nfillh == NULL || vigobj1->white.used)) ||
              (rcbv_compare->cextend1t == 1 &&
                (vigobj1->outlines.nfillt == NULL || vigobj1->white.used))) &&
             ((rcbv_compare->cextend2h + rcbv_compare->cextend2t > 1) ||
              (rcbv_compare->cextend2h == 1 &&
                (vigobj2->outlines.nfillh == NULL || vigobj2->white.used)) ||
              (rcbv_compare->cextend2t == 1 &&
                (vigobj2->outlines.nfillt == NULL || vigobj2->white.used))) &&
             (vigobj1->style != VDS_StrongContained) ) {
          HQFAIL("Merging vignettes - both requiring extending, "
                  "and can't use normal white_lobj, NYI!");
          return FALSE;
        }

        /* This implies that both vignettes really are vignettes */
        HQTRACE(debug_vignette &&
                (vigobj1->confidence == VDC_Low ||
                 vigobj2->confidence == VDC_Low),
                 ("promoted vignette to VDC_High"
                   "(individual elements comparison)"));
        vigobj1->confidence = VDC_High;
        vigobj2->confidence = VDC_High;

        rcbv_compare->matchstyle = RCBV_ELEMENTS_MATCH;
        rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
        return TRUE;
      }
    }
  }
  return FALSE;
}

static Bool rcbv_compare_vignette_outline(rcbv_compare_t *rcbv_compare,
                                           VIGNETTEOBJECT *vigobj1,
                                           VIGNETTEOBJECT *vigobj2)
{
  NFILLOBJECT *nfillo, *nfill1, *nfill2;

  HQASSERT(rcbv_compare != NULL, "rcbv_compare null");
  HQASSERT(vigobj1 != NULL, "vigobj1 null");
  HQASSERT(vigobj2 != NULL, "vigobj2 null");

  nfill1 = vigobj1->outlines.nfillm;
  nfill2 = vigobj2->outlines.nfillm;

  /* Tests on the outline of vignettes.
   * Note: can't call rcbt_comparetrap as this will allow two vignettes,
   * both doughnut to doughnut, common centre points, but one inside the
   * other, to match incorrectly
   */

  /* If the previous test fails, compare the outline of each vignette. */
  if ( nfill1 != NULL && nfill2 != NULL ) {
    if ( same_nfill_objects(nfill1, nfill2) ||
         ( nfill1->rcbtrap != NULL &&
           nfill2->rcbtrap != NULL &&
           rcbt_compareexacttrap(nfill1->rcbtrap, nfill2->rcbtrap,
                                  FALSE /* disallow donuts */)) ) {
      /* If we've got a rolled rect, then the real vignette must match. */
      if ( vigobj1->rolledrect != VDR_Unknown ) {
        NFILLOBJECT *rfill1 = vigobj1->outlines.nfillr;
        NFILLOBJECT *rfill2 = vigobj2->outlines.nfillr;
        HQASSERT(rfill1, "somehow lost rfill1");
        HQASSERT(rfill2, "somehow lost rfill2");
        if ( !same_nfill_objects(rfill1, rfill2) &&
             !(rfill1->rcbtrap != NULL &&
                rfill2->rcbtrap != NULL &&
                rcbt_compareexacttrap(rfill1->rcbtrap, rfill2->rcbtrap,
                                       FALSE /* disallow donuts */)) )
          return FALSE;
      }

      /* This implies that both vignettes really are vignettes.*/
      HQTRACE(debug_vignette &&
             (vigobj1->confidence == VDC_Low ||
              vigobj2->confidence == VDC_Low),
              ("promoted vignette to VDC_High (0)"));
      vigobj1->confidence = VDC_High;
      vigobj2->confidence = VDC_High;

      /* Check to see if we've already extended one and need to extend the
       * other.
       */
      if ( (vigobj1->outlines.nfillh || vigobj1->outlines.nfillt) &&
           (vigobj2->outlines.nfillh || vigobj2->outlines.nfillt) ) {
        if ( (!vigobj1->white.used) && (vigobj2->white.used) ) {
          rcbv_compare->cextend1h = vigobj1->outlines.nfillh != NULL;
          rcbv_compare->cextend1t = vigobj1->outlines.nfillt != NULL;

          rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
          rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
          return TRUE;
        }
        if ( (vigobj1->white.used) && (!vigobj2->white.used) ) {
          rcbv_compare->cextend2h = vigobj2->outlines.nfillh != NULL;
          rcbv_compare->cextend2t = vigobj2->outlines.nfillt != NULL;

          rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
          rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
          return TRUE;
        }
      }
      rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
      rcbv_compare->matchtype  = MERGE_FUZZY;
      return TRUE;
    }
  }

  if ( vigobj1->rolledrect )
    return FALSE;

  nfill1 = vigobj1->outlines.nfills;
  nfill2 = vigobj2->outlines.nfills;

  /* If the previous test fails, compare the fuzzy circular outline of each
   * vignette.
   */
  if ( nfill1 != NULL && nfill2 != NULL ) {
    if ( same_nfill_objects(nfill1, nfill2) ||
         (nfill1->rcbtrap != NULL && nfill2->rcbtrap != NULL &&
           rcbt_compareexacttrap(nfill1->rcbtrap, nfill2->rcbtrap,
                                  FALSE /* disallow donuts */)) ) {
      /* This implies that both vignettes really are vignettes.*/
      HQTRACE(debug_vignette &&
              (vigobj1->confidence == VDC_Low ||
               vigobj2->confidence == VDC_Low),
               ("promoted vignette to VDC_High"));
      vigobj1->confidence = VDC_High;
      vigobj2->confidence = VDC_High;

      /* Special fill matched; leave outline as it was before, but replace
       * match. */
      vigobj1->outlines.nfillm = nfill1;
      vigobj2->outlines.nfillm = nfill2;

      rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
      rcbv_compare->matchtype  = MERGE_FUZZY;
      return TRUE;
    }
  }

  nfill1 = vigobj1->outlines.nfillm;
  nfill2 = vigobj2->outlines.nfillm;

  /* Try a match of extended 2 vs normal 1. */
  if ( nfill1 ) {
    nfillo = NULL;
    if ( vigobj2->outlines.nfillh )
      nfillo = vigobj2->outlines.nfillh;
    if ( vigobj2->outlines.nfillt )
      nfillo = vigobj2->outlines.nfillt;
    if ( nfillo ) {
      if ( same_nfill_objects(nfill1, nfillo) ||
           (nfill1->rcbtrap != NULL && nfillo->rcbtrap != NULL &&
            rcbt_compareexacttrap(nfill1->rcbtrap, nfillo->rcbtrap,
                                    FALSE /* disallow donuts */)) ) {
        /* This implies that both vignettes really are vignettes. */
        HQTRACE(debug_vignette &&
                (vigobj1->confidence == VDC_Low ||
                 vigobj2->confidence == VDC_Low),
                 ("promoted vignette to VDC_High"));

        /* Extend second vignette. */
        if ( !(rcbv_compare->cextend1h && vigobj2->outlines.nfillh) &&
             !(rcbv_compare->cextend1t && vigobj2->outlines.nfillt) ) {
          vigobj1->confidence = VDC_High;
          vigobj2->confidence = VDC_High;

          rcbv_compare->cextend2h = vigobj2->outlines.nfillh != NULL;
          rcbv_compare->cextend2t = vigobj2->outlines.nfillt != NULL;

          rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
          rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
          return TRUE;
        }
      }
    }
    nfill1 = nfillo;
  }

  /* Try a match of extended 1 vs normal 2. */
  if ( nfill2 ) {
    nfillo = NULL;
    if ( vigobj1->outlines.nfillh )
      nfillo = vigobj1->outlines.nfillh;
    if ( vigobj1->outlines.nfillt )
      nfillo = vigobj1->outlines.nfillt;
    if ( nfillo ) {
      if ( same_nfill_objects(nfillo, nfill2) ||
           (nfillo->rcbtrap != NULL && nfill2->rcbtrap != NULL &&
             rcbt_compareexacttrap(nfillo->rcbtrap, nfill2->rcbtrap,
                                    FALSE /* disallow donuts */)) ) {
        /* This implies that both vignettes really are vignettes. */
        HQTRACE(debug_vignette &&
                (vigobj1->confidence == VDC_Low ||
                 vigobj2->confidence == VDC_Low),
                 ("promoted vignette to VDC_High"));

        /* Extend first vignette. */
        if ( !(rcbv_compare->cextend2h && vigobj1->outlines.nfillh) &&
             !(rcbv_compare->cextend2t && vigobj1->outlines.nfillt) ) {
          vigobj1->confidence = VDC_High;
          vigobj2->confidence = VDC_High;

          rcbv_compare->cextend1h = vigobj1->outlines.nfillh != NULL;
          rcbv_compare->cextend1t = vigobj1->outlines.nfillt != NULL;

          rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
          rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
          return TRUE;
        }
      }
    }
    nfill2 = nfillo;
  }

  /* Try a match of extended 1 vs extended 2. */
  if ( nfill1 && nfill2 ) {
    if ( same_nfill_objects(nfill1, nfill2) ||
         (nfill1->rcbtrap != NULL && nfill2->rcbtrap != NULL &&
           rcbt_compareexacttrap(nfill1->rcbtrap, nfill2->rcbtrap,
                                 FALSE /* disallow donuts */)) ) {
      /* This implies that both vignettes really are vignettes. */
      HQTRACE(debug_vignette &&
             (vigobj1->confidence == VDC_Low ||
              vigobj2->confidence == VDC_Low),
              ("promoted vignette to VDC_High"));

      if ( !(vigobj2->outlines.nfillh && vigobj1->outlines.nfillh) &&
           !(vigobj2->outlines.nfillt && vigobj1->outlines.nfillt) ) {
        vigobj1->confidence = VDC_High;
        vigobj2->confidence = VDC_High;

        /* Extend both vignettes. */
        rcbv_compare->cextend1h = vigobj1->outlines.nfillh != NULL;
        rcbv_compare->cextend1t = vigobj1->outlines.nfillt != NULL;
        rcbv_compare->cextend2h = vigobj2->outlines.nfillh != NULL;
        rcbv_compare->cextend2t = vigobj2->outlines.nfillt != NULL;

        rcbv_compare->matchstyle = RCBV_OUTLINE_MATCH;
        rcbv_compare->matchtype  = MERGE_EXACT|MERGE_FUZZY;
        return TRUE;
      }
    }
  }
  return FALSE;
}

/**
 * The vignette comparator needs, effectively, to compare each sub-object.
 * Tell me whether the vignette nodes defined by the two LISTOBJECTs are
 * the same.
 */
rcb_merge_t rcbv_compare_vignettes(rcbv_compare_t *rcbv_compare,
                                   LISTOBJECT *lobj1,
                                   LISTOBJECT *lobj2,
                                   rcb_merge_t test_type,
                                   Bool fmergevigko)
{
  VIGNETTEOBJECT *vigobj1, *vigobj2;

  UNUSED_PARAM(uint32, test_type);

  HQASSERT(rcbn_enabled(), "not recombining separations");
  HQASSERT(!rcbn_first_separation(), "on first separation");
  HQASSERT(rcbv_compare != NULL, "rcbv_compare null");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "lobj1 or lobj2 null");
  HQASSERT((test_type & MERGE_SPLIT) == 0, "type MERGE_SPLIT");
  HQASSERT(lobj1->opcode == RENDER_vignette &&
           lobj2->opcode == RENDER_vignette,
           "lobj1 and lobj2 must be vignettes");

  rcbv_compare->matchtype = MERGE_NONE;

  vigobj1 = lobj1->dldata.vignette;
  vigobj2 = lobj2->dldata.vignette;
  HQASSERT(vigobj1 != NULL, "vigobj1 null");
  HQASSERT(vigobj2 != NULL, "vigobj2 null");

  if ( vigobj1->style != vigobj2->style ||
       vigobj1->rolledrect != vigobj2->rolledrect )
    return MERGE_NONE;

  /* Only merge vignettes with vignettes and knockouts with knockouts.
     Do not merge a vignette with a knockout (as a vignette) now.
     Knockouts for a vignette are fixed later by fixVignetteKnockouts */
  if ( !fmergevigko && ((vigobj1->colormonotonic == VDC_Neutral &&
        vigobj2->colormonotonic != VDC_Neutral) ||
       (vigobj1->colormonotonic != VDC_Neutral &&
        vigobj2->colormonotonic == VDC_Neutral)) )
    return MERGE_NONE;

  /* Check to see if part of the vignette has been merged
   * (due to overprinted ends) */
  if ( ((vigobj2->partialcolors != NULL) &&
         dl_common_colorants(lobj1->p_ncolor, vigobj2->partialcolors)) ||
       ((vigobj1->partialcolors != NULL) &&
         dl_common_colorants(lobj2->p_ncolor, vigobj1->partialcolors)) )
    return MERGE_NONE;

  rcbv_compare->cextend1h = 0;
  rcbv_compare->cextend1t = 0;
  rcbv_compare->cextend2h = 0;
  rcbv_compare->cextend2t = 0;

  /* Individual sub-object compare (only exact matches allowed) */
  if ( rcbv_compare_vignette_elements(rcbv_compare, lobj1, lobj2,
                                      vigobj1, vigobj2) )
    return rcbv_compare->matchtype;

  /* Outline check, including fuzzy and extended outlines */
  if ( rcbv_compare_vignette_outline(rcbv_compare, vigobj1, vigobj2) )
    return rcbv_compare->matchtype;

  HQASSERT(rcbv_compare->matchtype == MERGE_NONE, "Should be MERGE_NONE");
  return MERGE_NONE;
}

static rcb_merge_t compare_vignette_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                            rcb_merge_t test_type)
{
  VIGNETTEOBJECT *vigobj2;

  HQASSERT(lobj1 != NULL, "lobj1 null");
  HQASSERT(lobj2 != NULL, "lobj2 null");

  if ( lobj1->opcode != RENDER_vignette || lobj2->opcode != RENDER_vignette )
    return MERGE_NONE;

  vigobj2 = lobj2->dldata.vignette;
  HQASSERT(vigobj2 != NULL, "vigobj2 null");

  HQASSERT(vigobj2->compareinfo != NULL,
           "compareinfo should be in the second vignette");

  return rcbv_compare_vignettes(vigobj2->compareinfo, lobj1, lobj2,
                                test_type, FALSE);
}

static Bool same_form_objects(FORM *f1, FORM *f2)
{
  HQASSERT(f1 != NULL, "f1 NULL");
  HQASSERT(f2 != NULL, "f2 NULL");

  if ( f1->type == FORMTYPE_CHARCACHE ) {
    CHARCACHE *tc1 = (CHARCACHE *)f1;
    f1 = theForm(*tc1);
  }
  if ( f2->type == FORMTYPE_CHARCACHE ) {
    CHARCACHE *tc2 = (CHARCACHE *)f2;
    f2 = theForm(*tc2);
  }

  if ( f1->type != f2->type )
    return FALSE;
  if ( f1->w != f2->w )
    return FALSE;
  if ( f1->h != f2->h )
    return FALSE;
  if ( f1->size != f2->size )
    return FALSE;

  { int32 sbytes = f1->size;
    int32 *p1 = (int32 *)(f1->addr);
    int32 *p2 = (int32 *)(f2->addr);

    HQASSERT(sbytes >= 0, "sbytes should not be -ve");
    HQASSERT(p1 != NULL, "p1 should not be NULL");
    HQASSERT(p2 != NULL, "p2 should not be NULL");

    while ( (sbytes -= sizeof(int32)) >= 0 ) {
      if ( *p1++ != *p2++ )
        return FALSE;
    }
    sbytes += sizeof(int32);

    { uint8 *s1 = (uint8 *)p1;
      uint8 *s2 = (uint8 *)p2;
      while ( (sbytes -= sizeof(uint8)) >= 0 ) {
        if ( *s1++ != *s2++ )
          return FALSE;
      }
    }
  }

  return TRUE;
}

/**
 * Tell me whether the character nodes defined by the two dl objects are
 * the same.
 */
static rcb_merge_t compare_char_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type)
{
  int32 fEquivalent;
  DL_CHARS *ch1, *ch2;

  HQASSERT((test_type & MERGE_SPLIT) == 0, "should not be MERGE_SPLIT");
  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");
  HQASSERT(lobj2->opcode == RENDER_char, "bad opcode in lobj2");

  if ( lobj1->opcode != RENDER_char )
    return MERGE_NONE;

  ch1 = lobj1->dldata.text;
  ch2 = lobj2->dldata.text;

  /* ch2 is the new char listobject and, with object merging currently
     allowed, it should have been split up into individual listobjects for
     each character (compare_char_objects cannot merge listobjects with
     mutiple characters).  ch2 is already on the DL and may or may not contain
     multiple characters depending on the state of object-merging during its
     creation. */
  HQASSERT(ch2->nchars == 1,
    "listobject with multiple chars should be split up when merging is on");

  /* Cannot merge listobjects with multiple chars. */
  if ( ch1->nchars != 1 || ch2->nchars != 1 )
    return MERGE_NONE;

  /* Find out if the chars are identical or equivalent,
     taking no account of position */
  fEquivalent = (ch1->ch[0].form == ch2->ch[0].form ||
                 same_form_objects(ch1->ch[0].form, ch2->ch[0].form));


  /* To be an EXACT match, the characters must be equivalent and have the same
     position. For recombine there is only one character in the data */
  if ( (test_type & MERGE_EXACT) != 0 && fEquivalent &&
       ch1->ch[0].x == ch2->ch[0].x && ch1->ch[0].y == ch2->ch[0].y )
    return MERGE_EXACT;

  /* Characters cannot be trap matches if they are equivalent ie same size */
  if ( (test_type & MERGE_FUZZY) != 0 && !fEquivalent ) {
    SYSTEMVALUE dlt;
    OBJECT *o1, *o2;
    CHARCACHE *c1, *c2;

    c1 = (CHARCACHE *)ch1->ch[0].form;
    c2 = (CHARCACHE *)ch2->ch[0].form;
    HQASSERT(c1 != NULL, "c1 CHARCACHE should not be NULL");
    HQASSERT(c2 != NULL, "c2 CHARCACHE should not be NULL");

    if ( c1->type != FORMTYPE_CHARCACHE || c2->type != FORMTYPE_CHARCACHE )
      return MERGE_NONE;

    /* See if they are the same 'name' (OBJECT). */
    o1 = &(c1->glyphname);
    o2 = &(c2->glyphname);
    HQASSERT(oType(*o1) == ONAME, "o1 should be an ONAME OBJECT");
    HQASSERT(oType(*o2) == ONAME, "o2 should be an ONAME OBJECT");
    if ( oName(*o1) != oName(*o2) )
      return MERGE_NONE;

    /* See if they are at the same origin. This is done by adding the
     * x/y position that the character is blitted at to the side bearing
     * in the CHARCACHE. We allow some slop since the blit position is
     * rounded.
     * The error of 2.5 is to take account of the rounding effect of
     * converting real coordinates to device pixels. There are two rounding
     * effects; first that of the left side bearing (when character cached),
     * and secondly that of the current point (where character shown at).
     * Each of these can incurr an error of 0.5 in opposite directions for
     * each character, and so the accumulated error could be:
     *   2 * 2 * 0.5 == 2.
     * Hence the error term of 2.5 (the 0.5 being a little slop).
     */
#define RCB_FUZZY_POSITION_SLOP 2.5

    dlt = ( ch1->ch[0].x - c1->xbear) - ( ch2->ch[0].x - c2->xbear);
    if ( fabs(dlt) > RCB_FUZZY_POSITION_SLOP )
      return MERGE_NONE;

    dlt = ( ch1->ch[0].y - c1->ybear) - ( ch2->ch[0].y - c2->ybear);
    if ( fabs(dlt) > RCB_FUZZY_POSITION_SLOP )
      return MERGE_NONE;

    return MERGE_FUZZY;
  }

  return MERGE_NONE;
}

static rcb_merge_t compare_image_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                         rcb_merge_t test_type)
{
  /* tell me whether the images defined by the two LISTOBJECTs are
     the same so far as is reasonable */
  IMAGEOBJECT * image1, * image2;

  UNUSED_PARAM(int32, test_type);

  HQASSERT((test_type & MERGE_SPLIT) == 0, "Should not be MERGE_SPLIT");
  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");

  if (lobj1->opcode != RENDER_image)
    return MERGE_NONE;

  image1 = lobj1->dldata.image;
  image2 = lobj2->dldata.image;

  /* If the second image already has as many planes as we've seen so far
   * (plus the one we're currently working on) then the first image
   * obviously isn't suitable
   */
  if ( theINPlanes(image2) > rcbn_cseps())
    return MERGE_NONE;

  /* Don't recombine images with different expand formats */
  if (im_expandformat(image1->ime) != im_expandformat(image2->ime))
    return MERGE_NONE;

  /* we already know it is the right image type, so we check all of
   * the other bits. Note that the image depth is not checked since in
   * the case of 12 bit deep images, the output may go to 8/16 bit halftone.
   * in this case the depth can not be checked, so it is skipped */

  if (!bbox_equal(&image1->imsbbox, &image2->imsbbox) ||
      !bbox_equal(im_storebbox_original(image1->ims),
                  im_storebbox_original(image2->ims)) ||
      image1->optimize != image2->optimize )
    return MERGE_NONE;

  /** Compare bit depths of images. */
  /** \todo @@JJ FIXME. Revisit when 16 bit images are supported because 12 bit
   * images currently return 16.
   */
  if (im_storebpp(image1->ims) != im_storebpp(image2->ims))
    return MERGE_NONE;
  HQASSERT(im_expandbpp(image1->ime) == im_expandbpp(image2->ime),
           "Image expander bpp inconsistent with image store");

  /* Since we are limited to one trap match between images it is necessary to
     allow a one pixel error when comparing grid positions. This allows image
     planes with slight rounding differences to exact match and reduces the
     need for trap matches.
   */
  if (abs(image1->geometry.tx - image2->geometry.tx) <= 1 &&
      abs(image1->geometry.ty - image2->geometry.ty) <= 1 &&
      abs(image1->geometry.tx + image1->geometry.wx -
          image2->geometry.tx - image2->geometry.wx) <= 1 &&
      abs(image1->geometry.ty + image1->geometry.wy -
          image2->geometry.ty - image2->geometry.wy) <= 1 &&
      abs(image1->geometry.tx + image1->geometry.hx -
          image2->geometry.tx - image2->geometry.hx) <= 1 &&
      abs(image1->geometry.ty + image1->geometry.hy -
          image2->geometry.ty - image2->geometry.hy) <= 1)
    return MERGE_EXACT;

  return MERGE_NONE;
}

static rcb_merge_t compare_mask_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type)
{
  /* tell me whether the imagemask defined by the two LISTOBJECTs are
     the same so far as is reasonable */
  IMAGEOBJECT * image1, * image2;

  UNUSED_PARAM(int32, test_type);

  HQASSERT((test_type & MERGE_SPLIT) == 0, "Should not be MERGE_SPLIT");
  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");

  if (lobj1->opcode != RENDER_mask)
    return MERGE_NONE;

  image1 = lobj1->dldata.image;
  image2 = lobj2->dldata.image;

  /* we already know it is the right image type */

  if (!bbox_equal(&image1->imsbbox, &image2->imsbbox) ||
      image1->optimize != image2->optimize )
    return MERGE_NONE;

  if (theIAdler32(image1) == theIAdler32(image2) &&
      abs(image1->geometry.tx - image2->geometry.tx) <= 1 &&
      abs(image1->geometry.ty - image2->geometry.ty) <= 1 &&
      abs(image1->geometry.tx + image1->geometry.wx -
          image2->geometry.tx - image2->geometry.wx) <= 1 &&
      abs(image1->geometry.ty + image1->geometry.wy -
          image2->geometry.ty - image2->geometry.wy) <= 1 &&
      abs(image1->geometry.tx + image1->geometry.hx -
          image2->geometry.tx - image2->geometry.hx) <= 1 &&
      abs(image1->geometry.ty + image1->geometry.hy -
          image2->geometry.ty - image2->geometry.hy) <= 1)
    return MERGE_EXACT;

  return MERGE_NONE;
}

static rcb_merge_t compare_fill_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                        rcb_merge_t test_type)
{
  NFILLOBJECT *nfill1;
  NFILLOBJECT *nfill2;

  /* Tell me whether the fill nodes defined by the two LISTOBJECTs are
   * the same.
   */

  HQASSERT((test_type & MERGE_SPLIT) == 0, "Should not be MERGE_SPLIT");
  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");
  HQASSERT(lobj2->opcode == RENDER_fill, "bad opcode in lobj2");
  if ( lobj1->opcode != RENDER_fill )
    return MERGE_NONE;

  nfill1 = lobj1->dldata.nfill;
  nfill2 = lobj2->dldata.nfill;
  HQASSERT(nfill1 != NULL, "nfill1 NFILLOBJECT should not be NULL");
  HQASSERT(nfill2 != NULL, "nfill2 NFILLOBJECT should not be NULL");

  if ( (test_type & MERGE_EXACT) != 0 )
    if ( same_nfill_objects(nfill1, nfill2) )
      return MERGE_EXACT;

  /* If the exact match code fails, see if we're to find a fuzzy match */
  if ( (test_type & MERGE_FUZZY) != 0 ) {
    /* If we've collected some recombine trap info, then see if these are trap
     * matches. That is that the two objects are a natural trap of each other.
     * e.g. one is a larger rectangle than the other.
     */
    if ( nfill1->rcbtrap != NULL && nfill2->rcbtrap != NULL ) {
      if ( rcbt_comparetrap(nfill1->rcbtrap, nfill2->rcbtrap,
           FALSE /* disallow donuts */, TRUE /* do center check */) )
        return MERGE_FUZZY;
    }
  }

  return MERGE_NONE;
}

static void rcb_debug(VIGNETTEOBJECT *v1, VIGNETTEOBJECT *v2, Bool split,
                      char *reason)
{
#if defined(ASSERT_BUILD)
  static int32 vignette_split_count = 0;
  static int32 vignette_not_split_count = 0;
#else
  UNUSED_PARAM(VIGNETTEOBJECT *, v1);
  UNUSED_PARAM(VIGNETTEOBJECT *, v2);
  UNUSED_PARAM(Bool, split);
  UNUSED_PARAM(char *, reason);
#endif

  if ( split )
    HQTRACE(debug_recombine, ("splitting (%d) vignette when matching "
            "%s(%d) to %s(%d)", ++vignette_split_count,
            v1 ? "vignette" : "fill", v2 ? vig_len(v1) : 0,
            v2 ? "vignette" : "fill", v2 ? vig_len(v2) : 0));
  else
    HQTRACE(debug_recombine, ("NOT splitting (%d) vignette when matching "
            "%s(%d) to %s(%d) due to %s", ++vignette_not_split_count,
            v1 ? "vignette" : "fill", v1 ? vig_len(v1) : 0,
            v2 ? "vignette" : "fill", v2 ? vig_len(v2) : 0, reason));
}

rcb_merge_t compare_vignette_splitter(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                      rcb_merge_t test_type)
{
  DL_STATE *page = CoreContext.page;
  uint32 ret_type;
  DLREF *dlobj1, *dlobj2;
  VIGNETTEOBJECT *vigobj1 = NULL, *vigobj2 = NULL;
  Bool firstOrLast1, firstOrLast2;

  HQASSERT((test_type & MERGE_SPLIT) != 0, "Should include MERGE_SPLIT");
  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");
  HQASSERT(lobj1 != lobj2, "comparing eq lobj");

  if ( lobj1->opcode == RENDER_vignette ) {
    vigobj1 = lobj1->dldata.vignette;
    HQASSERT(vigobj1 && vig_dlhead(lobj1), "corrupt VIGNETTEOBJECT");
    if ( vigobj1->style != VDS_Adjacent )
      return MERGE_NONE;
  }
  else if ( lobj1->opcode == RENDER_fill ) {
    if ( (lobj1->marker & MARKER_VN_VNCANDIDATE) == 0 )
      return MERGE_NONE;
  }
  else
    return MERGE_NONE;

  if ( lobj2->opcode == RENDER_vignette ) {
    vigobj2 = lobj2->dldata.vignette;
    HQASSERT(vigobj2 && vig_dlhead(lobj2), "corrupt VIGNETTEOBJECT");
    if ( vigobj2->style != VDS_Adjacent )
      return MERGE_NONE;
  }
  else if ( lobj2->opcode == RENDER_fill ) {
    if ( (lobj2->marker & MARKER_VN_VNCANDIDATE) == 0 )
      return MERGE_NONE;
  }
  else
    return MERGE_NONE;

  if ( vigobj1 == NULL && vigobj2 == NULL ) /* Two fills */
    return MERGE_NONE;

  ret_type = MERGE_NONE;

  firstOrLast1 = TRUE;
  dlobj1 = (vigobj1 ? vig_dlhead(lobj1) : NULL);
  do {
    LISTOBJECT *tlobj1 = (vigobj1 ? dlref_lobj(dlobj1) : lobj1);

    HQASSERT(tlobj1, "missing link");
    HQASSERT(tlobj1->opcode == RENDER_fill, "Unknown sub-object");

    firstOrLast2 = TRUE;
    dlobj2 = (vigobj2 ? vig_dlhead(lobj2) : NULL);
    do {
      LISTOBJECT *tlobj2 = (vigobj2 ? dlref_lobj(dlobj2) : lobj2);

      HQASSERT(tlobj2, "missing link");
      HQASSERT(tlobj2->opcode == RENDER_fill, "Unknown sub-object");

      if ( tlobj1->opcode == tlobj2->opcode ) {

        if ( compare_fill_objects(tlobj1, tlobj2, MERGE_EXACT|MERGE_FUZZY) !=
                                                  MERGE_NONE ) {
          /* Case when an overprinted vignette ends in a white object.  If the
           * white object (either the first or last vignette sub object) was
           * set to overprint (by the application generating the PS/PDF) the
           * object will be dropped (at least if OverprintWhite==TRUE).
           * To avoid dropping the object the application sets the white
           * object to knockout.  We recognise this case and merge these extra
           * colorants for the white object into the partialcolor dl color.
           * For example,
           *
           * C 1 .8 .6 .4 .2 0  vignette colors
           * M 1 .8 .6 .4 .2 0  vignette colors
           * Y              (0) partial  colors
           * K              (0) "        "
           *
           * In the above example if we treat this as a vignette we will paint
           * the vignette in Cyan and Magenta only (ignoring the extra
           * colorants in the white object).  If the vignette is split
           * (because we think it is a color bar) then the partial colors are
           * merged into the white object.
           */
          if ( test_type == (MERGE_SPLIT|MERGE_FUZZY)) {
            if ( ((tlobj1->marker & MARKER_VN_WHITEOBJECT) != 0) &&
                 ((tlobj2->marker & MARKER_VN_WHITEOBJECT) != 0) &&
                (((vigobj1 && !vigobj2) && firstOrLast1 &&
                  (vigobj1->colormonotonic != VDC_Neutral)) ||
                 ((vigobj2 && !vigobj1) && firstOrLast2 &&
                  ( vigobj2->colormonotonic != VDC_Neutral))) ) {
              int32 overprintvignette;

              HQTRACE(debug_recombine, ("possible overprinted vignette"));

              /* If new (second) object is matching, check remainder of
               * components?
               */
              overprintvignette = TRUE;
              if ( vigobj2 ) {
                uint8 saved_opcode;
                DLREF *vdlobj;

                /* New object is a vignette; see if anything else in it
                 * matches. Must stop it matching with lobj1, so nuke that
                 * temporarily.
                 */
                saved_opcode = lobj1->opcode;
                lobj1->opcode = RENDER_void;
                for ( vdlobj = vig_dlhead(lobj2); vdlobj;
                      vdlobj = dlref_next(vdlobj) ) {
                  if ( vdlobj != dlobj2 ) {
                    uint8 tbasefunc;
                    LISTOBJECT *lobj = dlref_lobj(vdlobj);
                    HQASSERT(lobj, "missing link");
                    tbasefunc = lobj->opcode;
                    HQASSERT(tbasefunc == RENDER_fill,
                             "object has unknown sub-object");
                    if ( merge_dl_objects(page, lobj, compare_fill_objects,
                                          MERGE_CHECK|MERGE_EXACT|MERGE_FUZZY)
                         == MERGE_CHECK ) {

                      overprintvignette = FALSE;
                      break;
                    }
                  }
                }
                /* Reset lobj1 to what it should be. */
                lobj1->opcode = saved_opcode;
              }
              if ( overprintvignette ) {
                LISTOBJECT *vig_lobj, *fill_lobj;
                VIGNETTEOBJECT *vigobj;

                rcb_debug(vigobj1, vigobj2, FALSE, "overprint");

                if ( vigobj1 ) {
                  vig_lobj  = lobj1;
                  vigobj    = vigobj1;
                  fill_lobj = lobj2;
                } else {
                  vig_lobj  = lobj2;
                  vigobj    = vigobj2;
                  fill_lobj = lobj1;
                }

                /* Check merging would not break the z-order */
                if ( !rcb_restitch_check(page, vig_lobj, fill_lobj,
                                         vig_lobj->bbox.y1, vig_lobj->bbox.y2,
                                         &overprintvignette) )
                  return MERGE_ERROR;

                if ( overprintvignette ) {
                  /* If we've hit this special case of an overprinted vignette
                   * where we've found the single end overprint, then we're
                   * pretty sure it's a vignette. If it's not, then it will
                   * get split up anyway, so no harm in promoting it.
                   */
                  HQTRACE( debug_vignette && (vigobj->confidence == VDC_Low),
                           ("promoted vignette to VDC_High"));
                  vigobj->confidence = VDC_High;

                  /* Move vignette & knockout rect/fill next to each other */
                  if ( !rcb_restitch_update(page, vig_lobj, fill_lobj,
                                            vig_lobj->bbox.y1, vig_lobj->bbox.y2) )
                    return MERGE_ERROR;

                  /* Merge the extra colorants from the knocked out white
                   * object into partialcolors only */
                  if ( vigobj->partialcolors == NULL ) {
                    if ( !dl_copy(page->dlc_context, &vigobj->partialcolors,
                                  &fill_lobj->p_ncolor) )
                      return MERGE_ERROR;
                  } else {
                    if ( !dl_merge(page->dlc_context, &vigobj->partialcolors,
                                   &fill_lobj->p_ncolor) )
                      return MERGE_ERROR;
                  }
                  dl_to_none(page->dlc_context, &fill_lobj->p_ncolor);
                  return MERGE_DONE;
                }
              }
            }
          }
          /* Before committing to a split, check that we haven't got a
           * knockout (fill) for a vignette.
           */
          if ( ret_type != MERGE_SPLIT ) {
            if (( vigobj1 && !vigobj2 &&
                  rcbn_compare_vignette_knockout(lobj1, tlobj2)) ||
                ( !vigobj1 && vigobj2 &&
                  rcbn_compare_vignette_knockout(lobj2, tlobj1)) )
              rcb_debug(vigobj1, vigobj2, FALSE, "knockout");
            else
              ret_type = MERGE_SPLIT;
          }
        }
      }
      if ( dlobj2 ) {
        dlobj2 = dlref_next(dlobj2);
        if ( dlobj2 )
          firstOrLast2 = (dlref_next(dlobj2) == NULL);
      }
    } while ( dlobj2 );
    if ( dlobj1 ) {
      dlobj1 = dlref_next(dlobj1);
      if ( dlobj1 )
        firstOrLast1 = (dlref_next(dlobj1) == NULL);
    }
  } while ( dlobj1 );

  if ( ret_type == MERGE_SPLIT && vigobj1 && vigobj2 &&
       ( ( vigobj1->colormonotonic == VDC_Neutral &&
           vigobj2->colormonotonic != VDC_Neutral) ||
         ( vigobj1->colormonotonic != VDC_Neutral &&
           vigobj2->colormonotonic == VDC_Neutral)) ) {
    /* Suspect the two vignettes of being color bars.
       One of the vignettes may actually be a knockout for the other vignette.
       Compare vignettes again but allow neutral vs non-neutral matches */
    rcbv_compare_t compareinfo;
    rcb_merge_t ret_type_kos = rcbv_compare_vignettes(&compareinfo,
                                                      lobj1, lobj2,
                                                      MERGE_EXACT|MERGE_FUZZY,
                                                      TRUE);
    if ( (ret_type_kos & (MERGE_EXACT | MERGE_FUZZY)) != 0 )
      /* Vignettes will be merged later as vignette and knockout */
      return MERGE_NONE;
  }

  if ( ret_type == MERGE_SPLIT )
    rcb_debug(vigobj1, vigobj2, TRUE, "");

  return ( ret_type );
}

/**
 * For object types which should never exist or never be matched.
 */
static rcb_merge_t compare_invalid_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                           rcb_merge_t test_type)
{
  UNUSED_PARAM(LISTOBJECT *, lobj1);
  UNUSED_PARAM(LISTOBJECT *, lobj2);
  UNUSED_PARAM(int32, test_type);
  HQFAIL("Attempting to match an object type forbidden in recombine");
  return MERGE_NONE;
}

/**
 * For object types which can exist, but never match.
 */
static rcb_merge_t compare_reject_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                          rcb_merge_t test_type)
{
  UNUSED_PARAM(LISTOBJECT *, lobj1);
  UNUSED_PARAM(LISTOBJECT *, lobj2);
  UNUSED_PARAM(int32, test_type);
  return MERGE_NONE;
}

static rcb_merge_t compare_shfill_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                          rcb_merge_t test_type)
{
  SHADINGinfo *sinfo1, *sinfo2;
  OMATRIX *m1, *m2;
  DLREF *link1, *link2;
  rcb_merge_t result = MERGE_EXACT|MERGE_FUZZY;

  HQASSERT((test_type & MERGE_SPLIT) == 0, "Should not be MERGE_SPLIT");

  /* do this check just to remove a release build compiler warning */
  if ( (test_type & MERGE_SPLIT) != 0 )
    return MERGE_ERROR;

  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL, "lobj1 null");
  HQASSERT(lobj2 != NULL, "lobj2 null");

  if ( lobj1->opcode != RENDER_shfill || lobj2->opcode != RENDER_shfill )
    return MERGE_NONE;

  sinfo1 = lobj1->dldata.shade->info;
  sinfo2 = lobj2->dldata.shade->info;

  HQASSERT(sinfo1 != NULL && sinfo2 != NULL, "Shading info missing");

  /* For now, we totally ignore the information in the SHADINGOBJECT. It
     defines the rendering characteristics of the shfill rather than the
     inherent geometry. */
  if ( sinfo1->type != sinfo2->type ||
       sinfo1->antialias != sinfo2->antialias ||
       sinfo1->inpattern2 != sinfo2->inpattern2 ||
       (sinfo1->nfuncs != 0) != (sinfo2->nfuncs != 0) ||
       fabs(sinfo1->bbox.x1 - sinfo2->bbox.x1) > EPSILON ||
       fabs(sinfo1->bbox.y1 - sinfo2->bbox.y1) > EPSILON ||
       fabs(sinfo1->bbox.x2 - sinfo2->bbox.x2) > EPSILON ||
       fabs(sinfo1->bbox.y2 - sinfo2->bbox.y2) > EPSILON )
    return MERGE_NONE;

  /* For now, matrix components must match. Later we may allow slop and
     convert one matrix to another. */
  m1 = (OMATRIX *)(sinfo1 + 1);
  m2 = (OMATRIX *)(sinfo2 + 1);
  if ( !MATRIX_EQ(m1, m2) )
    return MERGE_NONE;

  HQASSERT(sinfo1 != NULL && sinfo2 != NULL, "Shading info missing");

  /* Note that number of components, functions, spotno, coercion, base_index
     etc. do not make any difference here, but are used for merging. */

  link1 = hdlOrderList(lobj1->dldata.shade->hdl);
  link2 = hdlOrderList(lobj2->dldata.shade->hdl);
  HQASSERT(link1 != NULL, "link1 null");
  HQASSERT(link2 != NULL, "link2 null");

  while ( link1 != NULL && link2 != NULL ) {
    LISTOBJECT *tlobj1, *tlobj2;
    COMPARE_LOBJS compare_lobjs;

    tlobj1 = dlref_lobj(link1);
    tlobj2 = dlref_lobj(link2);
    HQASSERT(tlobj1 != NULL, "tlobj1 null");
    HQASSERT(tlobj2 != NULL, "tlobj2 null");

    HQASSERT(tlobj1->opcode == RENDER_shfill_patch ||
             tlobj1->opcode == RENDER_fill,
             "bad sub-object type in shfill vignette 1");
    HQASSERT(tlobj2->opcode == RENDER_shfill_patch ||
             tlobj2->opcode == RENDER_fill,
             "bad sub-object type in shfill vignette 2");
    HQASSERT(tlobj1->opcode != RENDER_fill ||
             link1 == vig_dlhead(lobj1),
             "background not first object in shfill vignette 1");
    HQASSERT(tlobj2->opcode != RENDER_fill ||
             link2 == vig_dlhead(lobj2),
             "background not first object in shfill vignette 2");

    /* All patches and background objects must match. */
    if ( tlobj1->opcode != tlobj2->opcode )
      return MERGE_NONE;

    compare_lobjs = rcb_comparefn(tlobj1->opcode);

    result &= (*compare_lobjs)(tlobj1, tlobj2, rcb_compareop(tlobj1->opcode));

    if ( result == MERGE_NONE )
      return MERGE_NONE;

    link1 = dlref_next(link1);
    link2 = dlref_next(link2);
  }

  if ( link1 != link2 )
    return MERGE_NONE;

  return result;
}

static rcb_merge_t compare_patch_objects(LISTOBJECT *lobj1, LISTOBJECT *lobj2,
                                         rcb_merge_t test_type)
{
  rcbs_patch_t *patch1, *patch2;
  int32 type1, type2;

  HQASSERT((test_type & MERGE_SPLIT) == 0, "Should not be MERGE_SPLIT");

  /* do this check just to remove a release build compiler warning */
  if ( (test_type & MERGE_SPLIT) != 0 )
    return MERGE_ERROR;

  HQASSERT(rcbn_enabled(), "Not recombining separations");
  HQASSERT(!rcbn_first_separation(), "On first separation");
  HQASSERT(lobj1 != NULL && lobj2 != NULL, "NULL LISTOBJECT");
  HQASSERT(lobj1->opcode == RENDER_shfill_patch &&
           lobj2->opcode == RENDER_shfill_patch, "bad opcode");

  patch1 = lobj1->dldata.patch;
  patch2 = lobj2->dldata.patch;

  HQASSERT(patch1 != NULL && patch2 != NULL,
           "Patch info should be set for listobjects");

  type1 = rcbs_patch_type(patch1);
  type2 = rcbs_patch_type(patch2);

  if ( type1 != type2 )
    return MERGE_NONE;

  switch ( type1 ) {
  case 1: /* Function based */
    if ( rcbs_compare_function(patch1, patch2) )
      return MERGE_EXACT;
    break;
  case 2: /* Axial blend */
    if ( rcbs_compare_axial(patch1, patch2) )
      return MERGE_EXACT;
    break;
  case 3: /* Radial blend */
    if ( rcbs_compare_radial(patch1, patch2) )
      return MERGE_EXACT;
    break;
  case 4: case 5: /* Gouraud fills */
    if ( rcbs_compare_gouraud(patch1, patch2) )
      return MERGE_EXACT;
    break;
  case 6: case 7: /* Coons and Tensor patches */
    if ( rcbs_compare_tensor(patch1, patch2) )
      return MERGE_EXACT;
    break;
  default:
    HQFAIL("Invalid type in shfill recombine info");
  }

  return MERGE_NONE;
}

/* Log stripped */
