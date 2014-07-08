/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmlog.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Logging MMI Calls
 */

#ifndef __MMLOG_H__
#define __MMLOG_H__


#undef MM_DEBUG_LOGGING


#ifdef MM_DEBUG_LOGGING

void mm_log_init( void ) ;
void mm_log_finish( void ) ;
void mm_log( char *id, char *fmt, ... ) ;

#define MM_LOG_INIT()    mm_log_init()
#define MM_LOG_FINISH()  mm_log_finish()
#define MM_LOG( params ) mm_log params


/* Each log entry should have its own (unique) id, and its parameters */

#define LOG_RF "rf " /* Reserve Free: level, ptr, size */
#define LOG_RG "rg " /* Reserve Get: level, ptr, size */
#define LOG_PC "pc " /* Pool Create: ptr, class, size */
#define LOG_PD "pd " /* Pool Destroy: ptr, class, size */
#define LOG_PE "pe " /* Pool Clear (empty): pool, res */
#define LOG_IP "ip " /* Init Params: arena, addrsize, workingsize, emergency, useallmem */
#define LOG_IS "is " /* Init Success: fixedpool, temppool, pslocal, psglobal */
#define LOG_IF "if " /* Init Failure: fixedpool, temppool, pslocal, psglobal */
#define LOG_QU "qu " /* Quit */
#define LOG_CX "cx " /* Commit Extend: limit */
#define LOG_CS "cs " /* Commit Shrink: limit */
#define LOG_LI "li " /* Low handler In: tier, average cost, handler */
#define LOG_LO "lo " /* Low handler Out: tier, no-error, handler */
#define LOG_MF "mf " /* Free: pool, ptr, size */
#define LOG_MT "mt " /* Truncate: pool, baseptr, oldsize, newsize */
#define LOG_MI "mi " /* MMalloc In: pool, size, class */
#define LOG_MO "mo " /* MMalloc Out: pool, ptr, size, class */
#define LOG_DB "db " /* Dl promise Begin: pool, ptr, size */
#define LOG_DN "dn " /* Dl promise Next: pool, size, result */
#define LOG_DE "de " /* Dl promise End: pool, base */
#define LOG_DF "df " /* Dl promise Free: pool, ptr */
#define LOG_PS "ps " /* PSVM Save: newlevel */
#define LOG_PR "pr " /* PSVM Restore: newlevel */
#define LOG_AO "ao " /* PSVM Alloc Obj: pool, size, ptr */
#define LOG_AT "at " /* PSVM Alloc Typed: pool, size, ptr */
#define LOG_AW "aw " /* PSVM Alloc Weak Typed: pool, size, ptr */
#define LOG_AS "as " /* PSVM Alloc Str: pool, size, ptr */
#define LOG_GC "gc " /* GC: global, local */
#define LOG_SC "sc " /* Sac create: pool, sac, count */
#define LOG_SD "sd " /* Sac destroy: pool, sac */
#define LOG_SL "sl " /* Sac class: index, block_size, cached_count, freq */
#define LOG_SE "se " /* Sac flush (empty): pool, sac */
#define LOG_SI "si " /* Sac alloc In: pool, sac, size, class */
#define LOG_SO "so " /* Sac alloc Out: pool, sac, ptr, size, class */
#define LOG_SF "sf " /* Sac Free: sac, ptr, size */

#else

#define MM_LOG_INIT()    EMPTY_STATEMENT()
#define MM_LOG_FINISH()  EMPTY_STATEMENT()
#define MM_LOG( params ) EMPTY_STATEMENT()

#endif /* MM_DEBUG_LOGGING */

#endif /* __MMLOG_H__ */

/*
* Log stripped */

/* end of mmlog.h */
