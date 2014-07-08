/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWEVENT_H__
#define __FWEVENT_H__

/*
 * $HopeName: HQNframework_os!export:fwevent.h(EBDSDK_P.1) $
 * FrameWork External Event Handling
 */

/* 
 * Revision History:
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxevent.h"    /* Platform Dependent */

/* ---------------- Common between non-gui and gui ----------------------- */

/* ----------------------- Macros ------------------------------------------ */

/* Return values for FwEventBootThreadFnCanExecFn() */
enum
{
  FwEvent_CanExec_Never       = -1,
  FwEvent_CanExec_Later       = -2,
  FwEvent_CanExec_Now         = -3
};


/* ----------------------- Types ------------------------------------------- */

typedef struct FwEventNonUIContext
{
  int32                         _dummy_;        /* none yet */
#ifdef FW_PLATFORM_EVENT_NON_UI_CONTEXT
  FwPlatformEventNonUIContext    platform;
#endif
} FwEventNonUIContext;

typedef void (FwEventQuitByOSCallback)(void);

typedef void (FwEventBootThreadFn)(void *);

typedef int32 (FwEventBootThreadFnCanExecFn)(void);

/* ----------------------- Functions --------------------------------------- */

/* Sleep for some time. This behaves different on each platform.
 * On Motif, it will return when an interrupt happens.
 * On the PC or Mac, it will sleep for a certain amount of time regardless
 * of any events that might happen in the meantime.
 */

extern void FwEventRemainIdle( void );
/* FwEventHandle handles the next event in the queue. 
 * It returns TRUE if an event was processed, FALSE otherwise.
 */
extern int32 FwEventHandle( void );

/* FwEventBlock and FwEventUnblock increase and decrease respectively
 * the normal event semaphore defined in FrameWork.
 */
extern void FwEventBlock( void );
extern void FwEventUnblock( void );

/* Returns TRUE if the semaphore is not equal to 0,
 * FALSE otherwise.
 */
extern int32 FwEventAreEventsBlocked( void );

/* Set a special callback for when the app is quit by the OS.
 * e.g. when the machine is shutdown.
 */
extern void FwEventSetQuitByOSCallback( FwEventQuitByOSCallback * pCallback );

/* Calls pFn with pData on the boot thread
 * If called on the boot thread then just calls pFn immediately
 * If called on a non-boot thread causes pFn to be called from within
 * the next call of FwEventHandle() or FwEventRemainIdle(), blocking
 * the calling thread until pFn has completed.
 */
extern void FwEventExecuteOnBootThread( FwEventBootThreadFn * pFn, void * pData );

/* As FwEventExecuteOnBootThread(), but before invoking pFn it checks that
 * it is OK to do so by calling pCanExecFn.  If pCanExecFn returns FwEvent_CanExec_Now
 * pFn is invoked and FwEventExecuteOnBootThreadWhenCanExec() returns TRUE,
 * if pCanExecFn returns FwEvent_CanExec_Later it is called again on the next call
 * of FwEventHandle() / FwEventRemainIdle(), if pCanExecFn returns FwEvent_CanExec_Never
 * pFn is not invoked and FwEventExecuteOnBootThreadWhenCanExec() returns FALSE.
 */
extern int32 FwEventExecuteOnBootThreadWhenCanExec
  ( FwEventBootThreadFn * pFn, void * pData, FwEventBootThreadFnCanExecFn * pCanExecFn );

#ifdef FW_GUI

/* ----------------------- Macros ------------------------------------------ */


/* ----------------------- Types ------------------------------------------- */

typedef struct FwEventContext
{
  int32                         _dummy_;        /* none yet */
#ifdef FW_PLATFORM_EVENT_CONTEXT
  FwPlatformEventContext         platform;
#endif
} FwEventContext;


/* ----------------------- Functions --------------------------------------- */

/* Returns TRUE if the GUI is in an unavoidable modal state (eg menu
 * interaction or drag) which will cause some GUI operations (such as
 * showing or hiding dialogs, or changing menus) to block until it is
 * complete. At present this will only ever be TRUE on the Mac.
 */
extern int32 FwEventGuiBusy( void );

#endif /* FW_GUI */

#endif /* !__FWEVENT_H__ */

/* eof fwevent.h */
