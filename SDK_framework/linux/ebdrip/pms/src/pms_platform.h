/* Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_platform.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for Platform Wrapper.
 *
 */

#ifndef _PMS_PLATFORM_H_
#define _PMS_PLATFORM_H_

#if defined(linux)
   #define ALIGN4 __attribute__ ((aligned (4)))
#else
   #define ALIGN4
#endif
/* *** User input  *** */

int PMS_CheckKeyPress(void);
unsigned char PMS_GetKeyPress(void);


/* *** Threads *** */

/** \callergraph */
void * PMS_BeginThread(void( *start_address )( void * ), 
                       unsigned int stack_size, 
                       void *arglist );
int PMS_CloseThread(void *pThread, int nWaitForThreadToExit);


/* *** Semaphores *** */

void * PMS_CreateSemaphore(int initialValue);
int PMS_IncrementSemaphore(void * semaHandle);
int PMS_WaitOnSemaphore_Forever(void * semaHandle);
int PMS_WaitOnSemaphore(void * semaHandle, unsigned int nMilliSeconds);
void PMS_DestroySemaphore(void * semaHandle);


/* *** Time *** */

void PMS_RelinquishTimeSlice(void);
void PMS_Delay(int nMilliSeconds);
unsigned long PMS_TimeInMilliSecs(void);


/* *** Critical sections *** */

void * PMS_CreateCriticalSection(void);
void PMS_EnterCriticalSection(void * ptCritical_Section);
void PMS_LeaveCriticalSection(void * ptCritical_Section);
void PMS_DestroyCriticalSection(void * ptCritical_Section);

/* *** Hot folder search functions ***/
void *PMS_Initiate_DirectorySearch(char *szDir);
int PMS_Run_DirectorySearch(char *szDir, void *hFind, char *pFileName);;
void PMS_Stop_DirectorySearch(void *hFind);

/* *** Globals *** */
/* Consider placing the following semaphore and critical section 
 * prototypes in pms.h or even closer to the modules that use them.
*/

/*! \brief Counting semaphore to syncronize between engine printing out page and RIP.
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_semPageQueue;

/*! \brief Counting semaphore to syncronize submission of band packets from OIL to PMS.
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_semPageComplete;

/*! \brief Counting Semaphore Handle for counting page checkins.
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_semCheckin;

/*! \brief Critical Section for thread-safe accessing of g_pstPageList.
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_csPageList;

/*! \brief Critical Section for thread-safe accessing of nValidBytes in sockets
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_csSocketInput;

/*! \brief Critical Section for thread-safe accessing of l_tPMSMem 
 * 
 * PC/Windows
 *
 * Casted to void * for platform portability.
*/
extern void * g_csMemoryUsage;



#endif /* _PMS_PLATFORM_H_ */
