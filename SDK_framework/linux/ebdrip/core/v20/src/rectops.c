/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:rectops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to implement the rectangle operators:
 * rectclip , rectfill , rectstroke.
 */

#include "core.h"
#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "swoften.h"
#include "often.h"
#include "objects.h"
#include "fileio.h"
#include "fonts.h"        /* charcontext_t */
#include "graphics.h"
#include "namedef_.h"
#include "swpdfout.h"

#include "matrix.h"
#include "params.h"
#include "miscops.h"
#include "stacks.h"

#include "gstate.h"
#include "gstack.h"
#include "gscxfer.h"      /* gsc_analyze_for_forcepositive */

#include "rectops.h"
#include "display.h"
#include "binscan.h"
#include "utils.h"
#include "gu_rect.h"
#include "ndisplay.h"
#include "routedev.h"
#include "dl_bres.h"
#include "clipops.h"
#include "pathcons.h"
#include "plotops.h"
#include "system.h"

#include "idlom.h"
#include "fcache.h"
#include "trap.h"

#include "vndetect.h"
#include "rcbcntrl.h"

#include "gu_path.h"

/* ----------------------------------------------------------------------------
   function:            rectclip_()                   author:   Luke Tunmer
   creation date:       21-May-1991        last modification:   ##-###-####
   arguments:           none .
   description:

   Operator described on page 472, PS-2.

---------------------------------------------------------------------------- */
Bool rectclip_(ps_context_t *pscontext)
{
  Bool      result ;
  int32     number ;
  RECTANGLE *rects ;
  RECTANGLE stackrects[STACK_RECTS];

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* get the arguments off the stack */
  rects = stackrects ;
  number = STACK_RECTS ;

  if ( ! get_rect_op_args(&operandstack , &rects , &number , NULL , NULL ))
    return FALSE ;

  if ( number == 0 )
    return TRUE ;

  result = cliprectangles( rects , number ) ;

  if ( rects != stackrects )
    mm_free_with_header(mm_pool_temp,  rects ) ;

  return ( result ) ;
}

static Bool dorectfillgraphics( DL_STATE *page,
                                int32 do_vd ,
                                int32 do_pdfout ,
                                int32 do_hdlt ,
                                int32 isrectfill ,
                                int32 devhasrects ,
                                dbbox_t *rectfill ,
                                PATHINFO *rectpath ,
                                int32 exflags ,
                                int32 colorType )
{
  int32 result ;

  if ( ! do_vd ) {
    if ( ! flush_vignette( VD_Default ))
      return FALSE ;

    if ( do_pdfout &&
         ! pdfout_dofill( get_core_context_interp()->pdfout_h , rectpath ,
                          NZFILL_TYPE , 1 , GSC_FILL ))
      return FALSE ;

    if ( do_hdlt ) {
      switch ( IDLOM_FILL(colorType, NZFILL_TYPE, rectpath, NULL) ) {
      case NAME_false:          /* PS error in IDLOM callbacks */
        return FALSE ;
      case NAME_Discard:                /* just pretending */
        return TRUE ;
      default:                  /* only add, for now */
        ;
      }
    }
  } else { /* Doing vignette detection */
    setup_analyze_vignette() ;
  }

  result = TRUE ;
  if ( ! degenerateClipping ) {
    /* If we have a rectangle and the device supports rectangles
     * then put it on the DL as a rectangle, else as a path.
     */
    dl_currentexflags |= exflags ;
    if ( devhasrects ) {
      result = DEVICE_RECT(page, rectfill);
    }
    else {
      NFILLOBJECT *nfill;

      result = make_nfill(page, rectpath->firstpath, NFILL_ISRECT, &nfill) &&
               DEVICE_BRESSFILL(page, ( ISRECT | NZFILL_TYPE ), nfill);
    }
    dl_currentexflags &= (~exflags) ;
  }

  if ( do_vd ) {
    reset_analyze_vignette() ;
    if ( result )
      result = analyze_vignette_f(page, rectpath,
                                  isrectfill ? ISRECT : ISRECT | ISFILL,
                                  EOFILL_TYPE, TRUE, colorType) ;
  }

  return result ;
}

