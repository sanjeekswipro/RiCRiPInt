/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:icmini.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Modular colour chain processor for handling ICC profiles.
 */

#include "core.h"

#include "mmcompat.h"           /* mm stuff */
#include "objecth.h"            /* OBJECT */
#include "gs_colorpriv.h"       /* CLINK */
#include "swerrors.h"           /* TYPECHECK */

#include "icmini.h"

/**
 * \todo Scratch space for frontend and backend ICC invokes. This is a temporary
 * hack to workaround ICC_PROFILE_INFOs being shared between frontend and
 * backend chains. Only need one of each because the frontend is single-threaded
 * and the backend is locked around color chain invocations.
 */
static ICVALUE frontScratch[MAX_SCRATCH_SIZE];
static ICVALUE backScratch[MAX_SCRATCH_SIZE];

/*----------------------------------------------------------------------------*/

void* mi_alloc(size_t bytes)
{
  void *alloc = mm_alloc_with_header( mm_pool_color, bytes,
                                      MM_ALLOC_CLASS_ICC_PROFILE_CACHE );
  if (alloc == NULL)
    (void) error_handler(VMERROR);

  return alloc;
}

/* note that mi_free CAN safely be called with NULL or "invalid" pointers */
void mi_free(void* data)
{
  if (data==0 || data==INVALID_PTR)
    return;

  mm_free_with_header(mm_pool_color, (mm_addr_t) data);
}

/*----------------------------------------------------------------------------*/
/* conversion from internal values (0..1) to external XYZ values, optionally
 * converting from Lab to XYZ, and vice versa.
 */

/* convert internal Lab to external XYZ.
 * scale[] is normally [100/116,255/500,255/200] but these are multiplied by
 * 65535/65280 if old-style LAB.
 */
void mi_lab2xyz(void* indata, ICVALUE* colorant)
{
  ICVALUE X,Y,Z;
  MI_XYZ* data = indata;

  HQASSERT(data, "No mini-invoke data!");

  Y =     colorant[0] * data->scale[0] +    4/29.0f;
  X = Y + colorant[1] * data->scale[1] - 128/500.0f;
  Z = Y - colorant[2] * data->scale[2] + 128/200.0f;

  X = ( X >= 6/29.0f ) ? (ICVALUE) pow(X,3) : (X - 4/29.0f)*108/841.0f;
  Y = ( Y >= 6/29.0f ) ? (ICVALUE) pow(Y,3) : (Y - 4/29.0f)*108/841.0f;
  Z = ( Z >= 6/29.0f ) ? (ICVALUE) pow(Z,3) : (Z - 4/29.0f)*108/841.0f;

  colorant[0] = data->relative_whitepoint[CC_CIEXYZ_X] * X;
  colorant[1] = data->relative_whitepoint[CC_CIEXYZ_Y] * Y;
  colorant[2] = data->relative_whitepoint[CC_CIEXYZ_Z] * Z;
}


/* Convert external XYZ to internal Lab.
 * scale[] is normally [100/116,255/500,255/200] but these are multiplied by
 * 65535/65280 if old-style LAB.
 */
void mi_xyz2lab(void* indata, ICVALUE* colorant)
{
  ICVALUE r,s,t;
  MI_XYZ* data = indata;

  HQASSERT(data, "No mini-invoke data!");

  r = colorant[0] / data->relative_whitepoint[CC_CIEXYZ_X];
  s = colorant[1] / data->relative_whitepoint[CC_CIEXYZ_Y];
  t = colorant[2] / data->relative_whitepoint[CC_CIEXYZ_Z];

  r = ( r >= 216/24389.0f ) ? (ICVALUE) pow(r,1/3.0f) : (r*841/108.0f)+4/29.0f;
  s = ( s >= 216/24389.0f ) ? (ICVALUE) pow(s,1/3.0f) : (s*841/108.0f)+4/29.0f;
  t = ( t >= 216/24389.0f ) ? (ICVALUE) pow(t,1/3.0f) : (t*841/108.0f)+4/29.0f;

  t = (s - t + 128/200.0f) / data->scale[2];
  r = (r - s + 128/500.0f) / data->scale[1];
  s = (s - 4/29.0f) / data->scale[0];

  NARROW_01(t);
  NARROW_01(r);
  NARROW_01(s);

  colorant[0] = s;
  colorant[1] = r;
  colorant[2] = t;
}


