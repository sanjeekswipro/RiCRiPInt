/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:miscops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Miscellaneous PostScript operators.
 */

#ifndef MISCOPS_H
#define MISCOPS_H


extern Bool bind_automatically(corecontext_t *context, OBJECT *proc );
extern OBJECT * name_is_operator( OBJECT *name );
extern void  initUserRealTime( void );
extern void  defshadowproc ( OBJECT * o1, OBJECT ** o2p);
extern Bool xsuperexec(ps_context_t *pscontext);
extern Bool in_super_exec(void);

extern void  icache_init( void ) ;
extern void  icache_purge( int32 slevel ) ;
extern Bool run_ps_string_len (uint8* ps, int32 len);
extern Bool run_ps_string ( uint8 * ps ) ;

/** Return a new UniqueID.

 * These will eventually wrap around, but are discarded at the end of each job
 * anyway. */
int32 getnewuniqueid(void);


#endif /* MISCOPS_H multiple inclusion protection */


/* Log stripped */