Bool dorectfill(int32 number, RECTANGLE *rects, int32 colorType,
                RECT_OPTIONS options)
{
  corecontext_t *context = get_core_context_interp();
  int32 maxrects = number;
  Bool devhasrects = !rcbn_enabled();
  int32 exflags = 0;
  Bool do_vd = VD_DETECT((options & RECT_NOT_VIGNETTE) == 0 &&
                         maxrects == 1 &&
                         char_current_context() == NULL);
  Bool do_pdfout = ((options & RECT_NO_PDFOUT) == 0 &&
                    pdfout_enabled());
  Bool do_hdlt = ((options & RECT_NO_HDLT) == 0 &&
                   isHDLTEnabled(*gstateptr));

  /* Do not need a DEVICE_SETG if rectfill is being called in a charpath. */
  if ( char_doing_charpath() )
    options |= RECT_NO_SETG;

  if (! context->userparams->EnablePseudoErasePage )
    options |= RECT_NOT_ERASE;

  while ((--number) >= 0 ) { /* iterate for each individual rectangle */
    SYSTEMVALUE x1 , y1 , x3 , y3;
    SYSTEMVALUE x2 , y2;
    dbbox_t     rectfill;
    Bool        isrectfill = FALSE;

    if ( theIgsPageCTM( gstateptr ).opt != MATRIX_OPT_BOTH ) {
      MATRIX_TRANSFORM_XY(rects[number].x, rects[number].y, x1, y1,
                          &theIgsPageCTM(gstateptr));
      MATRIX_TRANSFORM_DXY(rects[number].w, rects[number].h, x3, y3,
                           &theIgsPageCTM(gstateptr));
      x3 += x1;
      y3 += y1;

      if ( !char_doing_charpath() ) {
        if ( devhasrects ) {
          int32 ixy1 , ixy3;

          isrectfill = TRUE;       /* Without a doubt its a rectangle */

          SC_C2D_INT(ixy1, x1);
          SC_C2D_INT(ixy3, x3);

          if ( ixy1 > ixy3 ) {
            ixy1 ^= ixy3; ixy3 ^= ixy1; ixy1 ^= ixy3;
          }
          rectfill.x1 = ixy1;
          rectfill.x2 = ixy3;

          SC_C2D_INT(ixy1, y1);
          SC_C2D_INT(ixy3, y3);

          if ( ixy1 > ixy3 ) {
            ixy1 ^= ixy3; ixy3 ^= ixy1; ixy1 ^= ixy3;
          }
          rectfill.y1 = ixy1;
          rectfill.y2 = ixy3;
        }
      }
      x2 = x1;
      y2 = y3;
    }
    else { /* Not orthogonal, can't be a device rect */
      MATRIX_TRANSFORM_XY(rects[number].x, rects[number].y, x1, y1,
                          &theIgsPageCTM(gstateptr));
      MATRIX_TRANSFORM_DXY(rects[number].w , 0.0, x2, y2,
                           &theIgsPageCTM(gstateptr));
      x2 += x1;
      y2 += y1;
      MATRIX_TRANSFORM_DXY(0.0, rects[number].h , x3, y3,
                           &theIgsPageCTM(gstateptr));
      x3 += x2;
      y3 += y2;
    }

    {
      PATHINFO pathInfo;
      Bool result = TRUE;

      path_init(& pathInfo);

      /* If its not a rectangle that fits on the DL, or if IDLOM is
       * enabled, create a path.
       */

      if ( do_vd || !isrectfill || do_pdfout || do_hdlt ) {
        /* Set up a rectangular path, must be freed later */
        if (! path_add_four(&pathInfo, x1, y1, x2, y2, x3, y3,
                            x1 - (x2 - x3), y1 - (y2 - y3)))
          return FALSE;

        /* fastfill can not be TRUE if this is a charpath. See if
         * this is a charpath and if so then use the path we've just set up.
         */
        if ( char_doing_charpath() ) {
          if ( ! add_charpath( & pathInfo , TRUE ))
            result = FALSE;

          path_free_list( thePath( pathInfo ), mm_pool_temp);

          if (! result)
            return FALSE;

          continue; /* continue around to the next 'rectangle' */
        }
      }

      /* If isrectfill is not set, see if the path is a rectangle */
      if ( isrectfill
           || (path_rectangles(thePath(pathInfo), FALSE, &rectfill) == 1) ) {
        isrectfill = TRUE;
        /* If we only had 1 rectangle on the list, and that rectangle
         * is the size of the page(or larger), then it is the same as
         * doing an erase. Check the transfer function to determine
         * if the page is now becoming a negative.
         */
        if (  (options & RECT_NOT_ERASE) == 0 && maxrects == 1 &&
              is_pagesize(context->page, &rectfill, colorType) &&
              !is_pseudo_erasepage()) {
          if ( !gsc_analyze_for_forcepositive(context, gstateptr->colorInfo, colorType,
                                              &context->page->forcepositive) ) {
            if ( pathInfo.lastline)
              path_free_list( thePath( pathInfo ), mm_pool_temp);
            return FALSE;
          }
          do_vd = FALSE;
          exflags = RENDER_PSEUDOERASE;
        }
      }

      if ( CURRENT_DEVICE() == DEVICE_NULL && !do_pdfout && !do_hdlt ) {
        if ( pathInfo.lastline)
          path_free_list( thePath( pathInfo ), mm_pool_temp);
        return TRUE;
      }

      /* Only call setg if we've not done it yet AND if
       * we're not in a charpath. SETG is postponed till here
       * since erasenegative/forcepositive can cause the color
       * to invert */
      if ( (options & RECT_NO_SETG) == 0 ) {
        options |= RECT_NO_SETG;
        if ( !DEVICE_SETG(context->page, colorType, DEVICE_SETG_NORMAL) ) {
          if ( pathInfo.lastline)
            path_free_list( thePath( pathInfo ), mm_pool_temp);
          return FALSE;
        }
      }
      if ( degenerateClipping && !do_pdfout && !do_hdlt ) {
        if ( pathInfo.lastline)
          path_free_list( thePath( pathInfo ), mm_pool_temp);
        return TRUE;
      }

      result = dorectfillgraphics(context->page, do_vd, do_pdfout, do_hdlt,
                                  isrectfill , isrectfill && devhasrects,
                                  &rectfill, &pathInfo, exflags, colorType);

      if ( pathInfo.lastline)
        path_free_list( thePath( pathInfo ), mm_pool_temp);

      if ( !result )
        return FALSE;
    }
  }

  /* If we're recombining or doing imposition and above we found
   * a rectangle that is larger than the imposition or the page
   * then call dlskip_pseudo_erasepage() to adjust the imposition
   * pointers
   */
  if ( !degenerateClipping )
    if ( rcbn_enabled() || doing_imposition )
      if ( exflags == RENDER_PSEUDOERASE )
        if ( ! dlskip_pseudo_erasepage(context->page))
          return FALSE;

  return TRUE;
}