/* scale[] has been precalculated to be either relativewhitepoint/whitepoint or
 * it's inverse.
 */
void mi_xyz2xyz(void* indata, ICVALUE* colorant)
{
  int i;
  MI_XYZ* data = indata;

  HQASSERT(data, "No mini-invoke data!");

  for ( i=0; i<3; i++ ) {
    colorant[i] *= data->scale[i];
  }
}

/*----------------------------------------------------------------------------*/
/* Piecewise linear interpolator.
 */
void mi_piecewise_linear(void* indata, ICVALUE* colorant)
{
  uint32  mask,i,index;
  ICVALUE a,b,factor;
  DATA_PIECEWISE_LINEAR* curve;
  MI_PIECEWISE_LINEAR* data = indata;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->channelmask, "Pointless invoke!");
  mask = data->channelmask;
  curve = &data->curves[0];

  for ( i=0; mask; i++, mask>>=1 ) {
    if ( (mask & 1) != 0 ) {
      NARROW_01(colorant[i]);

      factor = colorant[i] * curve->maxindex;
      index = (uint32) factor;
      factor -= index;
      a = curve->values[index];
      if ( factor != 0 ) {
        /* this isn't an optimisation, it's to avoid falling off the end */
        b = curve->values[index+1] - a;
        a += factor * b;
      }
      colorant[i] = a;

      curve = curve->next;
    }
  }
  return;
}

/* Inverse piecewise linear interpolator.
 * This does the reverse of the above, as used for TRCs in output profiles.
 */
void mi_inverse_linear(void* indata, ICVALUE* colorant)
{
  uint32  mask;
  int32   ichan;
  int32   ochan;
  int32   index, h, l;
  ICVALUE a,b;
  DATA_PIECEWISE_LINEAR* curve;
  MI_PIECEWISE_LINEAR* data = indata;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->channelmask, "Pointless invoke!");
  mask = data->channelmask;
  curve = &data->curves[0];

  for ( ichan=0, ochan=0; mask; ichan++, mask>>=1 ) {
    if ( (mask & 1) != 0 ) {
      NARROW_01(colorant[ichan]);

      /* Find segment in piecewise curve which spans the colorant value.
       * The curve has been forced monotonic by the constructor, so just
       * choose an ascending or descending binary search.
       *
       * Biasing the start comparison index from the usual half-way reduces
       * search time by about 5%.
       */
      l = 0;
      h = curve->maxindex-1;  /* both l and h are inclusive */
      if ( curve->values[curve->maxindex] >= curve->values[l] ) {

        /* ascending binary search */
        index = (int32)(colorant[ichan] * h); /* bias the start comparison */
        while ( l < h ) {
          if ( colorant[ichan] < curve->values[index] )
            h = index-1;
          else {
            if ( colorant[ichan] > curve->values[index+1] )
              l = index+1;
            else
              break;
          }
          index = (h+l+1)/2;
        }
        HQASSERT(( colorant[ichan] >= curve->values[index] || index == 0) &&
                 ( colorant[ichan] <= curve->values[index+1] ||
                   index+1 >= (int32)curve->maxindex),
                 "Inverse linear search failed");

      } else {

        /* descending binary search */
        index = (int32)((1-colorant[ichan]) * h); /* bias the start comparison */
        while ( l < h ) {
          if ( colorant[ichan] > curve->values[index] )
            h = index-1;
          else {
            if ( colorant[ichan] < curve->values[index+1] )
              l = index+1;
            else
              break;
          }
          index = (l+h+1)/2;
        }
        HQASSERT(( colorant[ichan] <= curve->values[index] || index == 0) &&
                 ( colorant[ichan] >= curve->values[index+1] ||
                   index+1 >= (int32)curve->maxindex),
                 "Inverse linear search failed");
      }

      /* the segment index to index+1 spans colorant[i] */
      a = colorant[ichan] - curve->values[index];
      b = curve->values[index+1] - curve->values[index];
      if ( b==0 ) { /* Cope with flat segments */
        a = 0;
        b = 1;
      }
      a = (index + a / b) / (ICVALUE)curve->maxindex;

      /* extrapolation is not impossible, so clip */
      NARROW_01(a);
      colorant[ochan] = a;

      ochan++;
      curve = curve->next;
    }
  }
  HQASSERT(ochan == ichan || ochan == 1,
           "Exected to output all or just 1 colorant");

  return;
}

