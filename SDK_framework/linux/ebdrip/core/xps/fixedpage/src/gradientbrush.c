/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:gradientbrush.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:gradientbrush.c,v 1.133.2.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload gradient brush callbacks.
 *
 * See gradientbrush_functions declaration at the end of this file for the
 * element callbacks this file implements.
 */

#include <float.h> /* FLT_EPSILON */

#include "core.h"

#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "graphics.h"
#include "gstate.h"
#include "dicthash.h"
#include "gschcms.h"
#include "gschead.h"   /* gsc_setcolordirect */
#include "gsc_icc.h"   /* gsc_geticcbasedinfo */
#include "hqmemcpy.h"  /* HqMemCpy */
#include "hqmemset.h"
#include "imagecontext.h"
#include "hqunicode.h"
#include "mmcompat.h"  /* mm_alloc_with_header etc.. */
#include "xml.h"
#include "xmltypeconv.h"
#include "objnamer.h"
#include "gu_ctm.h"   /* gs_modifyctm */
#include "swmemory.h" /* gs_cleargstates */
#include "constant.h" /* EPSILON */
#include "swcopyf.h"
#include "miscops.h" /* run_ps_string */
#include "pattern.h" /* gs_makepattern */
#include "pathops.h" /* bbox_transform */
#include "plotops.h"
#include "dlstate.h"    /* inputpage */
#include "clipops.h"

#include "xpspriv.h"
#include "xpsscan.h"
#include "xpsiccbased.h"
#include "fixedpagepriv.h"

/*=============================================================================
 * Utility functions
 *=============================================================================
 */
#define BRUSH_STATE_NAME "XPS Gradient Brush"

typedef struct GradientStop_s GradientStop;
struct GradientStop_s {
  USERVALUE offset;
  USERVALUE opacity;
  int32 n_colorants;  /* excluding alpha component */
  USERVALUE color[8];
  int32 colorspace ;
  xps_partname_t *color_profile_partname;
  GradientStop* next;
};


/** \brief Structure to contain Brush state.

    This structure will be split into multiple copies when brushes are
    separated into their own files. */
typedef struct xpsGradientBrushState_s {
  xpsCallbackStateBase base; /**< Superclass MUST be first element. */

  OMATRIX transform ; /**< Local Transform matrix. */

  int32 colortype ;   /**< Color chain used by this brush. */

  Bool radial;
  GradientStop* gradientstops;
  uint32 num_gradientstops;

  USERVALUE opacity ; /**< GradientBrush opacity attribute. */

  OBJECT_NAME_MEMBER
} xpsGradientBrushState ;

/* For converting GradientStop colorspaces */
enum { NO_CONVERSION = 0,
       CONVERT_TO_RGB,
       CONVERT_TO_BLEND,
       DUMMY_MAX_CONVERT_METHOD } ;


/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_brush_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsGradientBrushState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  *state_ptr = NULL;

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsGradientBrushState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_GRADIENTBRUSH )
    return FALSE ;

  VERIFY_OBJECT(state, BRUSH_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

static Bool gradient_functions_pair(xpsGradientBrushState *state, uint32 nsteps,
                                    Bool spreadmethod_set, xmlGIStr *spreadmethod,
                                    int32 nOutputColors,
                                    uint8** psbuf_ptr)
{
  int32 i;
  uint8 *psend = *psbuf_ptr;

  GradientStop *stop1 = state->gradientstops, *stop2 = stop1->next;

  /* The DataSource uses this - it should be true for now at least */
  HQASSERT( nOutputColors <= 8,
            "Too many output colors in gradient_functions_pair" );

  swcopyf(psend, (uint8*)
          "/Function <<\n"
          "  /FunctionType 0\n"
          "  /Domain [0 1]\n"
          "  /Range [ %d { 0 1 } repeat ]\n"
          "  /Order 1\n"
          "  /DataSource <0000000000000000FFFFFFFFFFFFFFFF> %d %d getinterval\n"
          "  /BitsPerSample 8\n"
          "  /Size [2]\n"
          "  /Decode [",
          nOutputColors,
          (8 - nOutputColors),
          (2 * nOutputColors));
  psend += strlen((const char *)psend) ;

  /* Fill in the Decode array */
  for ( i = 0; i < nOutputColors ; i++ ) {
    swcopyf(psend, (uint8*) "%f %f ", stop1->color[i], stop2->color[i]);
    psend += strlen((const char *)psend) ;
  }

  /* Finish the Decode array */
  swcopyf(psend, (uint8*) "]\n");
  psend += strlen((const char *)psend) ;

  if (spreadmethod_set &&
      (spreadmethod == XML_INTERN(Repeat) ||
       spreadmethod == XML_INTERN(Reflect))) {
    swcopyf(psend, (uint8*)
            "  /HqnSpreadMethod %s\n"
            "  /HqnSpreadFactor %d\n",
            spreadmethod == XML_INTERN(Repeat) ? "/Repeat" : "/Reflect",
            nsteps);
    psend += strlen((const char *)psend) ;
  }

  swcopyf(psend, (uint8*)
          ">>\n"
          "/HqnOpacity <<\n"
          "  /FunctionType 0\n"
          "  /Domain [0 1]\n"
          "  /Range [0 1]\n"
          "  /Decode [%f %f]\n"
          "  /Order 1\n"
          "  /DataSource <00FF>\n"
          "  /BitsPerSample 8\n"
          "  /Size [2]\n",
          stop1->opacity, stop2->opacity);

  psend += strlen((const char *)psend) ;

  if (spreadmethod_set &&
      (spreadmethod == XML_INTERN(Repeat) ||
       spreadmethod == XML_INTERN(Reflect))) {
    swcopyf(psend, (uint8*)
            "  /HqnSpreadMethod %s\n"
            "  /HqnSpreadFactor %d\n",
            spreadmethod == XML_INTERN(Repeat) ? "/Repeat" : "/Reflect",
            nsteps);
    psend += strlen((const char *)psend) ;
  }

  swcopyf(psend, (uint8*)
          ">>\n");
  psend += strlen((const char *)psend) ;

  *psbuf_ptr = psend ;

  return TRUE;
}


/* Set up the function dictionaries for the type 3 stitching functions.
 * The arrays involved, whose size depends on the number of gradientstops
 * have previously been defined in a dictionary,
 * (see setup_gradient_function_arrays), which is begun before this PS is run.
 * This avoids problems with very large numbers of gradientstops needing a
 * buffer longer than the max PS string length.
 */
static Bool gradient_functions_multiple(xpsGradientBrushState *state, uint32 nsteps,
                                        Bool spreadmethod_set, xmlGIStr *spreadmethod,
                                        int32 nOutputColors, uint8** psbuf_ptr)
{
  uint8 *psend = *psbuf_ptr;

  /* The DataSource uses this - it should be true for now at least */
  HQASSERT( nOutputColors <= 8,
            "Too many output colors in gradient_functions_multiple" );

  swcopyf(psend, (uint8*)
          "/Function <<\n"
          "  /FunctionType 3\n"
          "  /Domain [0 1]\n"
          "  /Range [ %d { 0 1 } repeat ]\n",
          nOutputColors);
  psend += strlen((const char *)psend) ;

  if (spreadmethod_set &&
      (spreadmethod == XML_INTERN(Repeat) ||
       spreadmethod == XML_INTERN(Reflect))) {
    swcopyf(psend, (uint8*)
            "  /HqnSpreadMethod %s\n"
            "  /HqnSpreadFactor %d\n",
            spreadmethod == XML_INTERN(Repeat) ? "/Repeat" : "/Reflect",
            nsteps);
    psend += strlen((const char *)psend) ;
  }

  /* The GradientBrushColorArray */
  swcopyf(psend, (uint8*)"  /Functions [ //GradientBrushColorArray\n") ;
  psend += strlen((const char *)psend) ;

  swcopyf(psend, (uint8*)
          "{\n"
          "<<\n"
          "  /Decode 3 -1 roll\n"
          "  /DataSource <0000000000000000FFFFFFFFFFFFFFFF> %d %d getinterval\n"
          "  /FunctionType 0\n"
          "  /Domain [0 1]\n"
          "  /Range [ %d { 0 1 } repeat ]\n"
          "  /Order 1\n"
          "  /BitsPerSample 8\n"
          "  /Size [2]\n"
          ">> } forall\n"
          "  ]\n",
          (8 - nOutputColors),
          (2 * nOutputColors),
          nOutputColors);
  psend += strlen((const char *)psend) ;


  /* The GradientBrushBoundsArray */
  swcopyf(psend, (uint8*)"  /Bounds //GradientBrushBoundsArray\n") ;
  psend += strlen((const char *) psend) ;

  swcopyf(psend, (uint8*)
          "  /Encode [ %d { 0 1 } repeat ]\n"
          ">>\n"
          "/HqnOpacity <<\n"
          "  /FunctionType 3\n"
          "  /Domain [0 1]\n"
          "  /Range [0 1]\n",
          state->num_gradientstops - 1);
  psend += strlen((const char *)psend) ;

  if (spreadmethod_set &&
      (spreadmethod == XML_INTERN(Repeat) ||
       spreadmethod == XML_INTERN(Reflect))) {
    swcopyf(psend, (uint8*)
            "  /HqnSpreadMethod %s\n"
            "  /HqnSpreadFactor %d\n",
            spreadmethod == XML_INTERN(Repeat) ? "/Repeat" : "/Reflect",
            nsteps);
    psend += strlen((const char *)psend) ;
  }

  /* The GradientBrushOpacityArray */
  swcopyf(psend, (uint8*)"  /Functions [ //GradientBrushOpacityArray\n");
  psend += strlen((const char *)psend) ;

  swcopyf(psend, (uint8*)
          "{ <<\n"
          "  /Decode 3 -1 roll\n"
          "  /DataSource <00FF>\n"
          "  /FunctionType 0\n"
          "  /Domain [0 1]\n"
          "  /Range [0 1]\n"
          "  /Order 1\n"
          "  /BitsPerSample 8\n"
          "  /Size [2]\n"
          ">> } forall\n"
          "  ]\n");

  psend += strlen((const char *)psend) ;

  /* The GradientBrushBoundsArray for the HqnOpacity function dict */
  swcopyf(psend, (uint8*)"  /Bounds //GradientBrushBoundsArray\n");
  psend += strlen((const char *)psend);

  swcopyf(psend, (uint8*)
          "  /Encode [ %d { 0 1 } repeat ] >>\n",
          state->num_gradientstops - 1);
  psend += strlen((const char *)psend) ;

  *psbuf_ptr = psend ;

  return TRUE ;
}


void gradientstop_free( GradientStop *stop)
{
  if (stop->color_profile_partname != NULL)
    xps_partname_free(&stop->color_profile_partname);

  mm_free(mm_xml_pool, stop, sizeof(GradientStop)) ;
}


static Bool gradientstop_new(xpsGradientBrushState *state,
                             xps_color_designator *color_designator,
                             USERVALUE offset, GradientStop **where)
{
  uint32 i;
  GradientStop* gradientstop;

  gradientstop = mm_alloc(mm_xml_pool, sizeof(GradientStop),
                          MM_ALLOC_CLASS_XPS_GRADIENTSTOP);
  if (gradientstop == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)gradientstop, (int)sizeof(GradientStop));

  gradientstop->opacity = color_designator->alpha;
  gradientstop->n_colorants = color_designator->n_colorants;
  gradientstop->colorspace = color_designator->colorspace;

  /* N.B. this simply takes over the color_profile_partname
   * from the color_designator rather than copying it.
   */
  if ( color_designator->color_profile_partname != NULL ) {
    gradientstop->color_profile_partname =
     color_designator->color_profile_partname;
    color_designator->color_profile_partname = NULL;
  }

  for ( i = 0; i < 8; i++ )
    gradientstop->color[i] = color_designator->color[i];

  gradientstop->offset = offset;

  /* Add to chain. */
  gradientstop->next = *where;
  *where = gradientstop;

  ++state->num_gradientstops;

  return TRUE;
}


