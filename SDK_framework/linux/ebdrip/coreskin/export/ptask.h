#ifndef __PTASK_H__
#define __PTASK_H__

/* $HopeName: SWcoreskin!export:ptask.h(EBDSDK_P.1) $
 *
* Log stripped */

/* ----------------------------- Includes ---------------------------------- */

#include "coreskin.h"

#ifdef __cplusplus
extern "C" {
#endif


/* ----------------------------- Macros ------------------------------------ */

/* The standard clock period within the coregui is in milliseconds */

#define timesAsec2period(t)     ( 1000 / ( t ) )
#define seconds2period(t)       ( ( t ) * 1000 )

#define block_semaphore( pSema ) MACRO_START (*(pSema))++; MACRO_END
#define clear_semaphore( pSema ) MACRO_START (*(pSema))--; MACRO_END
#define semaphore_blocked( pSema ) ((*(pSema)) > 0)

/* ----------------------------- Types ------------------------------------- */

typedef int32 PeriodicTaskCallback( void );

/* Enable this to turn ptask tracing on */
#if 0
#define TRACE_PTASKS  (1)
#endif

struct PeriodicTask
{
  struct PeriodicTask * pNext;
  struct PeriodicTask * pNextDestroyed;
  int32                 timer;
  int32                 period;
  PeriodicTaskCallback *pCallback;
  int32 *               pSema;
#ifdef TRACE_PTASKS
  char                  name[ 64 ];
#endif
};

typedef struct PeriodicTask PeriodicTask;       /* opaque */


/* This is used as the argument to
   coregui_may_wait_for_tickle(). Its horrible but will remain as so
   until someone reworks the skin code. --johnk */
typedef void (* ptask_generic_fp) ();

/* ----------------------------- Data -------------------------------------- */

extern int32    coregui_normalevent_sema;

extern int32    gEventPeriodIncrement;

/* ----------------------------- Functions --------------------------------- */

extern int32 coregui_tickle( void );
extern void coregui_may_wait_for_tickle ( ptask_generic_fp caller );
extern void coregui_wait_for_tickle( void );

extern void PeriodicTaskCreate
 (
   PeriodicTask **              ppTask,
   int32 *                      pSema,
   int32                        period,
   PeriodicTaskCallback *       pCallback
#ifdef TRACE_PTASKS
   ,char *                      name
#endif
 );

extern void PeriodicTaskDestroy( PeriodicTask ** ppTask );

extern int32 PeriodicTaskAdded( PeriodicTask * pTask );

extern int32 PeriodicTaskRun( PeriodicTask * pTask );

extern int32 PeriodicTaskBlocked( PeriodicTask * pTask );

extern void changePTaskPeriod( PeriodicTask * ptask, int32 period );

/* These manage a global semaphore on all tasks */
extern void PeriodicTasksBlock( void );
extern void PeriodicTasksUnBlock( void );
extern int32 PeriodicTasksAreBlocked( void );

extern void coregui_fastSpeedEventHandler( void );
extern void coregui_normalSpeedEventHandler( void );

extern void block_maineventhandler( void );
extern void clear_maineventhandler( void );

/* Returns the number of ptasks on the stack */
extern int32 PeriodicTaskDepth( void );

extern void coregui_DoIdle( void );

extern void pis_installperiodictasks( void );
extern void pis_installmoreperiodictasks( void );

#ifdef __cplusplus
}
#endif

#endif /* protection for multiple inclusion */
