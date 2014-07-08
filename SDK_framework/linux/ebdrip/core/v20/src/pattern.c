/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:pattern.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Procedures for pattern color spaces.
 */

#include "core.h"
#include "gstack.h"
#include "shading.h"
#include "swerrors.h"
#include "constant.h"
#include "vndetect.h"
#include "graphics.h"
#include "swpdfin.h"
#include "control.h"
#include "namedef_.h"
#include "display.h"
#include "params.h"
#include "gscdevci.h"
#include "rcbcntrl.h"
#include "routedev.h"
#include "idlom.h"
#include "swpdfout.h"
#include "gu_ctm.h"
#include "gschead.h"
#include "groupPrivate.h"
#include "hdlPrivate.h"
#include "gu_rect.h"
#include "halftone.h"
#include "swmemory.h"
#include "dicthash.h"
#include "clipops.h"
#include "gs_color.h"
#include "typeops.h"
#include "gu_chan.h"
#include "gschtone.h"
#include "dl_store.h"
#include "plotops.h"
#include "utils.h"
#include "gstate.h"
#include "tranState.h"
#include "pathcons.h"
#include "pattern.h"
#include "patternshape.h"
#include "surface.h"
#include "system.h"   /* gs_freecliprec */

/* -------------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
Bool debug_pattern = FALSE ;
#endif

/** This is the depth to which we copy PDF or non PSVM patterns before
    throwing an error. */
#define MAX_PATTERN_RECURSION 16

/* --- creators --- */
static Bool makepattern_1(OBJECT *newd, OBJECT *thea, OBJECT *thed) ;
static Bool makepattern_2(OBJECT *newd, OBJECT *thea, OBJECT *thed) ;

static Bool createPatternScreenDL(DL_STATE *page, int32 id, GSTATE *pgstate,
                                  TranAttrib *tranattrib, PATTERNOBJECT *patobj);
static Bool createPatternDL(DL_STATE *page, int32 id,
                            OBJECT *pppattern, OBJECT *impl,
                            int32 idlomHit, int32 colorType,
                            Bool overprinting[2], TranAttrib *tranattrib,
                            PATTERNOBJECT *patobj ) ;

/* --- other ------ */
static int32 create_pia_checksum( OBJECT *impl ) ;

/*-------------------------  F R O N T   E N D  -------------------------*/

/*-----------  D I S P L A Y   L I S T   G E N E R A T I O N  ------------*/

/*------  E X E C U T E   T H E   P A I N T P R O C   T O   D L  -------*/

/* context_patternid is the pattern id of the immediate parent
   at makepattern time. */
static int32 context_patternid = INVALID_PATTERN_ID ;

/* parent_patternid is the pattern id of the immediate parent
   at createPattern[Screen]DL time. */
static int32 parent_patternid = INVALID_PATTERN_ID ;

Bool pattern_executingpaintproc(int32 *id)
{
  if ( id )
    *id = parent_patternid ;
  return parent_patternid != INVALID_PATTERN_ID ;
}

static void snap_degenerate_stepping_vectors(SYSTEMVALUE *xstepx, SYSTEMVALUE *xstepy,
                                             SYSTEMVALUE *ystepx, SYSTEMVALUE *ystepy)
{
  /* Check for stepping vectors degenerating to (0,0), which could lead
     to excessively slow pattern replication.  Only snap the vectors to
     (1,0) if we are within 1/10th of a pixel of (0,0), otherwise the
     effect of the snapping could be to change the pattern's angle of
     rotation. */
  if ( *xstepx < 0.1 && *xstepy < 0.1 && *xstepy > -0.1 ) {
    *xstepx = 1.0; /* step significance lost - it is < 1 pixel */
    *xstepy = 0.0; /* this gives the same result */
  } else if ( !intrange(*xstepx) || !intrange(*xstepy) ) {
    /* note that all arms are checked here to catch infinities */
    *xstepx = BIGGEST_INTEGER - 1.0 ; /* step significance silly  - it is vast */
    *xstepy = 0.0;
  }
  if ( *ystepy < 0.1 && *ystepx < 0.1 && *ystepx > -0.1 ) {
    *ystepy = 1.0; /* ditto */
    *ystepx = 0.0;
  } else if ( !intrange(*ystepx) || !intrange(*ystepy) ) {
    *ystepy = BIGGEST_INTEGER - 1.0 ;
    *ystepx = 0.0;
  }
}

static Bool run_pattern_proc(DL_STATE *page, OBJECT *pppattern,
                             OBJECT *paintproc, OBJECT *shading)
{
  corecontext_t *context = get_core_context_interp();
  Bool result = FALSE ;

  if ( gs_gpush( GST_GSAVE ) ) {
    /* Execute it, to place the pattern definition on the local
     * display list. The assumption here is that an array or packed
     * array PaintProc means execute it as PS, but a file PaintProc
     * must be a PDF stream (in which case it must have a nonzero
     * PDF context ID - pdf_exec_stream checks for this).
     */
    if ( shading ) {
      if ( push( shading, &operandstack ))
        result = gs_shfill(&operandstack, gstateptr, GS_SHFILL_PATTERN2);
    } else if ( oType(*paintproc) == OFILE ) {
      HQASSERT( oFile(*paintproc) != NULL, "flptr NULL in pattern file" ) ;
      result = pdf_exec_stream( paintproc , PDF_STREAMTYPE_PATTERN );
    }
    else {
      int32 stacksize = theStackSize(operandstack);
      int32 dictstacksize = theStackSize(dictstack);

      if ( push( pppattern, &operandstack )) {
        if ( push( paintproc, &executionstack )) {
          result = interpreter(1, NULL) ;
          if ( !result ) {
            /* Can't test newerror as it gets cleared by interpreter */
            if ( error_latest_context(context->error) == VMERROR ) {
              /* Remove junk left on the operand and dictionary stacks
                 after VMERROR */
              if ( theStackSize(operandstack) > stacksize ) {
                npop(theStackSize(operandstack) - stacksize, &operandstack);
              }

              /* must use end_() repeatedly rather than npop for dictstack */
              while ( theStackSize(dictstack) > dictstacksize &&
                      end_(context->pscontext) )
                EMPTY_STATEMENT();
            }
          }
        } else {
          pop( & operandstack );
        }
      }
    }

    /* Ensure pattern objects end up on correct HDL */
    if ( result ) {
      if ( !flush_vignette(VD_Default) )
        result = FALSE ;
    } else
      abort_vignette(page) ;
  }

  return result ;
}

/** createPatternDL()
 *
 * Create a pattern sub-display list to attach to the display list state
 *
 * This calls the recursive interpreter using interpreter_dl_safe() to prevent
 * partial paints ( which free display list ), and traps the VMERRORs which
 * may result. However this means the outermost enclosing C routine which
 * allocates display list memory ( presently setg ) must handle VMERRORS by
 * explicitly calling handleLowMemory() and retrying. Retrying is safe because
 * paint procedures cant have side effects.
 */