static Bool gradientstop_clone(xpsGradientBrushState *state,
                               GradientStop *stop,
                               USERVALUE offset, GradientStop **where)
{
  uint32 i;
  GradientStop* gradientstop;

  gradientstop = mm_alloc(mm_xml_pool, sizeof(GradientStop),
                          MM_ALLOC_CLASS_XPS_GRADIENTSTOP);
  if (gradientstop == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)gradientstop, (int)sizeof(GradientStop));

  gradientstop->opacity = stop->opacity;
  gradientstop->n_colorants = stop->n_colorants;
  gradientstop->colorspace = stop->colorspace;

  if ( stop->color_profile_partname != NULL ) {
    /* Give this stop its own copy */
    if ( !xps_partname_copy( &gradientstop->color_profile_partname,
                             stop->color_profile_partname ))
      return FALSE;
  }

  for ( i = 0; i < 8; i++ )
    gradientstop->color[i] = stop->color[i];

  gradientstop->offset = offset;

  /* Add to chain. */
  gradientstop->next = *where;
  *where = gradientstop;

  ++state->num_gradientstops;

  return TRUE;
}


static Bool gradientstops_clean(xpsGradientBrushState *state, Bool pad)
{
  uint32 i;
  GradientStop **where, *stop, *prev = NULL ;

  HQASSERT(state->num_gradientstops >= 1,
           "Should have already checked there is a gradient stop");

  where = &state->gradientstops ;

  /* We need to:
     1) Remove any stops with offsets less than 0.0 or greater than 1.0.
     2) Create new stops for 0.0 and 1.0, if not present.
     3) Remove stops with the same offset, except for the first and last with
        an offset.
  */
  while ( (stop = *where) != NULL ) {
    GradientStop *next = stop->next ;

    HQASSERT(next == NULL || next->offset >= stop->offset,
             "Gradient stops are not pre-sorted") ;

    if ( stop && prev && next &&
         stop->opacity == prev->opacity &&
         stop->opacity == next->opacity &&
         stop->colorspace == prev->colorspace &&
         stop->colorspace == next->colorspace &&
         stop->color_profile_partname == prev->color_profile_partname &&
         stop->color_profile_partname == next->color_profile_partname ) {

      for ( i = 0; i < 8; i++ ) {
        if ( stop->color[i] != prev->color[i] || stop->color[i] != next->color[i] )
         break;
      }

      if ( i == 8 ) {
        *where = next ;
        gradientstop_free(stop) ;
        state->num_gradientstops -= 1 ;
        continue ;
      }
    }

    if ( stop->offset < 0.0f ) {
      if ( next != NULL ) {
        if ( next->offset <= 0.0f ) {
          /* This stop is redundant. The next stop is either zero or closer
             to zero, and so will be used to create the zero stop if it
             doesn't exist. */
          *where = next ;
          gradientstop_free(stop) ;
          state->num_gradientstops -= 1 ;
          continue ;
        }
      } else {
        /* We only appear to have one stop, its value is less than 0.0. We're
           supposed to create stops at 0.0 and 1.0 with the nearest stop
           values, so I suppose this is it. */
        stop->offset = 0.0f ;
        return gradientstop_clone(state, stop, 1.0f, &stop->next) ;
      }
    } else if ( stop->offset > 1.0f ) {
      if ( prev != NULL ) {
        if ( prev->offset >= 1.0f ) {
          /* This stop is redundant. The previous stop is at least one, so we
             can remove this stop. */
          *where = next ;
          gradientstop_free(stop) ;
          state->num_gradientstops -= 1 ;
          continue ;
        }
      } else {
        /* The first stop appears to have a value greater than 1.0. We're
           supposed to create stops at 0.0 and 1.0 with the nearest stop
           values, so I suppose this is it. */
        stop->offset = 0.0f ;
        if ( !gradientstop_clone(state, stop, 1.0f, &stop->next) )
          return FALSE ;
      }
    } else {
      /* Stop is between 0.0 and 1.0. */
      if ( prev == NULL && stop->offset > 0.0f ) {
        /* No valid 0.0f stop. Create one with the same values as this. */
        if ( !gradientstop_clone(state, stop, 0.0f, where) )
          return FALSE ;

        prev = *where ;
        where = &prev->next ;
      }

      if ( next == NULL && stop->offset < 1.0f ) {
        /* No valid 1.0f stop. Create one with the same values as this. */
        if ( !gradientstop_clone(state, stop, 1.0f, &stop->next) )
          return FALSE ;

        next = stop->next ;
      }

      if ( next != NULL && next->offset == stop->offset &&
           prev != NULL && prev->offset == stop->offset ) {
        /* Remove the stop if the previous and next are at the same offset. */
        *where = next ;
        gradientstop_free(stop) ;
        state->num_gradientstops -= 1 ;
        continue ;
      }

      if ( !pad ) {
        /** \todo @@@ TODO FIXME Feedback item raised on 12/12/2005

          If doing reflect or repeat, and there are duplicate offsets for
          0.0 or 1.0, remove the outer duplicates otherwise the outer
          offsets will result in a noticeable thin band in the output.
          Duplicate offset between 0.0 and 1.0 are not a problem.  It is not
          made clear in the spec that this is the right thing to do, but
          this behaviour matches the viewer. */
        if ( ( stop->offset == 0.0 && next != NULL && next->offset == 0.0 ) ||
             ( stop->offset == 1.0 && prev != NULL && prev->offset == 1.0 ) ) {
          /* Remove the outer duplicate offset to avoid a thin band. */
          *where = next ;
          gradientstop_free(stop) ;
          state->num_gradientstops -= 1 ;
          continue ;
        }
      }

      /** \todo @@@ TODO FIXME

        If the first two stops are both at 0.0, the pad color would be taken
        from the second function, instead of from the first function,
        because the shading code always does fn_evaluate UPWARDS on the
        first extend.  The proper solution would be to pass the information
        that we're in an XPS context through into the shading code so it can
        do an fn_evaluate DOWNWARDS, but with the current PS pattern and
        shfill setup this is difficult.
          As a temporary bodge, I push out the second stop slightly to
        ensure 0.0 is evaluated in the first function - provided changing
        the second stop's offset doesn't interfere with the following stop.
        THIS BODGE DOES NOT WORK IN ALL CASES!!
      */
      if ( prev == NULL && stop->offset == 0.0 &&
           next != NULL && next->offset == 0.0 &&
           ( next->next == NULL || FLT_EPSILON < next->next->offset ) ) {
        next->offset = FLT_EPSILON ;
      }
    }

    prev = stop ;
    where = &stop->next ;
  }

  return TRUE ;
}


/* Set up the arrays for the type 3 stitching functions used when there are
 * multiple gradientstops.  Define them in the dictionary on the top of the
 * stack and leave the dictionary there again.  Since this is only building
 * arrays, run_ps_string can be called when necessary, (unlike part-way
 * through creating the PaintProc), avoiding the necessity for buffers which
 * are longer than the maximum PS string length.
 */
