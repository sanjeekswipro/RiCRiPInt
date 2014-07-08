/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:uparse.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Userpath parsing functions, to turn userpath into internal representation
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "constant.h" /* DEG_TO_RAD */


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
#include "control.h"

#include "gu_cons.h"
#include "gu_path.h"
#include "gu_ctm.h"
#include "miscops.h"
#include "rectops.h"

#include "upath.h"

#include "uparse.h"

#include "ndisplay.h"


typedef struct {
  PATHINFO *userpath ;
  Bool uparsecached ;
  int32 parse_state ;
} userpath_parse_t ;

static Bool parse_normal_upath(OBJECT *theo, userpath_parse_t *state) ;

static Bool parse_encoded_upath(OBJECT *argobj, OBJECT *opobj,
                                userpath_parse_t *state) ;

/* Userpath parsing states. These keep track of whether ucache and setbbox are
   allowed */
enum {
  PARSE_UPATH_START = 1,       /**< At very start. */
  PARSE_UPATH_UCACHE,          /**< After ucache. */
  PARSE_UPATH_SETBBOX,         /**< After setbbox. */
  PARSE_UPATH_MOVETO           /**< After moveto. */
} ;

#define UPATH_REPEAT            32      /* repeat counts 32-255 */
#define UPATH_INVALID           -1      /* can't occur in userpath string */


/* names corresponding to encoded bytes */
static int32 encoded_name[] = {
  NAME_setbbox, NAME_moveto, NAME_rmoveto, NAME_lineto, NAME_rlineto,
  NAME_curveto, NAME_rcurveto, NAME_arc, NAME_arcn, NAME_arct,
  NAME_closepath, NAME_ucache
} ;

/* can't have more than this number of args to a single operator (curveto)
   was 6, changed to 8 since arct to curveto returns both lineto and curveto
   args in the array */
#define MAX_UPARSE_ARGS 8       /* maximum value from encoded_args array */

/* numbers of arguments for encoded operators */
static int32 encoded_args[] = { 4, 2, 2, 2, 2, 6, 6, 5, 5, 5, 0, 0 } ;


/* Top level userpath parsing routines. These are structured in a slightly
   baroque way, in which the top level routine initialises some static
   variables, passes a pair of routines into the main parsing routine, and
   tidies up any problems afterwards. This is so that the homogeneous number
   arrays used by the encoded userpaths do not need to be decoded multiple
   times if we require multiple passes over the data, while still sharing as
   much decoding and normalisation code as possible. */

/** upath_to_path parses a userpath object directly into a pathlist. This is
   used by the userpath operators that require a path, but not an internal
   representation. We have to return a success code rather than the path,
   because an empty path is  valid. We have to save some of the current
   graphics state and restore it afterwards; this is a lot lighterweight than
   a gsave/grestore, so we just do it in local variables. */
Bool upath_to_path(OBJECT *theo, int32 flags, PATHINFO *ppath)
{
  Bool result, cached ;
  SYSTEMVALUE oldtx = thegsPageCTM(*gstateptr).matrix[2][0] ;
  SYSTEMVALUE oldty = thegsPageCTM(*gstateptr).matrix[2][1] ;
  Bool oldcheckbbox = checkbbox ;
  PATHINFO userpath ;

  HQASSERT(theo != NULL, "Object pointer NULL in upath_to_path") ;
  HQASSERT(ppath != NULL, "Pathinfo pointer NULL in upath_to_path") ;

  /* set up path-building state */

  /* round translation to nearest value; doesn't check for overflowing
     integer limits */
  SC_RINTF( thegsPageCTM(*gstateptr).matrix[2][0] , thegsPageCTM(*gstateptr).matrix[2][0] )  ;
  SC_RINTF( thegsPageCTM(*gstateptr).matrix[2][1] , thegsPageCTM(*gstateptr).matrix[2][1] )  ;
  newctmin |= NEWCTM_TCOMPONENTS ;

  if ( (flags & UPARSE_APPEND) != 0 )
    userpath = *ppath ;         /* copy old values */
  else
    path_init(&userpath) ;
  checkbbox = TRUE ;

  /* run userpath parser */
  result = parse_userpath(theo, &userpath, &cached) ;

  if ( result && (flags & UPARSE_CLOSE) != 0 )
    result = path_close(MYCLOSE, &userpath) ;

  /* restore original state */
  thegsPageCTM(*gstateptr).matrix[2][0] = oldtx ;
  thegsPageCTM(*gstateptr).matrix[2][1] = oldty ;
  newctmin |= NEWCTM_TCOMPONENTS ;
  checkbbox = oldcheckbbox ;

  if ( ! result ) {     /* tidy up path */
    if ( (flags & UPARSE_APPEND) != 0 ) { /* had a path before, free trailing part */
      register LINELIST *theline, *nextline ;
      register PATHLIST *thepath, *nextpath ;

      if ( (theline = ppath->lastline) != NULL ) {
        nextline = theline->next ;
        theline->next = NULL ;

        while ( nextline ) {
          theline = nextline ;
          nextline = theline->next ;
          free_line(theline, mm_pool_temp) ;
        }

        thepath = ppath->lastpath ;
        if ( (nextpath = thepath->next) != NULL ) {
          thepath->next = NULL ;
          path_free_list(nextpath, mm_pool_temp) ;      /* free all subpaths */
        }
      }
    } else if ( thePath(userpath) )
      path_free_list(thePath(userpath), mm_pool_temp) ;
  } else {
    /* There have been unreproducible reports that some 3011 RIPs no longer
       preserve the original bbox around userpaths. If one is found then the
       following two statements should be skipped, possibly on a userparam
       to allow pre-3011 RIP behaviour */
    /* Restore original path bbox */
    userpath.bboxtype = ppath->bboxtype;
    userpath.bbox = ppath->bbox;
    *ppath = userpath ;
  }

  return result ;
}