/*----------------------------------------------------------------------------*/
/* Parametric 1D curve */
/* These are generalised to the form X=pow((ax+b),gamma)+e for x>=d, else X=cx*f
 */

void mi_parametric(void* indata, ICVALUE* colorant)
{
  uint32            mask,i;
  ICVALUE           a;
  DATA_PARAMETRIC*  param;
  MI_PARAMETRIC*    data = indata;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->channelmask, "Pointless invoke!");
  mask = data->channelmask;
  param = &data->curves[0];

  for ( i=0; mask; i++, mask>>=1 ) {
    if ( (mask & 1) != 0 ) {
      NARROW_01(colorant[i]);

      if ( (a = colorant[i]) >= param->d )
        a = (ICVALUE)pow((a * param->a + param->b), param->gamma) + param->e;
      else
        a = a * param->c + param->f;

      NARROW_01(a);
      colorant[i] = a;

      ++param;
    }
  }
  return;
}

/*----------------------------------------------------------------------------*/
/* the inverse parametric */

void mi_inverse_parametric(void* indata, ICVALUE* colorant)
{
  uint32  mask;
  int32   ichan;
  int32   ochan;
  ICVALUE                   col;
  DATA_INVERSE_PARAMETRIC*  param;
  MI_INVERSE_PARAMETRIC*    data = indata;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->channelmask, "Pointless invoke!");
  mask = data->channelmask;
  param = &data->curves[0];

  for ( ichan=0, ochan=0; mask; ichan++, mask>>=1 ) {
    if ( (mask & 1) != 0 ) {
      col = colorant[ichan];
      NARROW_01(col);

      if ( col >= param->crv_min && col < param->crv_max ) {
        /* The curved segment.
         * Note that p.gamma and p.a have already been inverted. */

        col = param->p.a * ( pow( col - param->p.e , param->p.gamma ) -
              param->p.b );

      } else if (col >= param->lin_min && col < param->lin_max ) {
        /* The linear segment.
         * Note that p.c has already been inverted. */

        col = param->p.c * (col - param->p.f);

      } else if (col >= param->gap_min && col < param->gap_max ) {
        /* The discontinuity between the two segments - interpolate. */

        col = col * param->gradient + param->offset;

      } else if ( col < param->minimum ) {
        /* Below the graph. */

        col = param->below;

      } else {
        /* We must be above the graph. */

        col = param->above;
      }

      NARROW_01(col);
      colorant[ochan] = col;

      ochan++;
      param++;
    }
  }
  HQASSERT(ochan == ichan || ochan == 1,
           "Exected to output all or just 1 colorant");

  return;
}

/*----------------------------------------------------------------------------*/
/* invert the colorants */

void mi_flip(void* indata, ICVALUE* colorant)
{
  uint32 i;
  MI_FLIP* data = indata;

  for ( i=0; i<data->channels; i++ ) {
    NARROW_01(colorant[i]);
    colorant[i] = 1 - colorant[i];
  }
}

/*----------------------------------------------------------------------------*/
/* various matrix mini-invokes */
/* if the matrix is just a scale (and offset), use mi_scale */
/* otherwise use mi_matrix */