static Bool setup_gradient_function_arrays( xpsGradientBrushState *state,
                                            int32 nOutputColors, uint8* psbuf,
                                            uint8** psbuf_ptr )
{
  int32 i;
  uint8 *psend = *psbuf_ptr;
  GradientStop *stop1, *stop2;

/* don't yet use last (radialgradient) FIXEDBUFLEN bytes of a max sized buffer */
#define BUFLIMIT (65535 - 2500)

  /* The DataSource uses this - it should be true for now at least */
  HQASSERT( nOutputColors <= 8,
            "Too many output colors in setup_gradient_function_arrays" );

  /* There should be a dictionary on the stack */
  HQASSERT( oType( *theTop( operandstack)) == ODICTIONARY,
            "Expected dictionary in setup_gradient_function_arrays" );

  /* Begin the dictionary */
  swcopyf(psend, (uint8*) "begin\n");
  psend += strlen((const char*)psend);

  /* Start a GradientBrushColorArray */
  swcopyf(psend, (uint8*) "/GradientBrushColorArray [\n");
  psend += strlen((const char *)psend);


  for (stop1 = state->gradientstops, stop2 = stop1->next;
       stop2;
       stop1 = stop2, stop2 = stop2->next) {

    /* Start a decode array */
    swcopyf(psend, (uint8*) "[ ");
    psend += strlen((const char *)psend);

    /* Fill in the array */
    for ( i = 0; i < nOutputColors ; i++ ) {
      swcopyf(psend, (uint8*) "%f %f ", stop1->color[i], stop2->color[i]);
      psend += strlen((const char *)psend);
    }

    /* Finish the array */
    swcopyf(psend, (uint8*) "]\n");
    psend += strlen((const char *)psend);

    /* If we are in danger of exceeding the PS string limit run what we have
     * so far, and reuse the buffer.
     */
    if (psend - psbuf > BUFLIMIT) {
      if (!run_ps_string((uint8*)psbuf))
        return FALSE;

      psend = psbuf;
    }
  }

  /* Finish and define the GradientBrushColorArray */
  swcopyf(psend, (uint8*) "] def\n");
  psend += strlen((const char *)psend);


  /* Start a GradientBrushBoundsArray */
  swcopyf(psend, (uint8*) "/GradientBrushBoundsArray [\n");
  psend += strlen((const char *)psend);

  for (stop1 = state->gradientstops, stop2 = stop1->next;
       stop2 && stop2->next;
       stop1 = stop2, stop2 = stop2->next) {
    swcopyf(psend, (uint8*)"%f\n", stop2->offset);
    psend += strlen((const char *)psend);

    /* If necessary run what we have so far and restart the buffer */
    if (psend - psbuf > BUFLIMIT) {
      if (!run_ps_string((uint8*)psbuf))
        return FALSE;

      psend = psbuf;
    }
  }

  /* Finish and define the GradientBrushBoundsArray */
  swcopyf(psend, (uint8*) "] def\n");
  psend += strlen((const char *)psend);


  /* Start a GradientBrushOpacityArray */
  swcopyf(psend, (uint8*) "/GradientBrushOpacityArray [\n");
  psend += strlen((const char *)psend);

  for (stop1 = state->gradientstops, stop2 = stop1->next;
       stop2;
       stop1 = stop2, stop2 = stop2->next) {

    swcopyf(psend, (uint8*)
            "[%f %f]\n",
            stop1->opacity, stop2->opacity);
    psend += strlen((const char *)psend);

    /* If necessary run what we have so far and restart the buffer */
    if (psend - psbuf > BUFLIMIT) {
      if (!run_ps_string((uint8*)psbuf))
        return FALSE;

      psend = psbuf;
    }
  }

  /* Finish and define the GradientBrushOpacityArray */
  swcopyf(psend, (uint8*) "] def\n");
  psend += strlen((const char *)psend);


  /* Ensure the dict is left on the stack and ended */
  swcopyf(psend, (uint8*) "currentdict end\n" );
  psend += strlen((const char *)psend);

  *psbuf_ptr = psend;
  return TRUE;

#undef BUFLIMIT
}


/* Get hold of the appropriate blend space and rendering intent
 * and push a dictionary defining them as the GradientBrushBlendSpace
 * and GradientBrushRI.
 */
static Bool setup_gradientbrush_blendspace( xmlDocumentContext *xps_ctxt,
                                            uint32 method,
                                            COLORSPACE_ID oColorSpace )
{
  OBJECT dict = OBJECT_NOTVM_NOTHING;
  OBJECT *blendSpace;
  NAMECACHE *renderingIntentName;
  Bool sRGBmatch = FALSE, scRGBmatch = FALSE;

  HQASSERT( method < DUMMY_MAX_CONVERT_METHOD,
            "Unexpected method in setup_gradientbrush_blendspace" );

  /* Find out what the blendSpace should be */
  if ( method != CONVERT_TO_BLEND ) {
    HQASSERT( oColorSpace == SPACE_DeviceRGB,
              "Unexpected colorspace in setup_gradientbrush_blendspace" );
    blendSpace = get_xps_sRGB();

    /* Get the default rendering intent */
    oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
  }
  else {

    blendSpace = gsc_getBlend(gstateptr->colorInfo, oColorSpace, REPRO_TYPE_VIGNETTE );

    if ( !blendSpace )
      return detail_error_handler(CONFIGURATIONERROR,
                                  "GradientBrush blend space not found");

    /* See if the blendSpace is one of our default sRGB or scRGB profiles */
    if ( !xps_profile_is_sRGB( blendSpace, &sRGBmatch ))
      return FALSE;

    if ( !sRGBmatch ) {
      if ( !xps_profile_is_scRGB( blendSpace,&scRGBmatch ))
        return FALSE;
    }

    if ( sRGBmatch || scRGBmatch ) {
      /* Get the default rendering intent */
      oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
    }
    else {
      /* Get the rendering intent from the blendSpace */
      if (!gsc_get_iccbased_intent(gstateptr->colorInfo,
                                   blendSpace, &renderingIntentName))
        return FALSE;
      oName(nnewobj) = renderingIntentName;
    }
  }

  /* Create a dictionary containing the GradientBrushBlendSpace and
   * GradientBrushRI and push it onto the stack.
   */
  if ( !ps_dictionary(&dict, 2) ||
       !fast_insert_hash_name(&dict, NAME_GradientBrushBlendSpace, blendSpace) ||
       !fast_insert_hash_name(&dict, NAME_GradientBrushRI, &nnewobj) ||
       !push( &dict, &operandstack ) )
    return FALSE;

  return TRUE;
}


/* Convert the gradientstop using the method specified. */
static Bool gradientstop_convert_color( xmlDocumentContext *xps_ctxt,
                                        xpsGradientBrushState *state,
                                        GradientStop *stop,
                                        COLORSPACE_ID oColorSpace,
                                        int32 nOutputColors)
{
  int32 i;

  OBJECT *colorspace;
  NAMECACHE *renderingIntentName;

  HQASSERT( stop->colorspace != sRGB,
            "sRGB GradientStop in gradientstop_convert_color" );
  HQASSERT( stop->colorspace == scRGB ||
            stop->color_profile_partname != NULL,
            "Null color profile partname in gradientstop_convert_color" );

  if ( stop->colorspace == scRGB ) {

    /* Set up the scRGB colorSpace */
    if ( !set_xps_scRGB( state->colortype ))
      return FALSE;

    /* And the default rendering intent */
    oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
  }
  else {

    if ( !xps_icc_cache_define( stop->color_profile_partname,
                               stop->n_colorants,
                               &colorspace ) ||
        !push( colorspace, &operandstack ) ||
        !gsc_setcolorspace( gstateptr->colorInfo, &operandstack, state->colortype))
      return FALSE;

    /* Set the rendering intent from the profile */
    if ( !gsc_get_iccbased_intent(gstateptr->colorInfo,
                                  colorspace, &renderingIntentName))
      return FALSE;

    oName(nnewobj) = renderingIntentName;
  }

  if ( !gsc_setrenderingintent(gstateptr->colorInfo, &nnewobj))
    return FALSE;

  /* Set up the input colors */
  if ( !gsc_setcolordirect( gstateptr->colorInfo, state->colortype, &stop->color[0]))
    return FALSE;

  /* Invoke the chain and get the output colors
   * This will convert the colors as far as the devicespace_id, i.e. using the
   * appropriate /BlendRGB or /BlendCMYK etc, originally set up from e.g. the
   * TestConfig or PrintTicket, or using the default /BlendRGB set up in
   * gradientstops_convert.
   */

  if ( !gsc_invokeChainTransform( gstateptr->colorInfo,
                                  state->colortype,
                                  oColorSpace,
                                  FALSE,
                                  stop->color ))
    return FALSE;

  /* Blat the remaining gradientstop color values */
  for ( i = nOutputColors; i < 8; i++ )
    stop->color[i] = 0.0f;

  return TRUE;
}


/* Convert the gradientstops to all be in the same colorspace, either the
 * default /RGBBlend space or the PageBlendColorSpace, and pass back the
 * underlying device colorspace and number of output colors.
 */
