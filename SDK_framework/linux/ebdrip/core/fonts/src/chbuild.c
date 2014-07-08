/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:chbuild.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Character path building routines for Type 1/2 charstrings.
 */


#define OBJECT_MACROS_ONLY

#include "core.h"
#include "hqmemcpy.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swstart.h"
#include "fileio.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "monitor.h"
#include "fonts.h"
#include "namedef_.h"

#include "paths.h"
#include "graphics.h" /* theLineType */
#include "system.h"   /* path_free_list */
#include "gu_path.h"
#include "t1hint.h"
#include "params.h"
#include "chbuild.h"

/* -------------------------------------------------------------------------- */
static Bool ch_initchar( void *data ) ;
static Bool ch_hstem(void *data, ch_float y1, ch_float y2, Bool tedge, Bool bedge,
                     int32 index) ;
static Bool ch_vstem(void *data, ch_float x1, ch_float x2, Bool ledge, Bool redge,
                     int32 index) ;
static Bool ch_hintmask( void *data, int32 index, Bool activate ) ;
static Bool ch_cntrmask( void *data, int32 index, uint32 group ) ;
static Bool ch_flex( void *data, ch_float curve_a[6], ch_float curve_b[6],
                     ch_float depth, ch_float thresh, Bool hflex ) ;
static Bool ch_dotsection( void *data ) ;
static Bool ch_change( void *data ) ;
static Bool ch_setwidth( void *data, ch_float xwidth , ch_float ywidth ) ;
static Bool ch_setbearing( void *data, ch_float xbear , ch_float ybear ) ;
static Bool ch_moveto( void *data, ch_float x , ch_float y ) ;
static Bool ch_lineto( void *data, ch_float x , ch_float y ) ;
static Bool ch_curveto( void *data, ch_float curve[6] ) ;
static Bool ch_closepath( void *data ) ;
static Bool ch_endchar( void *data, Bool result ) ;

typedef struct {
  PATHINFO *path ;
  ch_float *xwidth, *ywidth ;
  ch_float xlsb, ylsb ;
} ch_info_t ;

/*--------------------------------------------------------------------------*/
/* Setup a charstring builder to create a path, in the supplied storage. */
Bool ch_build_path(corecontext_t *context,
                   charstring_methods_t *t1fns,
                   OBJECT *stringo,
                   charstring_decode_fn decoder,
                   charstring_build_t *buildnew,
                   charstring_build_t *buildtop,
                   PATHINFO *path, ch_float *xwidth, ch_float *ywidth)
{
  ch_info_t ch_info ;
  Bool result ;

  static charstring_build_t ch_template = {
    NULL ,

    ch_initchar ,

    ch_setbearing ,
    ch_setwidth ,

    ch_hstem ,
    ch_vstem ,
    ch_hintmask ,
    ch_cntrmask ,

    ch_flex,
    ch_dotsection,
    ch_change,

    ch_moveto ,
    ch_lineto ,
    ch_curveto ,
    ch_closepath ,

    ch_endchar
  } ;

  HQASSERT(buildnew, "Nowhere to store charstring path creator") ;
  HQASSERT(buildtop, "Nowhere to store charstring build functions") ;
  HQASSERT(path, "No path for charstring path creator") ;
  HQASSERT(xwidth, "Nowhere for X width in charstring path creator") ;
  HQASSERT(ywidth, "Nowhere for Y width in charstring path creator") ;

  ch_info.path = path ;
  ch_info.xwidth = xwidth ;
  ch_info.ywidth = ywidth ;

  *buildnew = ch_template ;
  buildnew->data = &ch_info ;

  if ( !(*buildtop->initchar)(buildtop->data) )
    return FALSE ;

  result = (*decoder)(context, t1fns, stringo, buildtop) ;
  return (*buildtop->endchar)(buildtop->data, result) ;
}

static Bool ch_initchar( void *data )
{
  ch_info_t *ch_info = data ;

  *(ch_info->xwidth) = *(ch_info->ywidth) = 0 ;
  ch_info->xlsb = ch_info->ylsb = 0 ;
  path_init(ch_info->path) ;

  return TRUE ;
}

static Bool ch_setbearing( void *data, ch_float xbear , ch_float ybear )
{
  ch_info_t *ch_info = data ;

  ch_info->xlsb = xbear ;
  ch_info->ylsb = ybear ;

  return TRUE ;
}

static Bool ch_setwidth( void *data, ch_float xwidth , ch_float ywidth )
{
  ch_info_t *ch_info = data ;

  *(ch_info->xwidth) = xwidth ;
  *(ch_info->ywidth) = ywidth ;

  return TRUE ;
}

