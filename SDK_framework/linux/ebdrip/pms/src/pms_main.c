/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_main.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup PMS
 *  \brief PMS support functions.
 */

#include "pms.h"
#include "pms_platform.h"
#include "pms_malloc.h"
#include "oil_entry.h"
#include "pms_page_handler.h"
#include "pms_engine_simulator.h"
#include "pms_interface_oil2pms.h"
#include "pms_input_manager.h"
#include <string.h> /* for strcpy */
#include "pms_filesys.h"
#include "pms_config.h"
#ifdef PMS_INPUT_IS_FILE
#include "pms_file_in.h"
#endif
#include "pms_thread.h"
#ifdef PMS_SUPPORT_SOCKET
#include "pms_socket.h"
#endif
#include "pms_thread.h"

#include <ctype.h> /* for toupper */

#ifdef PMS_DEBUG
#define DEBUG_STR   "D"
#else
#define DEBUG_STR   "V"
#endif

#ifdef GPROF_BUILD
void moncontrol(int) ; /* This prototype is missing from some glibc versions */
#endif

/*! \brief Current version of PMS.*/
char    *PMSVersion = DEBUG_STR  "4.0r0";
extern char Changeset[];    /* defined in the pms_version.c file that is updated by the build system from Mercurial */

/* Global variables */
int g_argc;
char **g_argv;
int nJobs;
char **aszJobNames;
PMS_TyPageList *g_pstPageList;
int g_nTimeZero;
int g_nPageCount;
PMS_eRIPState g_eRipState;
PMS_eJOBState g_eJobState;
unsigned int g_SocketInPort;
int g_nInputTrays = 0;
PMS_TyTrayInfo * g_pstTrayInfo = NULL;
int g_nOutputTrays = 0;
PMS_TyOutputInfo * g_pstOutputInfo = NULL;
PMS_TySystem g_tSystemInfo;
PMS_TySystem g_tNextSystemInfo;
int g_bLogPMSDebugMessages;
int g_bTaggedBackChannel;
int g_bBackChannelPageOutput;
int g_bBiDirectionalSocket;
int g_nPMSStdOutMethod;
void *g_semTaggedOutput; /* Semaphore for keeping start/end tag with the actual message */
void *g_semCheckin;      /* Semaphore to for counting page checkins */
void *g_semPageQueue;    /* Semaphore to syncronize between engine printing out page and RIP */
void *g_semTaggedOutput; /* Semaphore for keeping start/end tag with the actual message */
void *g_semPageComplete; /* Semaphore to syncronize submission of band packets from OIL to PMS */
void *g_csPageList;      /* Critical section for thread-safe accessing of g_pstPageList */
void *g_csMemoryUsage;   /* Critical section for thread-safe accessing of nValidBytes in sockets */
void *g_csSocketInput;   /* Critical section for thread-safe accessing of l_tPMSMem */
int g_printPMSLog;

unsigned int g_bDebugMemory;
int l_bEarlyRIPStart = FALSE;  /* TRUE means start RIP before job is opened/received */

const char *g_mps_log = NULL ;
unsigned long g_mps_telemetry = 0x63 ; /* User, Alloc, Pool, Arena */
const char *g_profile_scope = NULL ;

char * g_pPMSConfigFile = NULL;
#ifdef PMS_HOT_FOLDER_SUPPORT
char * g_pPMSHotFolderPath = NULL;
#endif
/* Forward Declarations */
static void InitGlobals(void);
static PMS_TyJob * CreateJob(unsigned int nJobNumber);
static void CleanUp();
static void ParseCommandLine(void);

/** Array of PMS API function pointers */
static PMS_API_FNS l_apfnRip_IF_KcCalls = NULL;
extern int PMS_RippingComplete();

/**
 * \brief Entry point for PMS Simulator
 *
 * This routine is the main entry for PMS.\n
 */
int PMS_main()
{
  void *pPMSOutThread = NULL;
  void *pOILThread = NULL;

  /* Initialise PMS API Function pointers array */
  l_apfnRip_IF_KcCalls = PMS_InitAPI();

  /* Initialise essential structures, this memory will never be released. */
  /* Initialize Global structures */
  InitGlobals();

  /* Initialise file system */
  PMS_FS_InitFS();

  /* Parse command line */
  ParseCommandLine();

  /* Initialise input trays */
  g_nInputTrays = EngineGetTrayInfo();

    /* Initialise output trays */
  g_nOutputTrays = EngineGetOutputInfo();

  /* Read any config file specified on command line */
  if( g_pPMSConfigFile != NULL )
  {
    if( ! ReadConfigFile( g_pPMSConfigFile ) )
    {
      PMS_SHOW_ERROR("\n Failed to read PMS configuration file: %s.\n Exiting.....", g_pPMSConfigFile);
      CleanUp();
      return 0;
    }
  }

  /* Set up default job settings (after ParseCommandLine() so as to honour its settings) */
  EngineGetJobSettings();

  if(PMS_IM_Initialize() == 0)
  {
      CleanUp();
      return 0;
  }
  /* Run the RIP by calling OIL interface functions */
  if(strcmp("unknown", Changeset) == 0) 
  {
    PMS_SHOW_ERROR("PMS version: %s\r\n", PMSVersion);
  }
  else
  {
    PMS_SHOW_ERROR("\n PMS Version %s Changeset %s\n",PMSVersion ,Changeset);
  }
  
  /* Loop if last job contained a restart command. */
  do {
    if(g_tSystemInfo.nRestart)
    {
      g_tSystemInfo.nRestart = 0;
      g_tSystemInfo = g_tNextSystemInfo;
    }

    /* Clear the RIP and job states. */
    g_eRipState = PMS_Rip_Inactive;
    g_eJobState = PMS_Job_Inactive;

    pPMSOutThread = (void*)StartOutputThread();
    pOILThread = (void*)StartOILThread();

    /* wait for everything to finish.... */
    while(g_eJobState != PMS_AllJobs_Completed)
    {
      /* currently only supoprted under windows/linux */
#ifndef INTR_NO_SUPPORT
      if (PMS_CheckKeyPress())
      {
        if (PMS_GetKeyPress() == '!')
        {
          printf("****** INTERRUPT *******\n");
          OIL_JobCancel();
        }
      }
#endif
      PMS_Delay(100);
    };

    /* The OIL thread should not take make time to exit, so
       we'll allow it 1 second */
    PMS_CloseThread(pOILThread, 1000);
    pOILThread = NULL;

    /* The PMS output thread may have to wait for mechanical
       hardware before if finishes, and so we'll allow longer
       for the thread to close cleanly */
    PMS_CloseThread(pPMSOutThread, 5000);
    pPMSOutThread = NULL;

  } while(g_tSystemInfo.nRestart);

  /* Cleanup and free input modules */
  PMS_IM_Finalize();

  /* Shut down file system */
  PMS_FS_ShutdownFS();

  CleanUp();

#ifdef PMS_MEM_LIMITED_POOLS
  /* check if memory stat should be displayed */
  if (g_bDebugMemory)
    DisplayMemStats();

  CheckMemLeaks();
#endif

  return 1;
} /*main()*/