static Bool gradientstops_convert(xmlDocumentContext *xps_ctxt,
                                  xpsGradientBrushState *state,
                                  uint32 *method,
                                  COLORSPACE_ID *oColorSpace,
                                  int32 *nOutputColors)
{
  corecontext_t *context = get_core_context_interp();
  GradientStop *stop;
  ps_context_t *pscontext = context->pscontext;
  Bool need_convert = FALSE;
  Bool rgb_found = FALSE;
  int32 dummyname_id;

  HQASSERT(state->num_gradientstops >= 1,
           "Should have already checked there is a gradient stop");

  *method = NO_CONVERSION;
  *oColorSpace = SPACE_DeviceRGB;
  *nOutputColors = 3;

  stop = state->gradientstops;

  /* First check if any conversion is needed, and if so to what
   * colorspace.  Until ColorInterpolationMode is implemented
   * if any stops are in s(c)RGB all stops are converted to sRGB.
   */
  while ( stop != NULL ) {
    if ( stop->colorspace == sRGB || stop->colorspace == scRGB ) {
      rgb_found = TRUE;
    }

    /* Strictly speaking an scRGB stop wouldn't need to be converted if there
     * was an scRGB blendspace.  However, leave this at least until
     * ColorInterpolationMode is implemented. This could happen anyway
     * with an extended color, and the profiles will not be used if equal.
     */
    if ( stop->colorspace != sRGB ) {
      /* Extended or scRGB color stop */
      need_convert = TRUE;
    }

    if ( need_convert && rgb_found ) {
      *method = CONVERT_TO_RGB;
      break;
    }

    stop = stop->next;
  }

  if (need_convert && (!rgb_found))
    *method = CONVERT_TO_BLEND;

  if ( *method == NO_CONVERSION )
    return TRUE;  /* nothing to do */

  /* Build color chains to convert colors, so need to gsave first */
  if ( !gsave_(pscontext))
    return FALSE;

  if ( *method == CONVERT_TO_RGB ) {
    /* Use the default sRGB blend space */
    if ( !set_xps_blendRGB() ) {
      goto tidyup;
    }
  }
  else {
    HQASSERT( *method == CONVERT_TO_BLEND,
              "Unexpected conversion method in gradientstops_convert" );

    /* Get the underlying space of the PageBlendColorSpace.  This should be the
     * same as the VirtualDeviceSpace, and an effective blend space (i.e. blend
     * or invertible intercept space) should already have been set up for it.
     */
    dlVirtualDeviceSpace(context->page, &dummyname_id, oColorSpace);

    /* We don't yet do device independent VirtualDeviceSpaces */
    HQASSERT( *oColorSpace == SPACE_DeviceGray ||
              *oColorSpace == SPACE_DeviceRGB ||
              *oColorSpace == SPACE_DeviceCMYK,
              "Unexpected VirtualDeviceSpace in gradientstops_convert" );

    if ( *oColorSpace == SPACE_DeviceGray )
      *nOutputColors = 1;
    else if ( *oColorSpace == SPACE_DeviceCMYK )
      *nOutputColors = 4;
  }

  /* Now convert the individual gradientstops
   * N.B. Until ColorInterpolationMode is implemented, although we may need to
   * convert an scRGB stop we will never convert an sRGB stop, because in the
   * presence of an sRGB stop (or indeed an scRGB stop) all stops would be
   * converted to sRGB so it is already in the right colorspace.
   */
  stop = state->gradientstops ;

  while ( stop != NULL ) {

    if ( stop->colorspace != sRGB ) {

      /* Need to convert this one */
      if ( !gradientstop_convert_color( xps_ctxt, state, stop,
                                        *oColorSpace, *nOutputColors )) {
        (void)detail_error_handler(UNDEFINED,
                                   "Unable to convert gradientstop color to blend space color");
        goto tidyup;
      }
    }

    stop = stop->next;
  }

  return grestore_(pscontext) ;

tidyup:
  (void)grestore_(pscontext);
  return FALSE;
}


void gradientstops_interpolate(xpsGradientBrushState *state)
{
  uint32 i;
  GradientStop **where, *stop, *prev = NULL ;

  HQASSERT(state->num_gradientstops >= 1,
           "Should have already checked there is a gradient stop");

  where = &state->gradientstops ;

  /* We need to:
     Create new stops for 0.0 and 1.0, if not present.
  */
  while ( (stop = *where) != NULL ) {
    GradientStop *next = stop->next ;

    HQASSERT(next == NULL || next->offset >= stop->offset,
             "Gradient stops are not pre-sorted") ;

    if ( stop->offset < 0.0f ) {
      HQASSERT( next != NULL, "There should be two GradientStops by now" );
      HQASSERT( next->offset > 0, "Too many sub-zero GradientStops" );

      /* There is no 0.0 stop, but we have two stops straddling it.
         Interpolate a stop at 0.0 (we re-use the current stop rather
         than create a new one). */
      {
        USERVALUE difference = 1.0f / (next->offset - stop->offset) ;
        USERVALUE w0 = -stop->offset * difference ;
        USERVALUE w1 = next->offset * difference ;

        stop->opacity = stop->opacity * w0 + next->opacity * w1 ;

        for ( i = 0; i < 8; i++ ) {
          stop->color[i] = stop->color[i] *w0 + next->color[i] * w1 ;
        }

        stop->offset = 0.0f ;
      }
    }
    else if ( stop->offset > 1.0f ) {
      HQASSERT( prev != NULL, "There should be two GradientStops by now" );
      HQASSERT( prev->offset < 1.0f, "Too many high offset GradientStops" );

      /* There is no 1.0 stop, but we have two stops straddling it.
         Interpolate a stop at 1.0 (we re-use the current stop rather
         than create a new one). */
      {
        USERVALUE difference = 1.0f / (stop->offset - prev->offset) ;
        USERVALUE w0 = (stop->offset - 1.0f) * difference ;
        USERVALUE w1 = (1.0f - prev->offset) * difference ;

        stop->opacity = stop->opacity * w0 + prev->opacity * w1 ;

        for ( i = 0; i < 8; i++ ) {
          stop->color[i] = stop->color[i] *w0 + prev->color[i] * w1 ;
        }

        stop->offset = 1.0f ;
      }
    }

    prev = stop ;
    where = &stop->next ;
  }
}


/* Calculate the number of repeats or reflections required to fill brushbox. */
/** \todo @@@ TODO FIXME More work to come here... */
static void spreadmethod_nsteps(SYSTEMVALUE center[2], SYSTEMVALUE dx, SYSTEMVALUE dy,
                                SYSTEMVALUE radius, sbbox_t* brushbox, uint32* nsteps_ptr)
{
  uint32 nsteps = *nsteps_ptr;
  SYSTEMVALUE dist_sqd = dx * dx + dy * dy;
#define RADIALGRADIENT_MAXSTEPS (200)

  if (fabs(dist_sqd - (radius * radius)) < (dist_sqd * 0.01)) {
    /* Gradient origin is close to the circumference of the ellipse.  There
       is the potential of making a massive number of gourauds if we're not
       careful. */
    nsteps = RADIALGRADIENT_MAXSTEPS;
  } else {
    if (dist_sqd > (radius * radius)) {
      /* Gradient origin is well outside the circumference of the ellipse.
         This is the cone case. */
      nsteps = RADIALGRADIENT_MAXSTEPS;
    } else {
      /* Gradient origin is well inside the circumference of the ellipse.
         This is the simple donut-like case. */
      for (nsteps = 1; nsteps < RADIALGRADIENT_MAXSTEPS; ++nsteps) {
        /* For each of the four corners of brushbox, determine the distance from
           the corner to the center (using Pythagoras).  If the distance is
           greater than radius, more steps are required (all values are squared
           to avoid a sqrt).  The fact that the center of the circle shifts for
           each step comlicates things slightly. */
        SYSTEMVALUE cx, cy, r2, dx2, dy2;
        cx = center[0] + dx * (SYSTEMVALUE)(nsteps - 1);
        cy = center[1] + dy * (SYSTEMVALUE)(nsteps - 1);
        r2 = radius * (SYSTEMVALUE)nsteps;
        r2 = r2 * r2;
        dx2 = cx - brushbox->x1;
        dx2 = dx2 * dx2;
        dy2 = cy - brushbox->y1;
        dy2 = dy2 * dy2;
        if (dx2 + dy2 > r2)
          continue; /* More steps required. */
        dy2 = cy - brushbox->y2;
        dy2 = dy2 * dy2;
        if (dx2 + dy2 > r2)
          continue; /* More steps required. */
        dx2 = cx - brushbox->x2;
        dx2 = dx2 * dx2;
        dy2 = cy - brushbox->y1;
        dy2 = dy2 * dy2;
        if (dx2 + dy2 > r2)
          continue; /* More steps required. */
        dy2 = cy - brushbox->y2;
        dy2 = dy2 * dy2;
        if (dx2 + dy2 <= r2)
          break; /* All four corners are within radius distance to the center. */
      }
    }
  }

  if (nsteps > RADIALGRADIENT_MAXSTEPS) {
    HQFAIL("Radial gradient with a spread has a suspiciously large number of steps");
    nsteps = RADIALGRADIENT_MAXSTEPS;
  }

  *nsteps_ptr = nsteps;
}


