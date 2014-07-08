/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:charstring2.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to decode Adobe Type 2 charstrings, as found in CFF fonts.
 */


#include "core.h"
#include "objects.h"
#include "swerrors.h"
#include "namedef_.h"

#include "fcache.h"
#include "chbuild.h"
#include "mm.h"
#include "t1hint.h" /* For unitpixelsX/Y */
#include "charstring12.h" /* This file ONLY implements Type 2 charstrings */

/* Things still not supported:-
 (1) cntrmask operator (bytes are just thrown away);
*/

#if defined( ASSERT_BUILD )
static int32 debug_t2_charstrings = FALSE ;
#endif

/* -------------------------------------------------------------------------- */
#ifndef EOF
#define EOF (-1)
#endif

#define T2_OP_NUMBER    0xffff
#define T2_LENBUILDCHAR 32

#define T2_SUBRS_MAX    10
#define T2_HINTS_MAX    96
#define T2_STACK_MAX    48
#define T2_STEMH_MAX    96

#define T2_REGIY_WV     0
#define T2_REGIY_NDV    1
#define T2_REGIY_UDV    2
#define T2_REGIY_MAX    3

#define T2_STATE_W              0       /* width */
#define T2_STATE_HS             1       /* hstem or hstemhm */
#define T2_STATE_VS             2       /* vstem or vstemhm */
#define T2_STATE_CM             3       /* cntrmask */
#define T2_STATE_HM             4       /* hintmask */
#define T2_STATE_MT             5       /* moveto */
#define T2_STATE_SUBPATH        6       /* subpath */
#define T2_STATE_ENDCHAR        7       /* endchar */
#define T2_STATE_MISC           8       /* miscellaneous (e.g. div, roll,...) */
#define T2_STATE_RETURN         9       /* return from (g)subrs */
#define T2_STATE_ILLEGAL        -1      /* illegal i.e. should never get */
#define T2_STATE_RESERVED       T2_STATE_MISC   /* reserved i.e. opcode not allowed */

/* -------------------------------------------------------------------------- */
#define T2_CLOSE_PATH_IF_OPEN( _t2point , _t2c ) MACRO_START       \
  if ( ! (_t2point)->closed ) {                                    \
    if ( ! (*(_t2c)->buildfns->closepath)((_t2c)->buildfns->data)) \
      return FALSE ;                                               \
    (_t2point)->closed = TRUE ;                                    \
  }                                                                \
MACRO_END

#define T2_START_PATH_IF_CLOSED( _t2point , _t2c ) MACRO_START          \
  if ( (_t2point)->closed ) {                                           \
    if ( ! (*(_t2c)->buildfns->moveto)((_t2c)->buildfns->data,          \
                                       (_t2point)->x , (_t2point)->y )) \
      return FALSE ;                                                    \
    (_t2point)->closed = FALSE ;                                        \
  }                                                                     \
MACRO_END

/* -------------------------------------------------------------------------- */
typedef int32 T2_STATE ;

typedef struct T2_STACK {
  int32 size ;
  int32 base ;
  double elems[ T2_STACK_MAX ] ;
} T2_STACK ;

typedef struct T2_BUF {
  uint32 slen ;
  uint8 *smem ;
  int32 lenIV ;
  uint16 state ;
} T2_BUF ;

typedef struct T2_POINT {
  int32  closed ;
  ch_float x , y ;
} T2_POINT ;

typedef struct T2_WIDTH {
  double   defaultWidthX ;
  double   nominalWidthX ;
} T2_WIDTH ;

typedef struct T2_HINTS {
  int32 numhints ;
  uint32 group ;
} T2_HINTS ;

typedef struct T2_RANDOM {
  uint32 seed ;
} T2_RANDOM ;

typedef struct T2_TRANS {
  int32 size ;
  double *data ;
} T2_TRANS ;

typedef struct T2_REGIY {
  int32 size[ T2_REGIY_MAX ] ;
  double *data[ T2_REGIY_MAX ] ;
} T2_REGIY ;

typedef struct T2_SUBRS {
  int32   level ;
  OBJECT *subrs ;
  int32   subrsbias ;
  OBJECT *gsubrs ;
  int32   gsubrsbias ;
} T2_SUBRS ;

typedef struct T2_SEAC {
  int32   level ;
  ch_float  xbear ;
  ch_float  ybear ;
} T2_SEAC ;

typedef struct T2_CONTEXT {
  charstring_methods_t *t1fns ;
  charstring_build_t *buildfns ;
  T2_BUF   t2buf ;
  T2_STATE t2state ;
  T2_STACK t2stack ;
  T2_POINT t2point ;
  T2_WIDTH t2width ;
  T2_HINTS t2hints ;
  T2_TRANS t2trans ;
  T2_REGIY t2regiy ;
  T2_SUBRS t2subrs ;
  T2_RANDOM t2random ;
  T2_SEAC  t2seac ;
} T2_CONTEXT ;

#define DEFAULT_FLEX_DEPTH 50.0 /* For flex1, hflex1 and hflex operators */

/* -------------------------------------------------------------------------- */
typedef Bool (*T2OPFN)( T2_CONTEXT *t2c ) ;

/* -------------------------------------------------------------------------- */
static Bool t2_reserved( T2_CONTEXT *t2c ) ;
static Bool t2_hstem( T2_CONTEXT *t2c ) ;
static Bool t2_vstem( T2_CONTEXT *t2c ) ;
static Bool t2_vmoveto( T2_CONTEXT *t2c ) ;
static Bool t2_rlineto( T2_CONTEXT *t2c ) ;
static Bool t2_hlineto( T2_CONTEXT *t2c ) ;
static Bool t2_vlineto( T2_CONTEXT *t2c ) ;
static Bool t2_rrcurveto( T2_CONTEXT *t2c ) ;
static Bool t2_callsubr( T2_CONTEXT *t2c ) ;
static Bool t2_return( T2_CONTEXT *t2c ) ;
static Bool t2_escape( T2_CONTEXT *t2c ) ;
static Bool t2_endchar( T2_CONTEXT *t2c ) ;
static Bool t2_blend( T2_CONTEXT *t2c ) ;
static Bool t2_hstemhm( T2_CONTEXT *t2c ) ;
static Bool t2_hintmask( T2_CONTEXT *t2c ) ;
static Bool t2_cntrmask( T2_CONTEXT *t2c ) ;
static Bool t2_rmoveto( T2_CONTEXT *t2c ) ;
static Bool t2_hmoveto( T2_CONTEXT *t2c ) ;
static Bool t2_vstemhm( T2_CONTEXT *t2c ) ;
static Bool t2_rcurveline( T2_CONTEXT *t2c ) ;
static Bool t2_rlinecurve( T2_CONTEXT *t2c ) ;
static Bool t2_vvcurveto( T2_CONTEXT *t2c ) ;
static Bool t2_hhcurveto( T2_CONTEXT *t2c ) ;
static Bool t2_shortint( T2_CONTEXT *t2c ) ;
static Bool t2_callgsubr( T2_CONTEXT *t2c ) ;
static Bool t2_vhcurveto( T2_CONTEXT *t2c ) ;
static Bool t2_hvcurveto( T2_CONTEXT *t2c ) ;
static Bool t2_and( T2_CONTEXT *t2c ) ;
static Bool t2_or( T2_CONTEXT *t2c ) ;
static Bool t2_not( T2_CONTEXT *t2c ) ;
static Bool t2_store( T2_CONTEXT *t2c ) ;
static Bool t2_abs( T2_CONTEXT *t2c ) ;
static Bool t2_add( T2_CONTEXT *t2c ) ;
static Bool t2_sub( T2_CONTEXT *t2c ) ;
static Bool t2_div( T2_CONTEXT *t2c ) ;
static Bool t2_load( T2_CONTEXT *t2c ) ;
static Bool t2_neg( T2_CONTEXT *t2c ) ;
static Bool t2_eq( T2_CONTEXT *t2c ) ;
static Bool t2_drop( T2_CONTEXT *t2c ) ;
static Bool t2_put( T2_CONTEXT *t2c ) ;
static Bool t2_get( T2_CONTEXT *t2c ) ;
static Bool t2_ifelse( T2_CONTEXT *t2c ) ;
static Bool t2_random( T2_CONTEXT *t2c ) ;
static Bool t2_mul( T2_CONTEXT *t2c ) ;
static Bool t2_sqrt( T2_CONTEXT *t2c ) ;
static Bool t2_dup( T2_CONTEXT *t2c ) ;
static Bool t2_exch( T2_CONTEXT *t2c ) ;
static Bool t2_index( T2_CONTEXT *t2c ) ;
static Bool t2_roll( T2_CONTEXT *t2c ) ;
static Bool t2_hflex( T2_CONTEXT *t2c ) ;
static Bool t2_flex( T2_CONTEXT *t2c ) ;
static Bool t2_hflex1( T2_CONTEXT *t2c ) ;
static Bool t2_flex1( T2_CONTEXT *t2c ) ;

