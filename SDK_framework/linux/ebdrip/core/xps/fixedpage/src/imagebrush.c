/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:imagebrush.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:imagebrush.c,v 1.121.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload image brush callbacks.
 *
 * See imagebrush_functions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"
#include "graphics.h"
#include "gstate.h"
#include "gschead.h" /* gsc_setcolordirect */
#include "gschcms.h" /* gsc_setrenderingintent */
#include "gu_rect.h"
#include "imagecontext.h"
#include "hqunicode.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "objnamer.h"
#include "gu_ctm.h"   /* gs_modifyctm */
#include "constant.h"
#include "swcopyf.h"
#include "miscops.h"
#include "pattern.h" /* gs_makepattern */
#include "params.h"
#include "mmcompat.h"
#include "plotops.h"
#include "clipops.h"
#include "tranState.h"  /* tsConstantAlpha */

#include "xpspriv.h"
#include "xpsscan.h"
#include "fixedpagepriv.h"
#include "xpsdebug.h"    /* debug_xps */

/*=============================================================================
 * Utility functions
 *=============================================================================
 */
#define BRUSH_STATE_NAME "XPS Image Brush"

/** \brief Structure to contain Brush state. */
typedef struct xpsImageBrushState_s {
  xpsCallbackStateBase base; /**< Superclass MUST be first element. */

  OMATRIX transform ; /**< Local Transform matrix. */

  int32 colortype ;   /**< Color chain used by this brush. */

  USERVALUE opacity ; /**< ImageBrush opacity attribute. */

  OBJECT_NAME_MEMBER
} xpsImageBrushState ;

/* Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_brush_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsImageBrushState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  *state_ptr = NULL; /* silence compiler warning */

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsImageBrushState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_IMAGEBRUSH )
    return FALSE ;

  VERIFY_OBJECT(state, BRUSH_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

typedef struct BUF2PS
{
  uint8 *base;
  uint8 *current;
  int32 used;
  int32 maxlen;
} BUF2PS;

/**
 * We swcopyf() into an arbitrary sized buffer and check to see
 * we haven't fallen off the end.
 * \todo BMJ 04-Oct-07 :  should be using swncopyf to preevent buffer overrurn
 */
static void buf2ps_update(BUF2PS *b2ps)
{
  int32 len = (int32)strlen((const char *)b2ps->current);

  b2ps->current += len;
  b2ps->used    += len;

  HQASSERT(b2ps->used < b2ps->maxlen && b2ps->base[b2ps->maxlen] == '!',
           "Uh-oh just run off the end of psbuf");
}

static void xps_instantiate_imagedictionary(
      BUF2PS *b2ps,
      xmlGIStr *image_mimetype,
      RECTANGLE* viewbox,
      int32 xref)
{
  char *image_format = NULL ;
  HQASSERT(image_mimetype != NULL, "image_mimetype is NULL") ;

  switch ( XML_INTERN_SWITCH(image_mimetype) ) {
    case XML_INTERN_CASE(mimetype_jpeg):
      image_format = "[ /JFIF /JPEG ]" ;
      break ;
    case XML_INTERN_CASE(mimetype_png):
      image_format = "/PNG " ;
      break ;
    case XML_INTERN_CASE(mimetype_tiff):
      image_format = "/TIFF " ;
      break ;
    case XML_INTERN_CASE(mimetype_ms_photo):
      image_format = "/WMPHOTO " ;
      break ;
  default:
    /* We should never reach the default, but make it safe anyway. */
    HQFAIL("Unknown image mimetype.") ;
    image_format = "[ /PNG /JFIF /TIFF /JPEG /WMPHOTO ]" ;
  }

  swcopyf(b2ps->current, (uint8*)
          "ImageFileString (r) file dup <<\n"
          "  /ImageFormat %s\n"
          ">> imagecontextopen not {\n"
          "  /imagebrush /rangecheck //systemdict /.error get exec\n"
          "} if\n"
          "20 dict begin\n"
          "  /ImageFormat exch def \n"
          "  /XRef %d def \n"
          "  dup {\n"
/*        "    1 index =print ( )print dup ==\n" */ /* DEBUG */
          "    def currentdict length 17 eq\n" /* 2 plus arraysize */
          "  } [\n"
          "    /BitsPerComponent /ColorSpace /DataSource /Decode \n"
          "    /Height /ImageType /InterleaveType /EmbeddedICCProfile \n"
          "    /MultipleDataSources /PreMult /Width /XResolution /YResolution \n"
          "    /OriginalSourceColor /ImageMatrix\n"
          "  ] imagecontextinfo \n"
          "  \n"
          "  ImageProfileString type /stringtype ne \n"
          "  { \n"
          "    currentdict /EmbeddedICCProfile known \n"
          "    { \n"
          "      currentdict true \n"
          "      /HqnICCBased /ProcSet findresource /InstallImageProfile get exec \n"
          "    } { \n"
          "      currentdict /ColorSpace known \n"
          "      { \n"
          "        currentdict \n"
          "        /HqnICCBased /ProcSet findresource /InstallDefaultProfile get exec \n"
          "      } { \n"
          "        currentdict \n"
          "        /HqnICCBased /ProcSet findresource dup 3 1 roll /GetImageDimensions get exec \n"
          "        exch /SetSubstituteColorSpace get exec \n"
          "      } ifelse \n"
          "    } ifelse \n"
          "  } { \n"
          "    currentdict true \n"
          "    /HqnICCBased /ProcSet findresource dup /ImageProfileDict get XRef \n"
          "    2 copy known \n"
          "    { \n"
          "      get exch \n"
          "    } { \n"
          "      ImageProfileString (r) file dup 5 1 roll put \n"
          "    } ifelse \n"
          "    /InstallImageProfile get exec \n"
          "  } ifelse \n"
          "  \n"
          "  currentdict /Decode known \n"
          "  { \n"
          "    currentcolorspace dup \n"
          "    /HqnICCBased /ProcSet findresource /GetColorSpaceType get exec \n"
          "    4 1 roll pop pop pop \n"
          "    { \n"
          "      dup geticcbasedinfo /Lab eq 3 -1 roll geticcbasedisscrgb or \n"
          "      { \n"
          "        Decode dup 0 4 -1 roll getcurrentcolorspacerange putinterval \n"
          "        /Decode exch def \n" /* Allow for scRGB and Lab ICCBased colorspaces */
          "      } { \n"
          "        pop \n"
          "      } ifelse \n"
          "    } { \n"
          "      pop \n"
          "    } ifelse \n"
          "  } if \n"
          "  \n"
          "  /ImageMatrix [%f 0 0 %f %f %f]\n"
          "    currentdict /XResolution known {\n"
          "      0 2 4 {1 index exch 2 copy get XResolution 96 div mul put} for\n"
          "    } if\n"
          "    currentdict /YResolution known {\n"
          "      1 2 5 {1 index exch 2 copy get YResolution 96 div mul put} for\n"
          "    } if\n"
          /*
           * Ignore Orientation specified by Tiff, but for WMPhoto
           * convert the matrix returned by imagecontext into XPS space
           * and then concat it.
           */
          "  ImageFormat /WMPHOTO eq\n"
          "  currentdict /ImageMatrix known and {\n"
          "    ImageMatrix\n"
          "    0 2 2 {1 index exch 2 copy get Width div put} for\n"
          "    1 2 3 {1 index exch 2 copy get Height div put} for\n"
          "    [ 1 0 0 -1 0 Height ] matrix concatmatrix\n"
          "    matrix concatmatrix\n"
          "  } if\n"
          "  def\n"
          "  \n"
          "  currentdict /ImageFormat undef \n"
          "  \n"
          "  ImageType 1 eq {\n"
          "    currentdict\n" /* Image dict */
          "  } {\n" /* Masked/Alpha channel image */
          "    <<\n" /* Top-level image dict */
          "      /ImageType ImageType\n"
          "      /InterleaveType InterleaveType\n"
          "      /DataDict currentdict\n"
          "      /MaskDict <<\n" /* Mask dict */
          "        /ImageType 1\n"
          "        /Width Width\n"
          "        /Height Height\n"
          "        /BitsPerComponent ImageType 12 eq \n"
          "                          InterleaveType 0 ne or\n"
          "                          {BitsPerComponent}{1}ifelse\n"
          "        /ImageMatrix ImageMatrix\n"
          "        /Decode [0 1]\n"
          "        DataSource type /arraytype eq {\n"
          "          /DataSource DataSource dup length 1 sub get\n"
          "        }if\n"
          "      >>\n"
          "      /ImageType 1 def\n" /* Reset type in DataDict */
          "      /DataSource DataSource dup type /arraytype eq {\n"
          "        0 1 index length 1 sub getinterval\n"
          "      }if def\n" /* Remove alpha channel */
          "    >>\n"
          "  }ifelse\n"
          "end\n",
/*        "(%%stderr%%) (w) file 1 index emit (\n)print\n" */ /* DEBUG */

          image_format,
          xref,
          viewbox->w, viewbox->h, viewbox->x, viewbox->y);

  buf2ps_update(b2ps);
}

void allow_direct_image(xmlDocumentContext *xps_ctxt)
{
  Bool allow = TRUE;

  HQASSERT(!xps_ctxt->direct_image.allow, "Direct image handling is already allowed");

#ifdef DEBUG_BUILD
  allow = (debug_xps & DEBUG_DISABLE_DIRECT_IMAGES) == 0 ;
#endif
  xps_ctxt->direct_image.allow = allow;
}

void disallow_direct_image(xmlDocumentContext *xps_ctxt)
{
  xps_ctxt->direct_image.allow = FALSE;
}

void reset_direct_image(xmlDocumentContext *xps_ctxt)
{
  xps_direct_image_t *di = &xps_ctxt->direct_image, init = { 0 } ;

  *di = init;
}

Bool drawing_direct_image(xmlDocumentContext *xps_ctxt)
{
  return xps_ctxt->direct_image.drawing;
}

Bool xps_draw_direct_image(xmlDocumentContext *xps_ctxt, Bool direct)
{
  xps_direct_image_t *di;
  int32 xref ;
  BUF2PS b2ps;
  uint8 psbuf[17000+1]; /* arbitrary sized buf plus 1 extra for safety marker */

  di = &xps_ctxt->direct_image;

  /* For optimising color caches over multiple uses of the image's profile */
  xref = di->xref;

  /* As this is part of the gstate it will be set up again at the start of the
   * PaintProc.  It is possible the intent set here will be overridden in the
   * HqnICCBased procset by an intent from an embedded or associated profile,
   * but setting the default here seems the most straightforward approach.
   */
  oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
  if ( !gsc_setrenderingintent( gstateptr->colorInfo, &nnewobj ))
    return FALSE;

  /* Objects that will become ImageFileString & ImageProfileString */
  if (!push(&di->image_profilenameobj, &operandstack) ||
      !push(&di->image_filenameobj, &operandstack))
    return FALSE;

  /** \todo @@@ TODO FIXME major hack to create a pattern. Should use DIDL
      and imagecontext C interface instead of PostScript for internals. */
  b2ps.base   = b2ps.current = psbuf;
  b2ps.used   = 0;
  b2ps.maxlen = sizeof(psbuf)-1;
  b2ps.base[b2ps.maxlen] = '!'; /* End of buffer Safety marker */

  /* The "-0.5 -0.5 idtransform translate" fudge factor is here to compensate
     for !theIArgsFitEdges behaviour in im_calculatematrix, which translates by
     0.5, 0.5 to get to pixel centres.  This transform should affect the image
     only, not the clip. */

  if ( direct ) {
    int row;
    RECTANGLE rect;
    ps_context_t *pscontext = CoreContext.pscontext;
    USERVALUE alpha;

    /* The opacity is the combination of image opacity and the opacity applied
       to the path. */
    alpha = di->opacity * tsConstantAlpha(gsTranState(gstateptr), GSC_FILL);

    /* Draw the image directly. */

    /* This section implements in C, this sequence of PS. It's been separated
       because we have to apply HqnMatrixAdjustment in C:
       gsave
       %f %f %f %f %f %f concat   % using di->transform
       0 0 1 1 rectclip           % clip to the transposed viewport
     */
    if (!gsave_(pscontext))
      return FALSE;

    gs_modifyctm(&di->transform);

    rect.x = rect.y = 0;
    rect.w = rect.h = 1;
    if (!cliprectangles(&rect, 1))
      return FALSE;

    /* Adjust the CTM using the same HqnMatrixAdjustment method/hack that is
       applied to the pattern case to adjust for the difference between CTMs
       that have been clipped to float values.
       This doesn't exactly match the roundings applied to the non-direct route
       because doubles are choked to floats in the ImageMatrix in both methods,
       but this hack produces the closest output to the non-direct method. */
    for (row = 0; row < 3; ++row) {
      thegsPageCTM(*gstateptr).matrix[row][0] += di->matrix_adj.matrix[row][0] ;
      thegsPageCTM(*gstateptr).matrix[row][1] += di->matrix_adj.matrix[row][1] ;
    }
    MATRIX_SET_OPT_BOTH(&thegsPageCTM(*gstateptr)) ;

    swcopyf(b2ps.current, (uint8*)
            "-0.5 -0.5 idtransform translate\n"
            "10 dict begin\n"
            "   /ImageFileString exch def\n"
            "   /ImageProfileString exch def\n"
            "   1183615869 internaldict begin\n"
            "     /imagecontextopen dup load\n"
            "     /imagecontextclose dup load\n"
            "     /imagecontextinfo dup load\n"
            "   end\n"
            "   def def def\n"
            "   [ /CA %f /ca %f /SetTransparency pdfmark\n",
            alpha, alpha);
    buf2ps_update(&b2ps);

    xps_instantiate_imagedictionary(&b2ps, di->image_mimetype, &di->viewbox, xref);
    swcopyf(b2ps.current, (uint8*)
            "   image\n"
            "   imagecontextclose closefile\n"
            "end grestore\n" /* 10 dict begin */);
    buf2ps_update(&b2ps);

    if ( !run_ps_string(b2ps.base) )
      return FALSE;
  } else {
    /* Can't draw the image directly after all. Set up the pattern instead. */
    OBJECT pattern_dict = OBJECT_NOTVM_NOTHING,
      pattern_name = OBJECT_NOTVM_NOTHING;

    swcopyf(b2ps.current, (uint8*)
            "<<\n"
            "   /ImageFileString 3 -1 roll\n"
            "   /ImageProfileString 5 -1 roll\n"
            "   /PatternType 101\n"
            "   /PaintType 1\n"
            "   /TilingType 0\n"
            "   /BBox [0 0 1 1]\n"
            "   /XStep 1\n"
            "   /YStep 1\n"
            "   /HqnMatrixAdjustment [%f %f %f %f %f %f]\n"
            "   1183615869 internaldict begin\n"
            "     /imagecontextopen dup load\n"
            "     /imagecontextclose dup load\n"
            "     /imagecontextinfo dup load\n"
            "   end\n"
            "   /PaintProc {\n"
            "       begin\n"
            "         [ /CA %f /ca %f /SetTransparency pdfmark\n",
            di->matrix_adj.matrix[0][0], di->matrix_adj.matrix[0][1],
            di->matrix_adj.matrix[1][0], di->matrix_adj.matrix[1][1],
            di->matrix_adj.matrix[2][0], di->matrix_adj.matrix[2][1],
            di->opacity, di->opacity);
    buf2ps_update(&b2ps);

    xps_instantiate_imagedictionary(&b2ps, di->image_mimetype, &di->viewbox, xref);
    swcopyf(b2ps.current, (uint8*)
            "       clipsave 0 0 1 1 rectclip matrix currentmatrix\n"
            "         -0.5 -0.5 idtransform translate exch image setmatrix cliprestore\n"
            "       imagecontextclose closefile\n"
            "       end\n"
            "   }\n"
            ">> [ %f %f %f %f %f %f ]",
            di->transform.matrix[0][0], di->transform.matrix[0][1],
            di->transform.matrix[1][0], di->transform.matrix[1][1],
            di->transform.matrix[2][0], di->transform.matrix[2][1]);
    buf2ps_update(&b2ps);

    object_store_name(&pattern_name, NAME_Pattern, LITERAL);

    if ( !run_ps_string(b2ps.base) ||
         !gs_makepattern(&operandstack, &pattern_dict) ||
         !object_access_reduce(READ_ONLY, &pattern_dict) ||
         !push2(&pattern_dict, &pattern_name, &operandstack) ||
         !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, di->colortype) ||
         !gsc_setcolor(gstateptr->colorInfo, &operandstack, di->colortype) )
      return FALSE;
  }

  return TRUE;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

/** Bitflags for tile flip directions. */
enum {
  FLIP_X = 1,
  FLIP_Y = 2
} ;

static
Bool xps_ImageBrush_Commit(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;
  int32 flip, tilingtype ;
  OBJECT image_filenameobj = OBJECT_NOTVM_NOTHING,
    image_profilenameobj = OBJECT_NOTVM_NOTHING,
    pattern_dict = OBJECT_NOTVM_NOTHING,
    pattern_name = OBJECT_NOTVM_NOTHING;
  OMATRIX transform, matrix_adj = {0};
  Bool success = FALSE ;
  xmlGIStr *image_mimetype ;
  xmlGIStr *profile_mimetype ;
  uint8 *ps_filename = NULL ;
  uint8 *ps_iccfilename = NULL ;
  uint32 ps_filename_len = 0 ;
  uint32 ps_iccfilename_len = 0 ;
  int32 xref ;
  BUF2PS b2ps;
  uint8 psbuf[17000+1]; /* arbitrary sized buf plus 1 extra for safety marker */

  static RECTANGLE viewbox, viewport ;
  static xmlGIStr *viewboxunits, *viewportunits, *tilemode ;
  static xps_imagesource_designator imagesource ;
  static Bool tilemode_set, dummy ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(TileMode), NULL, &tilemode_set, xps_convert_ST_TileMode, &tilemode},
    { XML_INTERN(Viewbox), NULL, NULL, xps_convert_ST_ViewBox, &viewbox},
    { XML_INTERN(ViewboxUnits), NULL, &dummy, xps_convert_ST_ViewUnits, &viewboxunits},
    { XML_INTERN(Viewport), NULL, NULL, xps_convert_ST_ViewBox, &viewport},
    { XML_INTERN(ViewportUnits), NULL, &dummy, xps_convert_ST_ViewUnits, &viewportunits},
    { XML_INTERN(ImageSource), NULL, NULL, xps_convert_ST_UriCtxBmp, &imagesource},
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_CONTENT_TYPES image_content_types[] = {
    { XML_INTERN(mimetype_jpeg) },
    { XML_INTERN(mimetype_png) },
    { XML_INTERN(mimetype_tiff) },
    { XML_INTERN(mimetype_ms_photo) },
    XPS_CONTENT_TYPES_END
  } ;

  static XPS_CONTENT_TYPES profile_types[] = {
    { XML_INTERN(mimetype_iccprofile) },
    XPS_CONTENT_TYPES_END
  } ;

  UNUSED_PARAM(const xmlGIStr *, prefix) ;

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  if ( !xps_brush_state(filter, NULL, &state) )
    return error_handler(UNREGISTERED) ;

#define return DO_NOT_RETURN_go_to_cleanup_INSTEAD!
  /* Do not consume all attributes. */
  if ( !xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE) ) {
    (void) error_handler(UNDEFINED);
    goto cleanup ;
  }

  /* Non-invertible matrices are not an error. Metro 0.6d defines both the
     case of mapping a degenerate viewport and a degenerate viewbox as if a
     transparent brush were used.  Degenerate clipping implies an empty
     pattern bbox, so make the brush transparent in this case as well. */
  if ( clippingisdegenerate(gstateptr) ||
       viewport.w < EPSILON || viewport.h < EPSILON ||
       viewbox.w < EPSILON || viewbox.h < EPSILON ) {
    state->colortype = GSC_UNDEFINED ;
    success = TRUE ;
    goto cleanup ;
  }

  flip = 0 ;
  tilingtype = 0 ;
  if ( tilemode_set )
    switch ( XML_INTERN_SWITCH(tilemode) ) {
    case XML_INTERN_CASE(Flipxy):
    case XML_INTERN_CASE(FlipXY):
      flip = FLIP_X|FLIP_Y ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Flipy):
    case XML_INTERN_CASE(FlipY):
      flip = FLIP_Y ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Flipx):
    case XML_INTERN_CASE(FlipX):
      flip = FLIP_X ;
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(Tile):
      tilingtype = 2 ;
      break ;
    case XML_INTERN_CASE(None):
      break ;
    default:
      HQFAIL("TileMode value should have been checked in start callback") ;
    }

  transform.matrix[0][0] = viewport.w ;
  transform.matrix[0][1] = 0 ;
  transform.matrix[1][0] = 0 ;
  transform.matrix[1][1] = viewport.h ;
  transform.matrix[2][0] = viewport.x ;
  transform.matrix[2][1] = viewport.y ;
  MATRIX_SET_OPT_0011(&transform) ;
  matrix_mult(&transform, &state->transform, &transform) ;

  /* Small tile handling on the cheap!
     If the width or height of the brush tile is below two device pixels then the
     viewport is scaled to avoid pathological rendering problems and to give a
     vague averaging impression (2x2 pixels allows some mid-tone colours).  For these
     cases the viewer produces an average colour to paint across the brush area,
     but since this method is only optional, and awkward for us to implement, we
     don't do it.  The result is likely to vary considerably from the viewer. */
  if ( tilemode_set && XML_INTERN_SWITCH(tilemode) != XML_INTERN_CASE(None) ) {
    OMATRIX test ;
    double dx1, dy1, dx2, dy2, fac1, fac2 ;
#define MIN_TILE_SIZE 2 /* device pixels */

    matrix_mult(&transform, &thegsPageCTM(*gstateptr), &test) ;
    MATRIX_TRANSFORM_DXY(viewbox.w, 0, dx1, dy1, &test) ;
    MATRIX_TRANSFORM_DXY(0, viewbox.h, dx2, dy2, &test) ;

    if ( (dx1 * dx1 + dy1 * dy1) < (EPSILON * EPSILON) ||
         (dx2 * dx2 + dy2 * dy2) < (EPSILON * EPSILON) ) {
      /* Give up and make it completely transparent. */
      state->colortype = GSC_UNDEFINED ;
      success = TRUE ;
      goto cleanup ;
    }

    fac1 = MIN_TILE_SIZE / sqrt(dx1 * dx1 + dy1 * dy1) ;
    fac2 = MIN_TILE_SIZE / sqrt(dx2 * dx2 + dy2 * dy2) ;
    if ( fac1 > 1 || fac2 > 1 ) {
      HQTRACE(TRUE,("WARNING! ImageBrush tile size is smaller than a few device pixels "
              "- result may be unexpected.\n")) ;
      /* Re-scale the viewport and re-do the transform so we end up with a
         brush tile that is at least two device pixels by two device pixels. */
      if ( fac1 > 1 )
        viewport.w *= fac1 ;
      if ( fac2 > 1 )
        viewport.h *= fac2 ;
      MATRIX_00(&transform) = viewport.w / viewbox.w ;
      MATRIX_01(&transform) = 0.0 ;
      MATRIX_10(&transform) = 0.0 ;
      MATRIX_11(&transform) = viewport.h / viewbox.h ;
      MATRIX_20(&transform) = viewport.x - viewbox.x * MATRIX_00(&transform) ;
      MATRIX_21(&transform) = viewport.y - viewbox.y * MATRIX_11(&transform) ;
      MATRIX_SET_OPT_BOTH(&transform) ;
      matrix_mult(&transform, &state->transform, &transform) ;
    }
  }

  {
    OMATRIX test, transform_choked = {0}, m1_accurate, m1_choked ;
    uint32 row ;

    /* Non-invertible matrices are not an error. Metro 0.6d defines them as
       if a transparent brush were used. */
    matrix_mult(&transform, &thegsPageCTM(*gstateptr), &test) ;
    if ( !matrix_inverse(&test, &test) ) {
      state->colortype = GSC_UNDEFINED ;
      success = TRUE ;
      goto cleanup ;
    }

    /** \todo @@@ TODO FIXME XPS uses doubles for coords, but brush coords
        end-up being choked down to floats because brushes are implemented as PS
        patterns.  HqnMatrixAdjustment adjusts m1 to compensate for the loss of
        precision, therefore removing problems like gaps between abutting images
        etc.  This code can be removed once XPS implements brushes in DIDL. */

    matrix_mult(&transform, &thegsPageCTM(*gstateptr), &m1_accurate) ;

    for ( row = 0 ; row < 3 ; ++row ) {
      transform_choked.matrix[row][0] = (double)((float)transform.matrix[row][0]) ;
      transform_choked.matrix[row][1] = (double)((float)transform.matrix[row][1]) ;
    }
    MATRIX_SET_OPT_BOTH(&transform_choked) ;
    matrix_mult(&transform_choked, &thegsPageCTM(*gstateptr), &m1_choked) ;

    for ( row = 0 ; row < 3 ; ++row ) {
      matrix_adj.matrix[row][0] = m1_accurate.matrix[row][0] - m1_choked.matrix[row][0] ;
      matrix_adj.matrix[row][1] = m1_accurate.matrix[row][1] - m1_choked.matrix[row][1] ;
    }
    MATRIX_SET_OPT_BOTH(&matrix_adj) ;
  }

  /* Because image source is non-optional, if we get this far, it MUST
     be set. */
  HQASSERT(imagesource.image != NULL, "imagesource.image is NULL") ;
  if ( !xps_ps_filename_from_partname(filter, imagesource.image,
                                      &ps_filename, &ps_filename_len,
                                      XML_INTERN(rel_xps_2005_06_required_resource),
                                      image_content_types,
                                      &image_mimetype) ||
       !ps_string(&image_filenameobj, ps_filename, ps_filename_len) )
    goto cleanup ;

  /* This assignment is necessary to persuade the InstallImageProfile procset
   * procedure to do the right thing when the job doesn't assign a profile.
   */
  xref = imagesource.image->uid;
  /* See if an ICC profile ought to be applied to this image.
   * N.B. A profile here overrides any embedded profile.
   */
  if (imagesource.profile != NULL) {
    if ( !xps_ps_filename_from_partname(filter, imagesource.profile,
                                        &ps_iccfilename, &ps_iccfilename_len,
                                        XML_INTERN(rel_xps_2005_06_required_resource),
                                        profile_types,
                                        &profile_mimetype) ||
         !ps_string(&image_profilenameobj, ps_iccfilename, ps_iccfilename_len) )
      goto cleanup;

    /* For optimising color caches over multiple uses of the profile */
    xref = imagesource.profile->uid;
  }

  /* Check if the image brush is suitable for converting into a clipped image. */
  if ( xps_ctxt->direct_image.allow &&
       (!tilemode_set || XML_INTERN_SWITCH(tilemode) == XML_INTERN_CASE(None)) ) {
    /* Store any state required to draw the image later when the clip is ready. */
    xps_direct_image_t *di = &xps_ctxt->direct_image;

    di->drawing = TRUE;
    di->colortype = state->colortype;
    di->transform = transform;
    di->matrix_adj = matrix_adj;
    di->opacity = state->opacity;
    di->viewbox = viewbox;
    di->xref = xref;
    di->image_filenameobj = image_filenameobj;  /* Struct copy */
    di->image_profilenameobj = image_profilenameobj;  /* Struct copy */
    di->image_mimetype = image_mimetype;

    success = TRUE;
    goto cleanup;
  }

  /* As this is part of the gstate it will be set up again at the start of the
   * PaintProc.  It is possible the intent set here will be overridden in the
   * HqnICCBased procset by an intent from an embedded or associated profile,
   * but setting the default here seems the most straightforward approach.
   */
  oName(nnewobj) = xps_ctxt->defaultRenderingIntentName;
  if ( !gsc_setrenderingintent( gstateptr->colorInfo, &nnewobj ))
    goto cleanup;

  /* Objects that will become ImageFileString & ImageProfileString */
  if (!push(&image_profilenameobj, &operandstack) ||
      !push(&image_filenameobj, &operandstack))
    goto cleanup;

  /** \todo @@@ TODO FIXME major hack to create a pattern. Should use DIDL
      and imagecontext C interface instead of PostScript for internals. */
  b2ps.base   = b2ps.current = psbuf;
  b2ps.used   = 0;
  b2ps.maxlen = sizeof(psbuf)-1;
  b2ps.base[b2ps.maxlen] = '!'; /* End of buffer Safety marker */

  swcopyf(b2ps.current, (uint8*)
          "<<\n"
          "   /ImageFileString 3 -1 roll\n"
          "   /ImageProfileString 5 -1 roll\n"
          "   /PatternType 101\n"
          "   /PaintType 1\n"
          "   /TilingType %d\n"
          "   /BBox [%d %d 1 1]\n"
          "   /XStep %d\n"
          "   /YStep %d\n"
          "   /HqnMatrixAdjustment [%f %f %f %f %f %f]\n"
          "   1183615869 internaldict begin\n"
          "     /imagecontextopen dup load\n"
          "     /imagecontextclose dup load\n"
          "     /imagecontextinfo dup load\n"
          "   end\n"
          "   /PaintProc {\n"
          "       begin\n"
          "         [ /CA %f /ca %f /SetTransparency pdfmark\n",
          tilingtype,
          (flip & FLIP_X) ? -1 : 0, /* BBox X */
          (flip & FLIP_Y) ? -1 : 0, /* BBox Y */
          (flip & FLIP_X) ? 2 : 1,  /* XStep */
          (flip & FLIP_Y) ? 2 : 1,  /* YStep */
          matrix_adj.matrix[0][0], matrix_adj.matrix[0][1],
          matrix_adj.matrix[1][0], matrix_adj.matrix[1][1],
          matrix_adj.matrix[2][0], matrix_adj.matrix[2][1],
          state->opacity, state->opacity);
  buf2ps_update(&b2ps);

  /* The "-0.5 -0.5 idtransform translate" fudge factor is here to compensate
     for !theIArgsFitEdges behaviour in im_calculatematrix, which translates by
     0.5, 0.5 to get to pixel centres.  This transform should affect the image
     only, not the clip. */

  xps_instantiate_imagedictionary(&b2ps, image_mimetype, &viewbox, xref);
  swcopyf(b2ps.current, (uint8*)
          "       clipsave 0 0 1 1 rectclip matrix currentmatrix\n"
          "         -0.5 -0.5 idtransform translate exch image setmatrix cliprestore\n"
          "       imagecontextclose closefile\n");
  buf2ps_update(&b2ps);

  if ( (flip & FLIP_X) != 0 ) {
    xps_instantiate_imagedictionary(&b2ps, image_mimetype, &viewbox, xref);
    swcopyf(b2ps.current, (uint8*)
            "       [-1 0 0 1 0 0] concat\n"
            "       clipsave 0 0 1 1 rectclip matrix currentmatrix\n"
            "         -0.5 -0.5 idtransform translate exch image setmatrix cliprestore\n"
            "       imagecontextclose closefile\n");
    buf2ps_update(&b2ps);
  }
  if ( (flip & FLIP_Y) != 0 ) {
    xps_instantiate_imagedictionary(&b2ps, image_mimetype, &viewbox, xref);
    swcopyf(b2ps.current, (uint8*)
            "       [1 0 0 -1 0 0] concat\n"
            "       clipsave 0 0 1 1 rectclip matrix currentmatrix\n"
            "         -0.5 -0.5 idtransform translate exch image setmatrix cliprestore\n"
            "       imagecontextclose closefile\n");
    buf2ps_update(&b2ps);
  }
  if ( (flip & (FLIP_X|FLIP_Y)) == (FLIP_X|FLIP_Y) ) {
    xps_instantiate_imagedictionary(&b2ps, image_mimetype, &viewbox, xref);
    swcopyf(b2ps.current, (uint8*)
            "       [-1 0 0 1 0 0] concat\n"
            "       clipsave 0 0 1 1 rectclip matrix currentmatrix\n"
            "         -0.5 -0.5 idtransform translate exch image setmatrix cliprestore\n"
            "       imagecontextclose closefile\n");
    buf2ps_update(&b2ps);
  }
  /* Transform should be to viewport, so  */
  swcopyf(b2ps.current, (uint8*)
          "       end\n"
          "   }\n"
          ">> [ %f %f %f %f %f %f ]",
          transform.matrix[0][0],
          transform.matrix[0][1],
          transform.matrix[1][0],
          transform.matrix[1][1],
          transform.matrix[2][0],
          transform.matrix[2][1]);
  buf2ps_update(&b2ps);

  object_store_name(&pattern_name, NAME_Pattern, LITERAL);

  if ( !run_ps_string(b2ps.base) ||
       !gs_makepattern(&operandstack, &pattern_dict) ||
       !object_access_reduce(READ_ONLY, &pattern_dict) ||
       !push2(&pattern_dict, &pattern_name, &operandstack) ||
       !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, state->colortype) ||
       !gsc_setcolor(gstateptr->colorInfo, &operandstack, state->colortype) )
    goto cleanup;

  success = TRUE ;

 cleanup:
  if (imagesource.image != NULL)
    xps_partname_free(&imagesource.image) ;
  if (imagesource.profile != NULL)
    xps_partname_free(&imagesource.profile) ;
  if ( ps_filename )
    mm_free_with_header(mm_xml_pool, ps_filename) ;
  if ( ps_iccfilename )
    mm_free_with_header(mm_xml_pool, ps_iccfilename) ;