static Bool createPatternDL(DL_STATE *page, int32 id,
                            OBJECT *pppattern, OBJECT *impl,
                            int32 idlomHit, int32 colorType,
                            Bool overprinting[2], TranAttrib *tranattrib,
                            PATTERNOBJECT *patobj)
{
  USERPARAMS *userparams = get_core_context_interp()->userparams;
  OBJECT *theo;
  OBJECT *paintproc = NULL, *shading = NULL ;
  int32 painttype, tilingtype, patterntype ;
  Bool old_imposition, result;
  pattern_nonoverlap_t nonoverlap = { FALSE, 0, 0, 0, 0 } ;
  GSTATE *gs ;
  OMATRIX m1 ; /* CTM to be installed */
  SYSTEMVALUE bb[4], xstep, ystep;
  SYSTEMVALUE xstepx, xstepy, ystepx, ystepy;
  SYSTEMVALUE bbx, bby, bsizex, bsizey;
  int32 w, h ;
  int32 previous_context_patternid = context_patternid ;
  int32 previous_parent_patternid = parent_patternid ;

  HQASSERT(pppattern, "No pattern object") ;

  HQASSERT(oType(*pppattern) == ODICTIONARY, "Corrupt pattern dictionary") ;

  HQASSERT(impl, "No pattern implementation") ;
  HQASSERT(impl == fast_extract_hash_name(pppattern, NAME_Implementation),
           "Pattern implementation does not match pattern dictionary") ;
  if ( ! check_pia_valid( impl )) {
    HQTRACE( debug_pattern, ("createPatternDL: corrupt pattern detected")) ;
    return error_handler( UNDEFINED );
  }

  painttype = oInteger(oArray(*impl)[PIA_PAINTTYPE]) ;
  tilingtype = oInteger(oArray(*impl)[PIA_TILINGTYPE]) ;

  HQASSERT( patobj , "patobj NULL in createPatternDL" ) ;

  HQTRACE( debug_pattern, ("createPatternDL: painttype %d, tilingtype %d clip %d - p %x\n",
                           painttype, tilingtype, patobj )) ;

  gs = oGState(oArray(*impl)[PIA_GSTATE]) ;
  HQASSERT( gs , "gs NULL in createPatternDL" ) ;

  MATRIX_COPY(&m1, &thegsPageCTM(*gs)) ;

  xstep = object_numeric_value(oArray(*impl) + PIA_XSTEP) ;
  ystep = object_numeric_value(oArray(*impl) + PIA_YSTEP) ;

  bb[0] = object_numeric_value(oArray(*impl) + PIA_LLX) ;
  bb[1] = object_numeric_value(oArray(*impl) + PIA_LLY) ;
  bb[2] = object_numeric_value(oArray(*impl) + PIA_URX) ;
  bb[3] = object_numeric_value(oArray(*impl) + PIA_URY) ;

  theo = fast_extract_hash_name( pppattern, NAME_PatternType );
  if ( ! theo )
    return error_handler( UNDEFINED );
  patterntype = oInteger(*theo) ;

  switch ( patterntype ) {
  case 1:
    /* Apply tiling type override, if there is one. */
    if ( tilingtype != 0 &&
         userparams->OverridePatternTilingType >= 1 &&
         userparams->OverridePatternTilingType <= 3 )
      tilingtype = userparams->OverridePatternTilingType ;
    /* FALLTHROUGH */

  case 101: /* Possibly untiled for XPS. */
  case 102: /* Untiled pattern for XPS gradients, requiring KO group. */
    paintproc = fast_extract_hash_name( pppattern, NAME_PaintProc );
    if ( ! paintproc )
      return error_handler( UNDEFINED );

    HQASSERT(painttype == COLOURED_PATTERN || painttype == UNCOLOURED_PATTERN,
             "Invalid painttype for tiled pattern" ) ;
    HQASSERT(tilingtype >= 0 && tilingtype <= 3,
             "Invalid tilingtype for tiled pattern" ) ;

    /* now make adjustments to the matrix if required to ensure nice tiling
     * according to the TilingType.  Also set up the deltas in pixels.
     */
    if ( tilingtype != 0 ) {
      SYSTEMVALUE xx, fy, fx, yy, a, b, area;
      int32 i ;

      xx = m1.matrix[0][0];
      fy = m1.matrix[1][0];
      fx = m1.matrix[0][1];
      yy = m1.matrix[1][1];

      area = (bb[2]-bb[0])*(bb[3]-bb[1]);
      if ( fabs(area) < 1 ) /* if width/height are zero, default to unit square */
        area = 1;

      if (fabs((xx*yy-fy*fx)*area) < EPSILON)
        return error_handler( UNDEFINEDRESULT );

      if ( tilingtype == 2 ) {
        /* the exact spacing specified is required on average, so
         * store xstep, ystep translated into real and positive pixels.
         */
        xstepx = xstep * xx;
        xstepy = xstep * fx;
        ystepx = ystep * fy;
        ystepy = ystep * yy;
        if ( xstepx < 0.0 ) { xstepx = -xstepx; xstepy = -xstepy; }
        if ( ystepy < 0.0 ) { ystepy = -ystepy; ystepx = -ystepx; }

        /* For tiling type 2 patterns which are intended to tesselate, stretch
           the key cell by up to one pixel to help avoid gaps later on. */
#define TESSELLATE_EPSILON 0.0001
        if ( ( ( m1.opt == MATRIX_OPT_0011 && xx != 0 && yy != 0 ) ||
               ( m1.opt == MATRIX_OPT_1001 && fx != 0 && fy != 0 ) ) &&
             fabs(fabs(bb[2] - bb[0]) - fabs(xstep)) < TESSELLATE_EPSILON &&
             fabs(fabs(bb[3] - bb[1]) - fabs(ystep)) < TESSELLATE_EPSILON ) {
          SYSTEMVALUE blx, bly, brx, bry, tlx, tly, magx, magy ;

          blx = bb[0] * xx + bb[1] * fy ; /* bottom-left */
          bly = bb[0] * fx + bb[1] * yy ;
          brx = bb[2] * xx + bb[1] * fy ; /* bottom-right */
          bry = bb[2] * fx + bb[1] * yy ;
          tlx = bb[0] * xx + bb[3] * fy ; /* top-left */
          tly = bb[0] * fx + bb[3] * yy ;

          /* Magnitude of the key cell's X and Y vectors, in device coords. */
          magx = sqrt( (brx - blx) * (brx - blx) + (bry - bly) * (bry - bly) ) ;
          magy = sqrt( (tlx - blx) * (tlx - blx) + (tly - bly) * (tly - bly) ) ;

          if ( magx != 0 && magy != 0 ) {
            SYSTEMVALUE xfac = ceil(magx) / magx ;
            SYSTEMVALUE yfac = ceil(magy) / magy ;
            SYSTEMVALUE d_xx = fabs(xstepx), d_xy = fabs(xstepy),
                        d_yx = fabs(ystepx), d_yy = fabs(ystepy) ;

            snap_degenerate_stepping_vectors(&d_xx, &d_xy, &d_yx, &d_yy) ;

            matrix_translate(&m1, bb[0], bb[1], &m1) ;
            matrix_scale(&m1, xfac, yfac, &m1) ;
            matrix_translate(&m1,  -bb[0], -bb[1], &m1) ;

            /* Update xx etc, but don't change the stepping vectors. */
            xx = m1.matrix[0][0];
            fy = m1.matrix[1][0];
            fx = m1.matrix[0][1];
            yy = m1.matrix[1][1];

            /* Pattern replicator must handle overlapping tile edges and remove
               the double paint. Note, this flag may be subsequently cleared
               when more is known about the pattern DL to try to workaround fine
               lines dropping in some cases.  The original stepping vectors are
               required for tile edge detection, not the ones optimised for
               replication convergence. */
            nonoverlap.enabled = TRUE ;
            nonoverlap.xx = (USERVALUE)d_xx ;
            nonoverlap.xy = (USERVALUE)d_xy ;
            nonoverlap.yx = (USERVALUE)d_yx ;
            nonoverlap.yy = (USERVALUE)d_yy ;

          }
        }

      } else { /* tilingtype == 1 || tilingtype == 3 */
        /* constant spacing required.
         * NB. Both tilingtypes are treated the same because performance can't be
         * improved very much and any difference in output isn't worth the hassle.
         */
        SYSTEMVALUE x,y;
        int notplusq; /* flag whether in first quadrant */
        SYSTEMVALUE rounder = 0.5; /* round to whole pixels */

        /* deal with the _step_ ( xstep, 0 ) */
        if ( xstep < 0.0 ) xstep = -xstep;
        x = xstep * xx;
        y = xstep * fx;
        /* these values are the steps in pixels for the xstep in pattern space */

        notplusq = 0;
        if ( x < 0.0 ) { notplusq ^= 1; x = -x; }
        if ( y < 0.0 ) { notplusq ^= 1; y = -y; }

        if ( x < rounder && y < rounder ) {
          SYSTEMVALUE minstep = rounder * 2.0 ;

          /* We cannot allow the step to round to zero. Project the step so
             the longer edge is the minimum replication step, and let it round
             from there. This preserves the general direction of the step. */
          if ( x > y ) {
            y = y * minstep / x ;
            x = minstep ;
          } else {
            x = x * minstep / y ;
            y = minstep ;
          }
        }

        /* round the pixel deltas as required */
        xstepx = (SYSTEMVALUE)((int32)(x + rounder));
        xstepy = (SYSTEMVALUE)((int32)(y + rounder));

        HQASSERT(xstepx > 0 || xstepy > 0, "Rounded step to zero") ;

        /* now all we want to do is xx = xstepx / xstep; but preserve sign */

#define ADJFACTOR(a) if ( a >= 100.0 ) a = 0.5; else a /= 200.0
        /* and round up by 1/2 a pixel plus delta to ensure patterns tile. */
        ADJFACTOR(x);
        ADJFACTOR(y);

        if ( xx >= 0.0 ) xx =   (xstepx + x) / xstep;
        else             xx = - (xstepx + x) / xstep;
        /* similarly set fx */
        if ( fx >= 0.0 ) fx =   (xstepy + y) / xstep;
        else             fx = - (xstepy + y) / xstep;

        /* now set back to the correct half of the plane - in this case
         * that where x > 0, so flip xstepy if not in the +ve quadrant.
         */
        if ( notplusq ) xstepy = -xstepy;

        /* now the same process for the _step_ ( 0, ystep ) */
        if ( ystep < 0.0 ) ystep = -ystep;
        x = ystep * fy;
        y = ystep * yy;
        notplusq = 0;
        if ( x < 0.0 ) { notplusq ^= 1; x = -x; }
        if ( y < 0.0 ) { notplusq ^= 1; y = -y; }

        if ( x < rounder && y < rounder ) {
          SYSTEMVALUE minstep = rounder * 2.0 ;

          /* We cannot allow the step to round to zero. Project the step so
             the longer edge is the minimum replication step, and let it round
             from there. This preserves the general direction of the step. */
          if ( x > y ) {
            y = y * minstep / x ;
            x = minstep ;
          } else {
            x = x * minstep / y ;
            y = minstep ;
          }
        }

        ystepx = (SYSTEMVALUE)((int32)(x + rounder));
        ystepy = (SYSTEMVALUE)((int32)(y + rounder));

        HQASSERT(ystepx > 0 || ystepy > 0, "Rounded step to zero") ;

        ADJFACTOR(x);
        ADJFACTOR(y);
#undef ADJFACTOR

        if ( fy >= 0.0 ) fy =   (ystepx + x) / ystep;
        else             fy = - (ystepx + x) / ystep;
        if ( yy >= 0.0 ) yy =   (ystepy + y) / ystep;
        else             yy = - (ystepy + y) / ystep;

        if ( notplusq ) ystepx = -ystepx;

        /* now [xy]step[xy] are set and in their respective positive
         * half planes, and {xx,yy,fx,fy} are set
         */

        /* so adjust the CTM to match */
        m1.matrix[0][0] = xx;
        m1.matrix[1][0] = fy;
        m1.matrix[0][1] = fx;
        m1.matrix[1][1] = yy;
        HQASSERT( matrix_assert( &m1 ) , "different result expected" ) ;
      }

      /* So now the state is that (xstepx, xstepy) is the transformed xstep,
       * in pixel coordinates, and xstepx >= 0.0 .  (ystepx, ystepy) is the
       * transformed ystep, ystepy >= 0.0 .
       *   The next stage is to ensure that the xstep is the least "steep",
       * i.e. closest to the x axis of the two, by swapping them if necessary.
       */
      a = xstepy * ystepx;
      if ( a < 0.0 ) a = -a;
      b = xstepx * ystepy;
      /* this says if ( fabs(ystepy/ystepx) < fabs(xstepy/xstepx) ) */
      if ( b < a ) {
        /* then swap, x is steeper */
        a = xstepx; xstepx = ystepx; ystepx = a;
        a = xstepy; xstepy = ystepy; ystepy = a;
        /* and reassert correct halfplane position */
        if ( xstepx < 0.0 ) { xstepx = -xstepx; xstepy = -xstepy; }
        if ( ystepy < 0.0 ) { ystepy = -ystepy; ystepx = -ystepx; }
      }

      /* And now the same holds, but we know that the transformed xstep
       * is closest to horizontal in device coordinates, the remaining
       * useful preparation is to ensure that the transformed ystep as steep
       * as possible, so that the replication algorithm converges as fast as
       * possible.
       */

      /* Add enough X steps to take the Y step as close as possible ot the
         vertical axis. Since X step is shallower than Y step, this must be
         steeper than the existing Y step. */
      i = (int32)(ystepx/xstepx) ;
      ystepx -= (SYSTEMVALUE)i * xstepx;
      ystepy -= (SYSTEMVALUE)i * xstepy;

      /* It is possible that adding or subtracting another X step to bring the
         Y step past vertical may give a steeper vector */
      {
        SYSTEMVALUE tempx, tempy ;

        if ( ystepx > 0.0 ) {
          tempx = ystepx - xstepx ;
          tempy = ystepy - xstepy ;
        } else {
          tempx = ystepx + xstepx ;
          tempy = ystepy + xstepy ;
        }

        /* Is it steeper than old Y step? */
        if ( fabs(tempy * ystepx) > fabs(ystepy * tempx) ) {
          ystepx = tempx ;
          ystepy = tempy ;
        }
      }

      /* Hugo says that geometry says that ystep remains in the y > 0.0 half
       * plane and that the whole plane is still tiled by xstep and the new
       * ystep. Mind you, he was wrong last time :-)
       */

      HQASSERT(ystepy > 0.0, "Y step not in +ve Y hemicircle") ;
      HQASSERT(xstepx > 0.0, "X step not in +ve X hemicircle") ;

      /* now we store the coordinates of the top-left corner of a bounding box
       * of the transformed bbox, so we can look at rendering coordinates
       * relative to it, as they will all be relatively +ve.
       * (because y coordinates are upside down in coordinate space)
       */
      bsizex = bbx = bb[0] * xx + bb[1] * fy; /* botleft */
      bsizey = bby = bb[0] * fx + bb[1] * yy;

      a = bb[2] * xx + bb[1] * fy; /* botright */
      b = bb[2] * fx + bb[1] * yy;
      if ( a < bbx ) bbx = a;
      if ( b < bby ) bby = b;
      if ( a > bsizex ) bsizex = a;
      if ( b > bsizey ) bsizey = b;

      a = bb[2] * xx + bb[3] * fy; /* topright */
      b = bb[2] * fx + bb[3] * yy;
      if ( a < bbx ) bbx = a;
      if ( b < bby ) bby = b;
      if ( a > bsizex ) bsizex = a;
      if ( b > bsizey ) bsizey = b;

      a = bb[0] * xx + bb[3] * fy; /* topleft */
      b = bb[0] * fx + bb[3] * yy;
      if ( a < bbx ) bbx = a;
      if ( b < bby ) bby = b;
      if ( a > bsizex ) bsizex = a;
      if ( b > bsizey ) bsizey = b;

      bsizex -= bbx; /* the size of the transformed bounding box's bounding box*/
      bsizey -= bby; /* needed for determining tiling grid points later. */

      if ( bsizex < 0 || bsizey < 0 )
        return error_handler(RANGECHECK);

      /* do this afterward rather than at each stage - the result is the same */
      xx = bbx;
      yy = bby;
      bbx += m1.matrix[2][0];
      bby += m1.matrix[2][1];

      /* and move the pattern to 0,0 in device space */
      m1.matrix[2][0] = -xx + SC_PIXEL_OFFSET ;
      m1.matrix[2][1] = -yy + SC_PIXEL_OFFSET ;

      /* now we must do some range checking: */
      if ( !intrange(bbx) || !intrange(bby) ) {
        bbx = bby = 0.0; /* phase information has lost significance */
      }

      /* Check for stepping vectors degenerating to (0,0), which could lead
         to excessively slow pattern replication.  */
      snap_degenerate_stepping_vectors(&xstepx, &xstepy, &ystepx, &ystepy) ;

      w = (int32)bsizex + 1 + 1 /* -1 */ ;
      h = (int32)bsizey + 1 + 1 /* -1 */ ;

      HQASSERT(xstepx != 0.0 || xstepy != 0.0 || ystepx != 0.0 || ystepx != 0.0,
               "degenerate step size") ;

      HQASSERT(fabs(xstepx*ystepy - xstepy*ystepx) > EPSILON,
               "degenerate matrix - should have been mapped to 0 matrix");

      /* Catch an idiot way of turning off pattern tiling (added for task 60626).
         Compare the magnitude of the stepping vectors with the magnitude of the
         device width and height.  Also, only disable tiling if there is a
         substantial space between the tiles, ie four times the width/height. */
#define TILE_SPACE_MULTIPLE (4) /* arbitrary really */
      if ( fabs(xstep) > (TILE_SPACE_MULTIPLE * fabs(bb[2] - bb[0])) &&
           fabs(ystep) > (TILE_SPACE_MULTIPLE * fabs(bb[3] - bb[1])) &&
           (xstepx * xstepx + xstepy * xstepy) > (page->page_w * page->page_w) &&
           (ystepx * ystepx + ystepy * ystepy) > (page->page_h * page->page_h) ) {
        /* Stepping is so large you'll never see any replication, therefore
           we'll turn tiling off to avoid a big unnecessary slowdown in the
           tiling replicators. m1 was adjusted for tiling and needs
           resetting to its original value. */
        MATRIX_COPY(&m1, &thegsPageCTM(*gs)) ;
        tilingtype = 0 ;
        xstepx = xstepy = 0.0 ;
        ystepx = ystepy = 0.0 ;
        bbx = bby = 0.0 ;
        bsizex = w = thegsDeviceW(*gs) ;
        bsizey = h = thegsDeviceH(*gs) ;
        /* Could make this conditional on debug_pattern, but this case shouldn't
           happen often and it is very useful to know when it has happened. */
        HQTRACE( TRUE /* debug_pattern */ ,
                 ("createPatternDL: disabling pattern tiling - xstep/ystep too large!")) ;
      }

    } else { /* tilingtype == 0 */
      xstepx = xstepy = 0.0 ;
      ystepx = ystepy = 0.0 ;
      bbx = bby = 0.0 ;
      bsizex = w = thegsDeviceW(*gs) ;
      bsizey = h = thegsDeviceH(*gs) ;
    }

    /* PatternOverprintOverride applies differently between pattern types 1 and 2. */
    if (userparams->PatternOverprintOverride) {
      /* Acrobat 8 initialises pattern type 1 overprinting to false
         (i.e. knocking out). */
      if ( !gsc_setoverprint(gs->colorInfo, GSC_FILL, FALSE) ||
           !gsc_setoverprint(gs->colorInfo, GSC_STROKE, FALSE) )
        return FALSE;
    }

    break ;
  case 2:
    shading = fast_extract_hash_name( pppattern, NAME_Shading );
    if ( ! shading || oType(*shading) != ODICTIONARY )
      return error_handler( UNDEFINED );

    HQASSERT(painttype == COLOURED_PATTERN,
             "Invalid painttype for shading pattern" ) ;
    HQASSERT(tilingtype == 0,
             "Invalid tilingtype for shading pattern" ) ;

    xstepx = xstepy = 0.0 ;
    ystepx = ystepy = 0.0 ;
    bbx = bby = 0.0 ;
    bsizex = w = thegsDeviceW(*gs) ;
    bsizey = h = thegsDeviceH(*gs) ;

    /* PatternOverprintOverride applies differently between pattern types 1 and 2. */
    if (userparams->PatternOverprintOverride) {
      /* Override the overprint settings in the pattern gstate with the
         overprint settings of the pattern'd object. */
      if (! gsc_setoverprint(gs->colorInfo, GSC_FILL, overprinting[GSC_FILL]))
        return FALSE;
      if (! gsc_setoverprint(gs->colorInfo, GSC_STROKE, overprinting[GSC_STROKE]))
        return FALSE;
    }

    break ;
  default:
    return error_handler( UNDEFINED );
  }

  /* Set painttype and tiling type */
  patobj->painttype = CAST_TO_UINT8(painttype) ;
  patobj->tilingtype = CAST_TO_UINT8(tilingtype) ;

  /* Set the tiling method. The high-level tiling method is faster if a small
     part of one tile is required per band (e.g. if the pattern DL contains a
     large image). Blit-level tiling is faster when many tiles are required per
     band. Recursive patterns always need to use blit-level tiling as it is
     difficult to handle different tiling contexts at the high level. May switch
     from high- to blit-level tiling later if the pattern DL contains recursive
     patterns to ensure correct tiling contexts, or if the pattern DL contains
     transparency which may require intersect clipping, currently incompatible
     with high-level tiling. */
  if ( tilingtype == 0 )
    patobj->tilingmethod = TILING_NONE;
  else if ( /* Possibly many tiles per band (actually a fraction of page height) */
#define SMALL_TILE_FAC 20 /* set empirically, from customer jobs */
            (double)bsizex * SMALL_TILE_FAC < page->page_h ||
            (double)bsizey * SMALL_TILE_FAC < page->page_h ||
            /* Recursive pattern */
            parent_patternid != INVALID_PATTERN_ID )
    patobj->tilingmethod = TILING_BLIT_LEVEL;
  else
    patobj->tilingmethod = TILING_HIGH_LEVEL;

  patobj->backdrop = FALSE;
  patobj->nonoverlap = nonoverlap ;

  /* track whether pattern contains only knockout objects, this is
   * important in prevent XPS patterns compositing unnecessarily (by
   * patterned objects appearing to be overprinted). This flag set
   * when any overprinted object added to pattern, not just when overrides
   * are applied. It applies to tiled patterns as well as shading patterns.
   */
  patobj->patternDoesOverprint = FALSE ;

  patobj->overprinting[GSC_FILL] = CAST_TO_UINT8(overprinting[GSC_FILL]);
  patobj->overprinting[GSC_STROKE] = CAST_TO_UINT8(overprinting[GSC_STROKE]);

  /* the stepping values are real */
  patobj->xx = (USERVALUE)xstepx ;
  patobj->xy = (USERVALUE)xstepy ;
  patobj->yx = (USERVALUE)ystepx ;
  patobj->yy = (USERVALUE)ystepy ;

  patobj->bbx = (USERVALUE)bbx ;
  patobj->bby = (USERVALUE)bby ;

  patobj->bsizex = -(USERVALUE)bsizex ; /* N.B. stored negated */
  patobj->bsizey = -(USERVALUE)bsizey ;

  patobj->patternid = id;
  patobj->parent_patternid = parent_patternid ;
  patobj->context_patternid = oInteger(oArray(*impl)[PIA_CONTEXT_PID]) ;
  patobj->pageBaseMatrixId = pageBaseMatrixId ;
  patobj->ta = tranattrib;
  /* Remember the group id for pattern lookups.  The group might later
     be removed, but all that means is further lookups won't match and
     the patterns are not shared. */
  patobj->groupid = page->currentGroup != NULL
    ? groupId(page->currentGroup) : HDL_ID_INVALID;
  patobj->opcode = RENDER_void;
  patobj->dldata.hdl = NULL ;

  old_imposition = doing_imposition;
  doing_imposition = FALSE;

  result = FALSE ;

#define return DO_NOT_RETURN_GO_TO_createPatternDL_fail_INSTEAD!

  parent_patternid  = id ;
  context_patternid = id ;

  /* Previous code here used to disable HDLT before we do the first gs_gpush
   * (and re-enable it before the gs_cleargstates) on the grounds that
   * this prevents spurious clip objects.
   * I believe this to be incorrect since the gstate prepared by makepattern,
   * and installed here has no clip; objects drawn inside this pattern will
   * thus be reported as having no clip, so there ought to be an HDLT clip
   * object callback telling us that the clipping has changed.
   */
  if ( gs_gpush( GST_PATTERN )) {
    int32 gid = gstackptr->gId ;
    int32 old_idlomstate = theIdlomState( *gstackptr ) ;
    int32 oldCurrentDevice = DEVICE_ILLEGAL;
    Bool enable_recombine_interception = FALSE;

    if (rcbn_enabled() &&
        (painttype == UNCOLOURED_PATTERN ||
         painttype == UNCOLOURED_TRANSPARENT_PATTERN)) {
      rcbn_disable_interception( gstateptr->colorInfo );
      enable_recombine_interception = TRUE;
      oldCurrentDevice = CURRENT_DEVICE();
    }

    /* The rasterstyle inside a pattern is the same as the rasterstyle in its
       surrounding context, not the rasterstyles of the saved pattern
       context. */
    HQASSERT(gsc_getRS(gs->colorInfo) == gsc_getRS(gstateptr->colorInfo),
             "Inconsistent device raster styles");
    gsc_setTargetRS(gs->colorInfo, gsc_getTargetRS(gstateptr->colorInfo)) ;

    if ( gs_setgstate( gs, GST_GSAVE, TRUE, FALSE, FALSE, NULL )) {
      Bool pdfout_was_enabled;

      /* HDLT has already cached this pattern but we haven't so we need
       * to disable HDLT whilst we (re)paint it - and re-enable when we've
       * finished.
       */
      old_idlomstate = theIdlomState( *gstateptr ) ;
      if ( idlomHit == IB_CacheHit )
        theIdlomState( *gstateptr ) = HDLT_DISABLED ;

      /* Must remember if PDF Out is enabled or not
         (before pdfout_beginpattern disables it) */
      pdfout_was_enabled = pdfout_enabled();

      HQASSERT( ( thegsDeviceW( *gstateptr ) == w && thegsDeviceH( *gstateptr ) == h ) ||
                tilingtype != 0, "untiled patterns should not change device width and height") ;
      thegsDeviceW( *gstateptr ) = w ;
      thegsDeviceH( *gstateptr ) = h ;

      gs_freecliprec(&gstateptr->thePDEVinfo.initcliprec) ;

      /* Sets the device matrix to be the same as the page matrix, for the
       * benefit of HDLT and pdfout, and cos this is a sensible thing to do *
       * - e.g. so 'matrix defaultmatrix' inside a patterns paintproc gives *
       * something sensible, and for fonts inside patterns.
       */
      if ( !gs_setdefaultctm( &m1 , FALSE ) ||
           !gs_setctm( &m1 , FALSE ) )
        goto createPatternDL_fail ;

      /* Reset the clip because the device width and height may have changed.
         Device width and height won't have changed for untiled patterns (this
         is asserted above), and any clipping needs to be kept to ensure the
         banded Group/HDL is setup correctly. */
      if ( tilingtype != 0 && !gs_initclip(gstateptr) )
        goto createPatternDL_fail ;

      /* Rendering color for self-coloured patterns is defined by the pattern.
       * For uncoloured patterns the pattern 'paints' a mask which is then
       * used in rendering other objects.
       */
      if ( painttype == UNCOLOURED_PATTERN ) {
        OBJECT *pspace ;
        USERVALUE *ivalues ;
        int32 dummyDims;

        /* We need to use the colorspace and color values from the current
         * context rather than the cached pattern context. It is necessary to
         * copy these attributes from the next gstate down the stack to do this.
         */
        HQASSERT(gsc_getcolorspace(gstackptr->colorInfo, colorType) == SPACE_Pattern,
                 "Parent object is not patterned") ;

        gsc_getcolorvalues(gstackptr->colorInfo, colorType, &ivalues, &dummyDims);
        pspace = gsc_getcolorspaceobject(gstackptr->colorInfo, colorType);

        if ( !gsc_setpatternmaskdirect(gstateptr->colorInfo, GSC_FILL, pspace, ivalues) ||
             !gsc_setpatternmaskdirect(gstateptr->colorInfo, GSC_STROKE, pspace, ivalues) ) {
          goto createPatternDL_fail ;
        }
      }

      /* Take a local copy of the pattern dict for the HDLT callbacks to
         refer to. The pattern callback procedure will consume it, but HDLT
         needs a reference to the object to hang onto between the begin and
         end target callbacks. */
      if ( push(pppattern, &temporarystack) ) {
        PDFCONTEXT **ppdfout_h = &get_core_context_interp()->pdfout_h;
        pppattern = theTop(temporarystack) ;

        if ( IDLOM_BEGINPATTERN(pppattern, id) != IB_PSErr &&
             (! pdfout_was_enabled ||
              pdfout_beginpattern(ppdfout_h, id, pppattern))) {
          RECTANGLE bbrect ;

          /* now, after adjusting the matrix, clip to the BBox */
          bbrect.x = bb[0] ;
          bbrect.y = bb[1] ;
          bbrect.w = bb[2] - bb[0] ;
          bbrect.h = bb[3] - bb[1] ;

          if ( patterntype == 2 || /* No clip for shading patterns */
               cliprectangles( &bbrect, 1 ) ) {
            HDL *hdl = NULL;
            dbbox_t save_clip;

            /* all this because the "recursive interpreter" is not generally
             * recursive; it drags a whole load of state down with it unless
             * we prevent destruction thereof.
             */
            save_clip = cclip_bbox;
            cclip_bbox.x1 = 0;
            cclip_bbox.y1 = 0;
            SC_C2D_INT( cclip_bbox.x2, -patobj->bsizex ) ;
            SC_C2D_INT( cclip_bbox.y2, -patobj->bsizey ) ;

            /* Create a group to capture the pattern cell's display list.
               If no transparency is used within the group, then the HDL
               will be extracted from the group and used on its own. */
            if ( gs_gpush(GST_GROUP) ) {
              int32 ggid = gstackptr->gId ;
              /** \todo @@@ TODO FIXME ajcd 2002/12/15: We know the
                  bounding box for non-shading patterns, supply it. */
              if ( groupOpen(page, onull /* colourspace */, FALSE /* not isolated */,
                             (patterntype == 2 || patterntype == 102) /* knockout for shading */,
                             (tilingtype == 0) /* banded for untiled */,
                             NULL /* bgcolor */, NULL /* xferfn */, tranattrib,
                             GroupPattern, &patobj->dldata.group) ) {
                patobj->opcode = RENDER_group;

                result = run_pattern_proc(page, pppattern, paintproc, shading) ;

                if ( groupClose(&patobj->dldata.group, result) ) {
                  if ( patobj->dldata.group == NULL ||
                       hdlIsEmpty(groupHdl(patobj->dldata.group)) ) {
                    /* Discard created subdl; DL memory will be recovered at
                       end of page. This is consistent with previous usage. */
                    patobj->opcode = RENDER_void;
                    patobj->dldata.group = NULL;
                  } else {
                    Bool eliminate;

                    /* Check to see if the group wrapped around the pattern
                       was really needed, if not, remove it. */
                    if ( !groupEliminate(patobj->dldata.group, tranattrib,
                                         &eliminate) )
                      result = FALSE;
                    else {
                      if ( eliminate ) {
                        Group *group = patobj->dldata.group;
                        patobj->opcode = RENDER_hdl;
                        patobj->dldata.hdl = groupHdlDetach(group);
                        groupDestroy(&group);
                      }

                      hdl = patternHdl(patobj);

                      /* Determine if the pattern DL is transparent;
                         Note that the group being retained does not
                         correlate with this. */
                      if ( hdlTransparent(hdl) )
                        patobj->backdrop = TRUE ;

                      /* Record if the pattern uses overprinted objects. If a
                         pattern is overprinted, compositing may be required. */
                      if ( hdlOverprint(hdl) )
                        patobj->patternDoesOverprint = TRUE;

                      /* Uncoloured and coloured patterns with transparency must
                         be rendered individually, not coalescing patterned
                         objects. */
                      if ( patobj->backdrop || !tranAttribIsOpaque(tranattrib) ) {
                        if ( patobj->painttype == UNCOLOURED_PATTERN )
                          patobj->painttype = UNCOLOURED_TRANSPARENT_PATTERN ;
                        else if ( patobj->painttype == COLOURED_PATTERN )
                          patobj->painttype = COLOURED_TRANSPARENT_PATTERN ;
                      }
                    }
                  }
                } else /* groupClose failed */
                  result = FALSE ;
              }
              if ( !gs_cleargstates(ggid, GST_GROUP, NULL) )
                result = FALSE ;
            }

            /* Tiling type 2 (real number stepping) can result in unintended
               gaps between tiles.  To try to avoid this we stretch the key cell
               by up to one device pixel and detect any overlap between tiles.
               Stretching the key cell and detecting overlapping edges can
               result in single pixel lines in the last column or row being
               dropped, so we only remove the double paint if transparency is
               involved. */
            if ( patobj->nonoverlap.enabled && hdl != NULL
                 && !(patobj->backdrop || !tranAttribIsOpaque(tranattrib)) ) {
              DLREF *bands = hdlOrderList(hdl) ;
              /* PS/PDF pattern or XPS visual brush (where fine lines may drop out). */
              if ( patterntype == 1 ||
                   (patterntype == 101 && dlref_lobj(bands)->opcode != RENDER_image &&
                    !dlref_next(bands)) )
                patobj->nonoverlap.enabled = FALSE ;
            }

            /* Switch from high-level tiling to blit-level tiling if the pattern
               DL contains recursive patterns to ensure correct tiling contexts,
               or if the pattern DL contains transparency which may require
               intersect clipping, currently incompatible with high-level
               tiling. */
            if ( patobj->tilingmethod == TILING_HIGH_LEVEL &&
                 hdl != NULL && (hdlPatterned(hdl) || patobj->backdrop) )
              patobj->tilingmethod = TILING_BLIT_LEVEL;

            if ( !result ) {
              HQTRACE( debug_pattern, ("interpreter_dl_safe()/pdf_exec_stream() failed\n" )) ;
              /* so discard created subdl */
              patobj->opcode = RENDER_void;
              patobj->dldata.hdl = NULL;
            }

            cclip_bbox = save_clip;
          }

          if (pdfout_was_enabled &&
              ! pdfout_endpattern(ppdfout_h, id, pppattern, result))
            result = FALSE;
          if (! IDLOM_ENDPATTERN(result))
            result = FALSE;
        }

        pppattern = NULL ; /* Reference to tempstack is becoming invalid. */
        pop(&temporarystack) ;
      }

    createPatternDL_fail:
      EMPTY_STATEMENT() ;
    }

    if (enable_recombine_interception) {
      /* SET_DEVICE needed because of a hidden call to gsc_setscreens in
       * rcbn_enable_interception which would otherwise fail */
      SET_DEVICE(oldCurrentDevice);
      rcbn_enable_interception(gstateptr->colorInfo);
    }

    /* Re-enable HDLT if we had to disable it */
    theIdlomState( *gstateptr ) = ( int8 )old_idlomstate ;
    if ( ! gs_cleargstates( gid , GST_PATTERN , NULL ))
      result = FALSE ;
  }

  doing_imposition = old_imposition ;

  parent_patternid = previous_parent_patternid ;
  context_patternid = previous_context_patternid ;

#undef return
  return result ;
}