void mi_scale(void* indata, ICVALUE* colorant)
{
  int32 i;
  MI_MATRIX* data = indata;

  HQASSERT(data, "No mini-invoke data!");

  for ( i=0; i<3; i++ )
    colorant[i] = colorant[i] * data->matrix[i][i] + data->matrix[i][3];

  if ( data->clip ) {
    for ( i=0; i<3; i++ )
      NARROW_01(colorant[i]);
  }
}

void mi_matrix(void* indata, ICVALUE* colorant)
{
  ICVALUE   v[3];
  int32     i,j;
  MI_MATRIX* data = indata;

  HQASSERT(data, "No mini-invoke data!");

  for ( i=0; i<3; i++ ) {
    v[i] = data->matrix[i][3];
    for ( j=0; j<3; j++ )
      v[i] += colorant[j] * data->matrix[i][j];
  }

  if ( data->clip ) {
    for ( i=0; i<3; i++ )
      NARROW_01(v[i]);
  }

  for ( i=0; i<3; i++ )
    colorant[i] = v[i];
}

/* Multiply a single value by a 3-tuple to get a new 3-tuple. Intended for
 * multiplying a relative Y tint value by an absolute XYZ luminance to get a
 * relative XYZ for the neutral tint.
 */
void mi_multiply(void* indata, ICVALUE* colorant)
{
  MI_MULTIPLY* data = indata;
  int32 i;

  HQASSERT(data, "No mini-invoke data");
  COLOR_01_ASSERT(colorant[0], "ICC multilply" ) ;

  for (i=2; i>=0; i--)
    colorant[i] = colorant[0] * data->color[i];
}

/* Convert a single L value into a neutral Lab tuple */
void mi_neutral_ab(void* indata, ICVALUE* colorant)
{
  UNUSED_PARAM(void *, indata);
  COLOR_01_ASSERT(colorant[0], "ICC neutral ab" ) ;

  colorant[1] = 0.5;
  colorant[2] = 0.5;
}

/*----------------------------------------------------------------------------*/
/* The multi-dimensional table lookups
 *
 * In each case we interpolate the last dimension first, from the table values
 * into the scratch space, and then interpolate the remaining dimensions in the
 * scratch space. Also, the final dimensional interpolation is output directly
 * into colorant[].
 *
 * NB: the 'k' loops in the following two lookups are backwards so we can output
 * into the colorant array which is holding the final interpolation factor! */

void mi_clut16(void* indata, ICVALUE* colorant)
{
  int32    i,j,k;
  uint16   *cell,*corner;
  ICVALUE  *in,*out;
  ICVALUE  a,b;
  MI_CLUT16 *data = indata;
  ICVALUE *scratch = IS_INTERPRETER() ? frontScratch : backScratch;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->in, "Silly input channels");
  HQASSERT(data->out, "Silly output channels");
  HQASSERT(scratch != NULL, "No scratch space!");

  /* find interpolation cell and interpolation factors */
  cell = &data->values[0];

  for ( i=0; i<data->in; i++ ) {
    NARROW_01(colorant[i]);

    colorant[i] *= data->maxindex[i];
    k = (uint32) colorant[i];
    colorant[i] -= k;
    /* colorant[i] is now an interpolation factor */

    /* avoid trying to interpolate off the edge of the data, or having to
     * special-case a factor of 0, by fiddling such factors */
    if ( k > 0 && colorant[i] == 0 ) {
      --k;
      colorant[i] = 1.0f;
    }

    cell += k * data->step[i];
  }
  /* cell[ 0 ] is the bottom corner of the interpolation cell, a single step
   * along any axis is cell[ step[i] ]
   *
   * Now start interpolating. Step 1 is from the original data, but thereafter
   * we shall be interpolating internal values (ICVALUEs). We are interpolating
   * the last dimension, 'n', which is most tightly bound in the clut.
   *
   * If we only have one input dimension, then output directly into colorant
   * array! */

  out = (data->in==1) ? colorant : scratch;
  i = data->in-1;
  for ( j=0; j < (1<<i); j++ ) { /* enumerate the lower corners */

    corner = cell;
    for ( k=0; k<i; k++ ) {
      if ( (j & 1<<k) != 0 )
        corner += data->step[k];
    }
    /* corner is now the lower of the two nodes to be interpolated */

    for ( k=data->out-1; k>=0; k-- ) {
      /* interpolate last dimension.
       * Note that we cannot fall off the edge of the array. */
      a = corner[ k ];
      b = corner[ k + data->step[i] ] - a;
      out[k] = (a + colorant[i] * b) / 65535.0f;
    }

    out += data->out;
  }

  /* if there was only one input dimension, we're done */
  if ( data->in == 1 )
    return;

  /* from now on we work entirely in the scratch space */
  for ( i=data->in-2; i>=0; i-- ) {
    /* for each dimension, interpolate. The last interpolation stores results
     * directly into the colorant array */
    in = scratch;
    out = (i==0) ? colorant : scratch;
    for ( j=0; j < (1<<i); j++ ) {
      for ( k=data->out-1; k>=0; k-- ) {
        a = in[ k ];
        b = in[ k + (data->out<<i) ] - a;
        out[k] = a + colorant[i] * b;
      }
      in += data->out;
      out += data->out;
    }
  }

  return;
}

