/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:upathops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Userpath operators
 */

#include "core.h"
#include "pscontext.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "namedef_.h"
#include "swpdfout.h"

#include "constant.h" /* BIGNUM */
#include "bitblts.h"
#include "matrix.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "pathcons.h"
#include "pathops.h"
#include "clippath.h"
#include "system.h"
#include "stacks.h"

#include "utils.h"
#include "miscops.h"
#include "params.h"
#include "psvm.h"
#include "swmemory.h"
#include "gu_ctm.h"
#include "gu_fills.h"
#include "gu_path.h"

#include "upath.h"
#include "uparse.h"
#include "upcache.h"

#include "upathenc.h"

#include "routedev.h"           /* for DEVICE_SETG */
#include "idlom.h"

#include "fileio.h"             /* needed by binscan.h */
#include "binscan.h"            /* asFloat, FOURBYTES */
#include "vndetect.h"


Bool ustrokepath_(ps_context_t *pscontext)
{
  Bool has_matrix = FALSE ;
  int32 nargs = 1 ;
  OBJECT *theo ;
  OMATRIX matrix ;
  PATHINFO upath ;
  Bool result = TRUE ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  theo = theTop(operandstack) ;
  if ( is_matrix_noerror(theo, & matrix) ) {
    if ( theStackSize(operandstack) < 1 )       /* 2 argument form */
      return error_handler( STACKUNDERFLOW ) ;
    if ( ! oCanRead(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
    has_matrix = TRUE ;
    nargs++ ;
    theo = stackindex( 1, &operandstack );
  }

  if ( ! upath_to_path(theo, UPARSE_NONE, &upath) )
    return FALSE ;

  if ( thePath(upath) ) {
    STROKE_PARAMS params ;
    PATHINFO spath ;

    set_gstate_stroke(&params, &upath, &spath, FALSE) ;

    if ( has_matrix ) {
      params.usematrix = TRUE ;
      matrix_mult( & matrix, & thegsPageCTM(*gstateptr), & params.orig_ctm ) ;
    }

    if ( (result = dostroke(&params, GSC_FILL, STROKE_NOT_VIGNETTE)) != 0 ) {
      (void)gs_newpath() ;
      thePathInfo(*gstateptr) = spath ;
    }
    path_free_list(thePath(upath), mm_pool_temp) ;      /* dispose of input path */
  }

  if ( result )
    npop( nargs, &operandstack ) ;

  return result ;
}

Bool uappend_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler( STACKUNDERFLOW ) ;

  /* Need to scan only once, to build path. Don't care about caching etc. */
  if ( ! upath_to_path(theTop(operandstack), UPARSE_APPEND,
                       & thePathInfo(*gstateptr) ) )
    return FALSE ;

  pop( &operandstack );

  return TRUE;
}

/** Calculate the bounds of the passed path, returning them in 'bbox'. If
'matrix' is not null, the points of the path will be transformed by it before
being used to construct the bounding box.

Returns the length of the path.
*/
static int32 pathBBox(PATHINFO* path, sbbox_t* bbox, OMATRIX* matrix)
{
  PATHLIST *tpath ;
  /* Size is initially 5 for the path bounding box (x1 y1 x2 y2 setbbox). */
  int32 upsize = 5;

  bbox_store(bbox, OINFINITY_VALUE, OINFINITY_VALUE,
             -OINFINITY_VALUE, -OINFINITY_VALUE) ;

  for (tpath = thePath(*path) ; tpath ; tpath = tpath->next) {
    LINELIST *tline, *pline ;

    for ( tline = theISubPath(tpath) ; tline ; tline = tline->next ) {
      int32 points = 0 ;
      int32 namenum = -1 ;

      switch ( theILineType(tline) ) {
      case CURVETO:
        points = 3 ;
        namenum = NAME_curveto ;
        break ;
      case MOVETO:
        points = 1 ;
        namenum = NAME_moveto ;
        break ;
      case LINETO:
        points = 1 ;
        namenum = NAME_lineto ;
        break ;
      case CLOSEPATH:
        points = 0 ;
        namenum = NAME_closepath ;
        break ;
      case MYMOVETO:
      case MYCLOSE:
        break ;
      default:
        HQFAIL("Invalid linetype in upath's path") ;
      }

      if ( namenum != -1 ) {
        upsize += 2 * points + 1 ;

        pline = tline ;
        while ( points-- ) {
          register SYSTEMVALUE px, py ;

          tline = pline ;

          HQASSERT(tline, "No points left on upath subpath") ;

          px = theX(theIPoint(tline)) ;
          py = theY(theIPoint(tline)) ;

          if (matrix != NULL)
            MATRIX_TRANSFORM_XY(px, py, px, py, matrix) ;

          /* Do not use bbox_union_point because it assumes the bbox
             is non-empty to start. */
          bbox_union_coordinates(bbox, px, py, px, py) ;

          pline = tline->next ;
        }
      }
    }
  }

  return upsize;
}

Bool upath_(ps_context_t *pscontext)
{
  OBJECT *obj, *olist = NULL;
  int32 cacheit, upsize ;
  sbbox_t bbox ;
  PATHLIST *tpath ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty(operandstack) )
    return error_handler( STACKUNDERFLOW ) ;

  obj = theTop(operandstack) ;

  if (oType(*obj) != OBOOLEAN)
    return error_handler(TYPECHECK);

  cacheit = oBool(*obj) ;

  if ( ! CurrentPoint )
    return error_handler( NOCURRENTPOINT ) ;

  if ( ( thePathInfo(*gstateptr)).protection )
    return error_handler( INVALIDACCESS ) ;

  /* Transform points by the inverse of the CTM. */
  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
    return error_handler( UNDEFINEDRESULT ) ;

  upsize = pathBBox(&thePathInfo(*gstateptr), &bbox, &sinv);
  /* Do we need an extra entry for 'ucache'? */
  if ( cacheit )
    upsize += 1;

  if ( upsize > 65535 )
    return error_handler(RANGECHECK) ;

  if ( NULL == (olist = get_omemory(upsize)) )
    return error_handler(VMERROR) ;

  /* re-use the top of the stack for the returned array object. Note that
     there is no chance of uninitialised objects being left in the object
     list, because this function cannot return a failure code after this
     code is executed */
  theTags(*obj) = OARRAY | EXECUTABLE | UNLIMITED ;
  SETGLOBJECT(*obj, corecontext) ;
  theLen(*obj) = (uint16)upsize ;
  oArray(*obj) = olist ;

  if ( cacheit ) {  /* add ucache operator if requested */
    object_store_name(olist++, NAME_ucache, EXECUTABLE) ;
  }

  object_store_numeric(olist++, bbox.x1) ;
  object_store_numeric(olist++, bbox.y1) ;
  object_store_numeric(olist++, bbox.x2) ;
  object_store_numeric(olist++, bbox.y2) ;

  object_store_name(olist++, NAME_setbbox, EXECUTABLE) ;

  for (tpath = thePath(thePathInfo(*gstateptr)) ; tpath ; tpath = tpath->next) {
    LINELIST *tline, *pline ;

    for ( tline = theISubPath(tpath) ; tline ; tline = tline->next ) {
      int32 points = 0 ;
      int32 namenum = -1 ;

      switch ( theILineType(tline) ) {
      case CURVETO:
        points = 3 ;
        namenum = NAME_curveto ;
        break ;
      case MOVETO:
        points = 1 ;
        namenum = NAME_moveto ;
        break ;
      case LINETO:
        points = 1 ;
        namenum = NAME_lineto ;
        break ;
      case CLOSEPATH:
        points = 0 ;
        namenum = NAME_closepath ;
        break ;
      case MYMOVETO:
      case MYCLOSE:
        break ;
      default:
        HQFAIL("Invalid linetype in upath's path") ;
      }

      if ( namenum != -1 ) {
        pline = tline ;
        while ( points-- ) {
          SYSTEMVALUE px, py ;
          SYSTEMVALUE ux, uy ;

          tline = pline ;

          HQASSERT(tline, "No points left on upath subpath") ;

          px = theX(theIPoint(tline)) ;
          py = theY(theIPoint(tline)) ;

          /* transform points into userspace */
          MATRIX_TRANSFORM_XY( px, py, ux, uy, & sinv ) ;
          object_store_numeric(olist++, ux) ;
          object_store_numeric(olist++, uy) ;

          pline = tline->next ;
        }
        object_store_name(olist++, namenum, EXECUTABLE) ;
      }
    }
  }

  return TRUE ;
}