/**
 * \brief PMS Global Structures initializing routine
 *
 * This routine intializes the global variables used in PMS.\n
 */
static void InitGlobals(void)
{
  /* Initialise g_nTimeZero = 0 to be used inside PMS_TimeInMilliSecs */
  g_nTimeZero = 0;
  g_nTimeZero = PMS_TimeInMilliSecs();

  g_pstPageList = NULL;
  g_nPageCount = 0;
  g_eRipState = PMS_Rip_Inactive;
  g_eJobState = PMS_Job_Inactive;
  g_tSystemInfo.uUseEngineSimulator = FALSE;
  g_tSystemInfo.uUseRIPAhead = TRUE;
  g_SocketInPort = 0;                 /* initialise to 0, this means no socket input is enabled */
  g_printPMSLog = 1;
  g_bLogPMSDebugMessages = 0;
  g_bTaggedBackChannel = 0;
  g_bBackChannelPageOutput = 0;
  g_bBiDirectionalSocket = 0;
  g_nPMSStdOutMethod = 1;


  memset(g_tSystemInfo.szOutputPath,0,PMS_MAX_OUTPUTFOLDER_LENGTH);
  g_tSystemInfo.cbRIPMemory = DEFAULT_WORKING_MEMSIZE * 1024 * 1024;
  g_tSystemInfo.nOILconfig = 0;                        /* no custom OIL config */
  g_tSystemInfo.ePaperSelectMode = PMS_PaperSelNone;
  g_tSystemInfo.uDefaultResX = 0;    /* set the default resolution in ParseCommandLine() */
  g_tSystemInfo.uDefaultResY = 0;
  g_tSystemInfo.eImageQuality = PMS_1BPP;
  g_tSystemInfo.bOutputBPPMatchesRIP = 1;
  g_tSystemInfo.uOutputBPP = 1;
  g_tSystemInfo.eDefaultColMode = PMS_CMYK_Composite;
  g_tSystemInfo.bForceMonoIfCMYblank = TRUE;
  g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_ORIPDefault;
  g_tSystemInfo.cPagesPrinted = 0;                    /* TODO: make this value persist */
  g_tSystemInfo.uPjlPassword = 0;                     /* TODO: make this value persist */
  g_tSystemInfo.ePersonality = PMS_PERSONALITY_AUTO;  /* TODO: make this value persist */
  g_tSystemInfo.eBandDeliveryType = PMS_PUSH_PAGE;
  g_tSystemInfo.bScanlineInterleave = FALSE;
  g_tSystemInfo.fTrapWidth = 0.0f;
  g_tSystemInfo.uColorManagement = 0;
  g_tSystemInfo.szProbeTraceOption[0] = '\0';
  memset(g_tSystemInfo.szProbeTraceOption,0,sizeof(g_tSystemInfo.szProbeTraceOption));
  g_tSystemInfo.nRestart = 0;
  g_tSystemInfo.nStrideBoundaryBytes = 4;
  g_tSystemInfo.nPrintableMode = 0;
  g_tSystemInfo.nStoreJobBeforeRip = FALSE;
  g_tSystemInfo.cbReceiveBuffer = (1 * 1024 * 1024);
  strcpy(g_tSystemInfo.szManufacturer, PMS_PRINTER_MANUFACTURER);
  strcpy(g_tSystemInfo.szProduct, PMS_PRINTER_PRODUCT);
  strcpy(g_tSystemInfo.szPDFSpoolDir, PMS_PDFSPOOL_DIR);
  g_tSystemInfo.bFileInput = FALSE;
  g_tSystemInfo.nRendererThreads = 1;
#ifdef PMS_SUPPORT_TIFF_OUT
  g_tSystemInfo.eOutputType = PMS_TIFF; /* Default to TIFF if supported */
#else
  g_tSystemInfo.eOutputType = PMS_NONE;
#endif

  /* Initialise all the memory pools to 0, which means unrestricated */
  g_tSystemInfo.cbSysMemory = 0;
  g_tSystemInfo.cbAppMemory = 0;
  g_tSystemInfo.cbJobMemory = 0;
  g_tSystemInfo.cbMiscMemory = 0;
  g_tSystemInfo.cbPMSMemory = 0;
#ifdef SDK_MEMTRACE
  g_bDebugMemory = FALSE;
#endif

  /* Memory usage critical sections must be created before OSMalloc (therefore PMSmalloc) is called */
  g_csMemoryUsage = PMS_CreateCriticalSection();
  g_csPageList = PMS_CreateCriticalSection();
  g_csSocketInput = PMS_CreateCriticalSection();

  /* Create semaphore function uses OSMalloc, therefore must be done after Memory usage critical section is created */
  g_semCheckin = PMS_CreateSemaphore(0);
  g_semPageQueue = PMS_CreateSemaphore(0);
  g_semTaggedOutput = PMS_CreateSemaphore(1);
  g_semPageComplete = PMS_CreateSemaphore(0);

}