/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static
Bool xps_LinearGradientBrush_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;
#define FIXEDBUFLEN (2000)
#define STOPBUFLEN (100)
#define OUTDIMBUFLEN (20)
  uint8 *psbuf, *psend;
  int32 psbufsize;
  sbbox_t brushbox;
  OMATRIX inverse_ctm;
  Bool success = FALSE;
  OBJECT pattern_dict = OBJECT_NOTVM_NOTHING,
    pattern_name = OBJECT_NOTVM_NAME(NAME_Pattern, LITERAL) ;
  int32 tilingtype;
  Bool do_reflection = FALSE;
  SYSTEMVALUE angle;
  COLORSPACE_ID oColorSpace;
  int32 nOutputColors;
  uint32 method;

  static xmlGIStr *spreadmethod, *mappingmode, *colorinterpolation ;
  static SYSTEMVALUE startpoint[2], endpoint[2] ;
  static Bool spreadmethod_set, dummy;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(ColorInterpolationMode), NULL, &dummy, xps_convert_ST_ClrIntMode, &colorinterpolation},
    { XML_INTERN(MappingMode), NULL, NULL, xps_convert_ST_MappingMode, &mappingmode},
    { XML_INTERN(SpreadMethod), NULL, &spreadmethod_set, xps_convert_ST_SpreadMethod, &spreadmethod},
    { XML_INTERN(StartPoint), NULL, NULL, xps_convert_ST_Point, &startpoint},
    { XML_INTERN(EndPoint), NULL, NULL, xps_convert_ST_Point, &endpoint},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  /* Do not consume all attributes */
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE))
    return error_handler(UNDEFINED) ;

  /* If the startpoint and endpoint coincide, we cannot pad the gradient, so
     treat it as transparent. */
  if ( startpoint[0] == endpoint[0] && startpoint[1] == endpoint[1] ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  /* Non-invertible matrices are treated as if a transparent brush were used.
     We make the HUGE assumption that if transform and the CTM are invertible,
     then the concatenation of them will be invertible. */
  if ( !matrix_inverse(&thegsPageCTM(*gstateptr), &inverse_ctm) ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  bbox_transform(&thegsPageClip(*gstateptr).rbounds, &brushbox, &inverse_ctm) ;

  /* Find the angle of the linear gradient, relative to the x-axis. */
  angle = atan2(endpoint[1] - startpoint[1], endpoint[0] - startpoint[0]);

  if ( angle > EPSILON || angle < -EPSILON ) {
    /* Rotate the linear gradient brush to make it colinear with the x-axis. */

    OMATRIX adjust, adjust_tmp;
    SYSTEMVALUE cos_angle = cos(angle), sin_angle = sin(angle);
    SYSTEMVALUE dx, dy;

    MATRIX_00(&adjust) = 1.0 ;
    MATRIX_01(&adjust) = 0.0 ;
    MATRIX_10(&adjust) = 0.0 ;
    MATRIX_11(&adjust) = 1.0 ;
    MATRIX_20(&adjust) = -startpoint[0];
    MATRIX_21(&adjust) = -startpoint[1];
    MATRIX_SET_OPT_BOTH(&adjust) ;

    MATRIX_00(&adjust_tmp) = cos_angle ;
    MATRIX_01(&adjust_tmp) = sin_angle ;
    MATRIX_10(&adjust_tmp) = -sin_angle ;
    MATRIX_11(&adjust_tmp) = cos_angle ;
    MATRIX_20(&adjust_tmp) = 0.0 ;
    MATRIX_21(&adjust_tmp) = 0.0 ;
    MATRIX_SET_OPT_BOTH(&adjust_tmp) ;
    matrix_mult(&adjust, &adjust_tmp, &adjust);

    MATRIX_00(&adjust_tmp) = 1.0 ;
    MATRIX_01(&adjust_tmp) = 0.0 ;
    MATRIX_10(&adjust_tmp) = 0.0 ;
    MATRIX_11(&adjust_tmp) = 1.0 ;
    MATRIX_20(&adjust_tmp) = startpoint[0];
    MATRIX_21(&adjust_tmp) = startpoint[1];
    MATRIX_SET_OPT_BOTH(&adjust_tmp) ;
    matrix_mult(&adjust, &adjust_tmp, &adjust);

    /* Add the transform to the adjust, which will become the pattern
       matrix. */
    matrix_mult(&adjust, &state->transform, &state->transform);

    /* The endpoint has been rotated to be colinear with the x-axis. */
    dx = endpoint[0] - startpoint[0];
    dy = endpoint[1] - startpoint[1];
    endpoint[0] = startpoint[0] + sqrt(dx*dx + dy*dy);
    endpoint[1] = startpoint[1];
  }

  /* Non-invertible matrices are treated as if a transparent brush were used.
     We make the HUGE assumption that if transform and the CTM are invertible,
     then the concatenation of them will be. */
  if ( !matrix_inverse(&state->transform, &inverse_ctm) ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  /* Need to take account of this adjustment and the transform on the
     brushbox to ensure the pattern fills the current clipping. */
  bbox_transform(&brushbox, &brushbox, &inverse_ctm);

  /* For tiling patterns, specify tiling type 2 as this matches Avalon behaviour. */
  tilingtype = 0;
  if (spreadmethod_set) {
    switch ( XML_INTERN_SWITCH(spreadmethod) ) {
    case XML_INTERN_CASE(Pad):
      break;
    case XML_INTERN_CASE(Repeat):
      tilingtype = 2;
      brushbox.x1 = startpoint[0];
      brushbox.x2 = endpoint[0];
      bbox_normalise(&brushbox, &brushbox) ;
      break;
    case XML_INTERN_CASE(Reflect):
      tilingtype = 2;
      brushbox.x1 = startpoint[0];
      brushbox.x2 = endpoint[0] + (endpoint[0] - startpoint[0]);
      bbox_normalise(&brushbox, &brushbox) ;
      break ;
    default:
      HQFAIL("SpreadMethod is invalid.") ;
    }
  }

  /* Degenerate clipping or a brushbox with zero width or height implies an
     empty pattern bbox, so make the brush transparent in this case as well. */
  if ( clippingisdegenerate(gstateptr) ||
       brushbox.x1 >= brushbox.x2 ||
       brushbox.y1 >= brushbox.y2 ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  /* N.B. startpoint and endpoint are deliberately reversed, because we
     reflect the offsets when adding gradientstops. The drawing order
     doesn't matter for LinearGradients, but makes life easier for
     RadialGradients.  This must be done *after* the brush has been
     rotated to be colinear with the x-axis. */
  {
    SYSTEMVALUE tmp[2];
    tmp[0] = startpoint[0];
    tmp[1] = startpoint[1];
    startpoint[0] = endpoint[0];
    startpoint[1] = endpoint[1];
    endpoint[0] = tmp[0];
    endpoint[1] = tmp[1];
  }

  if (state->num_gradientstops < 1)
    return detail_error_handler(RANGECHECK, "Not enough gradient stops.");

  if (! gradientstops_clean(state, (!spreadmethod_set ||
                                    XML_INTERN_SWITCH(spreadmethod) == XML_INTERN_CASE(Pad))) )
    return FALSE;

  /* Convert the stops so they are all in the same colorspace */
  if (! gradientstops_convert(xps_ctxt, state, &method, &oColorSpace, &nOutputColors))
    return FALSE;

  /* Interpolate now they are in the same colorspace */
  gradientstops_interpolate(state);

  /* Work out the buffersizes */
  psbufsize = state->num_gradientstops * STOPBUFLEN;

  if ( nOutputColors > 3 )
    psbufsize += (nOutputColors - 3) * state->num_gradientstops * OUTDIMBUFLEN;

  if (spreadmethod_set &&
      spreadmethod == XML_INTERN(Reflect))
    psbufsize *= 2;
  psbufsize += FIXEDBUFLEN;

  /* Limit the buffer if necessary as run_ps_string requires string length to fit in a uint16. */
  if ( psbufsize > 65535 )
    psbufsize = 65535;

  psbuf = mm_alloc(mm_xml_pool, psbufsize, MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (psbuf == NULL)
    return error_handler(VMERROR);

  psend = psbuf;

  /* Push a dictionary containing the appropriate GradientBrushBlendSpace
     and GradientBrushRI (rendering intent) onto the stack */
  if ( !setup_gradientbrush_blendspace( xps_ctxt, method, oColorSpace ))
    goto tidyup;

  if ( state->num_gradientstops != 2 ) {
    /* Define the arrays for the stitching functions in the same dictionary
     * and leave it on the stack again. */
    if ( !setup_gradient_function_arrays( state, nOutputColors, psbuf, &psend))
      goto tidyup;
  }

  /* Begin the dict and start the pattern PS */
  swcopyf(psend, (uint8*)
          "begin\n"
          "<< /PatternType 102\n"
          "   /PaintType 1\n"
          "   /TilingType %d\n"
          "   /BBox [%f %f %f %f]\n"
          "   /XStep %f\n"
          "   /YStep %f\n"
          "   /PaintProc {\n"
          "     begin\n"
          "     //GradientBrushRI setrenderingintent\n"
          "         [ /CA %f /ca %f /SetTransparency pdfmark\n",
          tilingtype,
          brushbox.x1, brushbox.y1,
          brushbox.x2, brushbox.y2,
          fabs(brushbox.x2 - brushbox.x1),
          fabs(brushbox.y2 - brushbox.y1),
          state->opacity, state->opacity);
  psend = psend + strlen((const char *)psend) ;

  do {
    swcopyf(psend, (uint8*)
            "       <<\n"
            "         /ShadingType 2\n"
            "         /ColorSpace //GradientBrushBlendSpace\n"
            "         /Coords [%f %f %f %f]\n"
            "         /Extend [ %s %s ]\n",
            startpoint[0], startpoint[1],
            endpoint[0], endpoint[1],
            !spreadmethod_set || spreadmethod == XML_INTERN(Pad) ? "true" : "false",
            !spreadmethod_set || spreadmethod == XML_INTERN(Pad) ? "true" : "false");
    psend += strlen((const char *)psend) ;

    if (state->num_gradientstops == 2) {
      /* Can use a single function to represent color and opacity shift. */
      if ( !gradient_functions_pair(state, 1, FALSE, NULL, nOutputColors, &psend))
        goto tidyup;
    } else {
      /* Need to use a stitching function to represent several color and opacity shifts. */
      HQASSERT(state->num_gradientstops > 2, "Expected num_gradientstops > 2");
      if ( !gradient_functions_multiple(state, 1, FALSE, NULL, nOutputColors, &psend))
        goto tidyup;
    }

    swcopyf(psend, (uint8*)
            "       >> shfill\n");
    psend += strlen((const char *)psend) ;

    if (spreadmethod_set &&
        spreadmethod == XML_INTERN(Reflect) ) {
      if (do_reflection) {
        /* Just handled the reflection - nothing more to do. */
        do_reflection = FALSE;
      } else {
        /* Do the reflection by translating the shfill, swapping startpoint and
           endpoint, and then repeating all the stops. */
        SYSTEMVALUE tmp, dx;

        dx = fabs(endpoint[0] - startpoint[0]);
        startpoint[0] += dx;
        endpoint[0] += dx;

        tmp = startpoint[0];
        startpoint[0] = endpoint[0];
        endpoint[0] = tmp;

        do_reflection = TRUE;
      }
    }

  } while (do_reflection);

  swcopyf(psend, (uint8*)
          "       end\n"
          "   }\n"
          ">> [%f %f %f %f %f %f]"
          "end\n",
          state->transform.matrix[0][0],
          state->transform.matrix[0][1],
          state->transform.matrix[1][0],
          state->transform.matrix[1][1],
          state->transform.matrix[2][0],
          state->transform.matrix[2][1]);
  psend += strlen((const char *)psend) ;

  HQASSERT(psend - psbuf < psbufsize, "Uh-oh just run off the end of psbuf");

  success = run_ps_string((uint8*)psbuf) &&
            gs_makepattern(&operandstack, &pattern_dict) &&
            object_access_reduce(READ_ONLY, &pattern_dict) &&
            push2(&pattern_dict, &pattern_name, &operandstack) &&
            gsc_setcolorspace(gstateptr->colorInfo, &operandstack, state->colortype) &&
            gsc_setcolor(gstateptr->colorInfo, &operandstack, state->colortype);

  mm_free(mm_xml_pool, psbuf, psbufsize);
  return success ;

tidyup:
  mm_free(mm_xml_pool, psbuf, psbufsize);
  return FALSE;

#undef FIXEDBUFLEN
#undef STOPBUFLEN
#undef OUTDIMBUFLEN
}


static Bool xps_LinearGradientBrush_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  static USERVALUE opacity ;
  static Bool opacity_set, colorinterp_set, dummy ;
  static OMATRIX matrix;
  static xmlGIStr *mappingmode, *colorinterpolation, *spreadmethod ;
  static SYSTEMVALUE startpoint[2], endpoint[2] ;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(LinearGradientBrush_Transform), &matrix
  };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(LinearGradientBrush_Transform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(LinearGradientBrush_GradientStops), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* ColorInterpolationMode, MappingMode, SpreadMethod, StartPoint and
       EndPoint are dealt with in the commit callback. */
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity},
    { XML_INTERN(Transform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator},
    /* We need these as we are checking all attributes in this callback. */
    { XML_INTERN(ColorInterpolationMode), NULL, &colorinterp_set, xps_convert_ST_ClrIntMode, &colorinterpolation},
    { XML_INTERN(MappingMode), NULL, NULL, xps_convert_ST_MappingMode, &mappingmode},
    { XML_INTERN(SpreadMethod), NULL, &dummy, xps_convert_ST_SpreadMethod, &spreadmethod},
    { XML_INTERN(StartPoint), NULL, NULL, xps_convert_ST_Point, &startpoint},
    { XML_INTERN(EndPoint), NULL, NULL, xps_convert_ST_Point, &endpoint},
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complexproperties[] = {
    { XML_INTERN(LinearGradientBrush_Transform), XML_INTERN(ns_xps_2005_06), XML_INTERN(Transform), NULL, TRUE },
    { XML_INTERN(LinearGradientBrush_GradientStops), XML_INTERN(ns_xps_2005_06), NULL, NULL, FALSE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  HQASSERT(xps_ctxt->colortype == GSC_FILL ||
           xps_ctxt->colortype == GSC_STROKE,
           "Expect colortype to be defined at this point");

  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);

  if (! xps_commit_register(filter, localname, uri, attrs, complexproperties,
                            xps_LinearGradientBrush_Commit))
    return FALSE;
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Make a new brush state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsGradientBrushState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (state == NULL) {
    return error_handler(VMERROR);
  }

  state->base.type = XPS_STATE_GRADIENTBRUSH;
  state->base.next = xps_ctxt->callback_state;
  state->colortype = xps_ctxt->colortype;
  state->transform = *matrix_designator.matrix;
  state->radial = FALSE;
  state->gradientstops = NULL;
  state->num_gradientstops = 0u;
  state->opacity = opacity_set ? opacity : 1.0f ;
  NAME_OBJECT(state, BRUSH_STATE_NAME) ;

  /* Good completion; link the new brush into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  return TRUE;
}

static Bool xps_LinearGradientBrush_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  xps_ctxt->colortype = state->colortype ;
  xps_ctxt->callback_state = state->base.next ;
  while (state->gradientstops) {
    GradientStop* tmp = state->gradientstops;
    state->gradientstops = tmp->next;
    gradientstop_free(tmp);
  }
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsGradientBrushState)) ;

  return success;
}

/* The colors of points outside the boundary of the cone shape are defined
   as having the color and alpha defined in the GradientStop with the
   Offset value of 0.0 for Reflect and with the Offset value of 1.0 for
   Repeat and Pad (that's what XPS 0.85 says!).
   Note: it's done using a shfill because rectfill/fill is not allowed;
   and the shfill Background dictionary entry does not include opacity.
   The opacity set for the background (from the first gradient stop's
   Opacity attribute) is cumulative with the Opacity attribute on the
   RadialGradient.
*/
static uint8* radial_gradient_background(xpsGradientBrushState *state,
                                         sbbox_t *brushbox, uint8 *psend,
                                         int32 nOutputColors, Bool reflect)
{
  int32 i;
  GradientStop *stop;

  /* The DataSource uses this - it should be true for now at least */
  HQASSERT( nOutputColors <= 8,
            "Too many output colors in radial_gradient_background" );

  if ( reflect ) {
    /* Oddly, Reflect uses a different gradient stop for the background. */
    /* The offset 0.0 gradient stop is actually at the end of the list. */
    stop = state->gradientstops;
    while ( stop->next ) {
      stop = stop->next;
    }
  } else {
    /* Pad and Repeat. */
    stop = state->gradientstops;
  }

  /* N.B. GradientBrushBlendSpace defined in setup_gradientbrush_blendspace */
  swcopyf(psend, (uint8*)
          "<<\n"
          "  /ShadingType 2\n"
          "  /ColorSpace //GradientBrushBlendSpace\n"
          "  /Domain [0 1]\n"
          "  /Coords [%f %f %f %f]\n"
          "  /Function <<\n"
          "    /FunctionType 0\n"
          "    /Domain [0 1]\n"
          "    /Range [ %d { 0 1 } repeat ]\n"
          "    /Order 1\n"
          "    /DataSource <0000000000000000FFFFFFFFFFFFFFFF> %d %d getinterval\n"
          "    /BitsPerSample 8\n"
          "    /Size [2]\n"
          "    /Decode [ ",
          brushbox->x1, brushbox->y1,
          brushbox->x2, brushbox->y2,
          nOutputColors,
          (8 - nOutputColors),
          (2 * nOutputColors));

  psend += strlen((const char *)psend) ;

  /* Fill in the Decode array */
  for ( i = 0; i < nOutputColors ; i++ ) {
    swcopyf(psend, (uint8*) "%f %f ", stop->color[i], stop->color[i]);
    psend += strlen((const char *)psend) ;
  }

  /* Finish the Decode array */
  swcopyf(psend, (uint8*) "]\n");
  psend += strlen((const char *)psend) ;


  swcopyf(psend, (uint8*)
          "  >>\n"
          "  /HqnOpacity <<\n"
          "    /FunctionType 0\n"
          "    /Domain [0 1]\n"
          "    /Range [0 1]\n"
          "    /Decode [%f %f]\n"
          "    /Order 1\n"
          "    /DataSource <00FF>\n"
          "    /BitsPerSample 8\n"
          "    /Size [2]\n"
          "  >>\n"
          ">> shfill\n",
          stop->opacity, stop->opacity);

  psend += strlen((const char *)psend) ;

  return psend ;
}

static
Bool xps_RadialGradientBrush_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsGradientBrushState *state ;
  SYSTEMVALUE coords[6];
  SYSTEMVALUE dx, dy;
#define FIXEDBUFLEN (2500)
#define STOPBUFLEN (100)
#define OUTDIMBUFLEN (30)
  uint8 *psbuf, *psend;
  int32 psbufsize;
  sbbox_t brushbox;
  OMATRIX inverse_ctm;
  Bool success = FALSE;
  OBJECT pattern_dict = OBJECT_NOTVM_NOTHING,
    pattern_name = OBJECT_NOTVM_NAME(NAME_Pattern, LITERAL) ;
  uint32 nsteps = 1;
  Bool smallest_wins = TRUE;
  COLORSPACE_ID oColorSpace;
  int32 nOutputColors;
  uint32 method;

  static xmlGIStr *spreadmethod, *colorinterpolation, *mappingmode ;
  static double radiusx, radiusy ;
  static SYSTEMVALUE center[2], gradientorigin[2] ;
  static Bool dummy, spreadmethod_set ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(ColorInterpolationMode), NULL, &dummy, xps_convert_ST_ClrIntMode, &colorinterpolation},
    { XML_INTERN(MappingMode), NULL, NULL, xps_convert_ST_MappingMode, &mappingmode},
    { XML_INTERN(SpreadMethod), NULL, &spreadmethod_set, xps_convert_ST_SpreadMethod, &spreadmethod},
    { XML_INTERN(Center), NULL, NULL, xps_convert_ST_Point, &center},
    { XML_INTERN(GradientOrigin), NULL, NULL, xps_convert_ST_Point, &gradientorigin},
    { XML_INTERN(RadiusX), NULL, NULL, xps_convert_dbl_ST_GEZero, &radiusx},
    { XML_INTERN(RadiusY), NULL, NULL, xps_convert_dbl_ST_GEZero, &radiusy},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if (state->num_gradientstops < 1)
    return detail_error_handler(RANGECHECK, "Not enough gradient stops.");

  /* Do not consume all attributes. */
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE))
    return error_handler(UNDEFINED) ;

  if (! gradientstops_clean(state, (!spreadmethod_set ||
                                    XML_INTERN_SWITCH(spreadmethod) == XML_INTERN_CASE(Pad))) )
    return FALSE ;

/* Convert the stops so they are all in the same colorspace */
  if (! gradientstops_convert(xps_ctxt, state, &method, &oColorSpace, &nOutputColors))
    return FALSE;

  /* Interpolate now they are in the same colorspace */
  gradientstops_interpolate(state) ;

  /* Non-invertible matrices are treated as if a transparent brush were used.
     We make the HUGE assumption that if transform and the CTM are invertible,
     then the concatenation of them will be invertible. */
  if ( !matrix_inverse(&thegsPageCTM(*gstateptr), &inverse_ctm) ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  bbox_transform(&thegsPageClip(*gstateptr).rbounds, &brushbox, &inverse_ctm) ;

  if ( fabs(radiusx - radiusy) > EPSILON ) {
    OMATRIX adjust ;

    /* The radii are significantly different, so re-scale the CTM and shift
       the gradientorigin and center to compensate. */
    MATRIX_00(&adjust) = radiusx ;
    MATRIX_01(&adjust) = 0.0 ;
    MATRIX_10(&adjust) = 0.0 ;
    MATRIX_11(&adjust) = radiusy ;
    MATRIX_20(&adjust) = center[0] ;
    MATRIX_21(&adjust) = center[1] ;
    MATRIX_SET_OPT_BOTH(&adjust) ;

    matrix_mult(&adjust, &state->transform, &state->transform);

    /* The gradientorigin will be interpreted relative to the new coordinate space.
       Adjust the shift to get the same as it would have given before. */

    if (radiusx > EPSILON) {
      gradientorigin[0] = (gradientorigin[0] - center[0]) / radiusx ;
    } else {
      gradientorigin[0] = 0.0 ;
    }

    if (radiusy > EPSILON) {
      gradientorigin[1] = (gradientorigin[1] - center[1]) / radiusy ;
    } else {
      gradientorigin[1] = 0.0 ;
    }

    /* We have scaled to fit the radii. */
    radiusx = radiusy = 1.0 ;

    /* We have translated to cx,cy */
    center[0] = center[1] = 0.0 ;
  }

  /* Non-invertible matrices are treated as if a transparent brush were used.
     We make the HUGE assumption that if transform and the CTM are invertible,
     then the concatenation of them will be. */
  if ( !matrix_inverse(&state->transform, &inverse_ctm) ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  /* Need to take account of this adjustment and the transform on the
     brushbox to ensure the pattern fills the current clipping. */
  bbox_transform(&brushbox, &brushbox, &inverse_ctm);

  /* Degenerate clipping or a brushbox with zero width or height implies an
     empty pattern bbox, so make the brush transparent in this case as well. */
  if ( clippingisdegenerate(gstateptr) ||
       brushbox.x1 >= brushbox.x2 ||
       brushbox.y1 >= brushbox.y2 ) {
    state->colortype = GSC_UNDEFINED ;
    return TRUE ;
  }

  dx = center[0] - gradientorigin[0];
  dy = center[1] - gradientorigin[1];

  if (spreadmethod_set) {
    switch ( XML_INTERN_SWITCH(spreadmethod) ) {
    case XML_INTERN_CASE(Pad):
      break;
    case XML_INTERN_CASE(Reflect):
    case XML_INTERN_CASE(Repeat):
      /* Calculate the number of repeats or reflections required to fill brushbox. */
      spreadmethod_nsteps(center, dx, dy, radiusx, &brushbox, &nsteps);

      if (spreadmethod == XML_INTERN(Reflect) && (nsteps & 1) == 0) {
        /* @@@ woops, got to have an odd number of steps to ensure final step at
           gradient origin has correct reflection */
        nsteps += 1;
      }

      dx *= nsteps;
      dy *= nsteps;
      radiusx *= nsteps;
      break ;
    default:
      HQFAIL("SpreadMethod in invalid.") ;
      return FALSE ;
    }
  }

  /* Work out the buffer sizes */
  psbufsize = state->num_gradientstops * STOPBUFLEN + FIXEDBUFLEN;

  if ( nOutputColors > 3 )
    psbufsize += (nOutputColors - 3) * state->num_gradientstops * OUTDIMBUFLEN;

  /* Limit the buffer if necessary as run_ps_string requires string length to fit in a uint16. */
  if ( psbufsize > 65535 )
    psbufsize = 65535;

  psbuf = mm_alloc(mm_xml_pool, psbufsize, MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (psbuf == NULL)
    return error_handler(VMERROR);

  psend = psbuf;

  /* Push a dictionary containing the appropriate GradientBrushBlendSpace
     and GradientBrushRI (rendering intent) onto the stack */
  if ( !setup_gradientbrush_blendspace( xps_ctxt, method, oColorSpace ))
    goto tidyup;

  if ( state->num_gradientstops != 2 ) {
    /* Define the arrays for the stitching functions in the same dictionary
     * and leave it on the stack again. */
    if ( !setup_gradient_function_arrays( state, nOutputColors, psbuf, &psend))
      goto tidyup;
  }

  /* Begin the dict and start the pattern PS */
  swcopyf(psend, (uint8*)
          "begin\n"
          "<< /PatternType 102\n"
          "   /PaintType 1\n"
          "   /TilingType 0\n"
          "   /BBox [%f %f %f %f]\n"
          "   /XStep %f\n"
          "   /YStep %f\n"
          "   /PaintProc {\n"
          "     begin\n"
          "       //GradientBrushRI setrenderingintent\n"
          "       1183615869 internaldict begin\n"
          "         [ /CA %f /ca %f /SetTransparency pdfmark\n",
          brushbox.x1, brushbox.y1,
          brushbox.x2, brushbox.y2,
          fabs(brushbox.x2 - brushbox.x1),
          fabs(brushbox.y2 - brushbox.y1),
          state->opacity, state->opacity);
  psend = psend + strlen((const char *)psend) ;

  if (smallest_wins) {
    coords[0] = gradientorigin[0] + dx;
    coords[1] = gradientorigin[1] + dy;
    coords[2] = radiusx;
    coords[3] = gradientorigin[0];
    coords[4] = gradientorigin[1];
    coords[5] = 0.0;
  } else {
    coords[0] = gradientorigin[0];
    coords[1] = gradientorigin[1];
    coords[2] = 0.0;
    coords[3] = gradientorigin[0] + dx;
    coords[4] = gradientorigin[1] + dy;
    coords[5] = radiusx;
  }

  /* The colors of points outside the boundary of the cone shape are defined
     as having the color and alpha defined in the GradientStop with the
     Offset value of 0.0 for Reflect and with the Offset value of 1.0 for
     Repeat and Pad (that's what XPS 0.85 says!). */
  psend = radial_gradient_background(state, &brushbox, psend, nOutputColors,
                                     (spreadmethod_set &&
                                      XML_INTERN_SWITCH(spreadmethod) == XML_INTERN_CASE(Reflect)));

  swcopyf(psend, (uint8*)
          "       <<\n"
          "         /ShadingType 3\n"
          "         /ColorSpace //GradientBrushBlendSpace\n"
          "         /Domain [0 1]\n"
          "         /Coords [%f %f %f %f %f %f]\n"
          "         /Extend [ %s %s ]\n",
          coords[0], coords[1], coords[2], coords[3], coords[4], coords[5],
          !spreadmethod_set || spreadmethod == XML_INTERN(Pad) ? "true" : "false",
          !spreadmethod_set || spreadmethod == XML_INTERN(Pad) ? "true" : "false");
  psend += strlen((const char *)psend) ;

  if (state->num_gradientstops == 2) {
    /* Can use a single function to represent color and opacity shift. */
    if ( !gradient_functions_pair(state, nsteps, spreadmethod_set, spreadmethod, nOutputColors, &psend))
      goto tidyup;
  } else {
    /* Need to use a stitching function to represent several color and opacity shifts. */
    HQASSERT(state->num_gradientstops > 2, "Expected num_gradientstops > 2");
    if ( !gradient_functions_multiple(state, nsteps, spreadmethod_set, spreadmethod, nOutputColors, &psend))
      goto tidyup;
  }

  swcopyf(psend, (uint8*)
          "       >> shfill\n");
  psend += strlen((const char *)psend) ;

  swcopyf(psend, (uint8*)
          "       end\n"
          "     end\n"
          "   }\n"
          ">> [%f %f %f %f %f %f]\n"
          "end\n",
          state->transform.matrix[0][0],
          state->transform.matrix[0][1],
          state->transform.matrix[1][0],
          state->transform.matrix[1][1],
          state->transform.matrix[2][0],
          state->transform.matrix[2][1]);
  psend += strlen((const char *)psend) ;


  HQASSERT(psend - psbuf < psbufsize, "Uh-oh just run off the end of psbuf");

  success = run_ps_string((uint8*)psbuf) &&
            gs_makepattern(&operandstack, &pattern_dict) &&
            object_access_reduce(READ_ONLY, &pattern_dict) &&
            push2(&pattern_dict, &pattern_name, &operandstack) &&
            gsc_setcolorspace(gstateptr->colorInfo, &operandstack, state->colortype) &&
            gsc_setcolor(gstateptr->colorInfo, &operandstack, state->colortype);

  mm_free(mm_xml_pool, psbuf, psbufsize);
  return success ;

tidyup:
  mm_free(mm_xml_pool, psbuf, psbufsize);
  return FALSE;

#undef FIXEDBUFLEN
#undef STOPBUFLEN
#undef OUTDIMBUFLEN
}

static Bool xps_RadialGradientBrush_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  static USERVALUE opacity ;
  static Bool opacity_set, colorinterp_set, dummy ;
  static double radiusx, radiusy ;
  static OMATRIX matrix;
  static xmlGIStr *mappingmode, *colorinterpolation, *spreadmethod ;
  static SYSTEMVALUE center[2], gradientorigin[2] ;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(RadialGradientBrush_Transform), &matrix
  };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(RadialGradientBrush_Transform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(RadialGradientBrush_GradientStops), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* ColorInterpolationMode, MappingMode, SpreadMethod, Center, GradientOrigin,
       RadiusX, RadiusY are handled in the commit callback. */
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity},
    { XML_INTERN(Transform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator},
    /* We need these as we are checking all attributes in this callback. */
    { XML_INTERN(ColorInterpolationMode), NULL, &colorinterp_set, xps_convert_ST_ClrIntMode, &colorinterpolation},
    { XML_INTERN(MappingMode), NULL, NULL, xps_convert_ST_MappingMode, &mappingmode},
    { XML_INTERN(SpreadMethod), NULL, &dummy, xps_convert_ST_SpreadMethod, &spreadmethod},
    { XML_INTERN(Center), NULL, NULL, xps_convert_ST_Point, &center},
    { XML_INTERN(GradientOrigin), NULL, NULL, xps_convert_ST_Point, &gradientorigin},
    { XML_INTERN(RadiusX), NULL, NULL, xps_convert_dbl_ST_GEZero, &radiusx},
    { XML_INTERN(RadiusY), NULL, NULL, xps_convert_dbl_ST_GEZero, &radiusy},
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complexproperties[] = {
    { XML_INTERN(RadialGradientBrush_Transform), XML_INTERN(ns_xps_2005_06), XML_INTERN(Transform), NULL, TRUE },
    { XML_INTERN(RadialGradientBrush_GradientStops), XML_INTERN(ns_xps_2005_06), NULL, NULL, FALSE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");
  HQASSERT(xps_ctxt->colortype == GSC_FILL ||
           xps_ctxt->colortype == GSC_STROKE,
           "Expect colortype to be defined at this point");

  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);

  if (! xps_commit_register(filter, localname, uri, attrs, complexproperties,
                            xps_RadialGradientBrush_Commit))
    return FALSE;
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Make a new brush state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsGradientBrushState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (state == NULL) {
    return error_handler(VMERROR);
  }

  state->base.type = XPS_STATE_GRADIENTBRUSH;
  state->base.next = xps_ctxt->callback_state;
  state->colortype = xps_ctxt->colortype;
  state->transform = *matrix_designator.matrix;
  state->radial = TRUE;
  state->gradientstops = NULL;
  state->num_gradientstops = 0u;
  state->opacity = opacity_set ? opacity : 1.0f ;
  NAME_OBJECT(state, BRUSH_STATE_NAME) ;

  /* Good completion; link the new brush into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  return TRUE;
}

static Bool xps_RadialGradientBrush_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  xps_ctxt->colortype = state->colortype ;
  xps_ctxt->callback_state = state->base.next ;
  while (state->gradientstops) {
    GradientStop* tmp = state->gradientstops;
    state->gradientstops = tmp->next;
    gradientstop_free(tmp);
  }
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsGradientBrushState)) ;

  return success;
}

static Bool xps_LinearGradientBrush_Transform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_LinearGradientBrush_Transform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should have captured the matrix */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
      "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  return success;
}

static Bool xps_LinearGradientBrush_GradientStops_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsGradientBrushState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(GradientStop), XML_INTERN(ns_xps_2005_06), XMLG_MIN_OCCURS, -2},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* do real work */
  return TRUE; /* keep on parsing */
}

static Bool xps_LinearGradientBrush_GradientStops_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_brush_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( state->gradientstops == NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "No gradient stops.")) ;

  return success;
}