/** Return the current path as a binary encoded user path. This consists of an
array of two binary encoded strings (or longstrings); the first string is the
'data' string, the second in the 'operand' string, as described in the
Postscript Reference Manual (3rd Edition) - section '4.6.2 Encoded User Paths'.
*/
Bool upathbinary_(ps_context_t *pscontext)
{
  Bool isLong ;
  int32 opCnt = 0, argCnt = 0, len ;
  uint8 *opStr, *argStr ;
  OBJECT array = OBJECT_NOTVM_NOTHING,
    operatorObject = OBJECT_NOTVM_NOTHING,
    argumentObject = OBJECT_NOTVM_NOTHING ;
  sbbox_t bbox ;
  PATHINFO *path = &thePathInfo(*gstateptr);

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! CurrentPoint )
    return error_handler( NOCURRENTPOINT ) ;

  if ( ( thePathInfo(*gstateptr)).protection )
    return error_handler( INVALIDACCESS ) ;

  /* Transform points by the inverse of the CTM. */
  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
    return error_handler( UNDEFINEDRESULT ) ;

  pathBBox(path, &bbox, &sinv);

  /* Encoded format has pairs of args, operators as strings. */
  if ( !upathEncBinaryCount(path, &opCnt, &argCnt) ||
       !ps_array(&array, 2) ||
       !ps_long_or_normal_string(&operatorObject, NULL, opCnt) )
    return FALSE ;

  len = argCnt * sizeof(float) + 4 ; /* Size for HNA */
  if ( len > MAXPSSTRING )
    len += 4 ; /* extra for Ext-HNA */

  /* Create argument string */
  if ( !ps_long_or_normal_string(&argumentObject, NULL, len) )
    return FALSE ;

  OCopy(oArray(array)[0], argumentObject) ;
  OCopy(oArray(array)[1], operatorObject) ;

  /* Populate the operand string. */
  if ( oType(argumentObject) == OLONGSTRING ) {
    argStr = theLSCList(*oLongStr(argumentObject)) ;
    isLong = TRUE ;
  } else {
    HQASSERT(oType(argumentObject) == OSTRING,
             "Argument string is not OSTRING or OLONGSTRING") ;
    argStr = oString(argumentObject) ;
    isLong = FALSE ;
  }

  if ( oType(operatorObject) == OLONGSTRING ) {
    opStr = theLSCList(*oLongStr(operatorObject)) ;
  } else {
    HQASSERT(oType(operatorObject) == OSTRING,
             "Argument string is not OSTRING or OLONGSTRING") ;
    opStr = oString(operatorObject) ;
  }

  /* Populate the argument string. */
  upathEncBinaryHeader((USERVALUE *)argStr, argCnt, isLong) ;
  argStr += 4 ;
  if ( isLong )
    argStr += 4 ;

  if ( ! upathEncBinaryData(&bbox, path, &sinv, opStr, opCnt,
                            (USERVALUE *)argStr, argCnt) )
    return FALSE ;

  return push(&array, &operandstack);
}