/**
 * \brief Routine to display the command line options
 *
 * This routine displays the commandline flags available\n
 */
static void DisplayCommandLine(void)
{
          printf("\nPMS version %s\n", PMSVersion);
          printf("OIL version %s\n", OILVersion);
          printf("RIP version %s\n\n", RIPVersion);
          printf("[-o <none");
#ifdef PMS_SUPPORT_TIFF_OUT
          printf("|tiff|tiff_sep");
#ifdef DIRECTVIEWPDFTIFF
          printf("|tiffv");
#endif
#endif
#ifdef PMS_SUPPORT_PDF_OUT
          printf("|pdf");
#ifdef DIRECTVIEWPDFTIFF
          printf("|pdfv");
#endif
#endif
#ifdef DIRECTPRINTPCLOUT
          printf("|print|printx");
#endif
          printf(">]\n");
          printf("[-m <RIP memory in MB>]\n");
#ifdef PMS_MEM_LIMITED_POOLS
          printf("[-msys <SYSTEM memory pool in MB>]\n");
          printf("[-mapp <APPLICATION memory pool in MB>]\n");
          printf("[-mjob <JOB memory pool in MB>]\n");
          printf("[-mmisc <MISCELANEOUS memory pool in MB>]\n");
          printf("[-mpms <PMS memory pool in MB>]\n");
#endif
          printf("[-mbuf <Store job in memory buffer before ripping (size MB)>]\n");
          printf("[-x <horizonal resolution in dpi>]\n");
          printf("[-y <vertical resolution in dpi>]\n");
          printf("[-n <number of renderer threads>]\n");
          printf("[-d <RIP depth in bpp>[,<Output depth in bpp>]]\n");
          printf("[-r <color mode 1=Mono; 3=CMYK Composite; 5=RGB Composite; 6=RGB Pixel Interleaved>]\n");
          printf("[-k <to force mono if cmy absent in cmyk jobs yes|no>]\n");
          printf("[-h <screen mode 0=auto, 1=photo, 2=graphics, 3=text, 4=rip_default(default), 5=job, 6=module>]\n");
          printf("[-j <delivery method 0=push page (default), 1=pull band, 2=push band, 3=direct single, 4=direct frame\n");
          printf("                     5=direct single scanline interleaved 6=direct frame scanline interleaved>]\n");
          printf("[-e <engine simulator on|off|bypass>]\n");
#ifdef PMS_HOT_FOLDER_SUPPORT
          printf("[-i <hot folder input path>]\n");
#endif
          printf("[-f <output path>]\n");
          printf("[-p <media selection mode 0=none(default), 1=rip, 2=pms>]\n");
#ifdef PMS_SUPPORT_SOCKET
          printf("[-s <a number for socket port to listen>]\n");
          printf("[-t n]\n");
          printf("     0 <unidirectional communication>\n");
          printf("     1 <bidirectional communication>\n");
          printf("     2 <bidirectional tagged communication>\n");
#endif
          printf("[-c <PMS configuration file path>]\n");
          printf("[-a <Trap width in points. 0.0 or not specified means no trapping (default is no trapping)>]\n");
          printf("[-v when the only parameter         <for retrieving version and changeset>]\n");
          printf("    when used with other parameters <for turning on PMS log messages on|off>]\n");
          printf("[-l <for turning on logging to file of PMS debug message calls on|off>]\n");
          printf("[-z n]\n");
          printf("     1 <enable scalable consumption features in RIP>\n");
          printf("     2 <for turning on OIL performance data>\n");
          printf("     3 <enable page checksum in OIL>\n");
          printf("     4 <disable genoa compliance settings in OIL>\n");
          /* Image Decimation command line control is disabled until further notice.
          printf("     5 <disable image decimation>\n"); */
          printf("     6 <enable HVD Internal setting in OIL>\n");
          printf("     7 <enable job config feedback in monitor output>\n");
          printf("    10 <for turning on memory usage data>\n");
          printf("    11 <start RIP before opening/receiving job>\n");
          printf("[-b <trace parameter. \'-b help' for options.>]\n");
          printf("[-g <color management method>]\n");
          printf("[-q n]\n");
          printf("     0 <no PMS stdout>\n");
          printf("     1 <PMS stdout (default)>\n");
}
/**
 * \brief Routine to parse the command line
 *
 * This routine parses the commandline (only for configurable options)
 * and intializes the global variables used in PMS.\n
 */