/*----------------------------------------------------------------------------*/

/* see mi_clut16 for comments */

void mi_clut8(void* indata, ICVALUE* colorant)
{
  int32    i,j,k;
  uint8    *cell,*corner;
  ICVALUE  *in,*out;
  ICVALUE  a,b;
  MI_CLUT8 *data = indata;
  ICVALUE *scratch = IS_INTERPRETER() ? frontScratch : backScratch;

  HQASSERT(data, "No mini-invoke data!");
  HQASSERT(data->in, "Silly input channels");
  HQASSERT(data->out, "Silly output channels");
  HQASSERT(scratch != NULL, "No scratch space!");

  cell = &data->values[0];

  for ( i=0; i<data->in; i++ ) {
    NARROW_01(colorant[i]);

    colorant[i] *= data->maxindex[i];
    k = (uint32) colorant[i];
    colorant[i] -= k;

    if ( k > 0 && colorant[i] == 0 ) {
      --k;
      colorant[i] = 1.0f;
    }

    cell += k * data->step[i];
  }

  out = (data->in==1) ? colorant : scratch;
  i = data->in-1;
  for ( j=0; j < (1<<i); j++ ) {

    corner = cell;
    for ( k=0; k<i; k++ ) {
      if ( (j & 1<<k) != 0 )
        corner += data->step[k];
    }

    for ( k=data->out-1; k>=0; k-- ) {
      a = corner[ k ];
      b = corner[ k + data->step[i] ] - a;
      out[k] = (a + colorant[i] * b) / 255.0f;
    }

    out += data->out;
  }

  if ( data->in == 1 )
    return;

  for ( i=data->in-2; i>=0; i-- ) {
    in = scratch;
    out = (i==0) ? colorant : scratch;
    for ( j=0; j < (1<<i); j++ ) {
      for ( k=data->out-1; k>=0; k-- ) {
        a = in[ k ];
        b = in[ k + (data->out<<i) ] - a;
        out[k] = a + colorant[i] * b;
      }
      in += data->out;
      out += data->out;
    }
  }

  return;
}

/*----------------------------------------------------------------------------*/
/* This routine adds a mini-invoke to the action list for this ICCBased invoke.
 * It also ensures there is enough scratch space available.
 */

/* It may be sensible to extend this to maintain a dimensional check here,
 * rather than within the calling routines.
 */