static Bool xps_RadialGradientBrush_Transform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(SYNTAXERROR) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  xps_ctxt->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_RadialGradientBrush_Transform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should have captured the matrix */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps_ctxt->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "Required MatrixTransform element is missing.")) ;

  xps_ctxt->transform = NULL ;

  return success;
}

static Bool xps_RadialGradientBrush_GradientStops_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsGradientBrushState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(GradientStop), XML_INTERN(ns_xps_2005_06), XMLG_MIN_OCCURS, -2},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix );

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(SYNTAXERROR) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* do real work */
  return TRUE; /* keep on parsing */
}

static Bool xps_RadialGradientBrush_GradientStops_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_brush_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( state->gradientstops == NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
                                               "No gradient stops.")) ;

  return success;
}

static Bool xps_GradientStop_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsGradientBrushState *state ;
  GradientStop **where ;
  Bool success = FALSE ;

  static USERVALUE offset ;
  static xps_color_designator color_designator = { XML_INTERN(GradientStop), FALSE };

  static XMLG_VALID_CHILDREN valid_children[] = {
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Offset), NULL, NULL, xps_convert_fl_ST_Double, &offset},
    { XML_INTERN(Color), NULL, NULL, xps_convert_ST_Color, &color_designator},
    XML_ATTRIBUTE_MATCH_END
  } ;