static void ParseCommandLine(void)
{
  int i;
  char *str;
  int nArgc = g_argc;  /* leave global pointers to arguments intact to be used elsewhere */
  char **aszArgv = g_argv;

  if (nArgc < 2)
  {
    DisplayCommandLine();
    PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
    exit(1);
  }

  /* check for command line arguments */
  for ( ++aszArgv ; --nArgc > 0 ; ++aszArgv )
  {
    if (*aszArgv[0] == '-')
    {
      char * pszSwitch = aszArgv[0];
      switch (pszSwitch[1])
      {
        case '?':
          DisplayCommandLine();
          break;

        case 'a':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for default trap width.\n");
            break;
          }

          g_tSystemInfo.fTrapWidth = (float)atof(str);
          break;

        case 'b':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          if(strlen(*aszArgv) + strlen(g_tSystemInfo.szProbeTraceOption) >= (sizeof(g_tSystemInfo.szProbeTraceOption)))
          {
            PMS_SHOW_ERROR("\n Trace parameter string too long. Increase size of string buffer, or consider adding a new probe group.\n Exiting.....");
            exit(1);
          }
          else
          {
            if(strlen(g_tSystemInfo.szProbeTraceOption) > 0)
            {
              strcat(g_tSystemInfo.szProbeTraceOption, " ");
            }
            strcat(g_tSystemInfo.szProbeTraceOption, *aszArgv);
          }
          break;

        case 'c':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          g_pPMSConfigFile = *aszArgv;
          break;

        case 'd':
          {
            char *opt;
            int nRIPbpp;
            int nOutputbpp;

            if (--nArgc < 1 || pszSwitch[2] != 0)
            {
              DisplayCommandLine();
              PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
              exit(1);
            }
            ++aszArgv;

            /* convert to number */
            str = *aszArgv;

            if(str == NULL)
            {
              PMS_SHOW_ERROR("Input a number for default depth.\n");
              break;
            }

            nRIPbpp = atoi(str);
            switch (nRIPbpp)
            {
            case 1:
              g_tSystemInfo.eImageQuality = PMS_1BPP;
              break;
            case 2:
              g_tSystemInfo.eImageQuality = PMS_2BPP;
              break;
            case 4:
              g_tSystemInfo.eImageQuality = PMS_4BPP;
              break;
            case 8:
              g_tSystemInfo.eImageQuality = PMS_8BPP_CONTONE;
              break;
            case 16:
              g_tSystemInfo.eImageQuality = PMS_16BPP_CONTONE;
              break;
            default:
              PMS_SHOW_ERROR("Error: -d (%d) - Unsupported RIP bit depth request, setting 1bpp\n", nRIPbpp);
              g_tSystemInfo.eImageQuality = PMS_1BPP;
              nRIPbpp = 1;
              break;
            }

            /* Default output bit depth matches rendered bit depth */
            g_tSystemInfo.bOutputBPPMatchesRIP = 1;

            opt = strstr(str, ",");
            if(opt)
            {
              nOutputbpp = atoi(opt+1);
              switch(nOutputbpp)
              {
              case 8:
                {
                  switch(nRIPbpp)
                  {
                  case 1:
                    g_tSystemInfo.uOutputBPP = 8;
                    g_tSystemInfo.bOutputBPPMatchesRIP = 0;
                    break;
                  default:
                    PMS_SHOW_ERROR("Error: -d (%d,%d) - Unsupported RIP to output bit depth conversion requested, setting default output depth to %dbpp.\n",
                      nRIPbpp, nOutputbpp, nRIPbpp);
                    break;
                  }
                }
                break;
              default:
                PMS_SHOW_ERROR("Error: -d (%d,%d) - Unsupported RIP to output bit output bit depth conversion requested, setting default output depth to %dbpp.\n",
                  nRIPbpp, nOutputbpp, nRIPbpp);
                break;
              }
            }
            else
            {
              /* Optional Output BPP not supplied. Make Output BPP the same as RIP BPP */
              g_tSystemInfo.uOutputBPP = nRIPbpp;
            }
          }
          break;

        case 'e':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to lowercase */
          str = *aszArgv;
          for( i = 0; str[ i ]; i++)
            str[ i ] = (char)tolower( str[ i ] );

          if(!strcmp("on", *aszArgv))
          {
            g_tSystemInfo.uUseEngineSimulator = TRUE;
            g_tSystemInfo.uUseRIPAhead = TRUE;
          }
          else if(!strcmp("off", *aszArgv))
          {
            g_tSystemInfo.uUseEngineSimulator = FALSE;
            g_tSystemInfo.uUseRIPAhead = TRUE;
          }
          else if(!strcmp("bypass", *aszArgv))
          {
            g_tSystemInfo.uUseEngineSimulator = FALSE;
            g_tSystemInfo.uUseRIPAhead = FALSE;
          }
          else
          {
            PMS_SHOW_ERROR("\n******Engine Simulation option \"%s\" is not supported.\n", *aszArgv);
            PMS_SHOW_ERROR(" Available options : on, off, bypass \n");
            PMS_SHOW_ERROR(" Defaulting to Engine Simulation %s... \n\n", g_tSystemInfo.uUseEngineSimulator==TRUE?"ON":"OFF");
          }
          break;

        case 'f':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          if(strlen(*aszArgv)<PMS_MAX_OUTPUTFOLDER_LENGTH)
          {
            strcpy(g_tSystemInfo.szOutputPath,*aszArgv);
            for(i=0;i<(int)(strlen(g_tSystemInfo.szOutputPath));i++)
            {
              if(g_tSystemInfo.szOutputPath[i] == 92) /* backslash */
                g_tSystemInfo.szOutputPath[i] = 47; /* forwardslash */
            }
            /* Do not send the page data back to the back channel */
            g_bBackChannelPageOutput = 0;
          }
          break;

        case 'g':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for color management method.\n");
            break;
          }

          g_tSystemInfo.uColorManagement = atoi(str);
          break;

        case 'h':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for default screen mode.\n");
            break;
          }

          switch (atoi(str))
          {
          case 0:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Auto;
            break;
          case 1:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Photo;
            break;
          case 2:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Graphics;
            break;
          case 3:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Text;
            break;
          case 4:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_ORIPDefault;
            break;
          case 5:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Job;
            break;
          case 6:
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Module;
            break;
          default:
            PMS_SHOW_ERROR("Error: -h (%d) - Unsupported screen mode request, setting auto \n", atoi(str));
            g_tSystemInfo.eDefaultScreenMode = PMS_Scrn_Auto;
            break;
          }
          break;
#ifdef PMS_HOT_FOLDER_SUPPORT
          case 'i':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          g_pPMSHotFolderPath = *aszArgv;
          break;
#endif
        case 'j':
          /* TODO jonw this is a temporary switch, for
             convenience. Not to be documented. */
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_PAGE;
            break;
          }
          ++aszArgv;
          str = *aszArgv;

          switch (atoi(str))
          {
          case 1:
            g_tSystemInfo.eBandDeliveryType = PMS_PULL_BAND;
            break;
          case 2:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND;
            break;
          case 3:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_SINGLE;
            break;
          case 4:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_FRAME;
            break;
          case 5:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_SINGLE;
            g_tSystemInfo.bScanlineInterleave = TRUE;
            break;
          case 6:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_FRAME;
            g_tSystemInfo.bScanlineInterleave = TRUE;
            break;
          default:
          case 0:
            g_tSystemInfo.eBandDeliveryType = PMS_PUSH_PAGE;
            break;
          }

          break; 

        case 'k':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to lowercase */
          str = *aszArgv;
          for( i = 0; str[ i ]; i++)
            str[ i ] = (char)tolower( str[ i ] );

          if(!strcmp("yes", *aszArgv))
          {
            g_tSystemInfo.bForceMonoIfCMYblank = TRUE;
          }
          else if(!strcmp("no", *aszArgv))
          {
            g_tSystemInfo.bForceMonoIfCMYblank = FALSE;
          }
          else
          {
            PMS_SHOW_ERROR("\n******Force mono if CMY absent option \"%s\" is not supported.\n", *aszArgv);
            PMS_SHOW_ERROR(" Available options : yes, no \n");
            PMS_SHOW_ERROR(" Defaulting to Force mono if CMY absent = %s... \n\n", g_tSystemInfo.bForceMonoIfCMYblank==TRUE?"YES":"NO");
          }
          break;

        case 'l':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to lowercase */
          str = *aszArgv;
          for( i = 0; str[ i ]; i++)
            str[ i ] = (char)tolower( str[ i ] );

          if(strcmp(*aszArgv,"on") == 0)
            g_bLogPMSDebugMessages = 1;
          else
            g_bLogPMSDebugMessages = 0;

          break;

        case 'm': /* Output raster handling override. */
          if (--nArgc < 1)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for memory size\n");
            break;
          }

          if(!strcmp(pszSwitch,"-m"))
          {
            g_tSystemInfo.cbRIPMemory = (atoi(str)) * 1024U * 1024U;
            if(g_tSystemInfo.cbRIPMemory < MIN_REQUIRED_RIP_MEM)
            {
              g_tSystemInfo.cbRIPMemory = MIN_REQUIRED_RIP_MEM;
              PMS_SHOW_ERROR("User has input insufficient memory requirement for RIP to run\n");
              PMS_SHOW_ERROR("Overwritting it to %d\n",MIN_REQUIRED_RIP_MEM);
            }
          }