static Bool ch_hstem(void *data, ch_float y1, ch_float y2,
                     Bool tedge, Bool bedge, int32 index)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(ch_float, y1) ;
  UNUSED_PARAM(ch_float, y2) ;
  UNUSED_PARAM( Bool , tedge ) ;
  UNUSED_PARAM( Bool , bedge ) ;
  UNUSED_PARAM(int32 , index) ;

  return TRUE ;
}

static Bool ch_vstem(void *data, ch_float x1, ch_float x2,
                     Bool ledge, Bool redge, int32 index)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(ch_float, x1) ;
  UNUSED_PARAM(ch_float, x2) ;
  UNUSED_PARAM( Bool , ledge ) ;
  UNUSED_PARAM( Bool , redge ) ;
  UNUSED_PARAM(int32 , index) ;

  return TRUE ;
}

static Bool ch_hintmask( void *data, int32 index, Bool activate )
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, index) ;
  UNUSED_PARAM(Bool, activate) ;

  return TRUE;
}

static Bool ch_cntrmask( void *data, int32 index, uint32 group )
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, index) ;
  UNUSED_PARAM(uint32, group) ;

  return TRUE;
}

static Bool ch_change( void *data )
{
  UNUSED_PARAM(void *, data) ;

  return TRUE;
}

static Bool ch_dotsection( void *data )
{
  UNUSED_PARAM(void *, data) ;

  return TRUE ;
}

static Bool ch_flex( void *data, ch_float curve_a[6], ch_float curve_b[6],
                     ch_float depth, ch_float thresh, Bool hflex)
{
  UNUSED_PARAM(ch_float, depth) ;
  UNUSED_PARAM(ch_float, thresh) ;
  UNUSED_PARAM(Bool, hflex) ;

  /* Do two Bezier curves instead of straight line. */
  return (ch_curveto(data, curve_a) && ch_curveto(data, curve_b)) ;
}

static Bool ch_moveto( void *data, ch_float x , ch_float y )
{
  ch_info_t *ch_info = data ;

  return path_moveto(ch_info->xlsb + x, ch_info->ylsb + y,
                     MOVETO, ch_info->path) ;
}

static Bool ch_lineto( void *data, ch_float x , ch_float y )
{
  ch_info_t *ch_info = data ;

  return path_segment(ch_info->xlsb + x, ch_info->ylsb + y,
                      LINETO, TRUE, ch_info->path) ;
}

static Bool ch_curveto( void *data, ch_float curve[6] )
{
  ch_info_t *ch_info = data ;
  uint32 i ;
  SYSTEMVALUE ncurve[6] ;

  for ( i = 0 ; i < 6 ; i += 2 ) {
    ncurve[i + 0] = ch_info->xlsb + curve[i + 0] ;
    ncurve[i + 1] = ch_info->ylsb + curve[i + 1] ;
  }

  return path_curveto(ncurve, TRUE, ch_info->path) ;
}

static Bool ch_closepath( void *data )
{
  ch_info_t *ch_info = data ;
  PATHINFO *adobepath = ch_info->path ;
  Bool result ;

  /* only close non-null subpaths - the MOVETO will be reused or stripped by endchar */
  if ( (adobepath->lastline) && theLineType(*(adobepath->lastline)) == MOVETO ) {
    result = TRUE ;
  }else{
    result = path_close(CLOSEPATH, ch_info->path) ;
  }

  return result ;
}

static Bool ch_endchar( void *data, Bool result )
{
  if ( result ) {
    /* Clean up trailing MOVETOs, but only if we might use the path. It will
       be discarded if this function returns FALSE anyway. */
    ch_info_t *ch_info = data ;
    PATHINFO *adobepath = ch_info->path ;

    if ( (adobepath->lastline) ) {

      if ( theLineType(*(adobepath->lastline)) == MOVETO ) {
        /* Remove last subpath from path and dispose of it.
           This may result in no path at all in the pathological case */
        PATHLIST *subpath ;

        path_remove_last_subpath(adobepath, &subpath) ;
        path_free_list(subpath, mm_pool_temp) ;
      }

      if ( (adobepath->lastline) &&
           theLineType(*(adobepath->lastline)) != CLOSEPATH &&
           theLineType(*(adobepath->lastline)) != MYCLOSE ) {

        LINELIST *theline, *newline;

        theline = theISubPath( adobepath->lastpath );
        if ( (newline=get_line(mm_pool_temp))!=NULL ) {
          theILineType(newline) = (uint8) MYCLOSE;
          theILineOrder(newline) = (uint8) 0;
          newline->point = theline->point;
          newline->next = NULL;
          adobepath->lastline->next = newline;
          adobepath->lastline = newline;
        } else result = FALSE;

      } /* CLOSEPATH || MYCLOSE */
    } /* LastLine */
  } /* result */

  return result ;
}

/*
Log stripped */