/*
  static XPS_CONTENT_TYPES profile_types[] = {
    { XML_INTERN(mimetype_iccprofile) },
    XPS_CONTENT_TYPES_END
  } ;
*/

  UNUSED_PARAM( const xmlGIStr* , prefix );

  color_designator.color_set = FALSE;
  color_designator.color_profile_partname = NULL;

  if (! xps_brush_state(filter, &xps_ctxt, &state))
    return error_handler(SYNTAXERROR) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    success = error_handler(UNDEFINED) ;
    goto cleanup;
  }

  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    success = error_handler(UNDEFINED);
    goto cleanup;
  }

  /* It MUST be set because its not optional in the match. */
  HQASSERT(color_designator.color_set,
           "color is not set") ;

  /* Put the gradient stops in smallest wins order for RadialGradientBrush as
     this is the most likely case. If either radius is -ve then the order is
     reversed back. We do this for LinearGradientBrush too, so we don't need
     to */

  offset = 1.0f - offset;

  where = &state->gradientstops ;

  /* Check stops arrive presorted. */
  if (state->num_gradientstops > 0) {
    GradientStop *stop = *where ;

    if (offset > stop->offset) {
      /* Non-strict mode allows unordered stops. Find the appropriate place
         to insert this stop. This sort sucks for performance, but we hope
         it will only be used rarely, so that's just too bad. */
      do {
        where = &stop->next ;
      } while ( (stop = *where) != NULL && offset > stop->offset ) ;
    }
  }

  /* Add to gradientstop list. */
  success = gradientstop_new(state, &color_designator, offset, where) ;