/* path_to_binupath makes a binary-encoded userpath from the given PATHINFO,
 *    and returns pointers and length counts for the two strings.  The strings
 *    are allocated from transient memory and must be fred by the client.
 */
#ifndef FLT_MAX
#define FLT_MAX   1E+37         /* min. ANSI maximal float value */
#define FLT_MIN   1E-37         /* max. ANSI minimal float value */
#endif



typedef struct _upathEncodeOp {
  int32 opname  ; /* PS operator that linetype maps onto */
  uint8 upathop ; /* upath operater that linetype maps onto */
  int8  numargs ; /* number of arguments for operator */
  int8  endpath ; /* does operator end this sub path? */
  int8  validop ; /* is this a valid mapping anyway? */
} upathEncodeOp ;

enum {
  OpStrip = -1,  /* Op is valid but not encoded (i.e. stripped from path) */
  OpInvalid = 0, /* Op is not valid, and an error will be generated */
  OpValid        /* Op is valid and will be appropriately encoded */
} ;

#define upathEncode_MAXOPS  UPATH_MYCLOSE + 1

upathEncodeOp upathEncodeOpTable[ upathEncode_MAXOPS ] = {
  /* INVALID   */ { NAME_setbbox,   UPATH_SETBBOX,   4, FALSE, OpInvalid },
  /* MOVETO    */ { NAME_moveto,    UPATH_MOVETO,    2, FALSE, OpValid   },
  /* INVALID   */ { NAME_rmoveto,   UPATH_RMOVETO,   2, FALSE, OpInvalid },
  /* LINETO    */ { NAME_lineto,    UPATH_LINETO,    2, FALSE, OpValid   },
  /* INVALID   */ { NAME_rlineto,   UPATH_RLINETO,   2, FALSE, OpInvalid },
  /* CURVETO   */ { NAME_curveto,   UPATH_CURVETO,   6, FALSE, OpValid   },
  /* INVALID   */ { NAME_rcurveto,  UPATH_RCURVETO,  6, FALSE, OpInvalid },
  /* INVALID   */ { NAME_arc,       UPATH_ARC,       5, FALSE, OpInvalid },
  /* INVALID   */ { NAME_arcn,      UPATH_ARCN,      5, FALSE, OpInvalid },
  /* INVALID   */ { NAME_arct,      UPATH_ARCT,      5, FALSE, OpInvalid },
  /* CLOSEPATH */ { NAME_closepath, UPATH_CLOSEPATH, 0, TRUE,  OpValid   },
  /* INVALID   */ { NAME_ucache,    UPATH_UCACHE,    0, FALSE, OpInvalid },
  /* MYMOVETO  */ { NAME_undefined, UPATH_MYMOVETO,  0, FALSE, OpStrip   },
  /* MYCLOSE   */ { NAME_undefined, UPATH_MYCLOSE,   0, TRUE,  OpStrip   }
} ;