/*------------  P A T T E R N   U S E   D E T E C T I O N  ------------*/

Bool patterncheck(DL_STATE *page, GSTATE *pgstate, int32 colorType,
                  int32 *patternid, int32 *parent_pid, int32 *patterntype,
                  TranAttrib *tranattrib)
{
  /* A number of cases to consider:
   * 1. OLD is not pattern and NEW is not pattern.
   * 2. OLD is not pattern and NEW is pattern.
   * 3. OLD is a pattern and NEW is not a pattern.
   * 4. OLD is a pattern and NEW is the same pattern.
   * 5. OLD is a pattern and NEW is a different pattern.
   */
  OBJECT *newpattern = NULL, *impl = NULL ;
  PATTERNOBJECT *newpstate ;
  int32 idlomHit ;
  Bool overprinting[2] = {FALSE, FALSE};
  dl_color_t *dlc_current = dlc_currentcolor(page->dlc_context);

  *patternid = INVALID_PATTERN_ID ;
  *parent_pid = parent_patternid ;
  *patterntype = 1 ; /* default value */

  /* First of all see if there is a new pattern. */
  if ( fTreatScreenAsPattern(gsc_getcolorspace (pgstate->colorInfo , colorType),
                             colorType,
                             gsc_getRS(pgstate->colorInfo),
                             gsc_getSpotno(pgstate->colorInfo))) {
    HQASSERT( gsc_getcolorspace(pgstate->colorInfo, colorType) != SPACE_Pattern ,
              "Can't have both pattern screen and Pattern colorspace" ) ;
    *patternid  = -gsc_getSpotno(pgstate->colorInfo) ;
    HQASSERT(*patternid < 0, "spot number not positive for screen pattern") ;
  }
  else if ( gsc_getcolorspace(pgstate->colorInfo, colorType) == SPACE_Pattern ) {
    if (!gsc_getpattern(pgstate->colorInfo, colorType, &newpattern))
      return FALSE;
    HQASSERT(oType(*newpattern) == ONULL ||
             oType(*newpattern) == ODICTIONARY,
             "pattern should be dictionary or null" ) ;
    if ( oType(*newpattern) == ODICTIONARY ) {
      OBJECT *theo = fast_extract_hash_name(newpattern, NAME_PatternType) ;
      HQASSERT( theo && oType(*theo) == OINTEGER, "PatternType should already have been checked" );
      if ( theo && oType(*theo) == OINTEGER )
        *patterntype = oInteger(*theo) ;

      /* If Implementation is missing then somethings screwed. We finish
       * off the existing pattern and the fact it's missing will be picked
       * up when we try and use it later on.
       */
      impl = fast_extract_hash_name(newpattern, NAME_Implementation) ;
      /* Simple check that implementation array is present and correct;
       * we will be more thorough about this shortly - in createPatternDL.
       */
      if ( impl && oType(*impl) == OARRAY &&
           theLen(*impl) == PATTERN_IMPLEMENTATION_SIZE ) {
        OBJECT *theo = oArray(*impl) + PIA_PID ;
        if ( oType(*theo) == OINTEGER ) {
          *patternid = oInteger(*theo) ;
          HQASSERT(*patternid > 0, "Pattern ID not positive for pattern space") ;
        }
      }
      if ( *patternid == INVALID_PATTERN_ID )
        return error_handler(UNDEFINED) ;
    }
  }

  if ( *patternid == INVALID_PATTERN_ID )
    return TRUE ;

  if (get_core_context_interp()->userparams->PatternOverprintOverride &&
      *patterntype == 2) {
    overprinting[GSC_FILL] = gsc_getoverprint(pgstate->colorInfo, GSC_FILL);
    overprinting[GSC_STROKE] = gsc_getoverprint(pgstate->colorInfo, GSC_STROKE);
  }

  /** \todo @@@ TODO FIXME ajcd 2002-12-30: We shouldn't really be using
     currentGroup here, because a setgstate may have altered the target HDL.
     However, the group stack doesn't currently recognise this, and a pattern
     group will be opened with currentGroup is its parent, so we need to
     match that. We need to think about using groupTop(page->targetHdl)
     for all currentGroup references, and allowing a tree rather than stack
     structure to the groups. */
  newpstate = patternObjectLookup(page->stores.pattern,
                                  *patternid, *parent_pid,
                                  pageBaseMatrixId, page->currentGroup,
                                  dlc_current, overprinting,
                                  tranattrib ) ;

  /* If pattern we're after is all ok, then simply return. We may need to
     re-run the pattern proc because IDLOM might need it, which will waste a
     bit of DL memory. */
  idlomHit = IDLOM_PATTERNLOOKUP(*patternid);

  if ( newpstate == NULL ||
       idlomHit != IB_CacheHit ||
       (pdfout_enabled() &&
        !pdfout_patterncached(get_core_context_interp()->pdfout_h,
                              *patternid)) ) {
    Bool result = FALSE ;
    PATTERNOBJECT patobj ;

    /* The current colour (as created by invoking the SPACE_Pattern colour
       chain by gsc_invokeSingle) is used as part of the pattern's hash
       function. Uncoloured patterns have a normal DL colour for this,
       coloured patterns will have the black colour. The colour is overridden
       at render time when drawing the patterned object. Uncoloured patterns
       are now implemented by translation into coloured patterns. They do not
       share pattern DLs if called with different base colours. */
    if ( !dlc_to_dl(page->dlc_context, &patobj.ncolor, dlc_current) )
      return error_handler(VMERROR) ;

    if ( flush_vignette(VD_Default) ) {
      /* Save the current colour et al; patterncheck is now called after invoking
         the setg() colour chain, so that spot separations are present in the
         virtual rasterstyle when creating the pattern sub-dl. */
      dl_color_t saved_color ;
      uint8 saved_spflags = dl_currentspflags(page->dlc_context) ;
      uint8 saved_exflags = dl_currentexflags ;
      uint8 saved_disposition = dl_currentdisposition;
      COLORVALUE saved_opacity = dl_currentopacity(page->dlc_context) ;
      HDL *saved_targetHdl = page->targetHdl ; /* hdlClose() clears the target HDL */
      Bool saved_degenerateClipping = degenerateClipping ;
      /* cclip_bbox saved/restored within createPattern(Screen)DL */
      int saved_dl_safe_recursion = dl_safe_recursion;

      dlc_copy_release(page->dlc_context, &saved_color, dlc_current) ;
      dl_safe_recursion++;

      if ( *patternid < 0 ) {
        result = createPatternScreenDL(page, *patternid, pgstate,
                                       tranattrib, &patobj) ;
      } else {
        /* Note restricted range of colorTypes */
        HQASSERT(colorType == GSC_FILL || colorType == GSC_STROKE,
                 "Invalid colour chain for pattern") ;

        result = createPatternDL(page, *patternid, newpattern, impl, idlomHit,
                                 colorType, overprinting, tranattrib, &patobj);
      }

      /* Restore current colour et al, possibly trashed by vignette flush or
         creating a pattern DL. */
      dl_safe_recursion = saved_dl_safe_recursion;
      degenerateClipping = saved_degenerateClipping ;
      page->targetHdl = saved_targetHdl ;
      dl_set_currentopacity(page->dlc_context, saved_opacity) ;
      dl_currentdisposition = saved_disposition ;
      dl_currentexflags = saved_exflags ;
      dl_set_currentspflags(page->dlc_context, saved_spflags) ;
      dlc_release(page->dlc_context, dlc_current) ;
      dlc_copy_release(page->dlc_context, dlc_current, &saved_color) ;
    }

    if ( !result ) {
      dl_release(page->dlc_context, &patobj.ncolor) ;
      return FALSE ;
    }

    /* Set number of bands required. Coloured patterns require pattern form,
       and the pattern shape form. */
    if ( !dl_reserve_band(page, (RESERVED_BAND_PATTERN |
                                 RESERVED_BAND_PATTERN_SHAPE |
                                 RESERVED_BAND_PATTERN_CLIP)) )
      return FALSE ; /* is the last one in the right place? */

    /* Finish off the pattern shapes for this pattern dl. */
    if ( !patternshape_finishdl(page, &patobj) )
      return FALSE ;

    if ( newpstate == NULL ) {
      /* If the pattern was not stored beforehand, then create a DL store
         entry for it. If it was stored, ignore the new entry, and let the
         DL memory go to waste (this can only happen for IDLOM re-caches,
         and will go away when the DIDL implementation is complete). */
      newpstate = (PATTERNOBJECT*)dlSSInsert(page->stores.pattern,
                                             &patobj.storeEntry,
                                             TRUE) ;
      if ( newpstate == NULL )
        return FALSE ;
    } /* The insert must be last in the if, because this is retried on error. */
  }

  HQASSERT(newpstate->painttype == COLOURED_PATTERN ||
           newpstate->painttype == COLOURED_TRANSPARENT_PATTERN ||
           newpstate->painttype == UNCOLOURED_PATTERN ||
           newpstate->painttype == UNCOLOURED_TRANSPARENT_PATTERN ||
           newpstate->painttype == LEVEL1SCREEN_PATTERN,
           "Strange painttype for pattern") ;

  return TRUE ;
}