cleanup:
  if ( color_designator.color_profile_partname != NULL)
    xps_partname_free(&color_designator.color_profile_partname) ;

  return success ;
}

static Bool xps_GradientStop_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsGradientBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_brush_state(filter, NULL, &state) )
    return success && error_handler(UNREGISTERED) ;

  return success;
}

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts gradientbrush_functions[] =
{
  { XML_INTERN(LinearGradientBrush),
    xps_LinearGradientBrush_Start,
    xps_LinearGradientBrush_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(RadialGradientBrush),
    xps_RadialGradientBrush_Start,
    xps_RadialGradientBrush_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(LinearGradientBrush_Transform),
    xps_LinearGradientBrush_Transform_Start,
    xps_LinearGradientBrush_Transform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(LinearGradientBrush_GradientStops),
    xps_LinearGradientBrush_GradientStops_Start,
    xps_LinearGradientBrush_GradientStops_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(RadialGradientBrush_Transform),
    xps_RadialGradientBrush_Transform_Start,
    xps_RadialGradientBrush_Transform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(RadialGradientBrush_GradientStops),
    xps_RadialGradientBrush_GradientStops_Start,
    xps_RadialGradientBrush_GradientStops_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(GradientStop),
    xps_GradientStop_Start,
    xps_GradientStop_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