#ifdef highbytefirst
#define REAL_ENDIAN_NATIVE HNA_REP_IEEE_HI
#else
#define REAL_ENDIAN_NATIVE HNA_REP_IEEE_LO
#endif

static Bool upathEncCount( PATHINFO *path, int32 doRep, int32 *opCnt, int32 *argCnt )
{
  PATHLIST *subpath ;
  LINELIST *line ;
  int32 opCtr, argCtr ;
  uint8 repOp = 0 ;
  int32 repCtr = 0 ;

  HQASSERT( path,   "upathEncCount: path is NULL" ) ;
  HQASSERT( argCnt, "upathEncCount: argCnt is NULL" ) ;
  HQASSERT( opCnt,  "upathEncCount: opCnt is NULL" ) ;

  if ( ! path->firstpath ) { /* no path */
    *argCnt = 0 ;
    *opCnt = 0 ;
    return TRUE ;
  }

  /* encoded path starts with a setbbox */
  opCtr = 1 ;
  argCtr = 4 ;
  if ( doRep ) {
    repOp = UPATH_SETBBOX ;
    repCtr = 1 ;
  }

  for ( subpath = path->firstpath; subpath; subpath = subpath->next) {
    int32 argsleft = 0 ;
#if defined( ASSERT_BUILD )
    uint8 prevPathOp = ( uint8 )-1 ;
#endif
    for ( line = theISubPath( subpath ); line; line = line->next) {
      uint8 pathOp = theILineType( line ) ;

      if ( pathOp >= upathEncode_MAXOPS ||
           upathEncodeOpTable[ pathOp ].validop == OpInvalid )
        return error_handler( UNREGISTERED ) ;

      if ( upathEncodeOpTable[ pathOp ].validop != OpStrip ) {
        if ( argsleft == 0 ) {
          /* Count arguments */
          argsleft = upathEncodeOpTable[ pathOp ].numargs ;
          argCtr += argsleft ;
#if defined( ASSERT_BUILD )
          prevPathOp = pathOp ;
          HQASSERT( argsleft % 2 == 0, "upathEncCount: path op with odd numargs" ) ;
#endif
        }
#if defined( ASSERT_BUILD )
        else {
          HQASSERT( prevPathOp == pathOp, "upathEncCount: prevPathOp missed some args" ) ;
        }
#endif

        if ( argsleft != 0 )
          argsleft -= 2 ;

        if ( argsleft == 0 ) {
          /* Count operator */
          if ( ! doRep ) {
            opCtr++ ;
          }
          else if ( pathOp == repOp && repCtr < MAX_UPATH_REPS ) {
            repCtr++ ;
            if ( repCtr == 2 ) /* only use repeat codes for runs >= 2 */
              opCtr++ ;
          }
          else {
            opCtr++ ;
            repOp = pathOp ;
            repCtr = 1 ;
          }
        }
      }
    }
  }

  *opCnt = opCtr ;
  *argCnt = argCtr ;
  return TRUE ;
}