#if defined ( ASSERT_BUILD )
#include "cce.h"
#endif

/* ----------------------------------------------------------------------------
   function:            createpatternscreendl  author:              Luke Tunmer
   creation date:       22-Apr-1992            last modification:   25-Oct-1995
   arguments:
   description:
   Modification history:
        25-Oct-95 (N.Speciner); Modified to write on all active planes, instead
                of only using plane 0.
---------------------------------------------------------------------------- */
static Bool createPatternScreenDL(DL_STATE *page, int32 id, GSTATE *pgstate,
                                  TranAttrib *tranattrib, PATTERNOBJECT *patobj)
{
  Bool result ;
  FORM *form ;
  CLIPOBJECT *theclip ;
  STATEOBJECT thestate ;
  int32 xx , xy , yx , yy, px, py ;
  dbbox_t save_clip;
  int32 savedevice = CURRENT_DEVICE() ;
  int32 previous_parent_patternid = parent_patternid ;
  dl_color_t *dlc_current = dlc_currentcolor(page->dlc_context);

  HQASSERT(id < 0, "Pattern screen ID should be negative") ;
  HQASSERT(id == -gsc_getSpotno( pgstate->colorInfo ),
           "Pattern screen ID should be negative spotnumber") ;
  HQASSERT(tranAttribIsOpaque(tranattrib),
           "pattern screen tranAttrib should be opaque (null or default)");
  HQASSERT(patobj, "Nowhere to put pattern") ;

  /* we need to build up the display list for the screen pattern. This
   * should contain a rectfill dlobject to clear the pattern bbox, and
   * then a char dlobject to place a character whose cached bitmap is the
   * halftone form for the pattern screen.
   */

  /* Use the default screen */
  if ( NULL == (form = ht_patternscreenform(-id, &xx, &xy, &yx, &yy, &px, &py )) )
    return error_handler(VMERROR) ;

  patobj->painttype = LEVEL1SCREEN_PATTERN ;    /* Fake the pattern type (coloured) */
  patobj->tilingtype = 1 ;
  patobj->backdrop = FALSE ;
  patobj->nonoverlap.enabled = FALSE ;
  patobj->nonoverlap.xx = 0 ;
  patobj->nonoverlap.xy = 0 ;
  patobj->nonoverlap.yx = 0 ;
  patobj->nonoverlap.yy = 0 ;
  patobj->patternDoesOverprint = FALSE ; /* no overprinted objects as yet */
  patobj->overprinting[GSC_FILL] = FALSE;
  patobj->overprinting[GSC_STROKE] = FALSE;

  /* determine the bounding box */
  patobj->bsizex = (USERVALUE)-( abs( xx ) + abs( yx ) ) ;
  patobj->bsizey = (USERVALUE)-( abs( yy ) + abs( xy ) ) ;

  /* transform into the appropriate octant for rendering */
  /* see reproops.c:makepattern_() */
  if ( abs( xx * yy ) < abs( xy * yx ) ) {
    int32 tmp;

    tmp = xx; xx = yx; yx = tmp;
    tmp = yy; yy = xy; xy = tmp;
  }
  if ( xx < 0 ) { xx = -xx; xy = -xy; }
  if ( yy < 0 ) { yy = -yy; yx = -yx; }
  if ( yx > 0 ) {
    int32 fact = yx / xx + 1 ;

    yx -= fact * xx ;
    yy -= fact * xy ;
  }
  if ( -yx >= xx ) {
    int32 fact = (-yx) / xx ;

    yx += fact * xx ;
    yy += fact * xy ;
  }

  patobj->xx = (USERVALUE)( xx ) ;
  patobj->xy = (USERVALUE)( xy ) ;
  patobj->yx = (USERVALUE)( yx ) ;
  patobj->yy = (USERVALUE)( yy ) ;
  patobj->bbx = (USERVALUE)( px ) ;
  patobj->bby = (USERVALUE)( py ) ;
  patobj->patternid = id ; /* out of the range of proper patterns so it isn't
                           * confused with a normal pattern */
  patobj->parent_patternid = parent_patternid ;
  /* Pattern screen can only be defined in the context of its immediate parent. */
  patobj->context_patternid = parent_patternid ;
  patobj->pageBaseMatrixId = pageBaseMatrixId ;
  patobj->ta = tranattrib;
  patobj->groupid = page->currentGroup != NULL
    ? groupId(page->currentGroup) : HDL_ID_INVALID;
  patobj->opcode = RENDER_void;
  patobj->dldata.hdl = NULL ;

  thestate = stateobject_new( gsc_getSpotno( pgstate->colorInfo ) ) ;
  thestate.gstagstructure = pgstate->theGSTAGinfo.structure ;

  /* Create the clipobject. */
  {
    dbbox_t cliprect ;

    cliprect.x1 = cliprect.y1 = 0 ;
    SC_C2D_INT( cliprect.x2 , -patobj->bsizex ) ;
    SC_C2D_INT( cliprect.y2 , -patobj->bsizey ) ;

    if ( !setup_rect_clipping(page, &theclip, &cliprect) )
      return FALSE ;

    thestate.clipstate = theclip ;
  }

  {
    int32 saved_parent_patternid = parent_patternid ;
    parent_patternid = id ;
    if ( !patternshape_lookup(page, NULL, &thestate) )
      return FALSE ;
    parent_patternid = saved_parent_patternid ;
  }

  /* Take copy of current color and then get white */
  dlc_get_white(page->dlc_context, dlc_current);

  dl_set_currentspflags(page->dlc_context, RENDER_KNOCKOUT) ;

  page->currentdlstate = (STATEOBJECT*)dlSSInsert(page->stores.state,
                                                  &thestate.storeEntry,
                                                  TRUE) ;
  if (page->currentdlstate == NULL)
    return FALSE ;

  /* Point of no return; all errors must be handled by goto CLEANUP_AND_RETURN. */
  result = FALSE ;
#define return DO_NOT_return__GO_TO_CLEANUP_AND_RETURN_INSTEAD!

  parent_patternid = id ;

  /* set up for subsequent output to pattern_dl */
  SET_DEVICE(DEVICE_PATTERN1) ;

  save_clip = cclip_bbox;
  bbox_store(&cclip_bbox, 0, 0, theX2Clip(*theclip), theY2Clip(*theclip)) ;

  /** \todo @@@ TODO FIXME ajcd 2005-05-31: Removed save/restore of gstate
      clip object here. We may need to save/restore the clipping state
      (CLIPRECORDs) instead. */

  if ( !hdlOpen(page, FALSE, HDL_PATTERN, &patobj->dldata.hdl) )
    goto CLEANUP_AND_RETURN;

  patobj->opcode = RENDER_hdl;

  HQASSERT(page->targetHdl == patobj->dldata.hdl,
           "Not going to add object to correct DL") ;

  /* add a white rectfill of bbox size to blat background */
  if ( !addrectdisplay(page, &cclip_bbox) )
    goto CLEANUP_AND_RETURN ;

  /* add the char to the pattern display list */
  if ( !finishaddchardisplay(page, 1) )
    goto CLEANUP_AND_RETURN ;

  /* Release last color and now use black */
  dlc_release(page->dlc_context, dlc_current);
  dlc_get_black(page->dlc_context, dlc_current);

  dl_set_currentspflags(page->dlc_context, RENDER_KNOCKOUT) ;

  result = addchardisplay(page, form , 0 , 0) ;

 CLEANUP_AND_RETURN:
  result = hdlClose(&patobj->dldata.hdl, result) ;

  cclip_bbox = save_clip;

  /* Release current color and copy back original one */
  dlc_release(page->dlc_context, dlc_current);

  SET_DEVICE(savedevice) ;

  parent_patternid = previous_parent_patternid ;

#undef return
  return result ;
}

