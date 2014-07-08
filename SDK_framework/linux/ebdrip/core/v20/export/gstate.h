/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:gstate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate support functions.
 */

#ifndef __GSTATE_H__
#define __GSTATE_H__

#include "graphict.h"
#include "stacks.h"
#include "gstack.h"

/* ----- External global variables ----- */
/* Local and global lists of gstate frames that have been removed from the
   graphics stack, by gstate. */
extern GSTATE *lgframes ;
extern GSTATE *ggframes ;
/* Temporary stack of gstates removed during grestore while device
   deactivation takes place. */
extern GSTATE *grframes ;

/* ----- Exported enums ------ */
/*
 * The type of stroke cap supported by the graphics core
 */
enum
{
  BUTT_CAP     = 0,
  ROUND_CAP    = 1,
  SQUARE_CAP   = 2,
  TRIANGLE_CAP = 10
};

/*
 * The type of stroke join supported by the graphics core
 */
enum
{
  MITER_JOIN     = 0,
  ROUND_JOIN     = 1,
  BEVEL_JOIN     = 2,
  MITERCLIP_JOIN = 10,
  TRIANGLE_JOIN  = 11,
  NONE_JOIN      = 12
};

/* ----- Exported functions ----- */

/** Lock or unlock the gstate lock, returning the previous state. */
Bool gs_lock(Bool state);

Bool gs_setlinewidth( STACK *stack );
Bool gs_setlinecap( STACK *stack );
Bool gs_setlinejoin( STACK *stack );
Bool gs_setmiterlimit( STACK *stack );
Bool gs_setdash( STACK *stack , Bool retainobj );
Bool gs_fakedash( OBJECT *theo , SYSTEMVALUE *dashlist , uint16 len );
Bool gs_storedashlist( LINESTYLE *linestyle, SYSTEMVALUE *dashlist, uint16 dashlistlen );

Bool gs_setflat( STACK *stack );

void freeGStatesList( int32 slevel ) ;
Bool apply_pagebasematrix_to_all_gstates(OMATRIX *oldPageBaseMatrix,
                                         OMATRIX *newPageBaseMatrix,
                                         int32 oldPageBaseMatrixId) ;

void clear_gstate_dlpointers( void ) ;
void invalidate_gstate_screens( void );

void gs_InvalidColorForSaveLevel( void ) ;
void gs_invalidateAllColorChains( void ) ;

Bool check_gstate(corecontext_t *corecontext, GSTATE *gs ) ;

Bool gs_forall(Bool (*gs_fn)(GSTATE *, void *), void *args,
               Bool dogstate, Bool dogrframes) ;

TranState* gsTranState(GSTATE* gs);

#endif /* protection for multiple inclusion */

/*
Log stripped */