#ifdef PMS_MEM_LIMITED_POOLS
          else if(!strncmp(pszSwitch,"-msys",strlen("-msys")))
            g_tSystemInfo.cbSysMemory = atoi(str)* 1024U * 1024U;
          else if(!strncmp(pszSwitch,"-mapp",strlen("-mapp")))
            g_tSystemInfo.cbAppMemory = atoi(str)* 1024U * 1024U;
          else if(!strncmp(pszSwitch,"-mjob",strlen("-mjob")))
            g_tSystemInfo.cbJobMemory = atoi(str)* 1024U * 1024U;
          else if(!strncmp(pszSwitch,"-mmisc",strlen("-mmisc")))
            g_tSystemInfo.cbMiscMemory = atoi(str)* 1024U * 1024U;
          else if(!strncmp(pszSwitch,"-mpms",strlen("-mpms")))
            g_tSystemInfo.cbPMSMemory = atoi(str)* 1024U * 1024U;
#endif
          else if(!strncmp(pszSwitch,"-mbuf",strlen("-mbuf"))) {
            g_tSystemInfo.cbReceiveBuffer = atoi(str)* 1024U * 1024U;
            g_tSystemInfo.nStoreJobBeforeRip = TRUE;
          }
          else {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
          }
          break;


        case 'n': /* Number of renderer threads */
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for number of renderer threads.\n");
            break;
          }

          g_tSystemInfo.nRendererThreads = atoi(str);
          if( g_tSystemInfo.nRendererThreads < 1 )
          {
            g_tSystemInfo.nRendererThreads = 1;
          }
          break;

        case 'o': /* Output raster handling override. */
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to uppercase */
          str = *aszArgv;
          for( i = 0; str[ i ]; i++)
            str[ i ] = (char)toupper( str[ i ] );

          if(!strcmp("NONE", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_NONE;
          }
#ifdef PMS_SUPPORT_TIFF_OUT
          else if(!strcmp("TIFF", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_TIFF;
          }
          else if(!strcmp("TIFF_SEP", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_TIFF_SEP;
          }
#ifdef DIRECTVIEWPDFTIFF
          else if(!strcmp("TIFFV", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_TIFF_VIEW;
          }
#endif
#endif
#ifdef PMS_SUPPORT_PDF_OUT
          else if(!strcmp("PDF", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_PDF;
          }
#ifdef DIRECTVIEWPDFTIFF
          else if(!strcmp("PDFV", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_PDF_VIEW;
          }
#endif
#endif
#ifdef DIRECTPRINTPCLOUT
          else if(!strcmp("PRINT", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_DIRECT_PRINT;
          }
          else if(!strcmp("PRINTX", *aszArgv))
          {
            g_tSystemInfo.eOutputType = PMS_DIRECT_PRINTXL;
          }
#endif
          else
          {
            PMS_SHOW_ERROR("%s Output Type is not supported.\n", *aszArgv);
#ifdef PMS_SUPPORT_TIFF_OUT
            PMS_SHOW_ERROR(" Defaulting to TIFF... \n");
            g_tSystemInfo.eOutputType = PMS_TIFF;
#else
            PMS_SHOW_ERROR(" Defaulting to no output... \n");
            g_tSystemInfo.eOutputType = PMS_NONE;
#endif
          }
          break;

        case 'p':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for Media selection mode (0=None, 1=RIP, 2=PMS).\n");
            break;
          }

          switch (atoi(str))
          {
          case 0:
            g_tSystemInfo.ePaperSelectMode = PMS_PaperSelNone;
            break;
          case 1:
            g_tSystemInfo.ePaperSelectMode = PMS_PaperSelRIP;
            break;
          case 2:
            g_tSystemInfo.ePaperSelectMode = PMS_PaperSelPMS;
            break;
          default:
            PMS_SHOW_ERROR("Error: -t (%d) - Unsupported media selection mode request, setting default \n", atoi(str));
            break;
          }
          break;

        case 'q':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for PMS stdout method.\n");
            break;
          }

          g_nPMSStdOutMethod = atoi(str);
          break;

        case 'r':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for default color mode \n(1=Mono; 3=CompositeCMYk; 5=CompositeRGB; 6=RGBPixelInterleaved)\n");
            break;
          }

          switch (atoi(str))
          {
          case 1:
            g_tSystemInfo.eDefaultColMode = PMS_Mono;
            break;
          case 2:
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n CMYK Separations not valid in current version.\n Exiting.....");
            exit(1);
/*            g_tSystemInfo.eDefaultColMode = PMS_CMYK_Separations;
            break;
            */
          case 3:
            g_tSystemInfo.eDefaultColMode = PMS_CMYK_Composite;
            break;
          case 4:
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n RGB Separations not valid in current version.\n Exiting.....");
            exit(1);
/*            g_tSystemInfo.eDefaultColMode = PMS_RGB_Separations;
            break;
            */
          case 5:
            g_tSystemInfo.eDefaultColMode = PMS_RGB_Composite;
            break;
          case 6:
            g_tSystemInfo.eDefaultColMode = PMS_RGB_PixelInterleaved;
            break;
          default:
            PMS_SHOW_ERROR("Error: -r (%d) - Unsupported color mode request, setting cmyk comp\n", atoi(str));
            g_tSystemInfo.eDefaultColMode = PMS_CMYK_Composite;
            break;
          }
          break;

        case 's':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for socket port to listen on.\n");
            break;
          }

          g_SocketInPort = atoi(str);
          break;

        case 't':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for mode (0=unidirectional, 1=bidirectioanl, 2=bidirectional tagged).\n");
            break;
          }

          switch (atoi(str))
          {
          case 0:
            g_bBiDirectionalSocket = 0;
            g_bBackChannelPageOutput = 0;
            g_bTaggedBackChannel = 0;
            break;
          case 1:
            g_bBiDirectionalSocket = 1;
            g_bBackChannelPageOutput = 0;
            g_bTaggedBackChannel = 0;
            break;
          case 2:
            g_bBiDirectionalSocket = 1;
            g_bBackChannelPageOutput = 1;
            g_bTaggedBackChannel = 1;
            break;
          default:
            PMS_SHOW_ERROR("Error: -t (%d) - Unsupported communication operational mode request, setting default \n", atoi(str));
            break;
          }

          break;

        case 'v':
          if (nArgc == 1)
          {
            PMS_SHOW_ERROR("\n PMS Version %s Changeset %s\n",PMSVersion ,Changeset);
            exit(1);
          }
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }

          ++aszArgv;

          /* convert to lowercase */
          str = *aszArgv;
          for( i = 0; str[ i ]; i++)
            str[ i ] = (char)tolower( str[ i ] );

          if(strcmp(*aszArgv,"on") == 0)
            g_printPMSLog = 1;
          else
            g_printPMSLog = 0;

          break;

        case 'x':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for default horizontal resolution.\n");
            break;
          }

          g_tSystemInfo.uDefaultResX = atoi(str);

          if(g_tSystemInfo.uDefaultResY == 0)
          {
            g_tSystemInfo.uDefaultResY = g_tSystemInfo.uDefaultResX;
          }
          break;

        case 'y':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for default vertical resolution.\n");
            break;
          }

          g_tSystemInfo.uDefaultResY = atoi(str);

          if(g_tSystemInfo.uDefaultResX == 0)
          {
            g_tSystemInfo.uDefaultResX = g_tSystemInfo.uDefaultResY;
          }
          break;

        case 'z':
          if (--nArgc < 1 || pszSwitch[2] != 0)
          {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch.\n Exiting.....");
            exit(1);
          }
          ++aszArgv;

          /* convert to number */
          str = *aszArgv;

          if(str == NULL)
          {
            PMS_SHOW_ERROR("Input a number for OIL configuration mode.\n");
            break;
          }

          /* QA can understand decimal...
             -z 1 to -z 8 are reserved for OIL configurations
             -z 10 and above are used for PMS features
           */
          switch(atoi(str))
          {
          case 1:
            g_tSystemInfo.nOILconfig |= 0x01; /* bitfield - 1 = enable scalable consumption RIP features */
            break;
          case 2:
            g_tSystemInfo.nOILconfig |= 0x02; /* bitfield - 2 = display OIL timing info */
            break;
          case 3:
            g_tSystemInfo.nOILconfig |= 0x04; /* bitfield - 3 = enable checksum */
            break;
          case 4:
            g_tSystemInfo.nOILconfig |= 0x08; /* bitfield - 4 = disable genoa compliance settings */
            break;
          /* Image Decimation command line control is disabled until further notice.
          case 5:
            g_tSystemInfo.nOILconfig |= 0x10; / * bitfield - 5 = disable image decimation settings * /
            break;
          */
          case 6:
            g_tSystemInfo.nOILconfig |= 0x20; /* bitfield - 6 = enable retained raster settings (HVD internal)*/
            break;
          case 7:
            g_tSystemInfo.nOILconfig |= 0x40;  /* bitfield - 7 = enable job config feedback */
            break;
          case 8:
            g_tSystemInfo.nOILconfig |= 0x80; /* bitfield - 8 = enable raster output byte swapping */
            break;
          /* The next controllable oil feature will be -z 9 which will use bit 0x100 in the nOILconfig value
          case 9:
            g_tSystemInfo.nOILconfig |= 0x100; / * bitfield - 8 = enable ... * /
            break;
          */
#ifdef PMS_MEM_LIMITED_POOLS
          case 10:
            g_bDebugMemory = TRUE;
            break;
#endif
          case 11:
            l_bEarlyRIPStart = TRUE;
            break;
          /* The next controllable pms feature will be -z 12
          case 12:
            g_b<NextPMSFeature> = TRUE;
            break;
          */
          default:
            PMS_SHOW_ERROR("-z - invalid option\n");
            break;
          }

          break;

        case 'M':
          if (--nArgc < 1 || pszSwitch[2] != 0 ) {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch\n");
            break;
          }
          if ( g_mps_log != NULL) {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Duplicate -M log argument\n");
          }

          g_mps_log = *++aszArgv;
          break ;

        case 'E': /* MPS telemetry control */
          if (--nArgc < 1 || pszSwitch[2] != 0) {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing arguments or improper switch\n");
            break;
          }
          g_mps_telemetry = strtoul(*++aszArgv, NULL, 0);
          break ;

        case 'T':