/* ---------------------------------------------------------------------- */
Bool fTreatScreenAsPattern(COLORSPACE_ID      colorSpace,
                           int32              colorType,
                           GUCR_RASTERSTYLE   *hRasterStyle,
                           SPOTNO             spotno)
{
  return colorSpace == SPACE_DeviceGray &&
         (colorType == GSC_FILL || colorType == GSC_STROKE) &&
         !gucr_halftoning(hRasterStyle) &&
         ht_isPattern(spotno);
}

/* ------------------------------------------------------------------------- */

/* **** WARNING ****
   The order of the implementation array is significant!
   The implementation array can be read by customers (i.e. Rampage)
   who rely on the order being maintained, unless there's a very good
   reason to change it.  Add new items prior to PIA_CHECKSUM, which
   must be last.  pia_types must match the 'pia' enum in pattern.h.
 */
static int32 pia_types[ PATTERN_IMPLEMENTATION_SIZE ] = {
  OINTEGER, /* PIA_PID */
  OGSTATE,  /* PIA_GSTATE */
  OINTEGER, /* PIA_PAINTTYPE */
  OINTEGER, /* PIA_TILINGTYPE */
  OREAL,    /* or OINTEGER: PIA_XSTEP */
  OREAL,    /* or OINTEGER: PIA_YSTEP */
  OREAL,    /* or OINTEGER: PIA_LLX */
  OREAL,    /* or OINTEGER: PIA_LLY */
  OREAL,    /* or OINTEGER: PIA_URX */
  OREAL,    /* or OINTEGER: PIA_URY */
  OARRAY,   /* PIA_MATRIX */
  OARRAY,   /* PIA_MATRIX_USPACE */
  OBOOLEAN, /* PIA_MATRIX_USPACE_REL */
  OINTEGER, /* PIA_CONTEXT_PID */
  OINTEGER  /* PIA_CHECKSUM */
} ;

