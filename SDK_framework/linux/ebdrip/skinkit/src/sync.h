/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:sync.h(EBDSDK_P.1) $
 * Thread synchronization related utility functions
 */

#ifndef __SYNC_H__
#define __SYNC_H__

#include "std.h"
#include "swoften.h"

/* ------------- Platform-specific functions -------------- */

/**
 * \file
 * \brief Cross platform synchronization primitives
 * \ingroup skinkit
 */

/**
 * \brief Create a thread and then call the function StartRip (defined
 * in ripthread.c).
 * \return TRUE if the thread was created successfully.
 */
extern HqBool StartRIPOnNewThread(void);

/**
 * \brief Wait for the RIP thread to exit.
 */
extern void WaitForRIPThreadToExit(void);

/**
 * \brief Create a semaphore with an initial value.
 * \return NULL if this failed, otherwise a platform-dependent handle
 * to the semaphore.
 */
extern void * PKCreateSemaphore(int32 initialValue);

/**
 * \brief If any threads are blocked on PKWaitOnSemaphore, then wake
 * them up. Otherwise increment the value of the semaphore.
 */
extern int32 PKSignalSemaphore(void * semaHandle);

/*
 * \brief If the semaphore value is > 0 then decrement it and carry
 * on. If it's already 0 then block.
 */
extern int32 PKWaitOnSemaphore(void * semaHandle);

/**
 * \brief Free up the resources associated with the semaphore.
 */
extern void PKDestroySemaphore(void * semaHandle);

/**
 * \brief Finish semaphore module, free up resources for remaining semaphores.
 */
extern void PKSemaFinish(void);

#endif