Bool upathEncBinaryCount( PATHINFO *path, int32 *opCnt, int32 *argCnt )
{
  HQASSERT( path,   "upathEncBinaryCount: path is NULL" ) ;
  HQASSERT( opCnt,  "upathEncBinaryCount: opCnt is NULL" ) ;
  HQASSERT( argCnt, "upathEncBinaryCount: argCnt is NULL" ) ;

  return upathEncCount( path, TRUE, opCnt, argCnt ) ;
}


Bool upathEncAsciiCount( PATHINFO *path, int32 *objCnt )
{
  int32 opCnt = 0, argCnt = 0 ;

  HQASSERT( path,   "upathEncAsciiCount: path is NULL" ) ;
  HQASSERT( objCnt, "upathEncAsciiCount: objCnt is NULL" ) ;

  if ( ! upathEncCount( path, FALSE, & opCnt, & argCnt ))
    return FALSE ;

  *objCnt = opCnt + argCnt ;
  return TRUE ;
}


void upathEncBinaryHeader( USERVALUE *argStr, int32 argCnt, Bool isLong)
{
  HQASSERT( argStr, "upathEncBinaryHeader: argStr is NULL" ) ;
  HQASSERT( argCnt > 0, "upathEncBinaryHeader: argCnt should be > 0" ) ;

  if ( isLong ) {
    *(( uint8 * )argStr ) = BINTOKEN_EXTHNA ;        /* HQN Extended-HNA... */
    *(( uint8 * )argStr + 1 ) = REAL_ENDIAN_NATIVE ; /* ...of IEEE reals... */
    *(( int16 * )argStr + 1 ) = 0 ;                  /* ...dummmy bytes...  */
    *(( int32 * )argStr + 1 ) = argCnt ;             /*  ...of argCnt items */
  }
  else {
    *(( uint8 * )argStr ) = BINTOKEN_HNA ;    /* Homogenous number array... */
    *(( uint8 * )argStr + 1 ) = REAL_ENDIAN_NATIVE ; /* ...of IEEE reals... */
    *(( uint16 * )argStr + 1 ) = ( uint16 )argCnt ;  /*  ...of argCnt items */
  }
}