Bool check_pia_valid( OBJECT *impl )
{
  OBJECT *theo ;
  int32 i ;

  HQASSERT( impl , "impl NULL in check_pia_valid" ) ;

  /* Is it still the same object we started with? */
  if ( oType(*impl) != OARRAY ||
       theLen(*impl) != PATTERN_IMPLEMENTATION_SIZE )
    return FALSE ;

  /* Are the types of each entry correct? Allow OINTEGER for all OREALs,
     we'll use object_numeric_value to extract the value.  */
  theo = oArray(*impl) ;
  for ( i = 0 ; i < PATTERN_IMPLEMENTATION_SIZE ; ++i, ++theo ) {
    if ( oType(*theo) != pia_types[ i ] &&
         (oType(*theo) != OINTEGER || pia_types[i] != OREAL) )
      return FALSE ;
  }

  /* Now check the checksum */
  theo = oArray(*impl) + PIA_CHECKSUM ;
  if ( oType(*theo) != OINTEGER ||
       create_pia_checksum( impl ) != oInteger(*theo) )
    return FALSE ;

  return TRUE ;
}

static int32 create_pia_checksum( OBJECT *impl )
{
  uint16 s1 = 0, s2 = 0;

  HQASSERT( impl , "impl NULL in create_pia_checksum" ) ;
  HQASSERT( oType(*impl) == OARRAY &&
            theLen(*impl) == PATTERN_IMPLEMENTATION_SIZE &&
            oArray(*impl),
            "impl is not valid in create_pia_checksum" ) ;

  /* Do the checksum over the length of the implementation array, up to but
   * not including where the checksum may be stored.
   */
  return calculateAdler32(( uint8 * )oArray(*impl),
                          sizeof( OBJECT ) * ( PATTERN_IMPLEMENTATION_SIZE - 1 ),
                          &s1, &s2 ) ;
}

Bool getPatternId(GS_COLORinfo *colorInfo,
                  int32        colorType,
                  int32        *patternId,
                  int32        *paintType)
{
  OBJECT *pattern;

  if (!gsc_getpattern(colorInfo, colorType, &pattern))
    return FALSE;
  HQASSERT(oType(*pattern) == ODICTIONARY || oType(*pattern) == ONULL,
           "pattern should be dictionary or NULL" ) ;
  if ( oType(*pattern) == ODICTIONARY ) {
    OBJECT *theo ;

    /* If Implementation is missing then somethings screwed. We finish
     * off the existing pattern and the fact it's missing will be picked
     * up when we try and use it later on.
     */
    theo = fast_extract_hash_name(pattern, NAME_Implementation) ;
    /* Simple check that implementation array is present and correct;
     * we will be more thorough about this shortly - in createPatternDL.
     */
    if ( theo == NULL || !check_pia_valid(theo) )
      return error_handler(UNDEFINED) ;

    theo = oArray(*theo) ;
    *patternId = oInteger(theo[PIA_PID]) ;
    *paintType = oInteger(theo[PIA_PAINTTYPE]) ;

    return TRUE ;
  }

  *patternId = INVALID_PATTERN_ID ;
  *paintType = NO_PATTERN ;

  return TRUE;
}