#ifdef GPROF_BUILD
          moncontrol(0) ;
#endif /* !GPROF_BUILD */
          if (--nArgc < 1 || pszSwitch[2] != 0 ) {
            DisplayCommandLine();
            PMS_SHOW_ERROR("\n Missing -T profile argument\n");
          }
          g_profile_scope = *++aszArgv ;
          break ;

        default:
            PMS_SHOW_ERROR("ParseCommandLine: unknown option %c\n", pszSwitch[1]);
            DisplayCommandLine();
            exit(1);
            break;
      }
    }
    else
    {
      /* next argument is the jobname so we have to handover the remaining
         arguments to other routines which may want to read the jobnames */
      nJobs = nArgc;
      aszJobNames = aszArgv;
      g_tSystemInfo.bFileInput = TRUE;
      break;
    }
  }

  /* Direct raster delivery methods cannot also change bit depth doing output as there is no copy
     raster stage... the RIP renderers directly into memory supplied by the destination callback. */
  if(g_tSystemInfo.bOutputBPPMatchesRIP == 0) {
    if((g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE) ||
       (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME)) {
      PMS_SHOW_ERROR("Configuration Error: Bit depth cannot be changed when using 'direct' raster delivery methods.  Changing to 'Push Band' (-j 2).\n");
      g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND;
    }
  }

  /* RGB pixel interleaved direct from RIP require 8 bpp */
  if(g_tSystemInfo.eDefaultColMode == PMS_RGB_PixelInterleaved) {
    if(g_tSystemInfo.eImageQuality != PMS_8BPP_CONTONE) {
      PMS_SHOW_ERROR("Warning: In-RIP RGB Pixel Interleaving requires 8 bits per pixel per colorant.  Changing to 8 bpp (-d 8).\n");
      g_tSystemInfo.eImageQuality = PMS_8BPP_CONTONE;
    }
  }

  /* if x and y are not set expicitly, then set them to default here */
  if(g_tSystemInfo.uDefaultResX == 0)
  {
    g_tSystemInfo.uDefaultResY = g_tSystemInfo.uDefaultResX = 600;
  }

  /* ensure that if socket is not used then no data is returned through socket - ie reset anything set by t flag */
  if ((g_SocketInPort == 0) && (g_bBiDirectionalSocket != 0))
  {
    PMS_SHOW_ERROR("Warning: no socket specified - ignoring 't' flag\n");
    g_bBiDirectionalSocket = 0;
    g_bBackChannelPageOutput = 0;
    g_bTaggedBackChannel = 0;
  }

  /* Push direct methods rely on the gFrameBuffer memory, but only one buffer has been
     allocated, therefore rip ahead cannot work. Rip Ahead can only work if there
     are more than one frame buffer or band buffer - so that the rip can continue
     rendering whilst the pms take its time to print the previous bands/frames.
     Other band delivery methods use the page store in OIL (oil_page_handler.c).
     Consider allocating more buffers if rip ahead is required.
     \todo Remove this section when the generic 'Page Store' module is implemented. */
  if(g_tSystemInfo.uUseRIPAhead == TRUE) {
    if((g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME)) {
      PMS_SHOW_ERROR("Warning: 'Push Band Direct Frame' does not work with 'Rip Ahead'. Switching to '-e bypass'.\n");
      g_tSystemInfo.uUseEngineSimulator = FALSE;
      g_tSystemInfo.uUseRIPAhead = FALSE;
    }
    if((g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE)) {
      PMS_SHOW_ERROR("Warning: 'Push Band Direct Single' does not work with 'Rip Ahead'. Switching to '-e bypass'.\n");
      g_tSystemInfo.uUseEngineSimulator = FALSE;
      g_tSystemInfo.uUseRIPAhead = FALSE;
    }
  }

  if( g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE &&
      g_tSystemInfo.nRendererThreads > 1 ) {
    PMS_SHOW_ERROR("Warning: 'Push Band Direct Single' does not work with more than 1 thread. Switching to 'Push Band'.\n");
    g_tSystemInfo.eBandDeliveryType = PMS_PUSH_BAND;
  }
}