Bool mi_add_mini_invoke(CLINKiccbased** pAction,
                        MINI_INVOKE function,
                        void* data)
{
  CLINKiccbased* Action = *pAction;
  int i;

  /* find terminator of action list */
  for ( i=0; Action->actions[i].function != 0; i++ );

  /* check whether action list needs extending */
  if ( Action->actions[i].u.remaining == 0 ) {
    /* action list is full, so extend */

    CLINKiccbased* New;
    New = mi_alloc(CLINKiccbased_SIZE( i + 1 + EXTEND_ACTION_LIST_LENGTH ));
    if (!New)
      return error_handler(VMERROR);

    memcpy(New, Action, CLINKiccbased_SIZE( i + 1 ));
    mi_free(Action);
    *pAction = Action = New;
    Action->actions[i].u.remaining += EXTEND_ACTION_LIST_LENGTH;
  }

  Action->actions[i+1].function = 0;
  Action->actions[i+1].u.remaining = Action->actions[i].u.remaining - 1;
  Action->actions[i].function = function;
  Action->actions[i].u.data = data;

  return TRUE;
}


/* This routine inserts a mini-invoke to the action list for this ICCBased
 * invoke, before a given action number.  It also ensures there is enough
 * scratch space available.
 */

/* It may be sensible to extend this to maintain a dimensional check here,
 * rather than within the calling routines.
 */

Bool mi_insert_mini_invoke(CLINKiccbased** pAction,
                           MINI_INVOKE function,
                           void* data,
                           int32 before)
{
  int i,j;
  CLINKiccbased* Action;
  DATA_ACTION temp;

  HQASSERT( before >= 0,
            "Unexpected insert position in mi_insert_mini_invoke" );

  /* Start by adding it to the end of the action list */
  if ( !mi_add_mini_invoke( pAction, function, data ))
    return FALSE;

  Action = *pAction;

  /* find terminator of action list */
  for ( i=0; Action->actions[i].function != 0; i++ );

  HQASSERT( i >= 1, "Action list too short in mi_insert_mini_invoke" );
  HQASSERT( before < i,
            "Inserting action after list end in mi_insert_mini_invoke" );

  /* The new mini-invoke has been added just before the terminator, i.e. at
   * position (i - 1).  This would also have been the old terminator position
   * at the time this function was called.  So if 'before' was (i - 1) there is
   * nothing more to do - it is already inserted, (though we could have just
   * called mi_add_mini_invoke).  If we asked for an insertion position after
   * the end of the list, we will assert above but can cope by leaving it
   * where it is at the effective end.
   */

  if ( before < i - 1 ) {

    /* Shuffle the action functions and data round */
    temp.function = Action->actions[i-1].function;
    temp.u.data = Action->actions[i-1].u.data;

    for ( j = i - 1; j > before; j-- ) {
      Action->actions[j].function = Action->actions[j-1].function;
      Action->actions[j].u.data = Action->actions[j-1].u.data;
    }

    Action->actions[before].function = temp.function;
    Action->actions[before].u.data = temp.u.data;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* the iccbased invoke does the following:
 *
 * 1) ensure there is enough scratch space for the largest mini-invoke
 * 2) scale input if scRGB or an output profile
 * 3) clip input to internal values
 * 4) call the mini-invokes
 */

Bool iccbased_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  CLINKiccbased* data = pLink->p.iccbased;
  DATA_ACTION*   action = &data->actions[0];
  int32          i;
  USERVALUE*     input = &pLink->iColorValues[0];
#ifdef IC_USE_DOUBLES
  double         working[MAX_CHANNELS];
#endif
  iccbasedInfoAssertions(data);

  for ( i=0; i<data->i_dimensions; i++) {
#ifdef IC_USE_DOUBLES
    working[i] = input[i];
#else
    oColorValues[i] = input[i];
#endif
  }

  /* call each of the mini-invokes in order */
  while ( action->function ) {
#ifdef IC_USE_DOUBLES
    (void) (*action->function)(action->u.data, working);
#else
    (void) (*action->function)(action->u.data, oColorValues);
#endif
    action++;
  }