HDL *patternHdl(PATTERNOBJECT *patobj)
{
  if ( patobj != NULL ) {
    if ( patobj->opcode == RENDER_hdl )
      return patobj->dldata.hdl;
    else if ( patobj->opcode == RENDER_group )
      return groupHdl(patobj->dldata.group);
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
   function:            makepattern_      author:              Hugo Tyson
   creation date:       19-Dec-1991       last modification:   03-Feb-1995
   arguments:           a matrix and a dictionary
   description:         see pp.200-210,453 in the big Red and White Bok.

Modifications:
        03-Feb-95 (N.Speciner); disable IDLOM during makepattern execution.
          When the pattern target is implemented in IDLOM, this is where the
          begin call will be made, and the end call will be put where we now
          restore the IDLOM state at the end of the routine.
---------------------------------------------------------------------------- */

static int32 patternID = 100;

Bool makepattern_(ps_context_t *pscontext)
{
  OBJECT newd = OBJECT_NOTVM_NOTHING;

  if ( ! ps_core_context(pscontext)->systemparams->PostScript )
    return error_handler( INVALIDACCESS ) ;

  if ( !gs_makepattern( & operandstack , & newd ))
    return FALSE ;

  /* Push the resulting dictionary and make it readonly. */

  if ( !push( & newd , & operandstack ))
    return FALSE ;

  return readonly_(pscontext) ;
}

Bool gs_makepattern( STACK *stack , OBJECT *newd )
{
  OBJECT *thematrix, *thedict, *oo;
  Bool result = FALSE ;

  enum { pdm_PatternType, pdm_XUID, pdm_Implementation, pdm_dummy } ;
  static NAMETYPEMATCH patterndictmatch[pdm_dummy + 1] = {
    { NAME_PatternType,                1, { OINTEGER }},
    { NAME_XUID | OOPTIONAL,           1, { OARRAY }},
    { NAME_Implementation | OOPTIONAL, 0 },
    DUMMY_END_MATCH
  };

  HQASSERT( newd , "newd NULL in gs_makepattern" ) ;
  HQASSERT( stack, "stack NULL in gs_makepattern" ) ;

  if ( theIStackSize(stack) < 1 )
    return error_handler(STACKUNDERFLOW) ;

  thematrix = theITop(stack);
  if ( oType(*thematrix) != OARRAY && oType(*thematrix) != OPACKEDARRAY )
    return error_handler(TYPECHECK);

  thedict = stackindex( 1, stack );

  if ( oType(*thedict) != ODICTIONARY )
    return error_handler(TYPECHECK);

  oo = oDict(*thedict);
  if ( !oCanRead(*oo) && !object_access_override(oo))
    return error_handler(INVALIDACCESS);

  if ( !dictmatch(thedict, patterndictmatch) )
    return FALSE;

  /* XUID */
  /* patterndictmatch[pdm_XUID] - do not care. */

  /* Implementation */
  if ( patterndictmatch[pdm_Implementation].result != NULL ) /* should not be here */
    return error_handler(RANGECHECK);

  /* PatternType */
  switch ( oInteger(*patterndictmatch[pdm_PatternType].result) ) {
  case 1:
  case 101: /* Allows untiled patterns for XPS. */
  case 102: /* Untiled pattern for XPS gradients, requiring KO group. */
    result = makepattern_1(newd, thematrix, thedict);
    break ;
  case 2:
    result = makepattern_2(newd, thematrix, thedict);
    break ;
  default:
    return error_handler(RANGECHECK);
  }

  if ( result )
    npop(2, stack); /* remove original operands */

  return result ;
}

struct PDC_DATA {
  OBJECT *dict;
  corecontext_t *context;
};

/* Helper function to copy pattern dictionaries; we have to copy all entries
   into PSVM, but shouldn't need to copy any entries not explicitly mentioned
   in the dictmatches. */
static Bool patterndictcopy(OBJECT *key, OBJECT *value, void *data)
{
  OBJECT copy = OBJECT_NOTVM_NOTHING;
  struct PDC_DATA *pdc_data = data;

  HQASSERT(key, "No key in dictionary copier for patterns") ;
  HQASSERT(value, "No key in dictionary copier for patterns") ;
  HQASSERT(pdc_data->dict, "No destination dictionary for patterns") ;

  if ( oType(*key) == ONAME ) {
    /* These are the composite objects that PDF puts in the shading dict: */
    switch ( oNameNumber(*key) ) {
    case NAME_BBox:
    case NAME_XUID:
    case NAME_PaintProc:
    case NAME_Shading:
    case NAME_Resources:
      if ( !psvm_copy_object(&copy, value, MAX_PATTERN_RECURSION,
                             pdc_data->context->glallocmode) )
        return FALSE ;
      value = &copy ;
      break ;
    }
  }

  HQASSERT(!isPSCompObj(*value) ||
           oType(*value) == OFILE || /* PDF files are not in PSVM */
           psvm_assert_check(oOther(*value)),
           "Pattern sub-object isn't in PostScript memory") ;

  return insert_hash(pdc_data->dict, key, value) ;
}

static Bool makepattern_1(OBJECT *newd, OBJECT *thea, OBJECT *thed)
{
  struct PDC_DATA pdc_data;
  corecontext_t *context = get_core_context_interp();
  int32   i ;
  Bool   result;
  OBJECT implementation = OBJECT_NOTVM_NOTHING,
    oMatrix = OBJECT_NOTVM_NOTHING, oMatrixUSpace = OBJECT_NOTVM_NOTHING;
  OBJECT *obbox;
  int32   tilingtype, painttype, patterntype;
  SYSTEMVALUE xstep, ystep;
  sbbox_t bbox ;
  Bool    old_doing_imposition;
  int8    oldIdlomState;
  Bool    oldglmode;
  int32   oldCurrentDevice = DEVICE_ILLEGAL;
  OMATRIX m1, pat_matrix, matrix_uspace;
  Bool enable_recombine_interception = FALSE;

  enum { patterndictmatch_PaintType,
         patterndictmatch_TilingType,
         patterndictmatch_BBox,
         patterndictmatch_XStep,
         patterndictmatch_YStep,
         patterndictmatch_PaintProc,
         patterndictmatch_PatternType,
         patterndictmatch_HqnMatrixAdjustment,
         patterndictmatch_dummy } ;

  static NAMETYPEMATCH patterndictmatch[patterndictmatch_dummy + 1] = {
    { NAME_PaintType,                  1, { OINTEGER }},         /* 0 */
    { NAME_TilingType,                 1, { OINTEGER }},         /* 1 */
    { NAME_BBox,                       1, { OARRAY }},           /* 2 */
    { NAME_XStep,                      2, { OINTEGER, OREAL }},  /* 3 */
    { NAME_YStep,                      2, { OINTEGER, OREAL }},  /* 4 */
    { NAME_PaintProc,                  3, { OARRAY | EXECUTABLE, /* 5 */
                                            OPACKEDARRAY | EXECUTABLE,
                                            OFILE | EXECUTABLE }},
    { NAME_PatternType,                1, { OINTEGER }},
    { NAME_HqnMatrixAdjustment|OOPTIONAL, 1, { OARRAY }},
    DUMMY_END_MATCH
  };

  HQASSERT( newd , "newd NULL in makepattern_1" ) ;
  HQASSERT( thea , "thea NULL in makepattern_1" ) ;
  HQASSERT( thed , "thed NULL in makepattern_1" ) ;

  if (! dictmatch (thed, patterndictmatch))
    return FALSE;

  /* PatternType */
  patterntype = oInteger(*patterndictmatch[patterndictmatch_PatternType].result);

  /* PaintType */
  painttype = oInteger(*patterndictmatch[patterndictmatch_PaintType].result);
  if ( painttype != 1 && painttype != 2)
    return error_handler( RANGECHECK );

  /* TilingType */
  tilingtype = oInteger(*patterndictmatch[patterndictmatch_TilingType].result);
  switch ( patterntype ) {
  case 1 :
    if ( tilingtype < 1 || tilingtype > 3)
      return error_handler( RANGECHECK );
    break;
  case 101 :
  case 102 :
    /* Allow PatternType 101 as a synonym for PatternType 1, but relaxing the
       restriction on tiling type to include untiled patterns.  PatternType 102
       is the same as 101 but puts the contents of the PaintProc into knockout
       Group (intended for tiled shfills). */
    if ( tilingtype < 0 || tilingtype > 3)
      return error_handler( RANGECHECK );
    break;
  default:
    HQFAIL( "Unexpected pattern type in makepattern_1" );
    return error_handler( UNREGISTERED );
  }

  /* BBox (sanity checked in pattern space in createPatternDL). */
  if ( !object_get_bbox(patterndictmatch[patterndictmatch_BBox].result, &bbox) )
    return FALSE ;

  /* XStep */
  if ( !object_get_numeric(patterndictmatch[patterndictmatch_XStep].result,
                           &xstep) )
    return FALSE ;

  /* YStep */
  if ( !object_get_numeric(patterndictmatch[patterndictmatch_YStep].result,
                           &ystep) )
    return FALSE;

  if ( xstep == 0.0 || ystep == 0.0 )
    return error_handler( RANGECHECK );

  /* PaintProc */
  /* patterndictmatch[patterndictmatch_PaintProc] - for now we can do nothing with this. */


  /* We want to work on a local copy of the CTM, so do the concatenation
   * of the CTM with the pattern matrix inline.
   * We also want to keep the original matrix for poking into the
   * implementation array.
   */
  if ( !is_matrix( thea, &pat_matrix ))
    return FALSE ;
  matrix_mult( &pat_matrix, &thegsPageCTM( *gstateptr ), &m1 ) ;

  /** \todo @@@ TODO FIXME XPS uses doubles for coords, but brush coords end-up
      being choked down to floats because brushes are implemented as PS
      patterns.  HqnMatrixAdjustment adjusts m1 to compensate for the loss of
      precision, therefore removing problems like gaps between abutting images
      etc.  This code can be removed once XPS implements brushes in DIDL. */
  if ( patterndictmatch[patterndictmatch_HqnMatrixAdjustment].result ) {
    OMATRIX matrix_adj ;
    uint32 row ;

    if ( !is_matrix(patterndictmatch[patterndictmatch_HqnMatrixAdjustment].result, &matrix_adj) )
      return FALSE ;

    for ( row = 0 ; row < 3 ; ++row ) {
      m1.matrix[row][0] += matrix_adj.matrix[row][0] ;
      m1.matrix[row][1] += matrix_adj.matrix[row][1] ;
    }
    MATRIX_SET_OPT_BOTH(&m1) ;
  }

  /* matrix_uspace is the transformation from pattern space to default
     user space, excluding the adjustment for nice tiling */
  {
    OMATRIX matrix_inversectm;

    if (! matrix_inverse(&thegsDeviceCTM(*gstateptr), &matrix_inversectm) )
      return error_handler(UNDEFINEDRESULT);
    matrix_mult(& m1, & matrix_inversectm, & matrix_uspace);
  }


  /* note that this was compiled PostScript which does, roughly:
   * { gsave
   *   concat           % multiply in the matrix supplied
   *   newpath          % discard any extant path
   *   initclip         % discard any clipping path
   *   dup              % copy for Implementation put later
   *   dup maxlength
   *   1 add dict       % space for Implementation
   *   copy             % make the pattern dictionary
   *   4 array          % implementation array
   *   dup 0 2 put      % an integer key in el 0
   *   dup 1 gstate put % saved gstate in el 1
   *     ...            % and xstep,ystep... in 2,3,4,5
   *   BBox rectclip    % clip to the BBox provided
   *   /Implementation
   *   exch put         % Implementation into pattern dict
   *   grestore         % cancel matrix effect
   * }
   * if this helps.
   * But note that we don't do this on the PS stack any more
   */

  /* Note this gs_gpush can be done before disabling HDLT since nothing in gs_gpush
   * depends on it. Doing it before simplifies the error return. The corresponding
   * pop at the end must be done before HDLT is re-enabled though.
   */
  if ( ! gs_gpush( GST_GSAVE )) /* Early exit! */
    return FALSE ;

#define return DO_NOT_RETURN_-_Skipping_rcbn_enable_interception_is_illegal!

  old_doing_imposition = doing_imposition ;
  doing_imposition = FALSE ;

  oldIdlomState = theIdlomState( *gstateptr ) ; /* disable for clip stuff here */
  theIdlomState( *gstateptr ) = HDLT_DISABLED ;

  /* Reset Transparency parameters. See section 7.5.6, p.453 of the PDF 1.4
     manual */
  tsDefault(gsTranState(gstateptr), gstateptr->colorInfo);

  /* The red book says that our dict should be created in local VM */
  oldglmode = setglallocmode(context, FALSE ) ;

  /* Get the object allocation out of the way */
  i = (int32)theLen(*thed) + 1 ;
  pdc_data.dict = newd;
  pdc_data.context = context;
  result = ps_dictionary(newd, i) &&
           ps_array(&implementation,  PATTERN_IMPLEMENTATION_SIZE) &&
           ps_array(&oMatrix, 6) &&
           ps_array(&oMatrixUSpace, 6) &&
           walk_dictionary(thed, patterndictcopy, &pdc_data) ;

  if ( result ) { /* No errors? good, keep going then */
    if ( painttype == 2 ) {
      /* set the color to black for an uncolored pattern */
      gsc_initgray( gstateptr->colorInfo ) ;

      /* Don't want recombine interception in an uncolored pattern. */
      if (rcbn_enabled()) {
        rcbn_disable_interception(gstateptr->colorInfo);
        enable_recombine_interception = TRUE;
        oldCurrentDevice = CURRENT_DEVICE();
      }

      /* Ensure that the pattern appears actually on the paper not clipped
       * more than it should be by the pagesize (as patterns centred at 0,0 are).
       */
      SET_DEVICE (DEVICE_PATTERN2);
    } else {
      SET_DEVICE (DEVICE_PATTERN1);
    }

    (void)gs_newpath(); /* discard any path in the gstate */

    /* Sets the device matrix to be the same as the page matrix, for the
     * benefit of HDLT and pdfout, and cos this is a sensible thing to do
     * - e.g. so 'matrix defaultmatrix' inside a patterns paintproc gives
     * something sensible, and for fonts inside patterns.
     *
     * Discard any extant clipping, but only for tiled patterns.
     */
    result = ( ( tilingtype == 0 || gs_initclip(gstateptr) ) &&
               gs_setdefaultctm(&m1, FALSE) ) ;
  }

  if ( result ) {
    /* Setting of device size, matrix, and clip to BBox will be done when DL
       is created. */

    /* Populate the implementation array
     * first entry in implementation array is the pattern ID
     * The context pattern id is for recursive patterns which are
     * defined within the paint proc of another pattern.
     */
    object_store_integer(&oArray(implementation)[PIA_PID], patternID++);
    object_store_integer(&oArray(implementation)[PIA_CONTEXT_PID], context_patternid);

    /* second entry the saved gstate */
    theIdlomState( *gstateptr ) = oldIdlomState; /* re-enable in saved state */
    result = gstate_(context->pscontext) && result;
    theIdlomState( *gstateptr ) = HDLT_DISABLED; /* and re-disable after save */
  }

  if ( result ) {
    OCopy(oArray(implementation)[PIA_GSTATE], *theTop(operandstack));
    pop( &operandstack );

    object_store_integer(&oArray(implementation)[PIA_PAINTTYPE], painttype);
    object_store_integer(&oArray(implementation)[PIA_TILINGTYPE], tilingtype);

    /* Unadjusted xstep, ystep in userspace units */
    OCopy(oArray(implementation)[PIA_XSTEP],
          *patterndictmatch[patterndictmatch_XStep].result);
    OCopy(oArray(implementation)[PIA_YSTEP],
          *patterndictmatch[patterndictmatch_YStep].result);

    obbox = oArray(*patterndictmatch[patterndictmatch_BBox].result);
    OCopy(oArray(implementation)[PIA_LLX], obbox[0]);
    OCopy(oArray(implementation)[PIA_LLY], obbox[1]);
    OCopy(oArray(implementation)[PIA_URX], obbox[2]);
    OCopy(oArray(implementation)[PIA_URY], obbox[3]);

    /* Pattern to userspace matrix  */
    OCopy(oArray(implementation)[PIA_MATRIX], oMatrix) ;
    result = from_matrix(oArray(oMatrix), &pat_matrix, oGlobalValue(oMatrix)) ;
    HQASSERT( result, "from_matrix unexpectedly failed in makepattern_1" ) ;

    /* Pattern to default userspace matrix excluding tiling adjustments */
    OCopy(oArray(implementation)[PIA_MATRIX_USPACE], oMatrixUSpace) ;
    result = from_matrix(oArray(oMatrixUSpace), &matrix_uspace, oGlobalValue(oMatrixUSpace)) ;
    HQASSERT( result, "from_matrix unexpectedly failed in makepattern_1" ) ;

    /* Indicates whether PIA_MATRIX_USPACE is relative (or absolute).
       Relative applies to nested patterns where makepattern is called
       within another pattern's paint proc */
    object_store_bool(&oArray(implementation)[PIA_MATRIX_USPACE_REL],
                      pattern_executingpaintproc(NULL));

    /* The last one gets a checksum */
    object_store_integer(&oArray(implementation)[PIA_CHECKSUM],
                         create_pia_checksum(&implementation)) ;

    /* Now put the implementation array into our dict */
    result = fast_insert_hash_name(newd, NAME_Implementation, &implementation) ;
  }

  if (enable_recombine_interception) {
    /* SET_DEVICE needed because of a hidden call to gsc_setscreens in
     * rcbn_enable_interception which would otherwise fail */
    SET_DEVICE(oldCurrentDevice);
    rcbn_enable_interception(gstateptr->colorInfo);
  }

  setglallocmode(context, oldglmode ) ;             /* done with enforcing local VM use */
  theIdlomState( *gstateptr ) = oldIdlomState ; /* re-enable as pattern stuff ends. */
  doing_imposition = old_doing_imposition ;

  /* undo changes to gstate */
  result = gs_setgstate( gstackptr, GST_GSAVE, FALSE, TRUE, FALSE, NULL ) &&
           result ;

#undef return

  return result ;
}

static Bool makepattern_2(OBJECT *newd, OBJECT *thea, OBJECT *thed)
{
  struct PDC_DATA pdc_data;
  corecontext_t *context = get_core_context_interp();
  int32   i ;
  Bool result;
  OBJECT implementation = OBJECT_NOTVM_NOTHING,
    oMatrix = OBJECT_NOTVM_NOTHING, oMatrixUSpace = OBJECT_NOTVM_NOTHING;
  Bool old_doing_imposition;
  int8    oldIdlomState;
  Bool    oldglmode;
  OMATRIX m1, pat_matrix, matrix_uspace;

  enum { pdm_Shading, pdm_PatternType, pdm_dummy } ;
  static NAMETYPEMATCH patterndictmatch[pdm_dummy + 1] = {
    { NAME_Shading, 1, { ODICTIONARY }},
    { NAME_PatternType, 1, { OINTEGER }},
    DUMMY_END_MATCH
  };

  HQASSERT( newd , "newd NULL in makepattern_2" ) ;
  HQASSERT( thea , "thea NULL in makepattern_2" ) ;
  HQASSERT( thed , "thed NULL in makepattern_2" ) ;

  if (! dictmatch(thed, patterndictmatch))
    return FALSE;

  /* Check shading dict is OK */
  if ( !is_shadingdict(patterndictmatch[pdm_Shading].result) )
    return FALSE ;

  /* We want to work on a local copy of the CTM, so do the concatenation
   * of the CTM with the pattern matrix inline.
   * We also want to keep the original matrix for poking into the
   * implementation array.
   */
  if ( !is_matrix( thea, &pat_matrix ))
    return FALSE ;
  matrix_mult( &pat_matrix, &thegsPageCTM( *gstateptr ), &m1 ) ;

  /* matrix_uspace is the transformation from pattern space to default
     user space, excluding the adjustment for nice tiling */
  {
    OMATRIX matrix_inversectm;
    if (! matrix_inverse(&thegsDeviceCTM(*gstateptr), &matrix_inversectm) )
      return error_handler(UNDEFINEDRESULT);

    matrix_mult(& m1, & matrix_inversectm, & matrix_uspace);
  }

  if ( ! gs_gpush( GST_GSAVE )) /* Early exit! */
    return FALSE ;

  old_doing_imposition = doing_imposition;
  doing_imposition = FALSE;

  oldIdlomState = theIdlomState( *gstateptr ) ; /* disable for clip stuff here */
  theIdlomState( *gstateptr ) = HDLT_DISABLED;

  /* Reset Transparency parameters. See section 7.5.6, p.453 of the PDF 1.4
     manual */
  tsDefault(gsTranState(gstateptr), gstateptr->colorInfo);

  /* The red book says that our dict should be created in local VM */
  oldglmode = setglallocmode(context, FALSE ) ;

  /* Get the object allocation out of the way */
  i = (int32)theLen(*thed) + 1 ;
  pdc_data.dict = newd;
  pdc_data.context = context;
  result = ps_dictionary(newd, i) &&
           ps_array(&implementation, PATTERN_IMPLEMENTATION_SIZE) &&
           ps_array(&oMatrix, 6) &&
           ps_array(&oMatrixUSpace, 6) &&
           walk_dictionary(thed, patterndictcopy, &pdc_data) ;

  if ( result ) { /* No errors? good, keep going then */
    /* Ensure that the pattern appears actually on the paper not clipped
     * more than it should be by the pagesize (as patterns centred at 0,0 are).
     */
    SET_DEVICE(DEVICE_PATTERN1);

    (void)gs_newpath(); /* discard any path in the gstate */
    /* Sets the device matrix to be the same as the page matrix, for the
     * benefit of HDLT and pdfout, and cos this is a sensible thing to do
     * - e.g. so 'matrix defaultmatrix' inside a patterns paintproc gives
     * something sensible, and for fonts inside patterns.
     */
    result = (gs_initclip(gstateptr) && /* discard any extant clipping */
              gs_setdefaultctm(&m1, FALSE)) ;
  }

  if ( result ) {
    /* Populate the implementation array
     * first entry in implementation array is the pattern ID
     * The context pattern id is for recursive patterns which are
     * defined within the paint proc of another pattern.
     */
    object_store_integer(&oArray(implementation)[PIA_PID], patternID++);
    object_store_integer(&oArray(implementation)[PIA_CONTEXT_PID], context_patternid);

    /* second entry the saved gstate */
    theIdlomState( *gstateptr ) = oldIdlomState; /* re-enable in saved state */
    result = gstate_(context->pscontext) && result;
    theIdlomState( *gstateptr ) = HDLT_DISABLED; /* and re-disable after save */
  }

  if ( result ) {
    OCopy(oArray(implementation)[PIA_GSTATE], *theTop(operandstack));
    pop( &operandstack );

    object_store_integer(&oArray(implementation)[PIA_PAINTTYPE], COLOURED_PATTERN);
    object_store_integer(&oArray(implementation)[PIA_TILINGTYPE], 0);

    /* Unadjusted xstep, ystep in userspace units */
    object_store_real(&oArray(implementation)[PIA_XSTEP], 0.0f);
    object_store_real(&oArray(implementation)[PIA_YSTEP], 0.0f);

    object_store_real(&oArray(implementation)[PIA_LLX], 0.0f);
    object_store_real(&oArray(implementation)[PIA_LLY], 0.0f);
    object_store_real(&oArray(implementation)[PIA_URX], 0.0f);
    object_store_real(&oArray(implementation)[PIA_URY], 0.0f);

    /* 11th is for the user matrix  */
    OCopy(oArray(implementation)[PIA_MATRIX], oMatrix) ;
    result = from_matrix(oArray(oMatrix), &pat_matrix, oGlobalValue(oMatrix)) ;
    HQASSERT( result, "from_matrix unexpectedly failed in makepattern_2" ) ;

    /* 12th is for the user matrix excluding tiling adjustments */
    OCopy(oArray(implementation)[PIA_MATRIX_USPACE], oMatrixUSpace) ;
    result = from_matrix(oArray(oMatrixUSpace), &matrix_uspace, oGlobalValue(oMatrixUSpace)) ;
    HQASSERT( result, "from_matrix unexpectedly failed in makepattern_2" ) ;

    /* 13th indicates whether PIA_MATRIX_USPACE is relative (or absolute).
       Relative applies to nested patterns where makepattern is called
       within another pattern's paint proc */
    object_store_bool(&oArray(implementation)[PIA_MATRIX_USPACE_REL],
                      pattern_executingpaintproc(NULL));

    /* The last one gets a checksum */
    object_store_integer(&oArray(implementation)[PIA_CHECKSUM],
                         create_pia_checksum(&implementation)) ;

    /* Now put the implementation array into our dict */
    result = fast_insert_hash_name(newd, NAME_Implementation, &implementation) ;
  }

  /* undo changes to gstate */
  result = gs_setgstate( gstackptr, GST_GSAVE, FALSE, TRUE, FALSE, NULL ) &&
           result ;
  setglallocmode(context, oldglmode ); /* done with enforcing local VM use */
  theIdlomState( *gstateptr ) = oldIdlomState; /* un-disable as pattern stuff ends */
  doing_imposition = old_doing_imposition;
  return result ;
}

/** Helper function for pattern_matrix_remake. */
static Bool not_implementation(OBJECT *theo, void *data)
{
  UNUSED_PARAM(void *, data) ;

  HQASSERT(theo, "No key object for copy dictionary routine") ;
  if ( oType(*theo) == ONAME &&
       oNameNumber(*theo) == NAME_Implementation )
    return FALSE ;

  return TRUE ;
}

/** Remake a pattern currently set in the gstate, using a different matrix. */
Bool pattern_matrix_remake(int32 colorType, OMATRIX *newmatrix, Bool absolute)
{
  OBJECT *pattern, *impl ;
  STACK *stack = &operandstack ;
  GSTATE *patterngs ;
  OMATRIX transform ;

  if ( gsc_getcolorspace(gstateptr->colorInfo, colorType) != SPACE_Pattern )
    return TRUE ;

  HQFAIL("This function is obsolete. Do not call it.") ;

  if (!gsc_getpattern(gstateptr->colorInfo, colorType, &pattern))
    return FALSE;
  HQASSERT(oType(*pattern) == ONULL || oType(*pattern) == ODICTIONARY,
           "Pattern is not an expected type");
  if ( oType(*pattern) != ODICTIONARY )
    return TRUE ;

  if ( (impl = fast_extract_hash_name(pattern, NAME_Implementation)) == NULL ||
       !check_pia_valid(impl) )
    return error_handler(TYPECHECK) ;

  patterngs = oGState(oArray(*impl)[PIA_GSTATE]) ;

  /* The matrix passed in is relative. We need to extract the CTM from the
     pattern, apply the transform to it, and reapply it. */
  if ( !absolute ) {
    matrix_mult(newmatrix, &thegsPageCTM(*patterngs), &transform) ;
    newmatrix = &transform ;
  }

  if ( gs_gpush(GST_GSAVE) ) {
    int32 gid = gstackptr->gId ;
    Bool result ;
    int32 painttype = oInteger(oArray(*impl)[PIA_PAINTTYPE]) ;
    OBJECT newpattern = OBJECT_NOTVM_NOTHING ;
    deactivate_pagedevice_t dpd ;
    int32 oldCurrentDevice = DEVICE_ILLEGAL;
    Bool reenableRecomb = FALSE;

    /* We want to reconstruct the pattern, but with the current matrix. To do
       this we are going to copy the pattern dictionary, excluding
       Implementation, and re-make the pattern. The snag is that we want the
       gstate modifications that were made to the original pattern, but these
       are not recorded anywhere. We get these by setting the gstate in the
       pattern, but tricking it into leaving the current device information,
       and re-setting the CTM to the one we want before calling
       gs_makepattern. The pattern matrix used for the original pattern is
       conveniently stored in the implementation array. The copied dictionary
       and the original pattern dictionary are both in PS memory. We are not
       worried that creating a new pattern will cause pattern store misses,
       because the matrix adjustment for the form would almost certainly have
       caused that anyway. */
    deactivate_pagedevice(gstateptr, patterngs, NULL, &dpd) ;
    dpd.action = PAGEDEVICE_DEFERRED_DEACTIVATE ;

    if (rcbn_enabled() &&
        (painttype == UNCOLOURED_PATTERN ||
         painttype == UNCOLOURED_TRANSPARENT_PATTERN)) {
      rcbn_disable_interception(gstateptr->colorInfo);
      reenableRecomb = TRUE;
      oldCurrentDevice = CURRENT_DEVICE();
    }

    /* The rasterstyle inside a pattern is the same as the rasterstyle in its
       surrounding context, not the rasterstyles of the saved pattern
       context. */
    HQASSERT(gsc_getRS(patterngs->colorInfo) == gsc_getRS(gstateptr->colorInfo),
             "Inconsistent device raster styles");
    gsc_setTargetRS(patterngs->colorInfo, gsc_getTargetRS(gstateptr->colorInfo)) ;

    result = (ps_dictionary(&newpattern, theLen(*pattern)) &&
              CopyDictionary(pattern, &newpattern, not_implementation, NULL) &&
              gs_setgstate(patterngs, GST_GSAVE, TRUE, FALSE, FALSE, &dpd) &&
              gs_setctm(newmatrix, FALSE) &&
              push(&newpattern, stack) &&
              push(&oArray(*impl)[PIA_MATRIX], stack) &&
              gs_makepattern(stack, &newpattern) &&
              reduceOaccess(READ_ONLY, TRUE, &newpattern)) ;

    if (reenableRecomb) {
      /* SET_DEVICE needed because of a hidden call to gsc_setscreens in
       * rcbn_enable_interception which would otherwise fail */
      SET_DEVICE(oldCurrentDevice);
      rcbn_enable_interception(gstateptr->colorInfo);
    }

    if ( gs_cleargstates(gid, GST_GSAVE, NULL) && result ) {
      if ( ! gsc_currentcolor(gstateptr->colorInfo, stack, colorType) )
        return FALSE ;

      /* Replace pattern dictionary on stack with new dict. */
      Copy(theITop(stack), &newpattern) ;

      return gsc_setcolor(gstateptr->colorInfo, stack, colorType) ;
    }
  }

  return FALSE ;
}

void init_C_globals_pattern(void)
{
#if defined( ASSERT_BUILD )
  debug_pattern = FALSE ;
#endif
  context_patternid = INVALID_PATTERN_ID ;
  parent_patternid = INVALID_PATTERN_ID ;
  patternID = 100 ;
}

/*
Log stripped */