/** Utility for the operators to read in encoded number strings. The
   vector_size arg gives the number of numbers in a unit - rectangle
   operators require 4, xyshow requires 2, etc. */
Bool decode_number_string( OBJECT *stro ,
                            SYSTEMVALUE  **ret_array ,
                            int32 *ret_number ,
                            int32 vector_size )
/* Parameters:
  stro        : the encoded string (or longstring) object
  ret_array   : returns pointer to alloced number array
  ret_number  : return the number of vectors found
  vector_size : the number of SYSTEMVALUE numbers required in a vector
                - e.g. rectangles has 4
*/
{
  SYSTEMVALUE *parray ;    /* alloced array of numbers */
  SYSTEMVALUE *psysval ;   /* points to current SYSTEMVALUE */
  FOURBYTES   fb ;         /* glorious hack to avoid too many type casts */
  uint8       *pstr ;      /* current byte in string */
  int32       r , l1 , l2 , i ;
  int32       len_string ; /* length of string in bytes */
  int32       len_array ;  /* length of array in elements */
  int32       hiorder ;
  int32       extra_long ; /* HNA length in first 4 bytes, not header */

  HQASSERT( oType(*stro) == OSTRING ||
            oType(*stro) == OLONGSTRING,
            "decode_number_string() called with non-string object" );

  if ( ! oCanRead( *stro ))
    return error_handler( INVALIDACCESS ) ;

  if ( oType( *stro ) == OLONGSTRING ) {
    len_string = theLSLen(*oLongStr(*stro)) ;
    pstr = theLSCList(*oLongStr(*stro)) ;
  }
  else { /* OSTRING */
    len_string = theLen(*stro) ;
    pstr = oString( *stro ) ;
  }

  if ( len_string < 4 ) /* too short for header bytes */
    return error_handler( TYPECHECK ) ;
  len_string -= 4 ;

  HQASSERT( pstr, "decode_number_string() called with invalid string object" );

  /* get the header bytes */
  if (( int32 )( *pstr ) == BINTOKEN_HNA )
    extra_long = FALSE ;
  else if (( int32 )( *pstr ) == BINTOKEN_EXTHNA )
    extra_long = TRUE ;
  else
    return error_handler( TYPECHECK ) ;
  pstr++ ;
  r = ( int32 ) *(pstr++) ;
  l1 = ( int32 ) *(pstr++) ;
  l2 = ( int32 ) *(pstr++) ;

  if ( r < HNA_REP_LOWORDER )
    hiorder = TRUE ;
  else {
    hiorder = FALSE ;
    r -= HNA_REP_LOWORDER ;
  }

  if ( extra_long ) {
    if ( len_string < 4 ) /* too short for length bytes */
      return error_handler( TYPECHECK ) ;
    len_string -= 4 ;
    len_array = 0 ;
    if ( hiorder ) {
      for ( i = 0 ; i < 4 ; i++ )
        len_array = ( len_array << 8 ) + *(pstr++) ;
    }
    else {
      for ( i = 0 ; i < 4 ; i++ )
        len_array = len_array + (*(pstr++) << ( 8 * i )) ;
    }
  } else {
    if ( hiorder )
      len_array = ( l1 << 8 ) + l2 ;
    else
      len_array = l1 + ( l2 << 8 ) ;
  }

  /* check for multiple of vector size numbers in array
   * Also, extra_longs may overflow signed len_array.
   * Empty arrays are pointless, but are valid and accepted anyway
   */
  if ( len_array % vector_size || len_array < 0 )
    return error_handler( TYPECHECK ) ;

  /* allocate array of numbers */
  if ( vector_size * (*ret_number) >= len_array ) {
    parray = (*ret_array) ;
  }
  else {
    if ( NULL == ( parray = ( SYSTEMVALUE * )mm_alloc_with_header(mm_pool_temp,
                                                                  len_array  *
                                                 sizeof( SYSTEMVALUE ),
                                                 MM_ALLOC_CLASS_RECT_NUMBERS )))
      return error_handler( VMERROR ) ;
  }
  psysval = parray ;

  /* handle the different number types
   * note that we don't care if the string we have is too long - just that
   * it's long enough to contain the required number of elements.
   */
  if ( r <= HNA_REP_32FP_HI ) {
    if (( len_string >> 2 ) < len_array ) {
      if ( parray != (*ret_array) )
        mm_free_with_header(mm_pool_temp,  parray ) ;
      return error_handler( TYPECHECK ) ;
    }
    for ( i = 0 ; i < len_array ; i++ ) {
      asInt( fb ) = *( ( uint32 * ) pstr ) ;
      if ( hiorder ) {
        HighOrder4Bytes( asBytes( fb )) ;
      } else {
        LowOrder4Bytes( asBytes( fb )) ;
      }
      *psysval = ( SYSTEMVALUE ) FixedToFloat( asSignedInt( fb ) , r ) ;
      psysval++ ;
      pstr += 4 ;
    }
  } else if ( r <= HNA_REP_16FP_HI ) {
    if (( len_string >> 1 ) < len_array ) {
      if ( parray != (*ret_array) )
        mm_free_with_header(mm_pool_temp,  parray ) ;
      return error_handler( TYPECHECK ) ;
    }
    r -= 32 ;
    for ( i = 0 ; i < len_array ; i++ ) {
      asShort( fb ) = *( ( uint16 * ) pstr ) ;
      if ( hiorder )
        HighOrder2Bytes( asBytes( fb )) ;
      else
        LowOrder2Bytes( asBytes( fb )) ;
      *psysval = ( SYSTEMVALUE ) FixedToFloat( asSignedShort( fb ) , r ) ;
      psysval++ ;
      pstr += 2 ;
    }
  } else if ( r == HNA_REP_IEEE_HI ) {
    if (( len_string >> 2 ) < len_array ) {
      if ( parray != (*ret_array) )
        mm_free_with_header(mm_pool_temp,  parray ) ;
      return error_handler( TYPECHECK ) ;
    }
    for ( i = 0 ; i < len_array ; i++ ) {
      asInt( fb ) = *( ( uint32 * ) pstr ) ;
      if ( hiorder ) {
        HighOrder4Bytes( asBytes( fb )) ;
      } else {
        LowOrder4Bytes( asBytes( fb )) ;
      }
      *psysval = ( SYSTEMVALUE ) IEEEToFloat( asFloat( fb )) ;
      psysval++ ;
      pstr += 4 ;
    }
  } else if ( r == HNA_REP_32NREAL ) {
    if (( len_string >> 2 ) < len_array ) {
      if ( parray != (*ret_array) )
        mm_free_with_header(mm_pool_temp,  parray ) ;
      return error_handler( TYPECHECK ) ;
    }
    for ( i = 0 ; i < len_array ; i++ ) {
      asInt( fb ) = *( ( uint32 * ) pstr ) ;
      *psysval = ( SYSTEMVALUE ) asFloat( fb ) ;
      psysval++ ;
      pstr += 4 ;
    }
  } else {
    if ( parray != (*ret_array) )
      mm_free_with_header(mm_pool_temp,  parray ) ;
    return error_handler( TYPECHECK ) ;
  }

  *ret_array = parray ;
  *ret_number = len_array / vector_size ;
  return TRUE ;
}