#ifdef IC_USE_DOUBLES
  for ( i=0; i<data->o_dimensions; i++)
    oColorValues[i] = (USERVALUE) working[i];
#endif
  return TRUE;
}

/* iccbased_invokeActions is similar to iccbased_invokeSingle but instead of
 * calling all the mini-invokes, allows one or more successive ones to be
 * called on the input values provided, starting from the action number
 * indicated by first_action.
 *
 * Input values are only scaled if first_action is really the first action of
 * the CLINKiccbased.  They are clipped unless the first action is the first
 * action of the CLINKiccbased and its clip element is not set.
 *
 * This function is intended to provide the facility to probe a CLINKiccbased
 * in order to check e.g. the profile creator's idea of which Lab scaling to
 * use, or where to put the whitepoint.
 */
Bool iccbased_invokeActions( CLINKiccbased* data,
                             int32 first_action,
                             int32 n_actions,
                             int8 i_dimensions,
                             int8 o_dimensions,
                             USERVALUE* input,
                             USERVALUE* oColorValues )
{
  int32          i;
  int32          end_actions;
  DATA_ACTION*   action;
#ifdef IC_USE_DOUBLES
  double         working[MAX_CHANNELS];
#endif
  iccbasedInfoAssertions(data);
  end_actions = first_action + n_actions;

  HQASSERT( first_action >= 0,
            "Negative first_action in iccbased_invokeActions" );
  HQASSERT( n_actions > 0,
            "Expected at least one action in iccbased_invokeActions" );
  HQASSERT( i_dimensions > 0,
            "Expected at least one input dimension in iccbased_invokeActions" );
  HQASSERT( o_dimensions > 0,
            "Expected at least one output dimension in iccbased_invokeActions" );
  HQASSERT( input != NULL,
            "Null input in iccbased_invokeActions" );
  HQASSERT( oColorValues != NULL,
            "Null oColorValues in iccbased_invokeActions" );

  action = &data->actions[first_action];

  for ( i=0; i<i_dimensions; i++) {
#ifdef IC_USE_DOUBLES
    working[i] = input[i];
#else
    oColorValues[i] = input[i];
#endif
  }

  /* Call each of the mini-invokes in order.  If we run out (which shouldn't
   * happen) can cope by just not calling remaining ones.
   */
  for ( i=first_action; i<end_actions; i++ ) {
    if ( !action->function ) {
      HQFAIL( "Null function in iccbased_invokeActions" );
      break;
    }
#ifdef IC_USE_DOUBLES
    (void) (*action->function)(action->u.data, working);
#else
    (void) (*action->function)(action->u.data, oColorValues);
#endif
    action++;
  }

#ifdef IC_USE_DOUBLES
  for ( i=0; i<o_dimensions; i++)
    oColorValues[i] = (USERVALUE) working[i];
#endif
  return TRUE;
}

/*----------------------------------------------------------------------------*/
void cc_getICCInfoRange( ICC_PROFILE_INFO *iccInfo, int32 index, SYSTEMVALUE range[2] )
{
  HQASSERT(iccInfo, "Null iccInfo pointer");
  HQASSERT(index >= 0, "Invalid channel index (too low)");
  HQASSERT(index < iccInfo->n_device_colors, "Invalid channel index (too high)");

  /** \todo TODO deal with scRGB */

  switch ( iccInfo->devicespace ) {

    case SPACE_Lab:
      if( index == 0 ) {
        range[0] = L_MIN_VALUE;
        range[1] = L_MAX_VALUE;
      }
      else {
        range[0] = AB_MIN_VALUE;
        range[1] = AB_MAX_VALUE;
      }
      break;

    default:
      range[0] = 0;
      range[1] = 1;
      break;
  }
}

void cc_getICCBasedRange( CLINKiccbased *iccbasedInfo, int32 index, SYSTEMVALUE range[2] )
{
  iccbasedInfoAssertions(iccbasedInfo);
  cc_getICCInfoRange(iccbasedInfo->profile, index, range);
}

/* Log stripped */
