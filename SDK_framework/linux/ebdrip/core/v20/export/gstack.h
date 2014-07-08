/** \file
 * \ingroup gstack
 *
 * $HopeName: SWv20!export:gstack.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate stack interface.
 */

#ifndef __GSTACK_H__
#define __GSTACK_H__

struct core_init_fns ; /* from SWcore */

/** \defgroup gstack Graphics stack
    \ingroup gstate */
/** \{ */

/* ----- Required headers ----- */
#include "devops.h"
#include "graphict.h" /* GSTATE */

void gstack_C_globals(struct core_init_fns *fns) ;

/* ----- Exported global variables ----- */
extern GSTATE *gstackptr ;
extern GSTATE *gstateptr ;

extern int32 gstateId ;

/* ----- Exported functions ----- */
void gs_updatePtrs(GSTATE *gs) ;

void gs_discardgstate( GSTATE *gs ) ;
void free_gstate( GSTATE *gs ) ;

Bool gs_cpush( void ) ;
Bool gs_ctop ( void ) ;

Bool gs_gpush( int32 gtype ) ;
void gs_gpop( void ) ;
Bool gs_gexch( int32 to, int32 from ) ;

Bool gs_setgstate(/*@in@*/ /*@notnull@*/ GSTATE *gs_from,
                  int32   gtype ,
                  Bool    docopy ,
                  Bool    dopop ,
                  Bool    doendpage ,
                  /*@in@*/ /*@null@*/ deactivate_pagedevice_t *dpd_val) ;

Bool gs_copygstate(/*@out@*/ /*@notnull@*/ GSTATE *gs_dst,
                   /*@in@*/ /*@notnull@*/ GSTATE *gs_src,
                   int32   gtype,
                  /*@in@*/ /*@null@*/ deactivate_pagedevice_t *dpd_val) ;

/** \} */

#endif /* protection for multiple inclusion */

/*
Log stripped */