/* -------------------------------------------------------------------------- */
typedef struct T2_OP {
  T2OPFN opcall ;
  int8   state ;
  uint8  minstack ;
  uint8  maxstack ;
  uint8  clearstack ;
} T2_OP ;

#define T2_STDOP_MAX    32
#define T2_EXTOP_MAX    38
#define T2_TOTALOP_MAX  (T2_STDOP_MAX+T2_EXTOP_MAX)

T2_OP t2_stdops[ T2_TOTALOP_MAX ] = {
/* 0 - 7 */
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  /* While we are fixing bug #27537, make hstem and hstemhm cope with having
     no operands. Such a malformed font has not yet been seen, but who knows
         what Jaws will do next...*/
  { t2_hstem ,          T2_STATE_HS ,           0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  /* Strictly, according to the spec. (TN #5177), the vstem should have
     a minimum of two operands, but none passes thru ok, and a duff PS job
     seems to require just that. (bug #11779) */
  { t2_vstem ,          T2_STATE_VS ,           0x00 , 0xff , TRUE } ,
  { t2_vmoveto ,        T2_STATE_MT ,           0x01 , 0x02 , TRUE } ,
  { t2_rlineto ,        T2_STATE_SUBPATH ,      0x02 , 0xff , TRUE } ,
  { t2_hlineto ,        T2_STATE_SUBPATH ,      0x01 , 0xff , TRUE } ,
  { t2_vlineto ,        T2_STATE_SUBPATH ,      0x01 , 0xff , TRUE } ,
/* 8 - 15 */
  { t2_rrcurveto ,      T2_STATE_SUBPATH ,      0x06 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_callsubr ,       T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_return ,         T2_STATE_RETURN ,       0x00 , 0xff , FALSE } ,
  { t2_escape ,         T2_STATE_ILLEGAL ,      0x00 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_endchar ,        T2_STATE_MISC ,         0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
/* 16 - 23 */
  { t2_blend ,          T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  /* (#27537) Again, should be two operands, but will cope with zero... */
  { t2_hstemhm ,        T2_STATE_HS ,           0x00 , 0xff , TRUE } ,
  { t2_hintmask ,       T2_STATE_HM ,           0x00 , 0xff , TRUE } ,
  { t2_cntrmask ,       T2_STATE_CM ,           0x00 , 0xff , TRUE } ,
  { t2_rmoveto ,        T2_STATE_MT ,           0x02 , 0x03 , TRUE } ,
  { t2_hmoveto ,        T2_STATE_MT ,           0x01 , 0x02 , TRUE } ,
  /* ditto: vstemhm has been seen without two operands (thank you Jaws PDF
     Server) so accept zero... (bug #27537) */
  { t2_vstemhm ,        T2_STATE_VS ,           0x00 , 0xff , TRUE } ,
/* 24 - 31 */
  { t2_rcurveline ,     T2_STATE_SUBPATH ,      0x08 , 0xff , TRUE } ,
  { t2_rlinecurve ,     T2_STATE_SUBPATH ,      0x08 , 0xff , TRUE } ,
  { t2_vvcurveto ,      T2_STATE_SUBPATH ,      0x04 , 0xff , TRUE } ,
  { t2_hhcurveto ,      T2_STATE_SUBPATH ,      0x04 , 0xff , TRUE } ,
  { t2_shortint ,       T2_STATE_ILLEGAL ,      0x00 , 0xff , FALSE } ,
  { t2_callgsubr ,      T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_vhcurveto ,      T2_STATE_SUBPATH ,      0x04 , 0xff , TRUE } ,
  { t2_hvcurveto ,      T2_STATE_SUBPATH ,      0x04 , 0xff , TRUE } ,

/* 12 0 - 12 7 */
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_and ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_or ,             T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_not ,            T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
/* 12 8 - 12 15 */
  { t2_store ,          T2_STATE_MISC ,         0x04 , 0xff , FALSE } ,
  { t2_abs ,            T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_add ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_sub ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_div ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_load ,           T2_STATE_MISC ,         0x03 , 0xff , FALSE } ,
  { t2_neg ,            T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_eq ,             T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
/* 12 16 - 12 23 */
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_drop ,           T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_put ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_get ,            T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_ifelse ,         T2_STATE_MISC ,         0x04 , 0xff , FALSE } ,
  { t2_random ,         T2_STATE_MISC ,         0x00 , 0xff , FALSE } ,
/* 12 24 - 12 31 */
  { t2_mul ,            T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_sqrt ,           T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_dup ,            T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_exch ,           T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_index ,          T2_STATE_MISC ,         0x01 , 0xff , FALSE } ,
  { t2_roll ,           T2_STATE_MISC ,         0x02 , 0xff , FALSE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
/* 12 32 - 12 37 */
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_reserved ,       T2_STATE_RESERVED ,     0x00 , 0xff , TRUE } ,
  { t2_hflex ,          T2_STATE_SUBPATH ,      0x07 , 0x07 , TRUE } ,
  { t2_flex ,           T2_STATE_SUBPATH ,      0x0d , 0x0d , TRUE } ,
  { t2_hflex1 ,         T2_STATE_SUBPATH ,      0x09 , 0x09 , TRUE } ,
  { t2_flex1 ,          T2_STATE_SUBPATH ,      0x0b , 0x0b , TRUE } ,
} ;

/* -------------------------------------------------------------------------- */
static Bool t2_initdecode( T2_CONTEXT *t2c ,
                           charstring_methods_t *t1fns ,
                           double *t2_transdata , int32 t2_transsize ,
                           uint8 *smem , uint32 slen,
                           charstring_build_t *buildfns) ;
static void  t2_termdecode( T2_CONTEXT *t2c ,
                            int32 t2_transsize ) ;

static Bool t2_init_getc( T2_BUF *t2buf ) ;
static int32 t2_getc( T2_BUF *t2buf ) ;

static Bool t2_decodestring( T2_CONTEXT *t2c ) ;
static Bool t2_decodeopnum( T2_BUF *t2buf , double *rnum , int32 *opcode ) ;

/* -------------------------------------------------------------------------- */
Bool decode_adobe2_outline(corecontext_t *context,
                           charstring_methods_t *t1fns,
                           OBJECT *stringo,
                           charstring_build_t *buildfns)
{
  Bool result ;
  T2_CONTEXT t2context ;

  double t2_lenbuildchar[ T2_LENBUILDCHAR ] ;

  UNUSED_PARAM(corecontext_t *, context);

  HQASSERT(oType(*stringo) == OSTRING, "No string for Type 2 charstring decode" ) ;
  HQASSERT(oString(*stringo), "String NULL in Type 2 charstring decode" ) ;

  result = t2_initdecode( & t2context ,
                          t1fns,
                          t2_lenbuildchar , T2_LENBUILDCHAR ,
                          oString(*stringo), theLen(*stringo), buildfns ) ;
  if ( result )
    result = t2_decodestring( & t2context ) ;

  t2_termdecode( & t2context , T2_LENBUILDCHAR ) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_inittrans( T2_CONTEXT *t2c , int32 t2_transsize , OBJECT *theo )
{
  int32 size ;

  HQASSERT( t2c , "t2c NULL in t2_inittrans" ) ;
  HQASSERT( theo , "theo NULL in t2_inittrans" ) ;

  HQASSERT( oType(*theo) == OINTEGER, "theo should be an OINTEGER" ) ;
  size = oInteger(*theo) ;
  if ( size < 0 )
    return error_handler( RANGECHECK ) ;
  if ( size > t2_transsize ) {
    double *dmem ;
    dmem = mm_alloc( mm_pool_temp , size * sizeof( double ) , MM_ALLOC_CLASS_CFF_DATA ) ;
    if ( ! dmem )
      return error_handler( VMERROR ) ;
    t2c->t2trans.size = size ;
    t2c->t2trans.data = dmem ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_initreg( T2_CONTEXT *t2c , int32 reg )
{
  OBJECT theo ;

  static int32 regnames[ T2_REGIY_MAX ] = {
    NAME_WeightVector ,
    NAME_NormalizedDesignVector ,
    NAME_UserDesignVector
  } ;

  HQASSERT( t2c , "t2c NULL in t2_initreg" ) ;
  HQASSERT( reg < T2_REGIY_MAX , "Registry entry index out of bounds" ) ;

  /* Get array size */
  if ( ! (*t2c->t1fns->get_info)( t2c->t1fns->data ,
                                  regnames[reg] ,
                                  -1 ,
                                  & theo ))
    return FALSE ;

  if ( oType(theo) == OINTEGER && oInteger(theo) > 0 ) {
    int32 size = oInteger(theo) ;
    int32 i ;
    double *dmem ;

    dmem = mm_alloc( mm_pool_temp , size * sizeof( double ) , MM_ALLOC_CLASS_CFF_DATA ) ;
    if ( ! dmem )
      return error_handler( VMERROR ) ;

    for ( i = 0 ; i < size ; ++i ) {
      /* Get indexed value from array */
      if ( ! (*t2c->t1fns->get_info)( t2c->t1fns->data ,
                                      regnames[ reg ] ,
                                      i ,
                                      & theo ))
        return FALSE ;

      if ( !object_get_numeric(&theo, &dmem[i]) ) {
        mm_free(mm_pool_temp, dmem, size * sizeof(double)) ;
        return error_handler( TYPECHECK ) ;
      }
    }

    t2c->t2regiy.size[ reg ] = size ;
    t2c->t2regiy.data[ reg ] = dmem ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */

static void t2_initoutline( T2_CONTEXT *t2c , uint8 *smem , uint32 slen )
{
  t2c->t2buf.slen = slen ;
  t2c->t2buf.smem = smem ;

  t2c->t2state = T2_STATE_W ;

  t2c->t2stack.size = 0 ;
  t2c->t2stack.base = 0 ;

  t2c->t2point.closed = TRUE ;  /* So first line/curve adds segment. */
  t2c->t2point.x = 0.0 ;
  t2c->t2point.y = 0.0 ;

  t2c->t2hints.numhints = 0 ;
  t2c->t2hints.group = 0 ;
}

static Bool t2_initdecode( T2_CONTEXT *t2c ,
                            charstring_methods_t *t1fns ,
                            double *t2_transdata , int32 t2_transsize ,
                            uint8 *smem , uint32 slen,
                            charstring_build_t *buildfns)
{
  int32 i ;
  OBJECT theo ;

  /* Initialise simple stuff; DON'T PUT ANY RETURNS BEFORE "Initialise complex stuff".
   * That is because t2_termdecode is always called which will trash things if
   *  "t2trans.size" & "t2regiy.size[ i ]" are not set (to 0).
   */
  t2c->t1fns = t1fns ;
  t2c->buildfns = buildfns ;

  t2c->t2width.defaultWidthX = 0.0 ;
  t2c->t2width.nominalWidthX = 0.0 ;

  t2c->t2trans.size = t2_transsize ;
  t2c->t2trans.data = t2_transdata ;

  for ( i = 0 ; i < T2_REGIY_MAX ; ++i ) {
    t2c->t2regiy.size[ i ] = 0 ;
    t2c->t2regiy.data[ i ] = NULL ;
  }

  t2c->t2random.seed = 0 ;

  t2c->t2seac.level = 0 ;
  t2c->t2seac.xbear = 0.0 ;
  t2c->t2seac.ybear = 0.0 ;

  t2c->t2subrs.level = 0 ;
  t2c->t2subrs.subrs = NULL ;
  t2c->t2subrs.gsubrs = NULL ;

  /* Initialise complex stuff. */
  if ( ! (*t2c->t1fns->get_info)(t2c->t1fns->data, NAME_initialRandomSeed, -1, &theo) )
    return FALSE ;
  HQASSERT( oType(theo) == OINTEGER || oType(theo) == ONULL,
            "initialRandomSeed should be an integer" ) ;
  if ( oType(theo) == OINTEGER )
    t2c->t2random.seed = (uint32)oInteger(theo) ;

  if ( ! (*t2c->t1fns->get_info)(t2c->t1fns->data, NAME_lenIV, -1, &theo) )
    return FALSE ;
  HQASSERT( oType(theo) == OINTEGER || oType(theo) == ONULL,
            "lenIV should be an integer" ) ;
  t2c->t2buf.lenIV = oType(theo) == OINTEGER ? oInteger(theo) : -1 ;

  t2_initoutline( t2c , smem , slen ) ;
  if ( ! t2_init_getc( & t2c->t2buf ))
    return FALSE ;

  if ( ! (*t2c->t1fns->get_info)(t2c->t1fns->data,
                                 NAME_defaultWidthX ,
                                 -1 ,
                                 &theo))
    return FALSE ;
  if ( oType(theo) != ONULL )
    t2c->t2width.defaultWidthX = object_numeric_value(&theo) ;

  if ( ! (*t2c->t1fns->get_info)( t2c->t1fns->data ,
                                  NAME_nominalWidthX ,
                                  -1 ,
                                  & theo ))
    return FALSE ;
  if ( oType(theo) != ONULL )
    t2c->t2width.nominalWidthX = object_numeric_value(&theo) ;

  { /* [64421] Ignore daft defaultWidthX */
    int silly = 0 ;

    if (t2c->t2width.defaultWidthX < 0 || t2c->t2width.defaultWidthX > 4096)
      silly |= 1 ;
    if (t2c->t2width.nominalWidthX < 0 || t2c->t2width.nominalWidthX > 4096)
      silly |= 2 ;

    switch (silly) {
    case 1: /* silly default but nominal is fine */
      t2c->t2width.defaultWidthX = t2c->t2width.nominalWidthX ;
      break ;
    case 2: /* daft nominal but default is plausible */
      t2c->t2width.nominalWidthX = t2c->t2width.defaultWidthX ;
      break ;
    case 3: /* both insane - 512 is arbitrary */
      t2c->t2width.defaultWidthX = t2c->t2width.nominalWidthX = 512 ;
      break ;
    }
  }

  if ( ! (*t2c->t1fns->get_info)( t2c->t1fns->data ,
                                  NAME_lenBuildCharArray ,
                                  -1 ,
                                  & theo ))
    return FALSE ;

  if ( oType(theo) == OINTEGER ) {
    if ( ! t2_inittrans( t2c , t2_transsize  , &theo ))
      return FALSE ;
  }

  for ( i = 0 ; i < T2_REGIY_MAX ; ++i ) {
    if ( ! t2_initreg( t2c , i ))
      return FALSE ;
  }

  for ( i = 0 ; i < t2c->t2trans.size ; ++i )
    t2c->t2trans.data[ i ] = 0.0 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static void t2_termdecode( T2_CONTEXT *t2c ,
                           int32 t2_transsize )
{
  int32 i ;
  if ( t2c->t2trans.size > t2_transsize )
    mm_free( mm_pool_temp ,
             t2c->t2trans.data ,
             t2c->t2trans.size * sizeof( double )) ;
  for ( i = 0 ; i < T2_REGIY_MAX ; ++i )
    if ( t2c->t2regiy.size[ i ] > 0 )
      mm_free( mm_pool_temp ,
               t2c->t2regiy.data[ i ] ,
               t2c->t2regiy.size[ i ] * sizeof( double )) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_init_getc( T2_BUF *t2buf )
{
  if ( t2buf->lenIV >= 0 ) {
    int32 i ;
#define FONT_SEED 4330
    uint16 state = FONT_SEED ;

    uint32 slen = t2buf->slen ;
    uint8 *smem = t2buf->smem ;

    if ((int32)slen < t2buf->lenIV )
      return error_handler( RANGECHECK ) ;

    for ( i = 0 ; i < t2buf->lenIV ; ++i ) {
      uint8 tmp = *smem++ ;
      slen -= 1 ;
      DECRYPT_CHANGE_STATE( tmp , state , DECRYPT_ADD , DECRYPT_MULT ) ;
    }
    t2buf->slen = slen ;
    t2buf->smem = smem ;
    t2buf->state = state ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static int32 t2_getc( T2_BUF *t2buf )
{
  int32 ch ;

  uint32 slen = t2buf->slen ;
  uint8 *smem = t2buf->smem ;

  if ( slen == 0 ) {
    ch = EOF ;
    ( void )error_handler( INVALIDFONT ) ;
  }
  else {
    if ( t2buf->lenIV >= 0 ) {
      uint16 state = t2buf->state ;
      uint8 in = *smem++ ;
      slen -= 1 ;

      ch = DECRYPT_BYTE( in , state ) ;
      DECRYPT_CHANGE_STATE( in , state , DECRYPT_ADD , DECRYPT_MULT ) ;
      t2buf->state = state ;
    }
    else {
      ch = (*smem++) ;
      slen -= 1 ;
    }
    t2buf->slen = slen ;
    t2buf->smem = smem ;
  }
  return ch ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_decodestring( T2_CONTEXT *t2c )
{
  int32 opcode = T2_STATE_ILLEGAL;
  double num = 0.0;

  /* w? {hs* vs* cm* hm* mt subpath}? {mt subpath}* endchar */
  T2_STACK *t2stack = & t2c->t2stack ;

  if ( t2c->t2subrs.level > T2_SUBRS_MAX )
    return error_handler( LIMITCHECK ) ;

  do {
    if ( ! t2_decodeopnum( & t2c->t2buf , & num , & opcode ))
      return error_handler( INVALIDFONT ) ;

    if ( opcode == T2_OP_NUMBER ) {
      if ( t2stack->size == T2_STACK_MAX )
        return error_handler( STACKOVERFLOW ) ;
      t2stack->elems[ t2stack->size++ ] = num ;
    }
    else {
      int32 oldstate ;
      int32 newstate ;
      T2OPFN opcall ;
      /* Range check new op for validity */
      HQASSERT( opcode >= 0 , "not expecting a -ve op code" ) ;
      if ( opcode >= T2_TOTALOP_MAX ) {
        HQTRACE(debug_t2_charstrings,
          ("out of range opcode: %d",opcode));
        return error_handler( INVALIDFONT ) ;
      }

      /* Is this a valid state transition. */
      oldstate = t2c->t2state ;
      newstate = t2_stdops[ opcode ].state ;

      /* Suppress INVALIDFONT for spurious early "0 0 rmoveto" [12790] */
      if (opcode == 21 && t2stack->size == 2
          && t2stack->elems[0] == 0.0 && t2stack->elems[1] == 0.0)
        newstate = oldstate ;

      if ( ! (( newstate >= oldstate ) ||
              /* [12623] Allow cntrmask within subpath - ignored by opcode */
              (( newstate == T2_STATE_HM || newstate == T2_STATE_CM ) &&
               ( oldstate == T2_STATE_SUBPATH || oldstate == T2_STATE_MT )) ||
              ( newstate == T2_STATE_MT && ( oldstate == T2_STATE_SUBPATH )))) {
        HQTRACE(debug_t2_charstrings,
          ("invalid state transition: %d, %d",oldstate,newstate));
        return error_handler( INVALIDFONT ) ;
      }

      /* Call function associated with opcode having checked stack requirements. */
      if ( t2_stdops[ opcode ].minstack > (uint8) t2stack->size )
        return error_handler( STACKUNDERFLOW ) ;
      if ( t2_stdops[ opcode ].maxstack < (uint8) t2stack->size ) {
        /* There are too many parameters on the stack! Do we have an implicit vstemvh
           that we could handle now? (bug 12153) */
        if ( t2c->t2state <= T2_STATE_VS ) {
          Bool ok ;
          int32 size;
          /* fiddle the stack to only contain the extra parameters */
          size = t2c->t2stack.size;
          t2c->t2stack.size -= t2_stdops[ opcode ].maxstack;
          /* call vstem */
          ok = t2_vstemhm(t2c);
          /* fiddle the stack to contain only the parameters we're expecting */
          t2c->t2stack.base = size - t2_stdops[ opcode ].maxstack;
          t2c->t2stack.size = size;
          /* the result is the same as Acrobat, so it MUST be right */
          if ( !ok ) return FALSE;
        } else
          return error_handler( RANGECHECK ) ;
      }

      opcall = t2_stdops[ opcode ].opcall ;
      if ( ! opcall( t2c ))
        return error_handler( INVALIDFONT ) ;

      if ( t2_stdops[ opcode ].clearstack )
        t2stack->size = t2stack->base = 0 ;

      if ( newstate == T2_STATE_RETURN )
        return TRUE ;

      /* Update state unless we're using a miscellaneous operator such as add. */
      if ( newstate != T2_STATE_MISC &&
           newstate > oldstate )
        t2c->t2state = newstate ;
    }
  } while ( t2c->t2state != T2_STATE_ENDCHAR ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_decodeopnum( T2_BUF *t2buf , double *rnum , int32 *opcode )
{
  int32 v ;

  if (( v = t2_getc( t2buf )) == EOF )
    return FAILURE(FALSE) ;
  if ( v >= 32 ) {
    double num ;
    if ( 32 <= v && v <= 246 ) {
      num = ( double )( v - 139 ) ;
    }
    else if ( 247 <= v && v <= 250 ) {
      int32 w ;
      if (( w = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      num = ( double )((( v - 247 ) << 8 ) + w + 108 ) ;
    }
    else if ( 251 <= v && v <= 254 ) {
      int32 w ;
      if (( w = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      num = ( double )( -(( v - 251 ) << 8 ) - w - 108 ) ;
    }
    else { /* v == 255 */
      int32 b1 , b2 , b3 , b4 ;
      int32 ival ;
      int32 fval ;
      if (( b1 = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      if (( b2 = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      if (( b3 = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      if (( b4 = t2_getc( t2buf )) == EOF )
        return FAILURE(FALSE) ;
      ival = (( b1 << 8 ) | b2 ) ;
      ival = ( int32 )(( int16 )ival ) ; /* sign extend the 16 bit num. */
      fval = (( b3 << 8 ) | b4 ) ;
      num = (( double )ival + ( double )fval / 65536.0) ;
    }
    (*opcode) = T2_OP_NUMBER ;
    (*rnum) = num ;
  }
  else if ( v == 28 ) {
    int32 b1 , b2 ;
    int32 ival ;
    double num ;
    if (( b1 = t2_getc( t2buf )) == EOF )
      return FAILURE(FALSE) ;
    if (( b2 = t2_getc( t2buf )) == EOF )
      return FAILURE(FALSE) ;
    ival = (( b1 << 8 ) | b2 ) ;
    ival = ( int32 )(( int16 )ival ) ; /* sign extend the 16 bit num. */
    num = ( double )( ival ) ;
    (*opcode) = T2_OP_NUMBER ;
    (*rnum) = num ;
  }
  else if ( v == 12 ) {
    int32 op ;
    if (( op = t2_getc( t2buf )) == EOF )
      return FAILURE(FALSE) ;
    (*opcode) = T2_STDOP_MAX + op ;     /* extended op */
  }
  else {
    (*opcode) = v ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_width( T2_CONTEXT *t2c , int32 iswidth )
{
  ch_float width ;

  if ( iswidth ) {
    /* Use num on bottom of stack + nominalWidthX */
    T2_STACK *t2stack = & t2c->t2stack ;

    width = t2stack->elems[ 0 ] + t2c->t2width.nominalWidthX ;
    t2stack->base = 1 ;
  }
  else {
    /* Use defaultWidthX */
    width = t2c->t2width.defaultWidthX ;
  }

  /* Don't need to set bearing, since defaults to (0,0). */
  return (*t2c->buildfns->setwidth)(t2c->buildfns->data, width , 0.0);
}

/* -------------------------------------------------------------------------- */
static Bool t2_reserved( T2_CONTEXT *t2c )
{
  UNUSED_PARAM( T2_CONTEXT * , t2c ) ;

  /* Although it would be nice to return an error, distiller produces reserved
   * op codes when it distills Type1 fonts down to Type1C fonts.
   */

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hstem( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float dy ;
  ch_float y1 , y2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  if ( t2c->t2state == T2_STATE_W )
    if (!t2_width( t2c , ( t2stack->size & 1 ) != 0 ))
      return FALSE;

  y2 = 0.0 ;
  size = t2stack->size ;
  base = t2stack->base ;
  for ( i = base ; i+1 < size ; i += 2 ) {
    Bool tedge, bedge ;
    if ( t2c->t2hints.numhints == T2_HINTS_MAX )
      return error_handler( LIMITCHECK ) ;

    dy = t2stack->elems[ i + 0 ] ;
    y1 = y2 + dy ;
    dy = t2stack->elems[ i + 1 ] ;
    y2 = y1 + dy ;
    /* use temporary variables here for the VxWorks compiler which
     * cannot cope with "complex" floating point expressions
     */
    tedge = (dy == -20.0) ;
    bedge = (dy == -21.0) ;
    if ( ! (*t2c->buildfns->hstem)(t2c->buildfns->data,
                                   y1 , y2 , tedge , bedge,
                                   t2c->t2hints.numhints))
      return FALSE ;

    ++t2c->t2hints.numhints ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vstem( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float dx ;
  ch_float x1 , x2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  if ( t2c->t2state == T2_STATE_W )
    if (!t2_width( t2c , ( t2stack->size & 1 ) != 0 ))
      return FALSE;

  x2 = 0.0 ;
  size = t2stack->size ;
  base = t2stack->base ;
  for ( i = base ; i+1 < size ; i += 2 ) {
    Bool tedge, bedge ;
    if ( t2c->t2hints.numhints == T2_HINTS_MAX )
      return error_handler( LIMITCHECK ) ;

    dx = t2stack->elems[ i + 0 ] ;
    x1 = x2 + dx ;
    dx = t2stack->elems[ i + 1 ] ;
    x2 = x1 + dx ;
    /* use temporary variables here for the VxWorks compiler which
     * cannot cope with "complex" floating point expressions
     */
    tedge = (dx == -20.0) ;
    bedge = (dx == -21.0) ;
    if ( ! (*t2c->buildfns->vstem)(t2c->buildfns->data,
                                   x1 , x2 , tedge , bedge,
                                   t2c->t2hints.numhints))
      return FALSE ;

    ++t2c->t2hints.numhints ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vmoveto( T2_CONTEXT *t2c )
{
  int32 i ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  T2_CLOSE_PATH_IF_OPEN( t2point , t2c ) ;

  if ( t2c->t2state == T2_STATE_W )
    if (!t2_width( t2c , t2stack->size == 2 ))
      return FALSE;

  i = t2stack->base ;
  t2point->y += t2stack->elems[ i + 0 ] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_rlineto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float x , y ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base ) % 2 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  x = t2point->x ;
  y = t2point->y ;

  for ( i = base ; i < size ; i += 2 ) {
    x += t2stack->elems[ i + 0 ] ;
    y += t2stack->elems[ i + 1 ] ;
    if ( ! (*t2c->buildfns->lineto)(t2c->buildfns->data, x , y ))
      return FALSE ;
  }
  t2point->x = x ;
  t2point->y = y ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hlineto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float x , y ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  x = t2point->x ;
  y = t2point->y ;

  size = t2stack->size ;
  base = t2stack->base ;
  i = base ;
  while ( i + 1 <= size ) {
    x += t2stack->elems[ i + 0 ] ;
    i += 1 ;
    if ( ! (*t2c->buildfns->lineto)(t2c->buildfns->data, x , y ))
      return FALSE ;
    if ( i + 1 <= size ) {
      y += t2stack->elems[ i + 0 ] ;
      i += 1 ;
      if ( ! (*t2c->buildfns->lineto)(t2c->buildfns->data, x , y))
        return FALSE ;
    }
  }
  t2point->x = x ;
  t2point->y = y ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vlineto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float x , y ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  x = t2point->x ;
  y = t2point->y ;

  size = t2stack->size ;
  base = t2stack->base ;
  i = base ;
  while ( i + 1 <= size ) {
    y += t2stack->elems[ i + 0 ] ;
    i += 1 ;
    if ( ! (*t2c->buildfns->lineto)(t2c->buildfns->data, x , y))
      return FALSE ;
    if ( i + 1 <= size ) {
      x += t2stack->elems[ i + 0 ] ;
      i += 1 ;
      if ( ! (*t2c->buildfns->lineto)(t2c->buildfns->data, x , y))
        return FALSE ;
    }
  }
  t2point->x = x ;
  t2point->y = y ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_rrcurveto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float curve[6] ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base ) % 6 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  curve[4] = t2point->x ;
  curve[5] = t2point->y ;

  for ( i = base ; i < size ; i += 6 ) {
    curve[0] = curve[4] + t2stack->elems[ i + 0 ] ;
    curve[1] = curve[5] + t2stack->elems[ i + 1 ] ;
    curve[2] = curve[0] + t2stack->elems[ i + 2 ] ;
    curve[3] = curve[1] + t2stack->elems[ i + 3 ] ;
    curve[4] = curve[2] + t2stack->elems[ i + 4 ] ;
    curve[5] = curve[3] + t2stack->elems[ i + 5 ] ;

    if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
      return FALSE ;
  }
  t2point->x = curve[4] ;
  t2point->y = curve[5] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_callglsubr( T2_CONTEXT *t2c , int32 global )
{
  int32 i, size, result ;
  T2_BUF t2buf ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  i = ( int32 )t2stack->elems[ size - 1 ] ;
  t2stack->size = size - 1 ;

  t2buf = t2c->t2buf ;

  if ( !(*t2c->t1fns->begin_subr)(t2c->t1fns->data, i, global,
                                  &t2c->t2buf.smem, &t2c->t2buf.slen) )
    return FALSE ;

  result = t2_init_getc( & t2c->t2buf ) ;
  if ( result ) {
    t2c->t2subrs.level++ ;
    result = t2_decodestring( t2c ) ;
    t2c->t2subrs.level-- ;
  }

  (*t2c->t1fns->end_subr)(t2c->t1fns->data,
                          &t2c->t2buf.smem, &t2c->t2buf.slen) ;

  t2c->t2buf = t2buf ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_callsubr( T2_CONTEXT *t2c )
{
  return t2_callglsubr( t2c , FALSE ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_return( T2_CONTEXT *t2c )
{
  UNUSED_PARAM( T2_CONTEXT * , t2c ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_escape( T2_CONTEXT *t2c )
{
  UNUSED_PARAM( T2_CONTEXT * , t2c ) ;
  HQFAIL("t2_escape should never be called");
  return error_handler( UNREGISTERED ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_callseac( T2_CONTEXT *t2c , int32 charno , double xbear , double ybear )
{
  Bool result ;
  uint8 *string ;
  uint32 length ;

  if ( !(*t2c->t1fns->begin_seac)(t2c->t1fns->data, charno,
                                  &string, &length) )
    return FALSE ;

  t2_initoutline( t2c , string , length) ;
  result = t2_init_getc( & t2c->t2buf ) ;

  if ( result ) {
    t2c->t2seac.xbear += xbear ;
    t2c->t2seac.ybear += ybear ;
    result = (*t2c->buildfns->setbearing)(t2c->buildfns->data,
                                          t2c->t2seac.xbear ,
                                          t2c->t2seac.ybear) ;
  }

  if ( result ) {
    t2c->t2seac.level++ ;
    t2c->t2subrs.level++ ;
    result = t2_decodestring( t2c ) ;
    t2c->t2subrs.level-- ;
    t2c->t2seac.level-- ;
  }

  (*t2c->t1fns->end_seac)(t2c->t1fns->data, &string, &length) ;

  if ( ! result )
    return FALSE ;

  t2c->t2seac.xbear -= xbear ;
  t2c->t2seac.ybear -= ybear ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_endchar( T2_CONTEXT *t2c )
{
  T2_POINT *t2point = & t2c->t2point ;

  if ( t2c->t2state <= T2_STATE_VS ) {
    T2_STACK *t2stack = & t2c->t2stack ;
    int32 size = t2stack->size ;

    /* endchar appears (as of at least 2000/02/24) to have an undocumented
       extension; if there are 4 or 5 numbers on the stack, the top 4 numbers
       are used to construct an accented character. This appears to work in
       the same way as SEAC in Type 1 fonts. We assert that the stack
       contains values for nothing, the character width, the SEAC data, or
       the width and the SEAC data. If it doesn't, it may be another
       undocumented extension. Grr... */

    HQASSERT(size == 0 || size == 1 || size == 4 || size == 5,
             "Another undocumented endchar extension found?") ;

    if ( size == 4 || size == 5 ) {
      double width = 0.0 ;
      double xbear , ybear ;
      int32 chno1 , chno2 ;

      /* Go get the string for the first character. */
      if ( size == 5 )
        width = t2stack->elems[ size - 5 ] ;
      xbear = t2stack->elems[ size - 4 ] ;
      ybear = t2stack->elems[ size - 3 ] ;
      chno1 = ( int32 )t2stack->elems[ size - 2 ] ;
      chno2 = ( int32 )t2stack->elems[ size - 1 ] ;

      HQTRACE(debug_t2_charstrings,
              ("Undocumented extension used: SEAC char %d accent %d xshift %f yshift %f", chno1, chno2, xbear, ybear));

      if ( ! t2_callseac( t2c , chno1 , 0.0 , 0.0 ) ||
           ! t2_callseac( t2c , chno2 , xbear , ybear ))
        return FALSE ;

      t2stack->size = t2stack->base = 0 ;
      if ( size == 5 ) {
        t2stack->size = 1 ;
        t2stack->elems[ 0 ] = width ;
      }
    }
    if (!t2_width( t2c , ( size & 1 ) != 0 ))
      return FALSE;
  }

  T2_CLOSE_PATH_IF_OPEN( t2point , t2c ) ;

  t2c->t2state = T2_STATE_ENDCHAR ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_blend( T2_CONTEXT *t2c )
{
  int32 i , j , l ;
  int32 n , k ;
  int32 size ;
  int32 base ;
  double num ;
  double *weights ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_REGIY *t2regiy = & t2c->t2regiy ;

  HQFAIL("NYT: t2_blend");

  size = t2stack->size ;
  n = ( int32 )t2stack->elems[ size - 1 ] ;
  if ( n < 1 )
    return error_handler( RANGECHECK ) ;
  size -= 1 ;
  k = t2regiy->size[ T2_REGIY_WV ] ;
  if ( k == 0 )
    return error_handler( UNDEFINEDRESULT ) ;
  base = size - n * k ;
  if ( base < 0 )
    return error_handler( STACKUNDERFLOW ) ;
  size = base + n ;
  t2stack->size = size ;

  weights = t2regiy->data[ T2_REGIY_WV ] ;
  i = base ;
  j = base + n ;
  while ((--n) >= 0 ) {
    num = t2stack->elems[ i ] ;
    for ( l = 1 ; l < k ; ++l )
      num += weights[ l ] * t2stack->elems[ j++ ] ;
    t2stack->elems[ i++ ] = num ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hstemhm( T2_CONTEXT *t2c )
{
  return t2_hstem( t2c ) ;
}

/* -------------------------------------------------------------------------- */
/* Sets or resets the "maskedOut" flag that controls whether or not a
   particular stem hint is active. */
static Bool t2_hintmask( T2_CONTEXT *t2c )
{
  int32 hintnum, hm ;
  T2_BUF   *t2buf   = &t2c->t2buf ;
  T2_STACK *t2stack = &t2c->t2stack ;

  /*
   * If the state is T2_STATE_W then assume that we have some implicit
   * vstem hints. Its an optimization -  see the description of
   * hintmask in Adobe TN5177.
   */
  if ( t2c->t2state <= T2_STATE_VS && t2stack->size != 0 ) {
    if ( t2stack->size < 2 )
      return error_handler( RANGECHECK ) ;
    if ( ! t2_vstemhm( t2c ))
      return FALSE ;
  }

  /* For each mask byte, read it in and process it for its flags */
  for ( hintnum = hm = 0 ; hintnum < t2c->t2hints.numhints ; ++hintnum, hm <<= 1 ) {
    if ( (hintnum & 7) == 0 ) { /* Need a new hint byte */
      if ( (hm = t2_getc(t2buf)) == EOF )
        return FAILURE(FALSE) ;
    }

    if ( !(*t2c->buildfns->hintmask)(t2c->buildfns->data, hintnum,
                                     ((hm & 0x80) != 0)) )
      return FALSE ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_cntrmask( T2_CONTEXT *t2c )
{
  int32 hintnum, cm ;
  uint32 group ;
  T2_BUF *t2buf = & t2c->t2buf ;
  T2_STACK *t2stack = & t2c->t2stack ;

  /* Do the same as hintmask since although spec doesn't say so, we've
   * found Adobe fonts that seem to presume the same behaviour (in that
   * there are some vstems on the stack).
   */
  if ( t2c->t2state <= T2_STATE_VS && t2stack->size != 0 ) {
    if ( t2stack->size < 2 )
      return error_handler( RANGECHECK ) ;
    if ( ! t2_vstemhm( t2c ))
      return FALSE ;
  }

  if ( t2c->t2state >= T2_STATE_MT ) {
    /* [12623] cntrmask within subpath - ignore completely and discard the
     * expected number of mask bytes.
     */
    for ( hintnum = 0 ; hintnum < t2c->t2hints.numhints ; hintnum += 8 )
      if ( (cm = t2_getc(t2buf)) == EOF )
        return FAILURE(FALSE) ;

    return TRUE ;
  }

  group = ++(t2c->t2hints.group) ;

  /* For each mask byte, read it in and process it for its flags */
  for ( hintnum = cm = 0 ; hintnum < t2c->t2hints.numhints ; ++hintnum, cm <<= 1 ) {
    if ( (hintnum & 7) == 0 ) { /* Need a new hint byte */
      if ( (cm = t2_getc(t2buf)) == EOF )
        return FAILURE(FALSE) ;
    }

    if ( (cm & 0x80) != 0 ) {
      if ( !(*t2c->buildfns->cntrmask)(t2c->buildfns->data, hintnum, group) )
        return FALSE ;
    }
  }

  /* A final call with index < 0 indicates the group is complete */
  return (*t2c->buildfns->cntrmask)(t2c->buildfns->data, -1, group) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_rmoveto( T2_CONTEXT *t2c )
{
  int32 i ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  T2_CLOSE_PATH_IF_OPEN( t2point , t2c ) ;

  if ( t2c->t2state == T2_STATE_W )
    if (!t2_width( t2c , t2stack->size == 3 ))
      return FALSE;

  i = t2stack->base ;
  t2point->x += t2stack->elems[ i + 0 ] ;
  t2point->y += t2stack->elems[ i + 1 ] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hmoveto( T2_CONTEXT *t2c )
{
  int32 i ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  T2_CLOSE_PATH_IF_OPEN( t2point , t2c ) ;

  if ( t2c->t2state == T2_STATE_W )
    if (!t2_width( t2c , t2stack->size == 2 ))
      return FALSE;

  i = t2stack->base ;
  t2point->x += t2stack->elems[ i + 0 ] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vstemhm( T2_CONTEXT *t2c )
{
  return t2_vstem( t2c ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_rcurveline( T2_CONTEXT *t2c )
{
  T2_STACK *t2stack = & t2c->t2stack ;

  t2stack->size -= 2 ;
  if ( ! t2_rrcurveto( t2c ))
    return FALSE ;

  t2stack->base = t2stack->size ;
  t2stack->size = t2stack->size + 2 ;
  return t2_rlineto( t2c ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_rlinecurve( T2_CONTEXT *t2c )
{
  T2_STACK *t2stack = & t2c->t2stack ;

  t2stack->size -= 6 ;
  if ( ! t2_rlineto( t2c ))
    return FALSE ;

  t2stack->base = t2stack->size ;
  t2stack->size = t2stack->size + 6 ;
  return t2_rrcurveto( t2c ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vvcurveto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float curve[6] ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base - 0 ) % 4 ) != 0 &&
      (( size - base - 1 ) % 4 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  curve[4] = t2point->x ;
  curve[5] = t2point->y ;

  if ((( size - base - 1 ) % 4 ) == 0 ) {
    i = base++ ;
    curve[4] += t2stack->elems[ i + 0 ] ;
  }

  for ( i = base ; i < size ; i += 4 ) {
    curve[0] = curve[4] ;
    curve[1] = curve[5] + t2stack->elems[ i + 0 ] ;
    curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
    curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
    curve[4] = curve[2] ;
    curve[5] = curve[3] + t2stack->elems[ i + 3 ] ;

    if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
      return FALSE ;
  }
  t2point->x = curve[4] ;
  t2point->y = curve[5] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hhcurveto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float curve[6] ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base - 0 ) % 4 ) != 0 &&
      (( size - base - 1 ) % 4 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  curve[4] = t2point->x ;
  curve[5] = t2point->y ;

  if ((( size - base - 1 ) % 4 ) == 0 ) {
    i = base++ ;
    curve[5] += t2stack->elems[ i + 0 ] ;
  }

  for ( i = base ; i < size ; i += 4 ) {
    curve[0] = curve[4] + t2stack->elems[ i + 0 ] ;
    curve[1] = curve[5] ;
    curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
    curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
    curve[4] = curve[2] + t2stack->elems[ i + 3 ] ;
    curve[5] = curve[3] ;

    if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
      return FALSE ;
  }
  t2point->x = curve[4] ;
  t2point->y = curve[5] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_shortint( T2_CONTEXT *t2c )
{
  UNUSED_PARAM( T2_CONTEXT * , t2c ) ;
  HQFAIL("t2_shortint should never be called");
  return error_handler( UNREGISTERED ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_callgsubr( T2_CONTEXT *t2c )
{
  return t2_callglsubr( t2c , TRUE ) ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_vhcurveto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float curve[6] ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base - 0 ) % 4 ) != 0 &&
      (( size - base - 1 ) % 4 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  curve[4] = t2point->x ;
  curve[5] = t2point->y ;

  i = base ;
  while ( i + 4 <= size ) {
    curve[0] = curve[4] ;
    curve[1] = curve[5] + t2stack->elems[ i + 0 ] ;
    curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
    curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
    curve[4] = curve[2] + t2stack->elems[ i + 3 ] ;
    curve[5] = curve[3] ;
    i += 4 ;
    if ( i + 1 == size )
      curve[5] += t2stack->elems[ i + 0 ] ;
    if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
      return FALSE ;
    if ( i + 4 <= size ) {
      curve[0] = curve[4] + t2stack->elems[ i + 0 ] ;
      curve[1] = curve[5] ;
      curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
      curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
      curve[4] = curve[2] ;
      curve[5] = curve[3] + t2stack->elems[ i + 3 ] ;
      i += 4 ;
      if ( i + 1 == size )
        curve[4] += t2stack->elems[ i + 0 ] ;
      if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
        return FALSE ;
    }
  }
  t2point->x = curve[4] ;
  t2point->y = curve[5] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hvcurveto( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  int32 base ;
  ch_float curve[6] ;
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  base = t2stack->base ;
  if ((( size - base - 0 ) % 4 ) != 0 &&
      (( size - base - 1 ) % 4 ) != 0 )
    return error_handler( RANGECHECK ) ;

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  curve[4] = t2point->x ;
  curve[5] = t2point->y ;

  i = base ;
  while ( i + 4 <= size ) {
    curve[0] = curve[4] + t2stack->elems[ i + 0 ] ;
    curve[1] = curve[5] ;
    curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
    curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
    curve[4] = curve[2] ;
    curve[5] = curve[3] + t2stack->elems[ i + 3 ] ;
    i += 4 ;
    if ( i + 1 == size )
      curve[4] += t2stack->elems[ i + 0 ] ;
    if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
      return FALSE ;
    if ( i + 4 <= size ) {
      curve[0] = curve[4] ;
      curve[1] = curve[5] + t2stack->elems[ i + 0 ] ;
      curve[2] = curve[0] + t2stack->elems[ i + 1 ] ;
      curve[3] = curve[1] + t2stack->elems[ i + 2 ] ;
      curve[4] = curve[2] + t2stack->elems[ i + 3 ] ;
      curve[5] = curve[3] ;
      i += 4 ;
      if ( i + 1 == size )
        curve[5] += t2stack->elems[ i + 0 ] ;
      if ( ! (*t2c->buildfns->curveto)(t2c->buildfns->data, curve) )
        return FALSE ;
    }
  }
  t2point->x = curve[4] ;
  t2point->y = curve[5] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_and( T2_CONTEXT *t2c )
{
  int32 and ;
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  if ( num1 != 0.0 && num2 != 0.0 )
    and = 1 ;
  else
    and = 0 ;
  t2stack->elems[ size - 2 ] = ( double )and ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_or( T2_CONTEXT *t2c )
{
  int32 or ;
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  if ( num1 != 0.0 || num2 != 0.0 )
    or = 1 ;
  else
    or = 0 ;
  t2stack->elems[ size - 2 ] = ( double )or ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_not( T2_CONTEXT *t2c )
{
  int32 not ;
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num = t2stack->elems[ size - 1 ] ;
  if ( num != 0.0 )
    not = 0 ;
  else
    not = 1 ;
  t2stack->elems[ size - 1 ] = ( double )not ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_store( T2_CONTEXT *t2c )
{
  int32 i , j ;
  int32 reg ;
  int32 count ;
  int32 size ;
  double *data ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_TRANS *t2trans = & t2c->t2trans ;
  T2_REGIY *t2regiy = & t2c->t2regiy ;

  HQFAIL("NYT: t2_store");

  size = t2stack->size ;
  count = ( int32 )t2stack->elems[ size - 1 ] ;
  i     = ( int32 )t2stack->elems[ size - 2 ] ;
  j     = ( int32 )t2stack->elems[ size - 3 ] ;
  reg   = ( int32 )t2stack->elems[ size - 4 ] ;
  if ( count < 0 ||
       i < 0 ||
       j < 0 ||
       reg < 0 ||
       reg >= T2_REGIY_MAX )
    return error_handler( RANGECHECK ) ;
  if ( t2trans->size == 0 ||
       t2regiy->size[ reg ] == 0 )
    return error_handler( UNDEFINEDRESULT ) ;
  if ( i + count > t2trans->size ||
       j + count > t2regiy->size[ reg ] )
    return error_handler( RANGECHECK ) ;

  data = t2regiy->data[ reg ] + j ;
  while ((--count) >= 0 )
    (*data++) = t2trans->data[ i++ ] ;

  t2stack->size = size - 4 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_abs( T2_CONTEXT *t2c )
{
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num = t2stack->elems[ size - 1 ] ;
  if ( num < 0.0 )
    t2stack->elems[ size - 1 ] = -num ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_add( T2_CONTEXT *t2c )
{
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  t2stack->elems[ size - 2 ] = num1 + num2 ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_sub( T2_CONTEXT *t2c )
{
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  t2stack->elems[ size - 2 ] = num1 - num2 ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_div( T2_CONTEXT *t2c )
{
  int32 size ;
  double num ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  if ( num2 == 0.0 )
    return error_handler( RANGECHECK ) ;
  num = num1 / num2 ;
  if ( num < ( 1.0 / 65536.0 ))         /* zero for underflow. */
    num = 0.0 ;
  t2stack->elems[ size - 2 ] = num ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_load( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 reg ;
  int32 count ;
  int32 size ;
  double *data ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_TRANS *t2trans = & t2c->t2trans ;
  T2_REGIY *t2regiy = & t2c->t2regiy ;

  HQFAIL("NYT: t2_load");

  size = t2stack->size ;
  count = ( int32 )t2stack->elems[ size - 1 ] ;
  i     = ( int32 )t2stack->elems[ size - 2 ] ;
  reg   = ( int32 )t2stack->elems[ size - 3 ] ;
  if ( count < 0 ||
       i < 0 ||
       reg < 0 ||
       reg >= T2_REGIY_MAX )
    return error_handler( RANGECHECK ) ;
  if ( t2trans->size == 0 ||
       t2regiy->size[ reg ] == 0 )
    return error_handler( UNDEFINEDRESULT ) ;
  if ( i + count > t2trans->size ||
       count > t2regiy->size[ reg ] )
    return error_handler( RANGECHECK ) ;

  data = t2regiy->data[ reg ] ;
  while ((--count) >= 0 )
    t2trans->data[ i++ ] = (*data++) ;

  t2stack->size = size - 3 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_neg( T2_CONTEXT *t2c )
{
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num = t2stack->elems[ size - 1 ] ;
  t2stack->elems[ size - 1 ] = -num ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_eq( T2_CONTEXT *t2c )
{
  int32 eq ;
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  if ( fabs( num1 - num2 ) < ( 1.0 / 65536.0 ))
    eq = 1 ;
  else
    eq = 0 ;
  t2stack->elems[ size - 2 ] = ( double )eq ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_drop( T2_CONTEXT *t2c )
{
  T2_STACK *t2stack = & t2c->t2stack ;

  t2stack->size-- ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_put( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_TRANS *t2trans = & t2c->t2trans ;

  size = t2stack->size ;
  i = ( int32 )t2stack->elems[ size - 1 ] ;
  if ( i < 0 ||
       i >= t2trans->size )
    return error_handler( RANGECHECK ) ;
  t2trans->data[ i ] = t2stack->elems[ size - 2 ] ;
  t2stack->size = size - 2 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_get( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_TRANS *t2trans = & t2c->t2trans ;

  size = t2stack->size ;
  i = ( int32 )t2stack->elems[ size - 1 ] ;
  if ( i < 0 ||
       i >= t2trans->size )
    return error_handler( RANGECHECK ) ;
  t2stack->elems[ size - 1 ] = t2trans->data[ i ] ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_ifelse( T2_CONTEXT *t2c )
{
  int32 size ;
  double s1_or_s2 ;
  double s1 , s2 , v1 , v2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  s1 = t2stack->elems[ size - 4 ] ;
  s2 = t2stack->elems[ size - 3 ] ;
  v1 = t2stack->elems[ size - 2 ] ;
  v2 = t2stack->elems[ size - 1 ] ;
  if ( v1 <= v2 )
    s1_or_s2 = s1 ;
  else
    s1_or_s2 = s2 ;
  t2stack->elems[ size - 4 ] = s1_or_s2 ;
  t2stack->size = size - 3 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_random( T2_CONTEXT *t2c )
{
  int32 size ;
  uint32 temp ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;
  T2_RANDOM *t2random = & t2c->t2random ;

  size = t2stack->size ;
  if ( size == T2_STACK_MAX )
    return error_handler( STACKOVERFLOW ) ;

  t2random->seed = t2random->seed * 1103515245u + 12345u ;
  temp = ( t2random->seed >> 16 ) & 0x7fff ;
  num = 1.0 - ( double )( temp % 1024 ) / 1024.0 ;

  t2stack->elems[ size ] = num ;
  t2stack->size = size + 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_mul( T2_CONTEXT *t2c )
{
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  t2stack->elems[ size - 2 ] = num1 * num2 ;
  t2stack->size = size - 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_sqrt( T2_CONTEXT *t2c )
{
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num = t2stack->elems[ size - 1 ] ;
  if ( num < 0.0 )
    return error_handler( RANGECHECK ) ;
  t2stack->elems[ size - 1 ] = sqrt( num ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_dup( T2_CONTEXT *t2c )
{
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  if ( size == T2_STACK_MAX )
    return error_handler( STACKOVERFLOW ) ;
  num = t2stack->elems[ size - 1 ] ;

  t2stack->elems[ size ] = num ;
  t2stack->size = size + 1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_exch( T2_CONTEXT *t2c )
{
  int32 size ;
  double num1 , num2 ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  num1 = t2stack->elems[ size - 2 ] ;
  num2 = t2stack->elems[ size - 1 ] ;
  t2stack->elems[ size - 2 ] = num2 ;
  t2stack->elems[ size - 1 ] = num1 ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_index( T2_CONTEXT *t2c )
{
  int32 i ;
  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;
  i = ( int32 )t2stack->elems[ size - 1 ] ;
  if ( i < 0 )
    i = 0 ;
  i += 1 ;  /* Since need to skip over index. */
  if ( i >= size )
    return error_handler( RANGECHECK ) ;

  num = t2stack->elems[ size - 1 - i ] ;
  t2stack->elems[ size - 1 ] = num ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_gcd( int32 n ,int32 j )
{
  if ( ! j )
    return ( n ) ;
  if ( ! n )
    return ( j ) ;

  return t2_gcd( j , n % j ) ;
}

static Bool t2_roll( T2_CONTEXT *t2c )
{
  int32 n , j ;
  int32 g , i , k ;
  int32 inner ;

  int32 size ;
  double num ;
  T2_STACK *t2stack = & t2c->t2stack ;

  size = t2stack->size ;

  n = ( int32 )t2stack->elems[ size - 2 ] ;
  j = ( int32 )t2stack->elems[ size - 1 ] ;
  size -= 2 ;
  if ( n < 0 ||
       n > size )
    return error_handler( RANGECHECK ) ;
  t2stack->size = size ;
  if ( n < 2 )
    return TRUE ;
  while ( j < 0 )
    j += n ;
  while ( j >= n )
    j -= n ;
  if ( ! j )
    return TRUE ;

  g = t2_gcd( n , j ) ;
  inner = n / g ;
  for ( i = 0 ; i < g ; ++i ) {
    int32 pos = i ;
    int32 newpos ;
    int32 oldpos ;
    newpos = size - 1 - pos ;
    num = t2stack->elems[ newpos ] ;
    for ( k = inner ; k > 1 ; --k ) {
      pos = pos + j ;
      if ( pos > n )
        pos -= n ;
      oldpos = newpos ;
      newpos = size - 1 - pos ;
      t2stack->elems[ oldpos ] = t2stack->elems[ newpos ] ;
    }
    t2stack->elems[ newpos ] = num ;
  }
  return TRUE ;
}


/* -------------------------------------------------------------------------- */
static Bool t2_flex( T2_CONTEXT *t2c )
{
  int32 base ;
  ch_float pts[12];
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;
  ch_float fd_thresh, fd_actual ;
  Bool horizontal;
  uint32 i ;

  base = t2stack->base ;
  HQASSERT( t2stack->size >= (base+13), "Insufficient params for flex in type2 charstring" );

  T2_START_PATH_IF_CLOSED( t2point, t2c ) ;

  pts[0] = t2point->x  + t2stack->elems[ base + 0 ] ;
  pts[1] = t2point->y  + t2stack->elems[ base + 1 ] ;
  for ( i = 2 ; i < 12 ; ++i ) {
    pts[i] = pts[i - 2] + t2stack->elems[ base + i ] ;
  }

  fd_thresh = t2stack->elems[ base + 12 ] ; /* flex depth threshold given explicitly */

  /* For flex, the line's orientation must be known.
     See p.20 of TN #5177 - the Adobe type 2 charstrings spec. */
  if (fabs( pts[8] - t2point->x ) > fabs( pts[9] - t2point->y )) {
    horizontal = TRUE;
    fd_actual = pts[11] - pts[5] ; /* Joining point Y to final point Y */
  } else {
    horizontal = FALSE;
    fd_actual = pts[10] - pts[4] ; /* Joining point X to final point X */
  }

  /* Do curve or straight line */
  if ( !(*t2c->buildfns->flex)(t2c->buildfns->data, &pts[0], &pts[6],
                               fd_actual, fd_thresh, horizontal) )
    return FALSE ;

  /* Maintain current point */
  t2point->x = pts[10];
  t2point->y = pts[11];

  return TRUE;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hflex( T2_CONTEXT *t2c )
{
  int32 base ;
  ch_float pts[12];
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  base = t2stack->base ;
  HQASSERT( t2stack->size >= (base+7), "Insufficient params for hflex in type2 charstring" );

  T2_START_PATH_IF_CLOSED( t2point , t2c ) ;

  pts[0] = t2point->x + t2stack->elems[ base + 0 ] ;
  pts[1] = t2point->y ;
  pts[2] = pts[0] + t2stack->elems[ base + 1 ] ;
  pts[3] = pts[1] + t2stack->elems[ base + 2 ] ;
  pts[4] = pts[2] + t2stack->elems[ base + 3 ] ;
  pts[5] = pts[3];

  pts[6] = pts[4] + t2stack->elems[ base + 4 ] ;
  pts[7] = pts[5];
  pts[8] = pts[6] + t2stack->elems[ base + 5 ] ;
  pts[9] = t2point->y;
  pts[10] = pts[8] + t2stack->elems[ base + 6 ] ;
  pts[11] = pts[9];

  /* Do curve or straight line */
  if ( !(*t2c->buildfns->flex)(t2c->buildfns->data, &pts[0], &pts[6],
                               pts[11] - pts[5], DEFAULT_FLEX_DEPTH,
                               TRUE /* horizontal */) )
    return FALSE ;

  /* Maintain the start point */
  t2point->x = pts[10];
  t2point->y = pts[11];

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_hflex1( T2_CONTEXT *t2c )
{
  int32 base ;
  ch_float pts[12];
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;

  base = t2stack->base ;
  HQASSERT( t2stack->size >= (base+9), "Insufficient params for hflex1 in type2 charstring" );

  T2_START_PATH_IF_CLOSED( t2point, t2c );

  pts[0] = t2point->x  + t2stack->elems[ base + 0 ] ;
  pts[1] = t2point->y  + t2stack->elems[ base + 1 ] ;
  pts[2] = pts[0] + t2stack->elems[ base + 2 ] ;
  pts[3] = pts[1] + t2stack->elems[ base + 3 ] ;
  pts[4] = pts[2] + t2stack->elems[ base + 4 ] ;
  pts[5] = pts[3];

  pts[6] = pts[4] + t2stack->elems[ base + 5 ] ;
  pts[7] = pts[5];
  pts[8] = pts[6] + t2stack->elems[ base + 6 ] ;
  pts[9] = pts[7] + t2stack->elems[ base + 7 ] ;
  pts[10] = pts[8] + t2stack->elems[ base + 8 ] ;
  pts[11] = t2point->y;

  /* Do curve or straight line */
  if ( !(*t2c->buildfns->flex)(t2c->buildfns->data, &pts[0], &pts[6],
                               pts[11] - pts[5], DEFAULT_FLEX_DEPTH,
                               TRUE /* horizontal */) )
    return FALSE ;

  /* Maintain the current point */
  t2point->x = pts[10];
  t2point->y = pts[11];

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool t2_flex1( T2_CONTEXT *t2c )
{
  int32 base ;
  ch_float pts[12];
  T2_POINT *t2point = & t2c->t2point ;
  T2_STACK *t2stack = & t2c->t2stack ;
  ch_float fd_actual ;
  Bool horizontal;

  base = t2stack->base ;
  HQASSERT( t2stack->size >= (base+11), "Insufficient params for flex1 in type2 charstring" );

  T2_START_PATH_IF_CLOSED( t2point, t2c );

  pts[0] = t2point->x  + t2stack->elems[ base + 0 ] ;
  pts[1] = t2point->y  + t2stack->elems[ base + 1 ] ;
  pts[2] = pts[0] + t2stack->elems[ base + 2 ] ;
  pts[3] = pts[1] + t2stack->elems[ base + 3 ] ;
  pts[4] = pts[2] + t2stack->elems[ base + 4 ] ;
  pts[5] = pts[3] + t2stack->elems[ base + 5 ] ;

  pts[6] = pts[4] + t2stack->elems[ base + 6 ] ;
  pts[7] = pts[5] + t2stack->elems[ base + 7 ] ;
  pts[8] = pts[6] + t2stack->elems[ base + 8 ] ;
  pts[9] = pts[7] + t2stack->elems[ base + 9 ] ;

  /* For flex1, the last point can be either an x or y depending on the line's
     orientation.  See p.20 of TN #5177 - the Adobe type 2 charstrings spec. */
  if (fabs( pts[8] - t2point->x ) > fabs( pts[9] - t2point->y )) {
    pts[10] = pts[8] + t2stack->elems[ base + 10 ] ;
    pts[11] = t2point->y;
    fd_actual = pts[11] - pts[5] ;
    horizontal = TRUE;
  } else {
    pts[10] = t2point->x;
    pts[11] = pts[9] + t2stack->elems[ base + 10 ] ;
    fd_actual = pts[10] - pts[4] ;
    horizontal = FALSE;
  }

  /* Do curve or straight line */
  if ( !(*t2c->buildfns->flex)(t2c->buildfns->data, &pts[0], &pts[6],
                               fd_actual, DEFAULT_FLEX_DEPTH,
                               horizontal) )
    return FALSE ;

  /* Maintain current point */
  t2point->x = pts[10];
  t2point->y = pts[11];

  return TRUE ;
}

void init_C_globals_charstring2(void)
{
#if defined( ASSERT_BUILD )
  debug_t2_charstrings = FALSE ;
#endif
}

/* Log stripped */
