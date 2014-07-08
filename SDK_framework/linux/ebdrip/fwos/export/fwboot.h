/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWBOOT_H__
#define __FWBOOT_H__

/*
 * $HopeName: HQNframework_os!export:fwboot.h(EBDSDK_P.1) $
 * FrameWork External Boot Interface
 */

/*
 * Revision History:
* Log stripped */


/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxboot.h"     /* Platform Dependent */

#include "fwerror.h"    /* FwErrorContext */
#include "fwevent.h"    /* FwEvemtNonUIContext */
#include "fwfile.h"     /* FwFileContext */
#include "fwmem.h"      /* FwMemContext */
#include "fwstring.h"   /* FwStrContext */
#include "fwtransl.h"   /* FwTranslContext */

#ifdef FW_GUI
#include "fwmenu.h"     /* FwMenuContext */
#include "fwdialog.h"   /* FwDlgContext */
#endif /* FW_GUI */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- Macros ------------------------------------------ */

/* Convenience macros to invoke the registered exiterror / log function */
#define FwExitErrorf (*FwGetExitErrorFunction())
#define FwLogf       (*FwGetLogFunction())


/* ----------------------- Types ------------------------------------------- */

/*****************************************************
* Contexts required for non User Interface FrameWork *
*****************************************************/

/* Control context */

/* If the application supplies a tickle function then this will be called
 * during time consuming operations to allow the application to do background
 * processing.
 * The nOp parameter identifies the FrameWork time consuming operation.
 * If the fAllowAbort flag is TRUE the application can request that the
 * FrameWork abort the operation by returning TRUE, otherwise it must return
 * FALSE.
 * An aborted operation will return the error fwErrorAbort.
 */
typedef int32   (FwApplicationTickleFn) ( uint32 nOp, int32 fAllowAbort );
enum
{
  FwTickleOp_FileIO = 0,                /* fAllowAbort normally TRUE */
  FwTickleOp_ModalInteraction = 1       /* fAllowAbort normally FALSE */
};


/* Any exiterrorf or logf functions provided to the framework by the application
 * must function correctly if called during framework initialisation/shutdown.
 * If they require framework services then they must check that the framework is
 * sufficiently booted for them to be available, they cannot assume this.
 * The framework may legitimately call either of these functions at any time between
 * the _start_ of FwBoot() and the _end_ of FwShutdown().
 *
 * exiterrorf is passed a message to output to the user before causing the application
 * to exit - it should not return
 * logf is passed a message which may be useful to OEMs but is not intended for the user
 */
typedef void    (FwExitErrorfFn) ( FwTextString ptbzFormat, ... );
typedef void    (FwLogfFn)       ( FwTextString ptbzFormat, ... );


/* Mutex services */
struct FwMutex;
typedef struct FwMutex FwMutex;

typedef FwMutex * (FwCreateMutexFn)  ( void );
typedef void      (FwLockMutexFn)    ( FwMutex * pMutex );
typedef void      (FwUnlockMutexFn)  ( FwMutex * pMutex );
typedef void      (FwDestroyMutexFn) ( FwMutex * pMutex );

typedef struct FwControlContext
{
  FwTextString                  ptbzAppName;         /* Required */
  FwExitErrorfFn *              exiterrorf;          /* Required */
  FwLogfFn *                    logf;                /* Optional, pass NULL if not provided */
  FwApplicationTickleFn *       application_tickle;  /* Optional, pass NULL if not provided */

  /* A user of FWOS may put some application-specific data here. It is up to
   * the application to decide what to do with it; FWOS does not care.
   * See FwBootGetAppPrivate().
   */
  void*                 pAppPrivate;

  /* Optional functions for providing mutex services to the FrameWork,
   * allowing its hash tables, lists, etc to be used in a multi-threaded
   * app.  Can also be invoked by client code via FwMutexCreate() et al.
   *
   * Client must either provide all or none of these functions.
   *
   * createmutexf returns NULL if mutex creation fails.  Client code must
   * check for this and act accordingly - it may well be a fatal error.
   *
   * lockmutexf, unlockmutexf and destroymutexf are always called with
   * non-NULL pMutex.
   */
  FwCreateMutexFn             * createmutexf;        /* Optional, pass NULL if not provided */
  FwLockMutexFn               * lockmutexf;          /* Optional, pass NULL if not provided */
  FwUnlockMutexFn             * unlockmutexf;        /* Optional, pass NULL if not provided */
  FwDestroyMutexFn            * destroymutexf;       /* Optional, pass NULL if not provided */

#ifdef FW_PLATFORM_CONTROL_CONTEXT
  FwPlatformControlContext      platform;
#endif
} FwControlContext;

