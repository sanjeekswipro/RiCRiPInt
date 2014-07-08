#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

/* $HopeName: HQN_CPP_CORBA!export:threadpool.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Implements thread pooling to avoid thread creation overheads for
 * short lived threads.
 */

#ifdef PRODUCT_HAS_API

#ifdef __cplusplus

/* ----------------------- Includes ---------------------------------------- */

#include <omnithread.h> // omni_mutex

/* ----------------------- Classes ----------------------------------------- */

// function type for task for PoolWorker to perform
typedef void PoolWorkerTask( void * );

// Thread pool implementation
class PoolWorker
{
public:
  // Schedule a function to be executed by a PoolWorker
  static void schedule( PoolWorkerTask * pfTask, void * arg );

  // Application should call this, when beginning its shut-down procedure. It
  // disables thread re-use and attempts to avoid making any further use of
  // globalLock. Aim is to reduce chance of problems at application exit.
  //
  // Harmless to call this more than once. However, once called, there is no
  // way to re-start the thread pool.
  //
  // Note that tasks assigned via schedule() will still run after this, but
  // they will get a new thread each, exiting immediately after the task.
  static void shutdown();

private:
  //
  // Static Fields
  //

  // For shared access to static data, i.e. the idleList
  static omni_mutex globalLock;

  // head of list of idle Workers
  static PoolWorker * idleList;

  static bool isShutDown;

  //
  // Instance Fields
  //

  // for access to individual PoolWorker
  omni_mutex       instanceLock;

  // to wait for something to do with a timeout, uses instanceLock
  omni_condition   instanceCondition;

  // lock * pGlobalLock or instanceLock to read if could be on idleList
  // lock * pGlobalLock then instanceLock to write if could be on idleList
  PoolWorkerTask * pfTask; // NULL while on idleList
  void           * arg;
  unsigned int   nTasks;

  // linked list entry for idleList chain, lock * pGlobalLock to access
  PoolWorker     * pNext; // NULL if not on chain or last
  PoolWorker     * pPrev; // NULL if not on chain or first


  //
  // Methods
  //

  ~PoolWorker();

  // Constructs the PoolWorker object, but does not start the new thread
  PoolWorker( PoolWorkerTask * pfTask, void * arg );

  // dummy copy constructor and operator= to prevent copying
  PoolWorker( const PoolWorker & );
  PoolWorker & operator=( const PoolWorker & );

  static void thread_action(void * pVoidPoolWorker);
  void taskLoop();

  // Starts the new thread and runs thread_action() on it
  void start();

  // Removes this PoolWorker from the idle list. Caller must have locked
  // globalLock. Must be on idle list (asserted).
  void removeFromIdleList();

  // Adds this PoolWorker to the idle list, at the head. Caller must have locked
  // globalLock. Must not be on idle list (asserted).
  void addToIdleList();

  // Assigns the given task to this PoolWorker, and signals its condition.
  // The instanceLock must already be locked. Must not be on idle list. Must
  // not have task already assigned.
  void assignTask(PoolWorkerTask* pfTask, void* arg);

  // Waits for task assignment to be signalled, or time-out, whichever happens
  // first. Caller must have locked instanceLock. Must be on idle list.
  // Returns 1 if signalled, 0 if timed-out.
  int waitForTask();

  // Runs the configured task, then clears it. Must not be on idle list. No
  // locks should be held when calling this.
  void doTask();

  // Returns flag indicating whether task is on idle list. Also asserts
  // internal consistency of list.
  bool isOnIdleList() const;

  // Debug only.
  static void debugTracePool(PoolWorker* pPW, const char* ptbzOperation);
};

#endif

#ifdef __cplusplus
extern "C" {
#endif
  /* C-callable version of PoolWorker::shutdown() */
  void poolWorkerShutdown();
#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* __THREADPOOL_H__ */

/* eof threadpool.h */