/**
 * \brief Starts the RIP
 *
 * This routine calls OIL interface functions to start the RIP and pass it PMS callback function pointers.\n
 */
void StartOIL()
{
  PMS_TyJob * pstJob;
  static unsigned int  nJobNumber = 1;
  int bJobSubmitted;
  int bJobSucceeded;

  g_eRipState = PMS_Rip_Initializing;

  /* Check that the PMS API function pointers have been initialised */
  PMS_ASSERT(l_apfnRip_IF_KcCalls, ("StartOIL: PMS API function array point not initialised.\n"));

  /* initialise the PDL */
  if(!OIL_Init((void(***)())l_apfnRip_IF_KcCalls))
  {
    PMS_RippingComplete();
    return ;
  }

  if(l_bEarlyRIPStart) {
    /* Used to start the RIP before a job is received.
       The default behaviour is to start the RIP when the first job is opened.
       The RIP is only shutdown if the shutdown mode is "OIL_RIPShutdownTotal".
       If start RIP isn't called now, it will be called at some point during OIL_Start function. */
    OIL_StartRIP();
  }

  while(PMS_IM_WaitForInput())
  {
    /* there is some data to process */
    pstJob = CreateJob(nJobNumber);

    if( pstJob!=NULL )
    {
      g_eRipState = PMS_Rip_In_Progress;

#ifdef PMS_INPUT_IS_FILE
      strcpy(pstJob->szJobName, szJobFilename);
      if(pstJob->bFileInput)
        strcpy(pstJob->szJobFilename,szJobFilename);
      else
        pstJob->szJobFilename[0] = '\0';
#endif

      do
      {
        /* start interpreter */
        bJobSucceeded = OIL_Start(pstJob, &bJobSubmitted);

        if( bJobSubmitted )
        {
          nJobNumber++;
          pstJob->uJobId = nJobNumber;
        }

      } while( bJobSucceeded );

      PMS_IM_CloseActiveDataStream();

      /* job finished, free resource */
      OSFree(pstJob,PMS_MemoryPoolPMS);
    }
    /* If restart is requested, then get out of this loop */
    if(g_tSystemInfo.nRestart) {
      break;
    }
  }

  /* tidy up at the end */
  FreePMSFramebuffer();
  OIL_Exit();
}