static Bool normalise_op(int32 name, int32 nargs, SYSTEMVALUE *args,
                         userpath_parse_t *state)
{
  HQASSERT( name != 0, "normalise_op called with bogus operator name") ;
  HQASSERT( nargs >= 0, "normalise_op called with negative number of args") ;
  HQASSERT( args, "normalise_op called with NULL args array") ;
  HQASSERT(state, "No userpath parse state") ;

  switch ( name ) {
  case NAME_ucache:
    if ( state->parse_state != PARSE_UPATH_START || nargs != 0 )
      return error_handler(TYPECHECK) ;
    state->parse_state = PARSE_UPATH_UCACHE ;
    state->uparsecached = TRUE ;
    return TRUE ;       /* no ucache operator callback */
  case NAME_setbbox:
    if ( state->parse_state == PARSE_UPATH_SETBBOX ||
         state->parse_state == PARSE_UPATH_MOVETO ||
         nargs != 4 )
      return error_handler(TYPECHECK) ;
    if ( args[2] < args[0] || args[3] < args[1] )
      return error_handler(RANGECHECK) ;
    state->parse_state = PARSE_UPATH_SETBBOX ;
    return set_bbox(args, state->userpath) ;
  case NAME_moveto:
    if ( state->parse_state == PARSE_UPATH_SETBBOX )
      state->parse_state = PARSE_UPATH_MOVETO ;
  case NAME_rmoveto:
    if ( nargs != 2 || (state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    return gs_moveto((name == NAME_moveto), args, state->userpath) ;
  case NAME_rlineto:
  case NAME_lineto:
    if ( nargs != 2 || (state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    return gs_lineto((name == NAME_lineto), TRUE, args, state->userpath) ;
  case NAME_rcurveto:
  case NAME_curveto:
    if ( nargs != 6 || (state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    return gs_curveto((name == NAME_curveto), TRUE, args, state->userpath) ;
  case NAME_arct:
    if ( nargs != 5 || (state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    if ( ! (*state->userpath).lastline )
      return error_handler(NOCURRENTPOINT) ;

    {
      SYSTEMVALUE dev_x, dev_y ;
      int32 flags;

      dev_x = theX(theIPoint((*state->userpath).lastline)) ;
      dev_y = theY(theIPoint((*state->userpath).lastline)) ;

      SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
      if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS ) )
        return error_handler( UNDEFINEDRESULT ) ;

      MATRIX_TRANSFORM_XY( dev_x, dev_y, dev_x, dev_y, & sinv ) ;

      if ( !arct_convert(dev_x, dev_y, args, NULL, &flags) ) {
        return FALSE;
      }

      if ( (flags&(ARCT_ARC|ARCT_ARCN)) != 0 ) {
        /* Arct uses an arc or arcn - looks after any lineto */
        return gs_arcto(flags, TRUE, args, state->userpath);
      }

      /* Arct uses a lineto/curveto combination */
      if ( (flags&ARCT_LINETO) != 0 ) {
        if ( !gs_lineto(TRUE, TRUE, args, state->userpath) ) {
          return FALSE;
        }
      }
      if ( (flags&ARCT_CURVETO) != 0 ) {
        if ( !gs_curveto(TRUE, TRUE, &args[2], state->userpath) ) {
          return FALSE ;
        }
      }
      return TRUE;
    }
  case NAME_arcn:
  case NAME_arc:
    if ( nargs != 5  ||
         (state->parse_state != PARSE_UPATH_SETBBOX &&
          state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    {   /* currentpoint set before first arc, close off previous path */
      int32 flags = 0;
      if ( state->parse_state != PARSE_UPATH_MOVETO &&
           (*state->userpath).lastline ) {
        flags |= ARCT_MOVETO;
      } else {
        flags |= ARCT_LINETO;
      }
      state->parse_state = PARSE_UPATH_MOVETO ;
      args[3] *= DEG_TO_RAD ;
      args[4] *= DEG_TO_RAD ;
      if ( name == NAME_arc ) {
        flags |= ARCT_ARC;
      } else {
        flags |= ARCT_ARCN;
      }
      return(gs_arcto(flags, TRUE, args, state->userpath));
    }
  case NAME_closepath:
    if ( nargs != 0  || (state->parse_state != PARSE_UPATH_MOVETO) )
      return error_handler(TYPECHECK) ;
    return path_close(CLOSEPATH, state->userpath) ;
  }

  return error_handler(TYPECHECK) ;     /* didn't recognise op type */
}


/** The main user path parsing routine. This scans the userpath object, and
   turns it into a PATHINFO structure, allocated out of temporary memory rather
   than system path caches.
*/

Bool parse_userpath(OBJECT *theo, PATHINFO *path, Bool *cached)
{
  int32 len ;
  Bool result ;
  userpath_parse_t state ;

  HQASSERT( theo, "parse_userpath called with NULL object pointer") ;

  len = theILen(theo) ;

  /* initial type check; userpaths must be arrays of either 2 or 5 or more
     elements */
  if ( (oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY) ||
       (len < 5 && len != 2) )
    return error_handler(TYPECHECK) ;

  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler(INVALIDACCESS) ;

  /* uconcat_ is special; it can take a normal userpath with setbbox and ucache,
     or just a list of operators and arguments */
  state.userpath = path ;
  state.parse_state = PARSE_UPATH_START ;
  state.uparsecached = FALSE ;

  /* now do the actual parsing differently for encoded and unencoded paths */
  if ( len == 2 )
    result = parse_encoded_upath(&oArray(*theo)[0], &oArray(*theo)[1], &state) ;
  else
    result = parse_normal_upath(theo, &state) ;

  *cached = state.uparsecached ;

  return result ;
}

/** Parse a normal userpath. */
static Bool parse_normal_upath(OBJECT *theo, userpath_parse_t *state)
{
  SYSTEMVALUE args[MAX_UPARSE_ARGS] ;
  OBJECT *olist ;
  int32 nargs = 0 ;
  int32 nops ;

  HQASSERT( theo && (oType(*theo) == OARRAY ||
                     oType(*theo) == OPACKEDARRAY) &&
            theILen(theo) >= 5,
            "parse_normal_upath parameter isn't an array of length >= 5" ) ;

  for (olist = oArray(*theo), nops = theILen(theo) ;
       nops ;
       olist++, nops-- ) {
    NAMECACHE *name ;

    switch ( oType(*olist) ) {
    case OINTEGER:      /* just stuff these onto the stack */
      if ( nargs >= MAX_UPARSE_ARGS )
        return error_handler(TYPECHECK) ;
      if ( state->parse_state == PARSE_UPATH_START )   /* don't allow ucache now */
        state->parse_state = PARSE_UPATH_UCACHE ;
      args[nargs++] = (SYSTEMVALUE)oInteger(*olist) ;
      break ;
    case OREAL:
      if ( nargs >= MAX_UPARSE_ARGS )
        return error_handler(TYPECHECK) ;
      if ( state->parse_state == PARSE_UPATH_START )   /* don't allow ucache now */
        state->parse_state = PARSE_UPATH_UCACHE ;
      args[nargs++] = (SYSTEMVALUE)oReal(*olist) ;
      break ;
    case OOPERATOR:
    case ONAME:
      if ( oType(*olist) == ONAME )
        name = oName(*olist) ;
      else
        name = theIOpName(oOp(*olist));
      if ( ! normalise_op(theINameNumber(name), nargs, args, state) )
        return FALSE ;
      nargs = 0 ;
      break ;
    default:
      return error_handler(TYPECHECK) ;
    }
  }

  if ( nargs )          /* check if invalid left over args */
    return error_handler(TYPECHECK) ;

  return TRUE ;
}

/** Parse an encoded userpath. There are two alternatives within this routine,
   for strings and for number arrays. The number array variant doesn't allocate
   the whole argument array at once. */
static Bool parse_encoded_upath(OBJECT *argobj, OBJECT *opobj,
                                userpath_parse_t *state)
{
  SYSTEMVALUE *numbers = NULL ;
  SYSTEMVALUE *numlist = NULL ;
  OBJECT *arglist = NULL ;
  int32 nargs, len ;
  uint8 *oplist ;
  SYSTEMVALUE args[MAX_UPARSE_ARGS] ;
  int32 repeat = 0 ;    /* repeat count of zero indicates no repeat */

  HQASSERT(argobj != NULL, "parse_encoded_upath called with NULL argobj") ;
  HQASSERT(opobj != NULL, "parse_encoded_upath called with NULL opobj") ;

  if ( oType(*opobj) != OSTRING && oType(*opobj) != OLONGSTRING )
    return error_handler(TYPECHECK) ;

  switch ( oType(*argobj) ) {
  case OSTRING:
  case OLONGSTRING:
    nargs = 0 ; /* No vector pre-allocated */
    if ( ! decode_number_string( argobj, &numbers, &nargs, 1 ))
      return FALSE ;
    numlist = numbers ;
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    nargs = theILen(argobj) ;
    arglist = oArray(*argobj) ;
    break ;
  default:
    return error_handler(TYPECHECK) ;
  }

  if ( (!oCanRead(*argobj) && !object_access_override(argobj)) ||
       (!oCanRead(*opobj) && !object_access_override(opobj)) )
    return error_handler(INVALIDACCESS) ;

  if ( oType(*opobj) == OLONGSTRING ) {
    oplist = theILSCList(oLongStr(*opobj)) ;
    len = theILSLen(oLongStr(*opobj)) ;
  }
  else { /* OSTRING */
    oplist = oString(*opobj) ;
    len = theILen(opobj) ;
  }
  for ( /* init above */ ; len ;  oplist++, len-- ) {
    uint8 opcode = *oplist ;
    int32 opargs ;

    if ( opcode > UPATH_REPEAT ) {
      if ( repeat ) {                   /* already got a repeat count */
        if ( numbers )
          mm_free_with_header(mm_pool_temp, numbers) ;
        return error_handler(TYPECHECK) ;
      }
      repeat = opcode - UPATH_REPEAT ;
    } else
      switch ( opcode ) {
      case UPATH_SETBBOX:
      case UPATH_MOVETO:
      case UPATH_RMOVETO:
      case UPATH_LINETO:
      case UPATH_RLINETO:
      case UPATH_CURVETO:
      case UPATH_RCURVETO:
      case UPATH_ARC:
      case UPATH_ARCN:
      case UPATH_ARCT:
      case UPATH_CLOSEPATH:
      case UPATH_UCACHE:
        opargs = encoded_args[opcode] ;
        do {
          int32 index ;

          if ( opargs > nargs ) {       /* check that we've got enough left */
            if ( numbers )
              mm_free_with_header(mm_pool_temp, numbers) ;
            return error_handler( TYPECHECK ) ;
          }

          for (index = 0 ; index < opargs ; index++ )
            if ( numlist )
              args[index] = *numlist++ ;
            else if ( !object_get_numeric(arglist++, &args[index]) )
              return FALSE ;

          if ( ! normalise_op(encoded_name[opcode], opargs, args, state) ) {
            if ( numbers )
              mm_free_with_header(mm_pool_temp, numbers) ;
            return FALSE ;
          }
          nargs -= opargs ;
        } while ( --repeat > 0 ) ;
        repeat = 0 ;            /* reset for next iteration */
        break ;
      default:  /* unknown opcode */
        if ( numbers )
          mm_free_with_header(mm_pool_temp, numbers) ;
        return error_handler(TYPECHECK) ;
      }
  }

  if ( numbers )
    mm_free_with_header(mm_pool_temp, numbers) ;

  if ( nargs || repeat )        /* check if invalid left over op or args */
    return error_handler(TYPECHECK) ;

  return TRUE ;
}



/* Log stripped */
