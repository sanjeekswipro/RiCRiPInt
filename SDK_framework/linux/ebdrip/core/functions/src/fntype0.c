/** \file
 * \ingroup samplefuncs
 *
 * $HopeName: COREfunctions!src:fntype0.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type 0 PS functions
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"

#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "control.h"
#include "dictscan.h"
#include "namedef_.h"
#include "psvm.h"
#include "caching.h"
#include "functns.h"

#include "fnpriv.h"
#include "fntype0.h"


/*----------------------------------------------------------------------------*/
typedef struct fntype0 {
  int32 bitspersample ;
  int32 order ;
  SYSTEMVALUE s_size[ 4 ] ;
  SYSTEMVALUE s_encode[ 8 ] ;
  SYSTEMVALUE s_decode[ 8 ] ;
  OBJECT *size ;
  OBJECT *encode ;
  int32 free_encode ;
  OBJECT *decode ;
  SYSTEMVALUE *workspace ;      /* Only used for general M to N case */
  Bool workspaceInitialised ;   /* Only used for general M to N case */
  uint32 workspacesize ;
  uint32 sampletablesize ;
  uint32 *sampletable ;

  /* These are used to determine whether a function is linear for the purpose of
   * the discontinuity tests which can significantly reduce the amount shfill
   * decomposition. The values returned by the evaluate functions aren't
   * affected by this linearity testing.
   */
  struct {
    Bool    all_linear; /* all functions in the set are linear */
    Bool    segment;
    SYSTEMVALUE linearity;
    uint32  segs_memsize;
    uint32  segs_max;
    uint32  *segs;
    uint32  numsegs;
  } discont;
} FNTYPE0 ;

/*----------------------------------------------------------------------------*/
static Bool fntype0_default_encode( FUNCTIONCACHE *fn );
static Bool fntype0_encode_input( SYSTEMVALUE *input, SYSTEMVALUE *encoded,
                                  FUNCTIONCACHE *fn );
static Bool fntype0_decode_output( SYSTEMVALUE *sample, SYSTEMVALUE *decoded,
                                   FUNCTIONCACHE *fn );
static void  fntype0_freecache( FUNCTIONCACHE *fn );
static Bool fntype0_evaluate_1_to_1( FUNCTIONCACHE *fn, Bool upwards,
                                     SYSTEMVALUE *input, SYSTEMVALUE *output );
static Bool fntype0_evaluate_1_to_N( FUNCTIONCACHE *fn, Bool upwards,
                                     SYSTEMVALUE *input, SYSTEMVALUE *output );
static Bool fntype0_evaluate_2_to_1( FUNCTIONCACHE *fn, Bool upwards,
                                     SYSTEMVALUE *input, SYSTEMVALUE *output );
static Bool fntype0_evaluate_2_to_N( FUNCTIONCACHE *fn, Bool upwards,
                                     SYSTEMVALUE *input, SYSTEMVALUE *output );
static Bool fntype0_evaluate_M_to_N( FUNCTIONCACHE *fn, Bool upwards,
                                     SYSTEMVALUE *input, SYSTEMVALUE *output );
static Bool fntype0_find_discontinuity( FUNCTIONCACHE *fn, int32 index,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity, int32 *order );
static Bool load_sampletable( uint32 *sptr, uint32 len,
                              int32 bitps, FILELIST *flptr );
static Bool load_sampletable_str( uint32 *sptr, uint32 len,
                                  int32 bitps, uint8 *string, int32 strlen );

