/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief PMS structures and defines.
 *
 */

#ifndef _PMS_H_
#define _PMS_H_

#include "pms_export.h"
#include <stdio.h>

#define PMS_SHOW_ERROR(_x_, ...) printf (_x_, ## __VA_ARGS__ );

/*! \brief Output a message.
 *
 * Use the same format and arguments as \c printf.
 *
 */
#define PMS_SHOW(x, ...) { if(g_printPMSLog) printf(x, ## __VA_ARGS__ ); }

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef UNUSED_PARAM
#define UNUSED_PARAM(_type_, _param_)  ((void)_param_)
#endif

#ifdef SDK_PANDA_GRANITE
#define PMS_LOWBYTEFIRST
#else /* not SDK_PANDA_GRANITE */
#ifdef MACINTOSH
#ifdef __LITTLE_ENDIAN__
#define PMS_LOWBYTEFIRST
#endif
#else /* else not Mac */
/* Linux platforms, and ARM CPUs can be either endian, so take care with the defaults below. */
#if !defined(_PPC_) && (defined(WIN32) || defined(linux) || defined(CPU_ARM9))
#define PMS_LOWBYTEFIRST
#endif /* !PPC && (Win32 or linux) */
#endif /* mac */
#endif /* SDK_PANDA_GRANITE */

#ifdef WIN32
#define PMS_HOT_FOLDER_SUPPORT
#else
#ifdef UNIX
#define PMS_HOT_FOLDER_SUPPORT
#endif
#endif

/*! \brief RIP minimum memory requirement. */
#define MIN_REQUIRED_RIP_MEM    10*1024*1024

/*! \brief Number of trays avilable. */
#define PMS_MAX_TRAY 3

#define DEFAULT_WORKING_MEMSIZE  256           /* may need to change later */

#ifdef PMS_DEBUG

/*! \brief Assert that a condition is true.
 *
 * If condition is true then no message is output.\n
 * If condition is false then ASSERT message is output.\n
 *
 * PMS_ASSERT(bFlagShouldBeTrue, ("Message if this is flag is false")) ;
 *
 * The message must be wrapped in brackets as it is passed to \c GG_SHOW
 * for use in the printf function.
 *
 * Variable arguments can be used and formatting is the same as printf function.\n
 * e.g. \n
 * PMS_ASSERT(nCountShouldbeThree == 3, ("Message if Count is not three, %d", nCountShouldBeThree)) ;
*/

#define PMS_ASSERT(condition, x) \
    if(!(condition)) \
    { \
        printf ("*** ASSERT ***  "); \
        printf x; \
    }
/*! \brief Assert without condition.
 * 
 * Similar to \c PMS_ASSERT except the condition is not checked.
 *
 * Brackets around the message are not required as with \c PMS_ASSERT.
 *
 * Variable arguments can be used and formatting is the same as printf function.\n
 * e.g. \n
 * PMS_FAIL("Output this message without checking, %d", nHelpfulErrorCode) ;
*/
#define PMS_FAIL(x, ...) printf ("*** ASSERT ***  " x, ## __VA_ARGS__ )

#else
#define PMS_ASSERT(condition, x) 
#define PMS_FAIL(x, ...) 
#endif /*PMS_DEBUG*/
/*! \brief Rip States. 
*/
enum {
  PMS_Rip_Inactive,           /**< RIP is idle */
  PMS_Rip_Initializing,       /**< RIP is initializing. */
  PMS_Rip_In_Progress,        /**< RIP is in progress. */
  PMS_Rip_Finished,           /**< RIP has finished ripping all jobs */
};
typedef int PMS_eRIPState;

/*! \brief Job States. 
*/
enum {
  PMS_Job_Inactive,           /**< Job is not yet started. */
  PMS_Waiting_For_Page,       /**< PMS is waiting for page checkin. */
  PMS_Page_Received,          /**< PMS has received a page from RIP. */
  PMS_Page_In_Progress,       /**< PMS is printing a page. */
  PMS_Page_Completed,         /**< PMS has completed printing the page. */
  PMS_Job_Completed,          /**< PMS has completed printing the job. */
  PMS_AllJobs_Completed,      /**< PMS has completed printing all the jobs. */
};
typedef int PMS_eJOBState;


/*! \brief Holds the count of command-line parameters. */
extern int g_argc;                       
/*! \brief Holds command-line parameter strings. */
extern char **g_argv;                    
/*! \brief List of Checked-in Pages. */
extern PMS_TyPageList *g_pstPageList;
/*! \brief storing the number of clock ticks when program started. */
extern int g_nTimeZero;
/*! \brief Count of pages checked-in. */
extern int g_nPageCount;
/*! \brief pointer to the page currently being processed. */
extern PMS_TyPage *g_pstCurrentPMSPage;
/*! \brief Indicates current state of the RIP. */
extern PMS_eRIPState g_eRipState;
/*! \brief Indicates current state of the Job. */
extern PMS_eJOBState g_eJobState;
/*! \brief  Input socket port number. 
    Must be set/reset in InitGlobals() in pms_main.c. can be overridden by command line by -s switch*/
extern unsigned int g_SocketInPort;
#ifdef PMS_HOT_FOLDER_SUPPORT
/*! \brief  Input Hot Folder. 
    Must be set/reset in InitGlobals() in pms_main.c. can be overridden by command line by -i switch*/
extern char * g_pPMSHotFolderPath; 
#endif
/*! \brief Holds the number of entries pointed to by g_pstTrayInfo. 
    Must be set/reset in InitGlobals() in pms_main.c */
extern int g_nInputTrays;
/*! \brief Pointer to information about input trays. 
    Must be set/reset in InitGlobals() in pms_main.c */
extern PMS_TyTrayInfo * g_pstTrayInfo;
/*! \brief Holds the number of entries pointed to by g_pstOutputInfo. 
    Must be set/reset in InitGlobals() in pms_main.c */
extern int g_nOutputTrays;
/*! \brief Pointer to information about output trays. 
    Must be set/reset in InitGlobals() in pms_main.c */
extern PMS_TyOutputInfo * g_pstOutputInfo;
/*! \brief  System state configuration. 
    Must be set/reset in InitGlobals() in pms_main.c. Parameters can be overridden by command line switches*/
extern PMS_TySystem g_tSystemInfo;
/*! \brief  Next System state configuration. 
    Used to store system configuration to be applied to the next job*/
extern PMS_TySystem g_tNextSystemInfo;
/*! \brief Controls logging of PMS debug messages */
extern int g_bLogPMSDebugMessages;
/*! \brief Controls output of page via Back Channel */
extern int g_bBackChannelPageOutput;
/*! \brief Specifies that each message sent via Back Channel is tagged. 
    Essential for differentiating between message types, PMS_eBackChannelWriteTypes. */
extern int g_bTaggedBackChannel;
/*! \brief Specifies whether the socket is used for backchannel */ 
extern int g_bBiDirectionalSocket;

extern void StartOIL();

extern int PMS_WriteDataStream(PMS_eBackChannelWriteTypes eType, void *pContext, unsigned char *pubBuffer, int nBytesToWrite, int *pnBytesWritten);


#endif /* _PMS_H_ */