#undef return
  return success ;
}

static
Bool xps_ignore_imagesource(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* NULL */)
{
  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;
  UNUSED_PARAM(void *, data) ;

  HQASSERT(data == NULL, "data is not NULL") ;

  value->codeunits = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;
  return TRUE ;
}

static int32 xps_ImageBrush_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;

  static USERVALUE opacity ;
  static Bool opacity_set, viewportunits_set, viewboxunits_set, tilemode_set, dummy ;
  static RECTANGLE viewbox, viewport ;
  static OMATRIX matrix;
  static xmlGIStr *viewboxunits, *viewportunits, *tilemode ;
  static xps_matrix_designator matrix_designator = {
    XML_INTERN(ImageBrush_Transform), &matrix
  };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(ImageBrush_Transform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* Viewbox, Viewport, TileMode, ViewboxUnits, ViewportUnits, and
       ImageSource are handled by the commit callback. */
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity},
    { XML_INTERN(Transform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator},
    /* We need these as we are checking all attributes in this callback. */
    { XML_INTERN(TileMode), NULL, &tilemode_set, xps_convert_ST_TileMode, &tilemode},
    { XML_INTERN(Viewbox), NULL, NULL, xps_convert_ST_ViewBox, &viewbox},
    { XML_INTERN(ViewboxUnits), NULL, &viewboxunits_set, xps_convert_ST_ViewUnits, &viewboxunits},
    { XML_INTERN(Viewport), NULL, NULL, xps_convert_ST_ViewBox, &viewport},
    { XML_INTERN(ViewportUnits), NULL, &viewportunits_set, xps_convert_ST_ViewUnits, &viewportunits},
    { XML_INTERN(ImageSource), NULL, NULL, xps_ignore_imagesource, NULL},
    XML_ATTRIBUTE_MATCH_END
  } ;

  static XPS_COMPLEXPROPERTYMATCH complexproperties[] = {
    { XML_INTERN(ImageBrush_Transform), XML_INTERN(ns_xps_2005_06), XML_INTERN(Transform), NULL, TRUE },
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
                            xps_ImageBrush_Commit))
    return FALSE;
  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE))
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Make a new brush state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsImageBrushState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE);
  if (state == NULL) {
    return error_handler(VMERROR);
  }

  state->base.type = XPS_STATE_IMAGEBRUSH;
  state->base.next = xps_ctxt->callback_state;
  state->colortype = xps_ctxt->colortype ;
  state->transform = *matrix_designator.matrix ;
  state->opacity = opacity_set ? opacity : 1.0f ;
  NAME_OBJECT(state, BRUSH_STATE_NAME) ;

  /* Good completion; link the new brush into the context. */
  xps_ctxt->callback_state = (xpsCallbackState*)state ;

  return TRUE;
}

static int32 xps_ImageBrush_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_brush_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  xps_ctxt->colortype = state->colortype ;
  xps_ctxt->callback_state = state->base.next ;
  UNNAME_OBJECT(state) ;
  mm_free(mm_xml_pool, state, sizeof(xpsImageBrushState)) ;

  return success;
}

static int32 xps_ImageBrush_Transform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;

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

static int32 xps_ImageBrush_Transform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsImageBrushState *state ;

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

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts imagebrush_functions[] =
{
  { XML_INTERN(ImageBrush),
    xps_ImageBrush_Start,
    xps_ImageBrush_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(ImageBrush_Transform),
    xps_ImageBrush_Transform_Start,
    xps_ImageBrush_Transform_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