/*----------------------------------------------------------------------------*/
Bool fntype0_initcache( FUNCTIONCACHE *fn )
{
  FNTYPE0 *t0 ;

  HQASSERT( fn , "fn is null in fntype0_initcache" ) ;
  HQASSERT( fn->specific == NULL ,
            "specific not null in fntype0_initcache" ) ;
  t0 = mm_alloc( mm_pool_temp ,
                 sizeof( FNTYPE0 ) ,
                 MM_ALLOC_CLASS_FUNCTIONS ) ;
  if ( t0 == NULL )
    return error_handler( VMERROR ) ;

  fn->specific = ( fn_type_specific )t0 ;

  t0->bitspersample = 0 ;
  t0->order = 0 ;
  t0->size = NULL ;
  t0->encode = NULL ;
  t0->free_encode = FALSE ;
  t0->decode = NULL ;
  t0->sampletablesize = 0 ;
  t0->sampletable = NULL ;
  t0->workspacesize = 0 ;
  t0->workspace = NULL ;
  t0->workspaceInitialised = FALSE ;

  t0->discont.segment = FALSE;
  t0->discont.linearity = 1.0;
  t0->discont.all_linear = FALSE;
  t0->discont.segs_memsize = 0;
  t0->discont.segs_max = 0;
  t0->discont.segs = NULL;
  t0->discont.numsegs = 0;

  fn->evalproc = NULL ; /* set in fntype0_unpack */
  fn->discontproc = fntype0_find_discontinuity ;
  fn->freeproc = fntype0_freecache ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/

/* retrieve a line segment's end point (val )given its starting point (start).
   return TRUE if found */
static Bool find_interval(uint32 * segs, uint32 * segs_end, uint32 start, uint32 * val)
{
  while (*segs != start) {
    segs++;
    segs++;
    if (segs >= segs_end)
      return FALSE; /* off the end */
  }

  *val = segs[1]; /* end point */

  return TRUE;
}

/* check this line fits on the function data
   (within a given margin of error given by linearity) */
static Bool isall_linear(uint32 dim, uint32 chan, uint32 *func,
                         uint32 start, uint32 end,
                         SYSTEMVALUE linearity)
{
   uint32 tick;
   SYSTEMVALUE grad;
   SYSTEMVALUE start_val = func[(dim * start) + chan];
   SYSTEMVALUE end_val = func[(dim * end) + chan];

   if (start == end)
     return TRUE;

   HQASSERT(end > start, "end <= start");

   grad = (end_val - start_val)/(end - start);

   for (tick = start; tick <= end; tick++) {
     SYSTEMVALUE y1 = func[(dim * tick) + chan];
     SYSTEMVALUE y2 = start_val + (grad * (tick - start));
     SYSTEMVALUE lin;

     lin = fabs(y2 - y1);
     if (lin > linearity)
       return FALSE;
   }

   return TRUE;
}



/* given a list of line segments' pairs of points, check to see if any are
   co-linear with respect to their neighbour and if so, merge them.
   return number of merged segments.

   The merged segments are recorded in order the output array */
static uint32 *merge_segs_samples(uint32 *segs,
                                  uint32 *segs_end,
                                  uint32 dim,
                                  uint32 chan,
                                  uint32 *func,
                                  SYSTEMVALUE linearity,
                                  uint32 *out)
{
  uint32 start,end1;
  uint32 end2 = 0;

  start = end1 = 0;

  /* merge lines if possible.
   * There are a couple of bail-outs in this block. These should never happen, but
   * might if the linearity tests in isall_linear() and colinear() fail to agree.
   */
  while (find_interval(segs, segs_end, end1, &end2)) {
    if (end2 <= end1) {
      /* Belt and braces. Shouldn't happen but just in case. */
      HQFAIL("Failed to merge type 0 segments");
      return NULL;
    }

    if (isall_linear(dim, chan, func, start, end2, linearity)) {
      /* it is! see if the next line segment is also co-linear */
      end1 = end2;
    } else {
      if (start == end1) {
        /* More belt and braces */
        HQFAIL("Segment isn't linear when it was in colinear()");
        return NULL;
      }

      /* nope! save and move onto next one */
      *out++ = start;
      *out++ = end1;

      start = end1;
    }
  }

  HQASSERT(end1 == end2 && end1 != 0, "Unexpected algorithm failure");

  *out++ = start;
  *out++ = end1;

  return out;
}


/* merge two ordered segment arrays rle_new has pairs whilst rle_curr
   has only discontinuity boundaries */
static uint32 merge_segs_chan(uint32 *rle_new,
                              uint32 in_count,
                              uint32 *rle_ws,
                              uint32 *rle_curr,
                              uint32 *numsegs)
{
  uint32 * curr = rle_curr;
  uint32 * ws = rle_ws;
  uint32 last;
  uint32 existing_count = *numsegs;
  uint32 * out_start = rle_curr;

  /* first merge into work space*/
  while (in_count | existing_count) {
     if (in_count) {
      if (existing_count) {
        if (*rle_new == *rle_curr) {
          *rle_ws++ = *rle_new++;
          in_count--;
          rle_curr++;
          existing_count--;
        } else {
          if (*rle_new < *rle_curr) {
            *rle_ws++ = *rle_new++;
            in_count--;
          } else {
            *rle_ws++ = *rle_curr++;
            existing_count--;
          }
        }
      } else {
        /* rle_curr exhausted */
        while (in_count) {
          *rle_ws++ = *rle_new++;
          in_count--;
        }
      }
    } else {
      /* rle_new exhausted */
      while (existing_count) {
        *rle_ws++ = *rle_curr++;
        existing_count--;
      }
    }
  }

  /* now copy workspace over current list */
  while (ws < rle_ws){
    last = *ws;
    *curr++ = *ws++;

    /* (drop double points) */
    if (last == *ws)
      ws++;
  }

  HQASSERT(curr > out_start, "merge_segs_chan: no segments merged" );

  /* update total */
  *numsegs = CAST_PTRDIFFT_TO_UINT32((curr-1) - out_start);

  return *numsegs;
}


/* given 2 end points on a candidate line (x1,y1) (x2, y2) and a function segment running from
   range values start to end, check the segment's centre point with the candidate line to see
   if it is co-linear. if so then recurse with each half until all points are exhausted.
   if all points are on the line then we record a co-linear segment. if not then divide
   the top segment and use end points as new line ends.

   records line segments start and end points in array, *p_segs.
   returns TRUE if all points are co-linear, p_segs is incremented and points past the last
   added array entry.

   note an error margin of linearity is allowed on a match but this still ensures
   all points on a candidate line do not vary by more than that amount
   (i.e. no compound error).

   (**NB: line segments are not recorded in order) */
static Bool colinear(uint32 **p_segs, uint32 dim, uint32 chan,
                     uint32 *func,
                     int32 start, int32 end,
                     uint32 x1, uint32 x2,
                     SYSTEMVALUE linearity)
{
   int32 mid;
   Bool left,right;
   SYSTEMVALUE estimated_mid_val;
   SYSTEMVALUE actual_mid_val;
   SYSTEMVALUE y1;
   SYSTEMVALUE y2;
   SYSTEMVALUE lin;

   mid = start + ((end - start)/2);

   if (mid == start)
     return TRUE;

   HQASSERT(x2 > x1, "x2 <= x1");

   /* predict line position*/
   y1 = func[(dim * x1) + chan];
   y2 = func[(dim * x2) + chan];
   estimated_mid_val = y1 + ((y2 - y1) * (mid - x1))/(x2 - x1);

   /* see how much the funciton varies from this */
   actual_mid_val = func[(dim * mid) + chan];
   lin = fabs(actual_mid_val - estimated_mid_val);

   /* if variation is too big then split and see if resulting halves
      are lines.*/
   if (lin > linearity) {
     left = colinear(p_segs, dim, chan,
                     func,
                     start, mid,
                     start, mid,
                     linearity);

     if (left) {
       /* left half is linear so record it */
       *(*p_segs)++ = start;
       *(*p_segs)++ = mid;
     }

     right = colinear(p_segs, dim, chan,
                      func,
                      mid, end,
                      mid, end,
                      linearity);

     if (right) {
       /* right half is liner so record it */
       *(*p_segs)++ = mid;
       *(*p_segs)++ = end;
     }

     return FALSE;
   } else {
     /* check that both left and right halves are on this line */
     left = colinear(p_segs, dim, chan,
                     func, start, mid,
                     x1, x2,
                     linearity);
     right = colinear(p_segs, dim, chan,
                      func,
                      mid, end,
                      x1, x2,
                      linearity);

     /* handle a half match */
     if ((left || right) && !(left && right) ) {

       /* if one side matches then try it against it's own end points */
       if (left) {
         left = colinear(p_segs, dim, chan,
                         func,
                         start, mid,
                         start, mid,
                         linearity);

         if (left) {
           /* left half is a line so record it */
           *(*p_segs)++ = start;
           *(*p_segs)++ = mid;
         }
       } else {
         right = colinear(p_segs, dim, chan,
                          func,
                          mid, end,
                          mid, end,
                          linearity);
         if (right) {
           /* right half is a line so record it */
           *(*p_segs)++ = mid;
           *(*p_segs)++ = end;
         }
       }
     }

     return (left && right);
   }
}

/*----------------------------------------------------------------------------*/

Bool fntype0_unpack( FUNCTIONCACHE *fn , OBJECT *thed ,
                     OBJECT *thes , FILELIST *flptr )
{
  FNTYPE0 *t0 ;
  OBJECT *optr ;
  uint32 len ;
  int32 bitps ; /* BitsPerSample */
  uint8 *string = NULL ;
  int32 strlen = 0 ;
  uint32 ws_needed ;

  enum {
    fn0_Size, fn0_BitsPerSample, fn0_Order, fn0_Encode, fn0_Decode,
    fn0_DataSource, fn0_dummy
  } ;
  static NAMETYPEMATCH fntype0_dict[fn0_dummy+1] = {
    { NAME_Size,                    3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_BitsPerSample,           1, { OINTEGER }},
    { NAME_Order      | OOPTIONAL , 1, { OINTEGER }},
    { NAME_Encode     | OOPTIONAL , 2, { OARRAY, OPACKEDARRAY }},
    { NAME_Decode     | OOPTIONAL , 2, { OARRAY, OPACKEDARRAY }},
    { NAME_DataSource | OOPTIONAL , 2, { OSTRING, OFILE }},
    DUMMY_END_MATCH
  } ;

  HQASSERT( fn , "fn is null in fnfntype0_unpack." ) ;
  HQASSERT( thed , "thed is null in fntype0_unpack." ) ;
  HQASSERT(oType(*thed) == ODICTIONARY ,
            "thed is not a dictionary in fntype0_unpack." ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_unpack" ) ;

  /* Domain will have already been checked as present by dictmatch. */
  if ( fn->out_dim == 0 )
    return error_handler( TYPECHECK ) ;

  if ( ! dictmatch( thed , fntype0_dict ))
    return FALSE ;

  if ( flptr == NULL ) {
    /* Then the sampletable must be present as the value to /DataSource. */
    OBJECT *data = fntype0_dict[fn0_DataSource].result ;
    if ( data == NULL )
      return error_handler( UNDEFINED ) ;
    if (oType(*data) == OFILE ) {
      flptr = oFile(*data) ;
      HQASSERT( thes == NULL ,
                "thes is not null in fntype0_unpack" ) ;
      thes = data ;
    }
    else {
      HQASSERT(oType(*data) == OSTRING ,
                "expected data to be a string" ) ;
      strlen = ( int32 ) theILen( data ) ;
      string = oString(*data) ;
    }
  }

  /* Size */

  /* Number of samples in each input dimension of the sample table. */
  /* Must be a positive integer or an array of positive integers (excl. zero) */
  if (oType(*fntype0_dict[fn0_Size].result) == OINTEGER ) {

    if ( fn->in_dim != 1 )
      return error_handler( TYPECHECK ) ;

    if ( oInteger(*fntype0_dict[fn0_Size].result) <= 0 )
      return error_handler( RANGECHECK ) ;

    t0->s_size[ 0 ] = oInteger(*fntype0_dict[fn0_Size].result) ;

  } else { /*oType(*fntype0_dict[fn0_Size].result) == O[PACKED]ARRAY */

    OBJECT *theo ;
    int32 len ;
    int32 i ;

    len = theILen( fntype0_dict[fn0_Size].result ) ;
    if ( fn->in_dim != len )
      return error_handler( RANGECHECK ) ;

    theo = oArray(*fntype0_dict[fn0_Size].result) ;

    for ( i = 0 ; i < len ; ++i ) {

      if (oType(*theo) != OINTEGER )
        return error_handler( TYPECHECK ) ;

      if ( oInteger(*theo) <= 0 )
        return error_handler( RANGECHECK ) ;

      if ( len <= 4 )
        t0->s_size[ i ] = oInteger(*theo) ;

      ++theo ;
    }

    t0->size = fntype0_dict[fn0_Size].result ;
  }

  /* BitsPerSample */

  /* Number of bits used to represent each sample value. */
  bitps = t0->bitspersample = oInteger(*fntype0_dict[fn0_BitsPerSample].result) ;
  switch ( bitps ) {
  case 1  : case 2  : case 4  : case 8  :
  case 12 : case 16 : case 24 : case 32 : break ;
  default : return error_handler( RANGECHECK ) ;
  }

  /* Order */

  /* Obtain the order of the interpolation function. */
  optr = fntype0_dict[fn0_Order].result ;
  if ( ! optr )
    t0->order = 1 ; /* default value */
  else {
    t0->order = oInteger(*optr) ;
    if ( t0->order != 1 ) {
      if ( t0->order == 3 ) {
        /** \todo ajcd 2007-12-28: Implement cubic spline interpolation
            sometime. */
        HQTRACE( trace_fn , ("Cubic-spline interpolation NYI.")) ;
        t0->order = 1 ; /* ignore given order, do linear instead */
      }
      else
        return error_handler( RANGECHECK ) ;
    }
  }

  /* Encode */

  if ( ! fntype0_dict[fn0_Encode].result ) {
    if ( ! fntype0_default_encode( fn ))
      return FALSE ;
  }
  else {
    if ( t0->encode != NULL && t0->free_encode ) {
      /* Free the object list and the array object itself. */
      mm_free( mm_pool_temp ,
               t0->encode ,
               ( theILen( t0->encode ) + 1 ) * sizeof( OBJECT )) ;
      t0->encode = NULL ;
    }
    t0->free_encode = FALSE ;

    if ( ( 2 * fn->in_dim ) != theILen( fntype0_dict[fn0_Encode].result ))
      return error_handler( RANGECHECK ) ;

    if ( fn->in_dim > 4 ) {
      if ( ! fn_typecheck_array( fntype0_dict[fn0_Encode].result ))
        return FALSE ;
      t0->encode = fntype0_dict[fn0_Encode].result ;
    } else {
      if ( ! object_get_numeric_array(fntype0_dict[fn0_Encode].result,
                                      t0->s_encode,
                                      theILen(fntype0_dict[fn0_Encode].result)) )
        return FALSE ;
    }
  }

  /* Decode */

  if ( ! fntype0_dict[fn0_Decode].result ) {
    /* Default decode array is the same as Range */
    if ( fn->out_dim > 4 )
      t0->decode = fn->range ;
    else
    {
      SYSTEMVALUE *range , *decode ;
      int32 i , len = fn->out_dim * 2 ;

      range = fn->s_range ;
      decode = t0->s_decode ;
      for ( i = 0 ; i < len ; ++i )
        *decode++ = *range++ ;
    }
  }
  else {
    if ( ( 2 * fn->out_dim ) != theILen( fntype0_dict[fn0_Decode].result ))
      return error_handler( RANGECHECK ) ;

    if ( fn->out_dim > 4 ) {
      if ( ! fn_typecheck_array( fntype0_dict[fn0_Decode].result ))
        return FALSE ;
      t0->decode = fntype0_dict[fn0_Decode].result ;
    } else {
      if ( ! object_get_numeric_array(fntype0_dict[fn0_Decode].result,
                                      t0->s_decode,
                                      theILen(fntype0_dict[fn0_Decode].result)) )
        return FALSE ;
    }
  }

  /* Workspace */

  if ( fn->in_dim == 1 ) {
    ws_needed = 0 ;
    fn->evalproc =
      ( fn->out_dim == 1 ? fntype0_evaluate_1_to_1 : fntype0_evaluate_1_to_N ) ;
  }
  else if ( fn->in_dim == 2 ) {
    ws_needed = 0 ;
    fn->evalproc =
      ( fn->out_dim == 1 ? fntype0_evaluate_2_to_1 : fntype0_evaluate_2_to_N ) ;
  }
  else {
    ws_needed =
      fn->in_dim * ( sizeof( uint32 ) +                       /* n_integral_part */
                     sizeof( uint32 ) +                       /* n_index_stride */
                     sizeof( uint32 ) +                       /* skip_dim */
                     sizeof( SYSTEMVALUE ) +                  /* n_fractional_part */
                     sizeof( SYSTEMVALUE )) +                 /* omn_fractional_part */
      ( (size_t)1u << fn->in_dim ) * sizeof( SYSTEMVALUE ) +          /* n_result */
      ( (size_t)1u << fn->in_dim ) * sizeof( uint32 ) ;               /* relative_pos */

    fn->evalproc = fntype0_evaluate_M_to_N ;
  }

  if ( t0->workspace && ws_needed != t0->workspacesize ) {
    mm_free( mm_pool_temp, t0->workspace,
            t0->workspacesize );
    t0->workspace = NULL ;
    t0->workspacesize = 0;
    t0->workspaceInitialised = FALSE ;
  }

  if ( ws_needed > 0 && t0->workspace == NULL ) {
    t0->workspace = mm_alloc( mm_pool_temp ,
                              ws_needed ,
                              MM_ALLOC_CLASS_FUNCTIONS ) ;
    if ( t0->workspace == NULL )
      return error_handler( VMERROR ) ;
    t0->workspacesize = ws_needed ;
  }

  /* Sampletable */

  /* Work-out the number of bytes in the stream - can't use the length
   * hint as this may not be present and in any case would be either 1
   * or 2 bytes longer than required depending on whether the stream
   * terminates with a CR or CRLF endstream. */
  if ( fn->in_dim <= 4 )
  {
    SYSTEMVALUE *ptr ;
    SYSTEMVALUE *end ;

    ptr = t0->s_size ;
    end = ptr + fn->in_dim ;
    len = fn->out_dim ;
    while ( ptr < end ) {
      len *= ( int32 ) *ptr ;
      ++ptr ;
    }
  }
  else {
    OBJECT *end ;

    optr = oArray(*t0->size) ;
    end = optr + fn->in_dim ;
    len = ( uint32 ) fn->out_dim ;
    while ( optr < end ) {
      int32 arg ;
      if ( !object_get_integer(optr++, &arg) )
        return FALSE ;
      len *= ( uint32 ) arg ;
    }
  }

  if ( t0->sampletable && t0->sampletablesize != len ) {
    mm_free( mm_pool_temp, t0->sampletable,
             sizeof( uint32 ) * t0->sampletablesize );
    t0->sampletable = NULL ;
    /* If the workspace was valid, a new value of out_dim invalidates it */
    t0->workspaceInitialised = FALSE ;
  }

  if ( ! t0->sampletable ) {
    t0->sampletable = mm_alloc( mm_pool_temp,
                                sizeof( uint32 ) * len ,
                                MM_ALLOC_CLASS_FUNCTIONS ) ;
    if ( ! t0->sampletable )
      return error_handler( VMERROR ) ;
  }

  t0->sampletablesize = len ;

  if ( flptr != NULL ) {
    Hq32x2 filepos;
    /* Rewind the file */
    if ( ! isIOpenFileFilter( thes , flptr ) ||
         ! isIInputFile( flptr ))
      return error_handler( IOERROR ) ;

    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      return error_handler( IOERROR );
    Hq32x2FromInt32(&filepos, 0);
    if ((*theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
      return error_handler( IOERROR );

    /* Load the sample table */
    if (!load_sampletable( t0->sampletable , t0->sampletablesize ,
                             bitps , flptr ) )
      return FALSE;
  } else {
    /* Load and expand the string into the sample table. */
    if (!load_sampletable_str( t0->sampletable , t0->sampletablesize ,
                                 bitps , string , strlen ) )
      return FALSE;
  }


  /* For 1 dimensional functions attempt to optimise the curve samples down to
   * a smaller set of linear segments for the purpose of finding discontinuities
   * only. Values returned by the evaluation functions aren't affected by this.
   * Only shaded fills are expected to require discontinuity searches so limit
   * this to functions from shadings.
   */
  t0->discont.segment = fn->in_dim == 1 &&
                        fn->usage == FN_SHADING;


  if (t0->discont.segment) {
    int32 chan;
    uint32 val,tick;
    uint32 samples;
    uint32 merge_count;
    uint32 *segs;
    uint32 *merged;
    uint32 *merged_end;
    uint32 *t_rle;
    Bool    *chan_linear;

    /* A value of difference between segment mid-point values and the linearly
     * estimated values that marks the limit of the linearity test. We put in
     * a discontinuity somewhere within the current segment.
     * The actual value was determined empirically by testing for differences
     * in output vs. performance.
     */
    if (fn->usage == FN_SHADING)
      t0->discont.linearity = 3.0;

    samples = t0->sampletablesize/fn->out_dim;

    chan_linear = mm_alloc( mm_pool_temp, fn->out_dim * sizeof(Bool),
                            MM_ALLOC_CLASS_FUNCTIONS ) ;
    if (chan_linear == NULL)
      return error_handler(VMERROR);

    /* quick check for overall flatness */
    t0->discont.all_linear = TRUE;
    for (chan = 0; chan < fn->out_dim; chan++) {
      chan_linear[chan] = TRUE;
      val = t0->sampletable[chan];
      for (tick = 1 ; tick < samples;tick++) {
        if (val != t0->sampletable[(fn->out_dim * tick) + chan]) {
          chan_linear[chan] = FALSE;
          t0->discont.all_linear = FALSE;
          break;
        }
      }
    }

    if (!t0->discont.all_linear) {
      /* check we have enough workspace */
      if (t0->discont.segs && (t0->discont.segs_max != samples)) {
        mm_free( mm_pool_temp, t0->discont.segs, t0->discont.segs_memsize );
        t0->discont.segs = NULL ;
      }

      /* reserve space for the maximum possible number of segments */
      if (t0->discont.segs == NULL) {
        t0->discont.segs_max = samples;
        t0->discont.segs_memsize = sizeof(uint32) * samples;
        t0->discont.segs = mm_alloc(mm_pool_temp, t0->discont.segs_memsize,
                                    MM_ALLOC_CLASS_FUNCTIONS ) ;
        if (t0->discont.segs == NULL) {
          mm_free( mm_pool_temp, chan_linear, fn->out_dim * sizeof(Bool) );
          return error_handler(VMERROR);
        }

        /* mark as empty */
        t0->discont.numsegs = 0;
      }

      /* reserve some local workspace (2 lumps) big enough to fit each segment
         marked with both start and end points */
      segs = mm_alloc(mm_pool_temp,
                      t0->discont.segs_memsize * 2,
                      MM_ALLOC_CLASS_FUNCTIONS);
      if (segs == NULL) {
        mm_free( mm_pool_temp, chan_linear, fn->out_dim * sizeof(Bool) );
        return error_handler(VMERROR);
      }

      merged = mm_alloc(mm_pool_temp, t0->discont.segs_memsize * 2,
                        MM_ALLOC_CLASS_FUNCTIONS);
      if (merged == NULL) {
        mm_free( mm_pool_temp, chan_linear, fn->out_dim * sizeof(Bool) );
        mm_free( mm_pool_temp, segs, t0->discont.segs_memsize * 2 );
        return error_handler(VMERROR);
      }

      for (chan = 0; (chan < fn->out_dim) && (!t0->discont.all_linear); chan++) {

        if (chan_linear[chan])
          continue;

        t_rle = segs;

        /* call line generator to get an array of start and end points (not in order) */
        if (colinear(&t_rle, fn->out_dim, chan,
                     t0->sampletable,
                     0, samples - 1,
                     0, samples - 1,
                     t0->discont.linearity))  {
          /* entire function is linear */
          chan_linear[chan] = TRUE;  /* then quick skip */
          continue;
        }

        /* attempt to merge line segments down to a smaller set */
        merged_end = merge_segs_samples(segs, t_rle, fn->out_dim,
                                        chan, t0->sampletable,
                                        t0->discont.linearity, merged);
        if (merged_end == NULL) {
          /* Something went very wrong in the merge. Belt and braces bailout. */
          t0->discont.segment = FALSE;
          break;
        }

        merge_count = CAST_PTRDIFFT_TO_UINT32(merged_end - merged);

        /* use segs array as work space and merge with other channel segments */
        merge_count = merge_segs_chan(merged, merge_count, segs,
                                      t0->discont.segs, &t0->discont.numsegs);
      }

      /* finally, some channels may have since been found to be totally linear
         so check again so see if all are linear (and thus without discontinuities) */
      if (!t0->discont.all_linear) {
        t0->discont.all_linear = TRUE;
        for (chan = 0; chan < fn->out_dim; chan++) {
          if (!chan_linear[chan]) {
            t0->discont.all_linear = FALSE;
            break;
          }
        }
      }

      /* dispose of workspaces */
      mm_free( mm_pool_temp, merged, t0->discont.segs_memsize * 2 );
      mm_free( mm_pool_temp, segs, t0->discont.segs_memsize * 2 );
    }

    mm_free( mm_pool_temp, chan_linear, fn->out_dim * sizeof(Bool) );
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_default_encode( FUNCTIONCACHE *fn )
{
  FNTYPE0 *t0 ;
  int32 arraylength ;

  /* Create the array [ 0 (size0 - 1) 0 (size2 - 1) ... ] */

  HQASSERT( fn , "fn is null in fntype0_default_encode" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_default_encode" ) ;

  arraylength = fn->in_dim * 2 ;

  if ( t0->free_encode &&
       t0->encode != NULL &&
       theILen( t0->encode ) != arraylength ) {
    /* Free the object list and the array object itself. */
    mm_free( mm_pool_temp ,
             t0->encode ,
             ( theILen( t0->encode ) + 1 ) * sizeof( OBJECT )) ;
    t0->encode = NULL ;
    t0->free_encode = FALSE ;
  }

  if ( fn->in_dim <= 4 ) {
    /* Can use the in-lined arrays. */
    SYSTEMVALUE *ptr ;
    SYSTEMVALUE *end ;
    SYSTEMVALUE *sizeptr ;

    ptr = t0->s_encode ;
    end = ptr + fn->in_dim * 2 ;
    sizeptr = t0->s_size ;

    while ( ptr < end ) {
      *ptr++ = 0.0 ;
      *ptr++ = *sizeptr++ - 1.0 ;
    }
  }
  else {
    /* Must allocate an array explicitly. */
    OBJECT *arrayobj ;
    OBJECT *array ;
    OBJECT *ptr ;
    OBJECT *end ;
    OBJECT *sizeptr ;
    OBJECT zero = OBJECT_NOTVM_INTEGER(0) ;

    HQASSERT( t0->size , "size is null in fntype0_default_encode." ) ;
    HQASSERT(oType(*t0->size) == OARRAY ||
             oType(*t0->size) == OPACKEDARRAY ,
              "size is not an array in fntype0_default_encode. " ) ;
    HQASSERT( theILen( t0->size ) == fn->in_dim ,
              "input dimension not consistent with length of size in "
              "fntype0_default_encode" ) ;

    if ( t0->encode == NULL || ! t0->free_encode ) {
      array = mm_alloc( mm_pool_temp ,
                        ( arraylength + 1 ) * sizeof( OBJECT ) ,
                        MM_ALLOC_CLASS_FUNCTIONS ) ;
      if ( array == NULL ) {
        t0->free_encode = FALSE ;
        return error_handler( VMERROR ) ;
      }
      arrayobj = array ;
      ++array ;
      t0->free_encode = TRUE ;
    }
    else {
      /* Can re-use old encode array. */
      HQASSERT( theILen( t0->encode ) == arraylength ,
                "Existing arraylength differs from required length" ) ;
      arrayobj = t0->encode ;
      array = oArray(*arrayobj) ;
    }

    ptr = array ;
    end = ptr + arraylength ;
    sizeptr = oArray(*t0->size);

    while ( ptr < end ) {
      *ptr++ = zero ; /* Struct copy to set slot properties */
      object_store_integer(object_slot_notvm(ptr), oInteger(*sizeptr) - 1) ;
      ++ptr ; ++sizeptr ;
    }

    (void)object_slot_notvm(arrayobj) ;
    theTags(*arrayobj)= OARRAY ;
    theILen( arrayobj ) = ( uint16 ) arraylength ;
    oArray(*arrayobj) = array ;

    t0->encode = arrayobj ;
  }
  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_encode_input( SYSTEMVALUE *input, SYSTEMVALUE *encoded,
                                  FUNCTIONCACHE *fn )
{
  FNTYPE0 *t0 ;
  SYSTEMVALUE tmp , lb , ub , elb , eub ;
  int32 in_dim ;
  int32 i ;

  HQASSERT( input , "input is null in fntype0_encode_input" ) ;
  HQASSERT( encoded , "encoded is null in fntype0_encode_input" ) ;
  HQASSERT( fn , "fn is null in fntype0_encode_input" ) ;

  in_dim = fn->in_dim ;
  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_encode_input" ) ;

  if ( in_dim <= 4 ) {
    SYSTEMVALUE *domain ;
    SYSTEMVALUE *encode ;
    SYSTEMVALUE *size ;

    domain = fn->s_domain ;
    encode = t0->s_encode ;
    size = t0->s_size ;

    /* For each input value. */
    for ( i = 0 ; i < in_dim ; ++i ) {

      tmp = *input++ ;

      /* Get the lower and upper domain bounds. */
      lb = *domain++ ;
      ub = *domain++ ;
      if ( lb == ub )
        return error_handler( UNDEFINEDRESULT ) ;

      /* Clip input values to the domain of the function. */
      fn_cliptointerval( tmp , lb , ub ) ;

      /* Get the lower and upper Encode bounds. */
      elb = *encode++ ;
      eub = *encode++ ;
      /* Encode the clipped input value. */
      tmp = ((tmp - lb) / (ub - lb)) * (eub - elb)  + elb ;

      /* Clip encoded inputs to interval [ 0 Sizei-1 ]. */
      lb = 0.0 ;
      ub = *size++ - 1.0 ;
      fn_cliptointerval( tmp , lb , ub ) ;

      *encoded++ = tmp ;
    }
  }
  else {
    OBJECT *domain ;
    OBJECT *encode ;
    OBJECT *size ;

    HQASSERT( fn->domain , "fn->domain is null in fntype0_encode_input." ) ;
    HQASSERT(oType(*fn->domain) == OARRAY ||
             oType(*fn->domain) == OPACKEDARRAY ,
              "fn->domain is not an array in fntype0_encode_input." ) ;
    HQASSERT( t0->encode , "fn->domain is null in fntype0_encode_input." ) ;
    HQASSERT(oType(*t0->encode) == OARRAY ||
             oType(*t0->encode) == OPACKEDARRAY ,
              "t0->encode is not an array in fntype0_encode_input." ) ;
    HQASSERT( t0->size , "t0->size is null in fntype0_encode_input." ) ;
    HQASSERT(oType(*t0->size) == OARRAY ||
             oType(*t0->size) == OPACKEDARRAY ,
              "t0->size is not an array in fntype0_encode_input." ) ;
    HQASSERT( theILen( fn->domain ) == theILen( t0->size ) * 2,
              "domain and size length don't match in fntype0_encode_input." ) ;
    HQASSERT( theILen( fn->domain ) == fn->in_dim * 2 ,
              "domain and in_dim don't match in fntype0_encode_input." ) ;

    domain = oArray(*fn->domain) ;
    encode = oArray(*t0->encode) ;
    size = oArray(*t0->size) ;

    /* For each input value. */
    for ( i = 0 ; i < fn->in_dim ; ++i ) {

      tmp = *input++ ;

      /* Get the lower and upper domain bounds. */
      if ( !object_get_numeric(domain++, &lb) ||
           !object_get_numeric(domain++, &ub) )
        return FALSE ;
      if ( lb == ub )
        return error_handler( UNDEFINEDRESULT ) ;

      /* Clip input values to the domain of the function. */
      fn_cliptointerval( tmp , lb , ub ) ;

      /* Get the lower and upper Encode bounds. */
      if ( !object_get_numeric(encode++, &elb) ||
           !object_get_numeric(encode++, &eub) )
        return FALSE ;
      /* Encode the clipped input value. */
      tmp = ((tmp - lb) / (ub - lb)) * (eub - elb)  + elb ;

      /* Clip encoded inputs to interval [ 0 Sizei-1 ]. */
      lb = 0.0 ;
      if ( !object_get_numeric(size++, &ub) )
        return FALSE ;
      --ub ;
      fn_cliptointerval( tmp , lb , ub ) ;

      *encoded++ = tmp ;
    }
  }
  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_decode_output( SYSTEMVALUE *sample, SYSTEMVALUE *decoded,
                                   FUNCTIONCACHE *fn )
{
  FNTYPE0 *t0 ;
  SYSTEMVALUE tmp , lb , ub , two_to_bps ;
  int32 out_dim ;
  int32 j ;

  HQASSERT( sample , "sample is null in fntype0_decode_output" ) ;
  HQASSERT( decoded , "decoded is null in fntype0_decode_output" ) ;
  HQASSERT( fn , "fn is null in fntype0_decode_output" ) ;
  HQASSERT( fn->out_dim <= 4 ,
            "Only support functions with an output dimension <= 4 in "
            "fntype0_decode_output" ) ;
  out_dim = fn->out_dim ;
  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_decode_output" ) ;

  two_to_bps = ( SYSTEMVALUE )( 0xFFFFFFFF >>
                                ( 32 - t0->bitspersample )) ;
  HQASSERT( two_to_bps == ( pow( 2 , t0->bitspersample ) - 1.0 ) ,
            "pow( 2 , bps ) optimisation failed in fntype0_decode_output" ) ;

  if ( out_dim <= 4 ) {
    SYSTEMVALUE *decode ;
    SYSTEMVALUE *range ;

    decode = t0->s_decode ;
    range = fn->s_range ;

    /* For each sample component. */
    for ( j = 0 ; j < out_dim ; ++j ) {

      tmp = *sample++ ;

      /* Get the lower and upper Decode bounds. */
      lb = *decode++ ;
      ub = *decode++ ;

      /* Decode the output value. */
      tmp =  (tmp / two_to_bps) * (ub - lb) + lb ;

      /* Get the lower and upper Range bounds. */
      lb = *range++ ;
      ub = *range++ ;
      /* Clip decoded output to Range interval. */
      fn_cliptointerval( tmp , lb , ub ) ;

      *decoded++ = tmp ;
    }
  }
  else {
    OBJECT *decode ;
    OBJECT *range ;

    HQASSERT( t0->decode , "t0->decode is null in fntype0_decode_output." ) ;
    HQASSERT(oType(*t0->decode) == OARRAY ||
             oType(*t0->decode) == OPACKEDARRAY ,
              "t0->decode is not an array in fntype0_decode_output." ) ;
    HQASSERT( fn->range , "fn->range is null in fntype0_decode_output." ) ;
    HQASSERT(oType(*fn->range) == OARRAY ||
             oType(*fn->range) == OPACKEDARRAY ,
              "fn->range is not an array in fntype0_decode_output." ) ;

    decode = oArray(*t0->decode) ;
    range = oArray(*fn->range) ;

    /* For each sample component. */
    for ( j = 0 ; j < out_dim ; ++j ) {

      tmp = *sample++ ;

      /* Get the lower and upper Decode bounds. */
      if ( !object_get_numeric(decode++, &lb) ||
           !object_get_numeric(decode++, &ub) )
        return FALSE ;

      /* Decode the output value. */
      tmp =  (tmp / two_to_bps) * (ub - lb) + lb ;

      /* Get the lower and upper Range bounds. */
      if ( !object_get_numeric(range++, &lb) ||
           !object_get_numeric(range++, &ub) )
        return FALSE ;
      /* Clip decoded output to Range interval. */
      fn_cliptointerval( tmp , lb , ub ) ;

      *decoded++ = tmp ;
    }
  }
  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static void fntype0_freecache( FUNCTIONCACHE *fn )
{
  FNTYPE0 *t0 ;

  HQASSERT( fn , "fn is null in fntype0_freecache" ) ;
  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_freecache" ) ;
  if ( t0->encode != NULL && t0->free_encode ) {
    /* Free the object list and the array object itself. */
    mm_free( mm_pool_temp ,
             t0->encode ,
             ( theILen( t0->encode ) + 1 ) * sizeof( OBJECT )) ;
    t0->encode = NULL ;
  }
  if ( t0->workspace != NULL ) {
    HQASSERT( t0->workspacesize > 0 , "fntype0_freecache: workspacesize <= 0" ) ;
    mm_free( mm_pool_temp , t0->workspace , t0->workspacesize ) ;
    t0->workspace = NULL ;
    t0->workspacesize = 0 ;
    t0->workspaceInitialised = FALSE ;
  }
  if ( t0->sampletable != NULL ) {
    mm_free( mm_pool_temp, t0->sampletable,

      sizeof( uint32 ) * t0->sampletablesize );
    t0->sampletable = NULL ;
  }

  if (t0->discont.segs != NULL) {
    mm_free( mm_pool_temp, t0->discont.segs, t0->discont.segs_memsize );
    t0->discont.segs = NULL;
  }

  mm_free( mm_pool_temp, fn->specific, sizeof( FNTYPE0 )) ;
  fn->specific = NULL ;
  fn_invalidate_entry( fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_evaluate_1_to_1( FUNCTIONCACHE *fn , Bool upwards,
                                     SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE0 *t0 ;
  uint32 integral_part ;
  SYSTEMVALUE fractional_part , delta ;
  uint32 *tptr ;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype0_evaluate_1_to_1" ) ;
  HQASSERT( fn->in_dim == 1 , "fntype0_evaluate_1_to_1: in_dim must be 1" ) ;
  HQASSERT( fn->out_dim == 1 , "fntype0_evaluate_1_to_1: out_dim must be 1" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_evaluate_1_to_1" ) ;

  if ( ! fntype0_encode_input( input , input , fn ))
    return FALSE ;

  /* Cast to uint32, equivalent to floor. */
  integral_part = ( uint32 ) input[ 0 ] ;
  HQASSERT( integral_part < (uint32)(t0->s_size[ 0 ]) ,
            "integral_part >= s_size[ 0 ] in fntype0_evaluate_1_to_1" ) ;
  fractional_part = input[ 0 ] - ( SYSTEMVALUE ) integral_part ;

  tptr = t0->sampletable + integral_part ;
  *output = ( SYSTEMVALUE ) *tptr ;

  if ( integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
       fractional_part != 0.0 ) {
    delta = ( SYSTEMVALUE ) tptr[ 1 ] - *output ;
    *output += ( delta * fractional_part ) ;
  }

  return fntype0_decode_output( output , output , fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_evaluate_1_to_N( FUNCTIONCACHE *fn , Bool upwards ,
                                     SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE0 *t0 ;
  int32 out_dim ;
  uint32 integral_part ;
  SYSTEMVALUE fractional_part ;
  SYSTEMVALUE delta ;
  uint32 *tptr , index ;
  int32 i ;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype0_evaluate_1_to_N" ) ;
  HQASSERT( fn->in_dim == 1 , "fntype0_evaluate_1_to_N: in_dim must be 1" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_evaluate_1_to_N" ) ;

  out_dim = fn->out_dim ;

  if ( ! fntype0_encode_input( input , input , fn ))
    return FALSE ;

  /* Cast to uint32, equivalent to floor. */
  integral_part = ( uint32 ) input[ 0 ] ;
  HQASSERT( integral_part < (uint32)(t0->s_size[ 0 ]) ,
            "integral_part >= t0->s_size[ 0 ] in fntype0_evaluate_1_to_N" ) ;

  fractional_part = input[ 0 ] - ( SYSTEMVALUE ) integral_part ;

  index = integral_part * ( uint32 ) out_dim ;

  tptr = t0->sampletable + index ;
  for ( i = 0 ; i < out_dim ; ++i )
    output[ i ] = ( SYSTEMVALUE ) tptr[ i ] ;

  if ( integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
       fractional_part != 0.0 )
    for ( i = 0 ; i < out_dim ; ++i ) {
      delta = ( SYSTEMVALUE ) tptr[ i + out_dim ] - output[ i ] ;
      /* Interpolate tablevalues. */
      output[ i ] += delta * fractional_part ;
    }

  return fntype0_decode_output( output , output , fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_evaluate_2_to_1( FUNCTIONCACHE *fn , Bool upwards ,
                                     SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE0 *t0 ;
  uint32 x_integral_part , y_integral_part ;
  SYSTEMVALUE x_fractional_part , y_fractional_part ;
  SYSTEMVALUE delta ;
  uint32 index ;
  uint32 *tptr ;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype0_evaluate_2_to_1" ) ;
  HQASSERT( fn->in_dim == 2 , "fntype0_evaluate_2_to_1: in_dim must be 2" ) ;
  HQASSERT( fn->out_dim == 1 , "fntype0_evaluate_2_to_1: out_dim must be 1" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_evaluate_2_to_1" ) ;

  if ( ! fntype0_encode_input( input , input , fn ))
    return FALSE ;

  /* Cast to uint32, equivalent to floor. */
  x_integral_part = ( uint32 ) input[ 0 ] ;
  HQASSERT( x_integral_part < (uint32)(t0->s_size[ 0 ]) ,
            "x_integral_part >= t0->s_size[ 0 ] in fntype0_evaluate_2_to_1" ) ;
  x_fractional_part = input[ 0 ] - ( SYSTEMVALUE ) x_integral_part ;
  /* Cast to uint32, equivalent to floor. */
  y_integral_part = ( uint32 ) input[ 1 ] ;
  HQASSERT( y_integral_part < (uint32)(t0->s_size[ 1 ]) ,
            "y_integral_part >= t0->s_size[ 1 ] in fntype0_evaluate_2_to_1" ) ;
  y_fractional_part = input[ 1 ] - ( SYSTEMVALUE ) y_integral_part ;

  index = y_integral_part * ( uint32 )(t0->s_size)[ 0 ] + x_integral_part ;

  tptr = t0->sampletable + index ;
  *output = ( SYSTEMVALUE ) tptr[ 0 ] ;

  if ( x_integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
       x_fractional_part != 0.0 ) {
    delta = ( SYSTEMVALUE ) tptr[ 1 ] - *output ;
    *output += delta * x_fractional_part ;
  }

  if ( y_integral_part + 1 < ( uint32 )(t0->s_size)[ 1 ] &&
       y_fractional_part != 0.0 ) {
    SYSTEMVALUE sample2 ;
    tptr += ( uint32 )(t0->s_size)[ 0 ] ;
    sample2 = ( SYSTEMVALUE ) tptr[ 0 ] ;
    if ( x_integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
         x_fractional_part != 0.0 ) {
      delta = ( SYSTEMVALUE ) tptr[ 1 ] - sample2 ;
      sample2 += delta * x_fractional_part ;
    }
    *output += ( sample2 - *output ) * y_fractional_part ;
  }

  return fntype0_decode_output( output , output , fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_evaluate_2_to_N( FUNCTIONCACHE *fn , Bool upwards ,
                                     SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE0 *t0 ;
  int32 out_dim ;
  uint32 x_integral_part , y_integral_part ;
  SYSTEMVALUE x_fractional_part , y_fractional_part ;
  SYSTEMVALUE delta ;
  uint32 index ;
  uint32 *tptr ;
  int32 i ;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype0_evaluate_2_to_N" ) ;
  HQASSERT( fn->in_dim == 2 , "fntype0_evaluate_2_to_N: in_dim must be 2" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_evaluate_2_to_N" ) ;

  out_dim = fn->out_dim ;

  if ( ! fntype0_encode_input( input , input , fn ))
    return FALSE ;

  /* Cast to uint32, equivalent to floor. */
  x_integral_part = ( uint32 ) input[ 0 ] ;
  HQASSERT( x_integral_part < (uint32)(t0->s_size[ 0 ]) ,
            "x_integral_part >= t0->s_size[ 0 ] in fntype0_evaluate_2_to_N" ) ;

  x_fractional_part = input[ 0 ] - ( SYSTEMVALUE ) x_integral_part ;
  /* Cast to uint32, equivalent to floor. */
  y_integral_part = ( uint32 ) input[ 1 ] ;
  HQASSERT( y_integral_part < (uint32)(t0->s_size[ 1 ]) ,
            "y_integral_part >= t0->s_size[ 1 ] in fntype0_evaluate_2_to_N" ) ;

  y_fractional_part = input[ 1 ] - ( SYSTEMVALUE ) y_integral_part ;

  index = y_integral_part * ( uint32 )(t0->s_size)[ 0 ] + x_integral_part ;
  index *= out_dim ;

  tptr = t0->sampletable + index ;
  for ( i = 0 ; i < out_dim ; ++i )
    output[ i ] = ( SYSTEMVALUE ) tptr[ i ] ;

  if ( x_integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
       x_fractional_part != 0.0 )
    for ( i = 0 ; i < out_dim ; ++i ) {
      delta = ( SYSTEMVALUE ) tptr[ i + out_dim ] - output[ i ] ;
      output[ i ] += delta * x_fractional_part ;
    }

  if ( y_integral_part + 1 < ( uint32 )(t0->s_size)[ 1 ] &&
       y_fractional_part != 0.0 ) {
    SYSTEMVALUE sample2 ;
    tptr += ( uint32 )(t0->s_size)[ 0 ] * out_dim ;
    for ( i = 0 ; i < out_dim ; ++i ) {
      sample2 = ( SYSTEMVALUE ) tptr[ i ] ;
      if ( x_integral_part + 1 < ( uint32 )(t0->s_size)[ 0 ] &&
           x_fractional_part != 0.0 ) {
        delta = ( SYSTEMVALUE ) tptr[ i + out_dim ] - sample2 ;
        sample2 += delta * x_fractional_part ;
      }
        output[ i ] += ( sample2 - output[ i ] ) * y_fractional_part ;
    }
  }

  return fntype0_decode_output( output , output , fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_evaluate_M_to_N( FUNCTIONCACHE *fn , Bool upwards ,
                                     SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE0 *t0 ;
  uint32 in_dim, out_dim ;
  uint32 base_offset , index_stride ;
  uint32 id, od, k ;
  uint32 nNonZero ;
  uint32 nonZeroDim ;
  uint32 *n_integral_part, *n_index_stride ;
  uint32 *skip_dim ;
  SYSTEMVALUE *n_fractional_part, *omn_fractional_part ;
  SYSTEMVALUE *n_result ;
  uint32 *relative_pos ;
  OBJECT *size ;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype0_evaluate_M_to_N" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_evaluate_M_to_N" ) ;

  in_dim = (uint32) fn->in_dim ;
  out_dim = (uint32) fn->out_dim ;

  HQASSERT( in_dim < 32 , "fntype0_evaluate_M_to_N: Too many input dimensions" ) ;

  if ( ! fntype0_encode_input( input , input , fn ))
    return FALSE ;

  /* SYSTEMVALUEs first, for alignment considerations */
  n_result = t0->workspace ;
  n_fractional_part = n_result + ((size_t)1 << in_dim) ;
  omn_fractional_part = n_fractional_part + in_dim ;
  n_integral_part = (uint32 *)(omn_fractional_part + in_dim) ;
  n_index_stride = n_integral_part + in_dim ;
  skip_dim = n_index_stride + in_dim ;
  relative_pos = skip_dim + in_dim ;

  HQASSERT( t0->size, "t0->size is null in fntype0_evaluate_M_to_N" );

  size = oArray(*t0->size) ;

  /* Precalcuate those things that can be */
  if (!t0->workspaceInitialised) {
    uint32 index_stride = out_dim ;

    t0->workspaceInitialised = TRUE;

    for ( id = 0 ; id < in_dim ; ++id ) {
      uint32 s = (uint32)oInteger( size[id] );
      n_index_stride[id] = index_stride ; /* Save for dimension increments */
      index_stride *= s ; /* Adjust stride for this dimension */
    }

    /* relative_pos is the relative position of a grid point within the current
     * mini-cube compared with the base corner. This can be used as an
     * optimisation to find the table values for a mini-cube when the origin
     * of the mini-cube isn't at the edge of any of it's dimensions - for those
     * that are we don't interpolate in that dimension.
     */
    for ( k = 0 ; k < (1u << in_dim) ; ++k ) {  /* 2^n points in hypercube */
      relative_pos[k] = 0;

      for ( id = 0 ; id < in_dim ; ++id )
        if ( (k & (1 << id)) != 0 ) /* Adjust address for hypercube dimension */
          relative_pos[k] += n_index_stride[id] ;
    }
  }

  /* Find the origin of the mini-cube and the fractional parts in n-dimensional
   * space */
  nNonZero = in_dim;
  nonZeroDim = 0;
  base_offset = 0 ;
  for ( id = 0 ; id < in_dim ; ++id ) {
    /* Cast to uint32, equivalent to floor. */
    n_integral_part[id] = ( uint32 ) input[id] ;
    HQASSERT( n_integral_part[id] < (uint32)oInteger(size[id]) ,
              "n_integral_part[id] >= t0->size[id] in fntype0_evaluate_function" ) ;

    n_fractional_part[id] = input[id] - ( SYSTEMVALUE )n_integral_part[id] ;

    /* We can't go past the end value so the fractional part should be 0.0 if
     * we're at the end of the cube in this dimension */
    HQASSERT( n_integral_part[id] < (uint32) oInteger(size[id]) - 1 ||
              (n_integral_part[id] == (uint32) oInteger(size[id]) - 1 &&
               n_fractional_part[id] == 0.0),
              "Fraction > 0.0 for end point in cube");

    omn_fractional_part[id] = 1.0 - n_fractional_part[id] ;
    skip_dim[id] = FALSE;

    /* Compress the fractions for the interpolation stage such that all non-zero
     * fractions should be at the start of the array. In the next stage we will
     * only interpolate the non-zero dimensions for a big reduction in processing.
     * NB. Only the fractional parts need to be compressed
     */
    if ( n_fractional_part[id] == 0.0 ) {
      nNonZero--;
      skip_dim[id] = TRUE;
    }
    else {
      n_fractional_part[nonZeroDim] = n_fractional_part[id];
      omn_fractional_part[nonZeroDim] = omn_fractional_part[id];

      nonZeroDim++;
    }

    /* set partial offset into value table for this dimension */
    base_offset += n_index_stride[id] * n_integral_part[id] ;
  }


  /* We've now identified a mini-cube in hypercube space around the sample
   * value. Use linear interpolation in each input component to derive the
   * output value.
   */
  for ( od = 0 ; od < out_dim ; ++od ) {
    uint32 *tptr = t0->sampletable + base_offset + od ; /* Base address of hypercube */

    /* Assign the values for each of the corners of the mini-cube. There are two
     * methods depending on whether we can avoid interpolating in any dimension */
    if (nNonZero == in_dim) {
      /* Full interpolation. Use the pre-calculated offsets for each corner */
      for ( k = 0 ; k < (1u << in_dim) ; ++k ) { /* 2^n points in hypercube */
        HQASSERT(base_offset + od + relative_pos[k] < t0->sampletablesize,
                 "Off the end of the sample table");
        n_result[k] = tptr[relative_pos[k]] ; /* Hypercube corner color component value */
      }
    }
    else {
      /* This stage is optimised by skipping iterations where the fractional part
       * is zero since the result in n_result[k] (for the subrange of k that is
       * passed on to the next iteration) is unchanged from doing the full
       * calculation. For larger values of in_dims, '1 << in_dims' gets prohibitively
       * large, so skipping iterations where possible is a good thing.
       * The fractional parts have previously been compressed into the lower slots.
       */
      for ( k = 0 ; k < (1u << nNonZero) ; ++k ) { /* 2^n points in hypercube */
        uint32 nonZeroDim = 0;
        uint32 relPos = 0;

        for ( id = 0 ; id < in_dim ; ++id ) {
          if (!skip_dim[id]) {
            if ((k & (1u << nonZeroDim)) != 0)
              relPos += n_index_stride[id];

            nonZeroDim++;
          }
        }

        HQASSERT(base_offset + od + relPos < t0->sampletablesize,
                 "Off the end of the sample table");
        n_result[k] = tptr[relPos] ; /* Hypercube corner color component value */
      }
    }

    /* Interpolate values from the first dimension to the final value. */
    for ( id = nNonZero ; id-- > 0 ; ) {
      index_stride = 1u << id ;

      for ( k = 0 ; k < index_stride ; ++k ) {
        n_result[k] = omn_fractional_part[id] * n_result[k] +
                      n_fractional_part[id] * n_result[k + index_stride] ;
      }
    }

    /* Final result left in n_result[0] */
    output[od] = n_result[0] ;
  }

  return fntype0_decode_output( output , output , fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype0_find_discontinuity( FUNCTIONCACHE *fn , int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order )
{
  FNTYPE0 *t0 ;
  int32 in_dim, bounds_order ;
  SYSTEMVALUE domain[2] , encode[2], encoded_bounds[2] , size_bounds[2] , stmp ;
  SYSTEMVALUE trans, scale ;
  int32 size, i ;

  if ( bounds[0] > bounds[1] ) { /* Type 3 can encode in reverse order */
    SYSTEMVALUE rbounds[2] ;

    rbounds[0] = bounds[1] ;
    rbounds[1] = bounds[0] ;

    return fntype0_find_discontinuity(fn, index, rbounds, discontinuity, order) ;
  }

  HQASSERT( fn , "fn is null in fntype0_find_discontinuity" ) ;

  t0 = ( FNTYPE0 * )fn->specific ;
  HQASSERT( t0 , "t0 is null in fntype0_find_discontinuity" ) ;

  HQASSERT(t0->discont.segment || fn->in_dim != 1,
           "Finding a discontinuity without a linearity test");

  if (t0->discont.all_linear)
    return TRUE;

  in_dim = fn->in_dim ;

  HQASSERT( index < in_dim, "index >= in_dim in fntype0_find_discontinuity" ) ;

  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8, 2 * in_dim,
                                  2 * index, &domain[0] ) )
    return FALSE ;
  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8, 2 * in_dim,
                                  2 * index +1, &domain[1] ) )
    return FALSE ;

  if ( domain[0] >= domain[1] ) {
    return error_handler( UNDEFINED ) ; /** \todo FIXME */
  }

  if ( fn_check_bounds( bounds , domain , discontinuity , order ) )
    return TRUE ;
  /* The interval is within the domain. */
  if ( !fn_extract_from_alternate( t0->encode, t0->s_encode, 8, 2 * in_dim,
                                  2 * index, &encode[0] ) )
    return FALSE ;
  if ( !fn_extract_from_alternate( t0->encode, t0->s_encode, 8, 2 * in_dim,
                                  2 * index +1, &encode[1] ) )
    return FALSE ;

  if ( encode[1] == encode[0] ) {
    /* The entire domain maps onto one value, so there are no discontinuities */
    return TRUE ;
  }
  scale = (encode[1] - encode[0]) / (domain[1] - domain[0]) ;
  trans = encode[0] - scale * domain[0] ;
  for ( i = 0 ; i < 2 ; i ++ ) {
    encoded_bounds[i] = bounds[i] * scale + trans ;
  }

  if ( !fn_extract_from_alternate( t0->size, t0->s_size, 4, in_dim,
                                  index , &stmp ) )
    return FALSE ;
  size = (int32) stmp ;
  size_bounds[0] = 0 ;
  size_bounds[1] = size -1;
  for ( ;; ) {
    bounds_order = -1 ;
    if ( fn_check_bounds( encoded_bounds, size_bounds, &stmp, &bounds_order ) ) {
      if ( bounds_order >= 0 ) { /* decode stmp to get the true position */
        SYSTEMVALUE result = (stmp - trans) / scale ;
        int maps_to_same;

        /* If we will map back to the same value, then try again with reduced
           range */
        MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[0] );
        if ( maps_to_same ) {
          encoded_bounds[0] = stmp ;
          continue ;
        } else {
          MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[1] );
          if ( maps_to_same ) {
            encoded_bounds[1] = stmp ;
            continue ;
          }
        }

        *discontinuity = result ;
        *order = bounds_order ;
      }
      return TRUE ;
    }
    break ;
  }

  for ( ;; ) {
    SYSTEMVALUE result ;
    SYSTEMVALUE sample_bounds[2] ;
    int maps_to_same;

    sample_bounds[0] = floor( encoded_bounds[0] ) + 1 ;
    sample_bounds[1] = ceil( encoded_bounds[1] ) - 1 ;

    if ( sample_bounds[0] > sample_bounds[1] ) {
      /* We're between samples - there's no discontinuity */
      return TRUE ;
    }

    /* We've caught some samples.  Return the middle one. */
    stmp = floor((sample_bounds[0] + sample_bounds[1]) /2);

    /* We may have optimised this function so segments of sample that appear linear are
       merged into linear segments, thus reducing the number of detected discontinuities.
       Check to see if sample_bounds lies within a piecewise linear segement */
    if (t0->discont.segment) {
      uint32 *pairs;
      uint32 seg_count = t0->discont.numsegs;

      pairs = t0->discont.segs;

      while (seg_count--) {
        if ((pairs[0] <= sample_bounds[0]) && (pairs[1] >= sample_bounds[1]))
          return TRUE; /* no discontinuity between bounds found */

        if ((pairs[0] < sample_bounds[1]) && (pairs[0] > sample_bounds[0])) {
          stmp = pairs[0];
          break;
        }

        if ((pairs[1] < sample_bounds[1]) && (pairs[1] > sample_bounds[0])) {
          stmp = pairs[1];
          break;
        }
        pairs++;
      }
    }

    result = (stmp - trans) / scale ;

    /* If we will map back to the same value, then try again with reduced
       range */
    MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[0] );
    if ( maps_to_same ) {
      encoded_bounds[0] = stmp ;
      continue ;
    } else {
      MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[1] );
      if ( maps_to_same ) {
        encoded_bounds[1] = stmp ;
        continue ;
      }
    }

    *discontinuity = result ;
    *order = 1 ;
    break ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool load_sampletable( uint32 *sptr , uint32 len ,
                              int32 bitps , FILELIST *flptr )
{
  HQASSERT( sptr ,    "sptr is NULL in load_sampletable" ) ;
  HQASSERT( len > 0 , "Nothing to do (len <= 0) in load_sampletable" ) ;
  HQASSERT( flptr ,   "flptr is NULL in load_sampletable" ) ;

  switch ( bitps ) {
  case 1  :
  case 2  :
  case 4  :
  case 12 :
  case 16 :
  case 24 :
  case 32 :
    {
      uint32 buffer , mask ;
      int32 bits_to_read , byte , bits , numbits ;

      bits_to_read = ( int32 ) len * bitps ;
      bits = 0 ;
      buffer = 0 ;
      mask = 0xFFFFFFFF >> ( 32 - bitps ) ;

      while ( bits_to_read > 0 ) {

        /* Number of bits to read into the buffer. */
        numbits = ( bits_to_read < 32 ? bits_to_read : 32 ) - bits ;

        /* Pack bytes into the buffer. */
        while ( numbits > 0 ) {
          if (( byte = Getc( flptr )) == EOF )
            return error_handler( IOERROR ) ;
          buffer = ( buffer << 8 ) | byte ;
          numbits -= 8 ;
          bits += 8 ;
        }

        /* Now unpack the buffer as appropriate to bits per sample. */
        while ( bits >= bitps && bits_to_read > 0 ) {
          *sptr++ = ( buffer >> ( bits - bitps )) & mask ;
          bits_to_read -= bitps ;
          bits -= bitps ;
        }
      }
    }
    break ;
  case 8  :
    /* Special case 8 bitspersample - probably the most common. */
    while ( len > 0 ) {
      uint8 *ptr = 0 ;
      int32 bytes = 0 ;
      int32 temp ;

      if ( ! GetFileBuff( flptr, len , & ptr , & bytes ))
        return error_handler( IOERROR ) ;

      len -= bytes ;

      for ( temp = bytes >> 3 ; temp != 0 ; temp-- ) {
        PENTIUM_CACHE_LOAD( sptr + 7 ) ;
        sptr[ 0 ] = ( uint32 ) ptr[ 0 ] ;
        sptr[ 1 ] = ( uint32 ) ptr[ 1 ] ;
        sptr[ 2 ] = ( uint32 ) ptr[ 2 ] ;
        sptr[ 3 ] = ( uint32 ) ptr[ 3 ] ;
        sptr[ 4 ] = ( uint32 ) ptr[ 4 ] ;
        sptr[ 5 ] = ( uint32 ) ptr[ 5 ] ;
        sptr[ 6 ] = ( uint32 ) ptr[ 6 ] ;
        sptr[ 7 ] = ( uint32 ) ptr[ 7 ] ;
        sptr += 8 ; ptr += 8 ; bytes -= 8 ;
      }
      while ( bytes ) {
        *sptr++ = ( uint32 ) *ptr++ ;
        --bytes ;
      }
    }
    break ;
  default :
    HQFAIL( "Shouldn't get here!." ) ;
    return error_handler( UNREGISTERED ) ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool load_sampletable_str( uint32 *sptr , uint32 len ,
                                  int32 bitps , uint8 *string , int32 strlen )
{
  HQASSERT( sptr ,    "sptr is NULL in load_sampletable_str" ) ;
  HQASSERT( len > 0 , "Nothing to do (len <= 0) in load_sampletable_str" ) ;

  HQASSERT( string != NULL && strlen != 0 , "strptr is null or strlen is 0" ) ;

  switch ( bitps ) {
  case 1  :
  case 2  :
  case 4  :
  case 12 :
  case 16 :
  case 24 :
  case 32 :
    {
      uint32 buffer , mask ;
      int32 bits_to_read , bits , numbits ;

      bits_to_read = ( int32 ) len * bitps ;
      bits = 0 ;
      buffer = 0 ;
      mask = 0xFFFFFFFF >> ( 32 - bitps ) ;

      if ( bits_to_read >> 3 > strlen )
        /* The string in /DataSource is of the wrong length. */
        return error_handler( RANGECHECK ) ;

      while ( bits_to_read > 0 ) {

        /* Number of bits to read into the buffer. */
        numbits = ( bits_to_read < 32 ? bits_to_read : 32 ) - bits ;

        /* Pack bytes into the buffer. */
        while ( numbits > 0 ) {
          buffer = ( buffer << 8 ) | *string++ ;
          numbits -= 8 ;
          bits += 8 ;
        }

        /* Now unpack the buffer as appropriate to bits per sample. */
        while ( bits >= bitps && bits_to_read > 0 ) {
          *sptr++ = ( buffer >> ( bits - bitps )) & mask ;
          bits_to_read -= bitps ;
          bits -= bitps ;
        }
      }
    }
    break ;
  case 8  :
    {
      /* Special case 8 bitspersample - probably the most common. */
      uint32 *ptr = sptr ;
      int32 temp ;

      if ( len > ( uint32 ) strlen )
        /* The string in /DataSource is of the wrong length. */
        return error_handler( RANGECHECK ) ;

      for ( temp = len >> 3 ; temp != 0 ; temp-- ) {
        PENTIUM_CACHE_LOAD( ptr + 7 ) ;
        ptr[ 0 ] = ( uint32 ) string[ 0 ] ;
        ptr[ 1 ] = ( uint32 ) string[ 1 ] ;
        ptr[ 2 ] = ( uint32 ) string[ 2 ] ;
        ptr[ 3 ] = ( uint32 ) string[ 3 ] ;
        ptr[ 4 ] = ( uint32 ) string[ 4 ] ;
        ptr[ 5 ] = ( uint32 ) string[ 5 ] ;
        ptr[ 6 ] = ( uint32 ) string[ 6 ] ;
        ptr[ 7 ] = ( uint32 ) string[ 7 ] ;
        ptr += 8 ; string += 8 ; len -= 8 ;
      }
      while ( len ) {
        *ptr++ = ( uint32 ) *string++ ;
        --len ;
      }
    }
    break ;
  default :
    HQFAIL( "Shouldn't get here!." ) ;
    return error_handler( UNREGISTERED ) ;
  }
  return TRUE ;
}

/*
* Log stripped */