/** Utility for the above functions to collect the arguments of the type :
          x y width height
   or             numarray
   or            numstring

   If got_matrix is not NULL, then the routine checks to see if the top
   object on the stack is a matrix (used in rectstroke only).
*/
Bool get_rect_op_args(STACK *stack,
                      RECTANGLE    **ret_array ,
                      int32        *ret_number ,
                      OMATRIX      *ret_matrix  ,
                      Bool         *got_matrix )
{
  register OBJECT *theo ;
  register int32  ssize ;
  register int32  i ;
  RECTANGLE       *prects ;     /* alloced array of rectangles */
  SYSTEMVALUE     *psysval ;    /* points to vals within rectangle array */
  int32           pop_off = 0 ; /* number of objects to pop off at end */
  int32           number ;      /* number of rects */

  /* get first argument off the stack to determine if there are more */
  ssize = theIStackSize( stack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theITop( stack ) ;
  if ( got_matrix != NULL ) {
    if ( is_matrix_noerror( theo , ret_matrix )) {
      if ( ssize < 1 )
        return error_handler( STACKUNDERFLOW) ;
      theo = stackindex( 1 , stack ) ;
      *got_matrix = TRUE ;
      pop_off++ ;
    } else
      *got_matrix = FALSE ;
  }
  prects = NULL ;

  number = 0 ;
  switch ( oType(*theo) ) {
  case OINTEGER :
  case OREAL :
    /* x y width height or x y w h matrix format */
    if ( (ssize - pop_off) < 3 )
      return error_handler( STACKUNDERFLOW ) ;
    if ( (*ret_number) >= 1 ) {
      prects = (*ret_array) ;
    }
    else {
      if ( NULL == ( prects = (RECTANGLE *)mm_alloc_with_header(mm_pool_temp,
                                             sizeof( RECTANGLE ),
                                             MM_ALLOC_CLASS_RECTS)))
        return error_handler( VMERROR ) ;
    }

    /* theo contains the height of the rectangle */
    prects->h = object_numeric_value(theo) ;

    /* get the width, y and x off the stack */
    psysval = ( SYSTEMVALUE * )( & prects->w ) ;
    for ( i = 1 ; i <= 3 ; i++ ) {
      theo = stackindex( pop_off + i , stack ) ;
      if ( !object_get_numeric(theo, psysval--) ) {
        if ( prects != (*ret_array) )
          mm_free_with_header(mm_pool_temp,  prects ) ;
        return FALSE ;
      }
    }
    pop_off += 4 ;
    number = 1 ;
    break ;

  case OARRAY :
  case OPACKEDARRAY :
    if ( ! oCanRead( *theo ))
      return error_handler( INVALIDACCESS ) ;
    number = ( int32 )theLen(*theo) ;
    /* Empty arrays, although pointless, are valid and accepted.
     * And cos number is taken from uint16 length, it can never be < 0
     * We do ensure that we have a multiple of 4 items though.
     */
    if ( number & 0x3 )
      return error_handler( TYPECHECK ) ;
    if ( 4 * (*ret_number) >= number ) {
      prects = (*ret_array) ;
    }
    else {
      prects = (RECTANGLE *)
        mm_alloc_with_header(mm_pool_temp, (number >> 2) * sizeof( RECTANGLE ),
                             MM_ALLOC_CLASS_RECTS);
      if ( NULL == prects )
        return error_handler( VMERROR ) ;
    }
    psysval = ( SYSTEMVALUE * ) prects ;
    theo = oArray(*theo) ;

    for ( i = 0 ; i < number ; i++ ) {
      if ( !object_get_numeric(theo++, psysval++) ) {
        if ( prects != (*ret_array) )
          mm_free_with_header(mm_pool_temp,  prects ) ;
        return FALSE ;
      }
    }
    number >>= 2 ;
    pop_off++ ;
    break ;

  case OSTRING :
  case OLONGSTRING:
    /* encoded number string */
    prects = (*ret_array) ;
    number = (*ret_number) ;
    if ( !decode_number_string( theo, (SYSTEMVALUE **)(void *)&prects,
                                /* RECTANGLE is a struct of four
                                   SYSTEMVALUEs, so we can pun them. */
                                &number, 4 ))
      return FALSE ;
    pop_off++ ;
    break ;
  default :
    return error_handler( TYPECHECK ) ;
  }

  npop( pop_off , stack ) ;
  *ret_array = prects ;
  *ret_number = number ;
  return TRUE ;
}

/* Log stripped */