/**
 * \brief Clean-Up Routine
 *
 * Routine to release the resources after job completion.\n
 */
static void CleanUp()
{
  if( g_pstTrayInfo != NULL )
  {
    OSFree( g_pstTrayInfo, PMS_MemoryPoolPMS );
    g_pstTrayInfo = NULL;
    g_nInputTrays = 0;
  }
  if( g_pstOutputInfo != NULL )
  {
    OSFree( g_pstOutputInfo, PMS_MemoryPoolPMS );
    g_pstOutputInfo = NULL;
    g_nOutputTrays = 0;
  }

  PMS_DestroySemaphore(g_semCheckin);
  PMS_DestroySemaphore(g_semPageQueue);
  PMS_DestroySemaphore(g_semTaggedOutput);
  PMS_DestroySemaphore(g_semPageComplete);

  PMS_DestroyCriticalSection(g_csPageList);
  PMS_DestroyCriticalSection(g_csSocketInput);
  PMS_DestroyCriticalSection(g_csMemoryUsage);
}

/**
 * \brief Job intializing routine
 *
 * Routine to initialize the job structure which is then passed on to the RIP.\n
 */
static PMS_TyJob * CreateJob(unsigned int nJobNumber)
{
  PMS_TyJob * pstJob = (PMS_TyJob *)OSMalloc(sizeof(PMS_TyJob),PMS_MemoryPoolPMS);

  if( pstJob != NULL )
  {
    memcpy( pstJob, &g_tJob, sizeof(PMS_TyJob) );

    pstJob->uJobId = nJobNumber;              /* Job ID number */
#ifdef PMS_INPUT_IS_FILE
    pstJob->szJobName[0] = '\0';              /* Job name */
#else
    strcpy(pstJob->szJobName, "Streamed");    /* Job name */
#endif
  }

  return pstJob;
}