Bool upathEncBinaryData( sbbox_t *bbox, PATHINFO *path, OMATRIX *transform,
                          uint8 *opStr, int32 opCnt,
                          USERVALUE *argStr, int32 argCnt )
{
  PATHLIST *subpath ;
  LINELIST *line ;
  uint8 repOp ;
  uint8 *opPtr = NULL ; /* to appease compiler */
  int32 repCtr ;
  SYSTEMVALUE ptX, ptY ;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( int32, opCnt ) ;
  UNUSED_PARAM( int32, argCnt ) ;
#endif

  HQASSERT( bbox,       "upathEncBinaryData: bbox is NULL" ) ;
  HQASSERT( path,       "upathEncBinaryData: path is NULL" ) ;
  HQASSERT( opStr,      "upathEncBinaryData: opStr is NULL" ) ;
  HQASSERT( opCnt > 0,  "upathEncBinaryData: opLen should be non-zero" ) ;
  HQASSERT( argStr,     "upathEncBinaryData: argStr is NULL" ) ;
  HQASSERT( argCnt > 0, "upathEncBinaryData: argLen should be non-zero" ) ;

  /* bbox is already assumed to be transformed */
#if defined( ASSERT_BUILD )
  opCnt-- ;
  HQASSERT( opCnt >= 0, "upathEncBinaryData: operator string too short" ) ;
  argCnt -= 4 ;
  HQASSERT( argCnt >= 0, "upathEncBinaryData: argument string too short" ) ;
#endif
  *( argStr++ ) = NativeToIEEE(( USERVALUE )bbox->x1 ) ;
  *( argStr++ ) = NativeToIEEE(( USERVALUE )bbox->y1 ) ;
  *( argStr++ ) = NativeToIEEE(( USERVALUE )bbox->x2 ) ;
  *( argStr++ ) = NativeToIEEE(( USERVALUE )bbox->y2 ) ;
  *( opStr++ ) = UPATH_SETBBOX ;
  repOp = UPATH_SETBBOX ;
  repCtr = 1 ;

  for ( subpath = path->firstpath; subpath; subpath = subpath->next) {
    int32 argsleft = 0 ;
#if defined( ASSERT_BUILD )
    uint8 prevPathOp = ( uint8 )-1 ;
#endif
    for ( line = theISubPath( subpath ); line; line = line->next) {
      uint8 pathOp = theILineType( line ) ;

      if ( pathOp >= upathEncode_MAXOPS ||
           upathEncodeOpTable[ pathOp ].validop == OpInvalid )
        return error_handler( UNREGISTERED ) ;

      if ( upathEncodeOpTable[ pathOp ].validop != OpStrip ) {
        /* Write arguments */
        if ( argsleft == 0 ) {
          argsleft = upathEncodeOpTable[ pathOp ].numargs ;
#if defined( ASSERT_BUILD )
          prevPathOp = pathOp ;
          argCnt -= argsleft ;
          HQASSERT( argCnt >= 0, "upathEncBinaryData: argument string too short" ) ;
          HQASSERT( argsleft % 2 == 0, "upathEncBinaryData: path op with odd numargs" ) ;
#endif
        }
#if defined( ASSERT_BUILD )
        else {
          HQASSERT( prevPathOp == pathOp, "upathEncBinaryData: prevPathOp missed some args" ) ;
        }
#endif

        if ( argsleft != 0 ) {
          if ( transform ) {
            MATRIX_TRANSFORM_XY( theX( theIPoint( line )),
                                 theY( theIPoint( line )),
                                 ptX, ptY, transform ) ;
          }
          else {
            ptX = theX( theIPoint( line )) ;
            ptY = theY( theIPoint( line )) ;
          }
          *( argStr++ ) = NativeToIEEE(( USERVALUE )ptX ) ;
          *( argStr++ ) = NativeToIEEE(( USERVALUE )ptY ) ;
          argsleft -= 2 ;
        }

        if ( argsleft == 0 ) {
          /* If we've seen this operator recently, then increment repeat
           * count, and write new value, and re-write operator
           */
          if ( pathOp == repOp && repCtr < MAX_UPATH_REPS ) {
            repCtr++ ;
            if ( repCtr == 2 ) { /* only use repeat codes for runs >= 2 */
              HQASSERT( --opCnt >= 0, "upathEncBinaryData: operator string too short" ) ;
              *( opStr++ ) = upathEncodeOpTable[ pathOp ].upathop ;
            }
            *( opPtr ) = ( uint8 )( repCtr + 32 ) ;
          }
          else {
            opPtr = opStr ;
            HQASSERT( --opCnt >= 0, "upathEncBinaryData: operator string too short" ) ;
            *( opStr++ ) = upathEncodeOpTable[ pathOp ].upathop ;
            repOp = pathOp ;
            repCtr = 1 ;
          }
        }
      }
    }
  }

  HQASSERT( opCnt == 0,  "upathEncBinaryData: operator string too long" ) ;
  HQASSERT( argCnt == 0, "upathEncBinaryData: argument string too long" ) ;
  return TRUE ;
}