/* Containing context for all of non-UI FrameWork */
typedef struct FwBootContext
{
  /* Put lower level modules first */
  FwControlContext      control;
  FwMemContext          memory;
  FwStrContext          string;
  FwErrorContext        error;
  FwFileContext         file;
  FwTranslContext       translate;
  FwEventNonUIContext   event;
} FwBootContext;


#ifdef FW_GUI
/************************************************************
* Additional contexts required for User Interface FrameWork *
************************************************************/

typedef struct FwUIContext
{
  /* Put lower level modules first */
  FwEventContext        event;
  FwMenuContext         menu;
  FwDlgContext          dialog;
} FwUIContext;
#endif /* FW_GUI */

/* ----------------------- Functions --------------------------------------- */

/* Fills in *pBootContext with default values where possible
 * Must be called by all clients _before_ calling FwBoot()
 */
extern void FwBootGetDefaultContext( FwBootContext * pBootContext );

/* Start up non user interface framework modules */
extern void FwBoot( FwBootContext * pBootContext );

/* returns TRUE <=> non user interface framework booted */
extern int32 FwBootDone( void );

/* Shut down non user interface framework modules */
extern void FwShutdown( void );

/* Fills in *pBootContext with default values where possible  */
extern void FwBootGetDefaultContext( FwBootContext * pBootContext );

extern int32 FwIsBootThread( void );

/* Get the application-specific data supplied at boot-time. */
extern void* FwBootGetAppPrivate( void );

/* Returns the application name supplied at boot-time. Caller must not modify
 * or free this string; caller must duplicate the returned value, if needing to do so.
 */
extern FwTextString FwBootGetAppName( void );

/* Return the current exiterror function */
extern FwExitErrorfFn * FwGetExitErrorFunction( void );

/* Return the current log function.
 * This will normally be the client-provided function from the boot context,
 * but will be a default "do nothing" function if the client has not provided
 * a function.
 */
extern FwLogfFn * FwGetLogFunction( void );

/* Creates a mutex via any createmutexf
 * Returns NULL if client has not provided a createmutexf,
 * or if calling that failed.
 */ 
extern FwMutex * FwMutexCreate( void );

/* Locks a mutex via any lockmutexf */ 
extern void FwMutexLock( FwMutex * pMutex );

/* Unlocks a mutex via any unlockmutexf */ 
extern void FwMutexUnlock( FwMutex * pMutex );

/* Destroys a mutex via any destroymutexf */ 
extern void FwMutexDestroy( FwMutex * pMutex );

#ifdef ASSERT_BUILD
/* Assert if mutex is not locked / unlocked */
extern void FwMutexAssertLocked( FwMutex * pMutex );
extern void FwMutexAssertUnlocked( FwMutex * pMutex );
#endif

#ifdef FW_GUI

/* Start up user interface framework modules */
extern void FwInitUI( FwUIContext * pUIContext );

/* returns TRUE <=> user interface framework initialised */
extern int32 FwInitUIDone( void );

/* Shut down user interface framework modules */
extern void FwShutdownUI( void );

#endif /* FW_GUI */


/* ----------------------- Data -------------------------------------------- */

#ifdef __cplusplus

  // Convenience class, for locking and unlocking a FwMutex, according
  // to scope. Does not create or destroy FwMutex.
  //
  // Implementation is included here in the header, as it is so trivial.
  class FwMutexLock_var
  {
    private:

    FwMutex* const _mutex;

    public:

    FwMutexLock_var(FwMutex* mutex) : _mutex(mutex) { FwMutexLock(_mutex); }
    ~FwMutexLock_var() { FwMutexUnlock(_mutex); }
  };

}
#endif

#endif /* !__FWBOOT_H__ */

/* EOF fwboot.h */