Bool upathEncAsciiData( sbbox_t *bbox, PATHINFO *path, OMATRIX *transform,
                         OBJECT *objList, int32 objCnt )
{
  PATHLIST *subpath ;
  LINELIST *line ;
  SYSTEMVALUE ptX, ptY ;

#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( int32, objCnt ) ;
#endif

  HQASSERT( bbox,       "upathEncAsciiData: bbox is NULL" ) ;
  HQASSERT( path,       "upathEncAsciiData: path is NULL" ) ;
  HQASSERT( objList,    "upathEncAsciiData: objList is NULL" ) ;
  HQASSERT( objCnt > 0, "upathEncAsciiData: objCnt should be non-zero" ) ;

  /* bbox is already assumed to be transformed */
#if defined( ASSERT_BUILD )
  objCnt -= 5 ;
  HQASSERT( objCnt >= 0, "upathEncAsciiData: object list too short" ) ;
#endif
  object_store_numeric(objList++, bbox->x1) ;
  object_store_numeric(objList++, bbox->y1) ;
  object_store_numeric(objList++, bbox->x2) ;
  object_store_numeric(objList++, bbox->y2) ;
  object_store_name(objList++, NAME_setbbox, EXECUTABLE) ;

  for ( subpath = path->firstpath; subpath; subpath = subpath->next) {
    int32 argsleft = 0 ;
#if defined( ASSERT_BUILD )
    uint8 prevPathOp = ( uint8 )-1 ;
#endif
    for ( line = theISubPath( subpath ); line; line = line->next) {
      uint8 pathOp = theILineType( line ) ;

      if ( pathOp >= upathEncode_MAXOPS ||
           upathEncodeOpTable[ pathOp ].validop == OpInvalid )
        return error_handler( UNREGISTERED ) ;

      if ( upathEncodeOpTable[ pathOp ].validop != OpStrip ) {
        /* Write arguments */
        if ( argsleft == 0 ) {
          argsleft = upathEncodeOpTable[ pathOp ].numargs ;
#if defined( ASSERT_BUILD )
          prevPathOp = pathOp ;
          objCnt-- ;
          objCnt -= argsleft ;
          HQASSERT( objCnt >= 0, "upathEncAsciiData: object list too short" ) ;
          HQASSERT( argsleft % 2 == 0, "upathEncAsciiData: path op with odd numargs" ) ;
#endif
        }
#if defined( ASSERT_BUILD )
        else {
          HQASSERT( prevPathOp == pathOp, "upathEncAsciiData: prevPathOp missed some args" ) ;
        }
#endif

        if ( argsleft != 0 ) {
          if ( transform ) {
            MATRIX_TRANSFORM_XY( theX( theIPoint( line )),
                                 theY( theIPoint( line )),
                                 ptX, ptY, transform ) ;
          }
          else {
            ptX = theX( theIPoint( line )) ;
            ptY = theY( theIPoint( line )) ;
          }
          object_store_numeric(objList++, ptX) ;
          object_store_numeric(objList++, ptY) ;
          argsleft -= 2 ;
        }

        if ( argsleft == 0 ) {
          /* Write operator */
          object_store_name(objList++, upathEncodeOpTable[pathOp].opname, EXECUTABLE) ;
        }
      }
      HQASSERT(( argsleft == 0 && upathEncodeOpTable[ pathOp ].endpath ) ^
               ( line->next != NULL ),
               "upathEncAscii: end of path does not coincide with operator containing endpath" ) ;
    }
  }

  HQASSERT( objCnt == 0, "upathEncAscii: object list too long" ) ;
  return TRUE ;
}



/* Log stripped */
