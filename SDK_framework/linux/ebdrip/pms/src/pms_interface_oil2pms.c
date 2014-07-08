/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_interface_oil2pms.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief OIL TO PMS Interface.
 *
 */

#include "pms.h"
#include "pms_export.h"
#include "pms_filesys.h"
#include "pms_file_in.h"
#include "pms_scrn.h"
#include "pms_platform.h"
#include "pms_interface_oil2pms.h"
#include "pms_malloc.h"
#include "pms_page_handler.h"
#include "pms_engine_simulator.h"
#include "pms_input_manager.h"
#include "oil_entry.h"
#include <string.h> /* for strcmp */
#include <math.h> /* for fabs */
#ifdef GG_DEBUG_MEM_TRACE
#include <stdio.h>
#endif

#define PMS_TRAY_TRACE(_msg_, ...)
/* #define PMS_TRAY_TRACE(_msg_, ...) printf(_msg_, ## __VA_ARGS__) */

#ifdef USE_UFST5
#define UFST_ENABLED 1
#endif
#ifdef USE_UFST7
#define UFST_ENABLED 1
#endif

#ifdef UFST_ENABLED
/* UFST */
#include "pms_ufst.h"
#endif

#ifdef USE_FF
#include "pms_ff.h"
#endif

#ifdef UFST_ENABLED
extern const unsigned char *g_fontset_fco;
extern PMS_TyUfstFontMap g_fontsetmap[];
extern unsigned int g_fontsetmap_len;
extern const unsigned char *g_plug__xi_fco;
extern const unsigned char *g_wd____xh_fco;
extern const unsigned char *g_umt_ss;
extern const unsigned char *g_utt_ss;
extern const unsigned char *g_uif_ss;
#endif

#ifdef USE_FF
extern const unsigned int ff_ss_data_length;
extern const unsigned char *ff_ss_data1;
extern const unsigned int ff_ss_mapdata_length;
extern const unsigned char *ff_ss_mapdata1;
extern unsigned int g_fontset_pfr_len;
extern unsigned int g_fontset_fit_len;
extern unsigned char *g_fontset_pfr;
extern unsigned char *g_fontset_fit;
extern PMS_TyUfstFontMap g_fontsetmap[];
extern unsigned int g_fontsetmap_len;
extern const unsigned int ffrom_support_pfr_len;
extern const unsigned char *g_ffrom_support_pfr;
extern const unsigned int ffrom_support_fit_len;
extern const unsigned char *g_ffrom_support_fit;
#endif

/* PMS_WriteStdOut output method. */
extern int g_nPMSStdOutMethod;
extern void *g_semTaggedOutput;

#include "pms_thread.h"

/**
 * \brief Array of function pointers to PMS callback functions.
 */
int (*apfnRip_IF_KcCalls[PMS_TOTAL_NUMBER_OF_API_FUNCTIONS])();

/** Optional log file output */
FILE * l_pLogFile = NULL;

typedef struct {
  int lineStride;
  int colorantStride;
} RasterLayout;

static RasterLayout sRasterLayout;

/*! \brief Paper Size table.
*/
/* UNPRINTABLE in unprintable area of the engine in pixels at 300dpi expressed as micro-inches
     - e.g for 50 pixels = (50/300 *1000000) */
#define UNPRINTABLE  166667, 166667, 166667, 166667

/* WideA4 information is defined in pms_export.h for use in oil_psconfig.c*/
/* PCL Logical page figure is defined by the PCL spec and should not be need to changed
   It is expressed as pixels at 300dpi - e.g for 60PX = (60/300 *1000000)
                                            table values are in micro-inches */
#define LOGICAL_PAGE 0, 0, 250000, 250000
#define LOGICALPAGE59PX 0, 0, 196667, 196667
#define LOGICALPAGE60PX 0, 0, 200000, 200000
#define LOGICALPAGE71PX 0, 0, 236667, 236667
#define LOGICALPAGE75PX 0, 0, 250000, 250000

/* Paper sizes defined in points (1/72"), these values match the ISO standard but may be changed if required */
static PMS_TyPaperInfo l_apPapers[] = {
  /* Portrait feed sizes */
  { PMS_SIZE_LETTER,     612.00,  792.00, "LETTER",
    UNPRINTABLE, LOGICALPAGE75PX,
    PMS_PCL5_LETTER, PMS_PCLX_LETTER,
    "/Letter", "(LETTER)"},
  { PMS_SIZE_A4,         595.20,  841.68, "A4",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_A4, PMS_PCLX_A4,
    "/A4", "(A4)"},
  { PMS_SIZE_LEGAL,      612.00, 1008.00, "LEGAL",
    UNPRINTABLE, LOGICALPAGE75PX,
    PMS_PCL5_LEGAL, PMS_PCLX_LEGAL,
    "/Legal", "(LEGAL)"},
  { PMS_SIZE_EXE,        522.00,  756.00, "EXE",
    UNPRINTABLE, LOGICALPAGE75PX,
    PMS_PCL5_EXE, PMS_PCLX_EXE,
    "/Executive", "(EXEC)"},
  { PMS_SIZE_A3,         841.68, 1190.40, "A3",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_A3, PMS_PCLX_A3,
    "/A3", "(A3)"},
  { PMS_SIZE_TABLOID,    792.00, 1224.00, "TABLOID",
    UNPRINTABLE, LOGICALPAGE75PX,
    PMS_PCL5_TABLOID, PMS_PCLX_TABLOID,
    "/Tabloid", "(TABLOID)"},
  { PMS_SIZE_A5,         420.96,  595.20, "A5",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_A5, PMS_PCLX_A5,
    "/A5", "(A5)"},
  { PMS_SIZE_A6,         298.00,  420.00, "A6",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_A6, PMS_PCLX_A6,
    "/A6", "(A6)"},
  { PMS_SIZE_C5_ENV,     459.21,  649.13, "C5_ENV",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_C5_ENV, PMS_PCLX_C5_ENV,
    "/C5", "(C5)"},
  { PMS_SIZE_DL_ENV,     311.81,  623.62, "DL_ENV",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_DL_ENV, PMS_PCLX_DL_ENV,
    "/DL", "(DL)"},
  { PMS_SIZE_LEDGER,    1224.00,  792.00, "LEDGER",
    UNPRINTABLE, LOGICALPAGE75PX,
    PMS_PCL5_LEDGER, PMS_PCLX_LEDGER,
    "/Ledger", "(LEDGER)"},
  { PMS_SIZE_OFUKU,     419.53,   566.93, "OFUKU",
    UNPRINTABLE, LOGICALPAGE71PX,
    PMS_PCL5_OFUKU, PMS_PCLX_OFUKU,
    "/Ofuku", "(OFUKU)"},
  { PMS_SIZE_JISB4,     727.20, 1029.60, "JISB4",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_JISB4, PMS_PCLX_JISB4,
    "/JISB4", "(JIS B4)"},
  { PMS_SIZE_JISB5,     518.40, 727.20, "JISB5",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_JISB5, PMS_PCLX_JISB5,
    "/JISB5", "(JIS B5)"},
    /* Landscape feed sizes */
  { PMS_SIZE_LETTER_R,   792.00,  612.00, "LETTER_R",
    UNPRINTABLE, LOGICALPAGE60PX,
    PMS_PCL5_LETTER, PMS_PCLX_LETTER,
    "/Letter", "(LETTER)"},
  { PMS_SIZE_A4_R,       841.68,  595.20, "A4_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_A4, PMS_PCLX_A4,
    "/A4", "(A4)"},
  { PMS_SIZE_LEGAL_R,   1008.00,  612.00, "LEGAL_R",
    UNPRINTABLE, LOGICALPAGE60PX,
    PMS_PCL5_LEGAL, PMS_PCLX_LEGAL,
    "/Legal", "(LEGAL)"},
  { PMS_SIZE_EXE_R,      756.00,  522.00, "EXE_R",
    UNPRINTABLE, LOGICALPAGE60PX,
    PMS_PCL5_EXE, PMS_PCLX_EXE,
    "/Executive", "(EXEC)"},
  { PMS_SIZE_A3_R,      1190.40,  841.68, "A3_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_A3, PMS_PCLX_A3,
    "/A3", "(A3)"},
  { PMS_SIZE_TABLOID_R, 1224.00,  792.00, "TABLOID_R",
    UNPRINTABLE, LOGICALPAGE60PX,
    PMS_PCL5_TABLOID, PMS_PCLX_TABLOID,
    "/Tabloid", "(TABLOID)"},
  { PMS_SIZE_A5_R,       595.20,  420.00, "A5_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_A5, PMS_PCLX_A5,
    "/A5", "(A5)"},
  { PMS_SIZE_A6_R,       420.00,  298.00, "A6_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_A6, PMS_PCLX_A6,
    "/A6", "(A6)"},
  { PMS_SIZE_C5_ENV_R,   649.13,  459.21, "C5_ENV_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_C5_ENV, PMS_PCLX_C5_ENV,
    "/C5", "(C5)"},
  { PMS_SIZE_DL_ENV_R,   623.62,  311.81, "DL_ENV_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_DL_ENV, PMS_PCLX_DL_ENV,
    "/DL", "(DL)"},
  { PMS_SIZE_LEDGER_R,   792.00, 1224.00, "LEDGER_R",
    UNPRINTABLE, LOGICALPAGE60PX,
    PMS_PCL5_LEDGER, PMS_PCLX_LEDGER,
    "/Ledger", "(LEDGER)"},
  { PMS_SIZE_OFUKU_R,    566.93,  419.53, "OFUKU_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_OFUKU, PMS_PCLX_OFUKU,
    "/Ofuku", "(OFUKU)"},
  { PMS_SIZE_JISB4_R,    1029.60,  727.20, "JISB4_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_JISB4, PMS_PCLX_JISB4,
    "/JISB4", "(JIS B4)"},
  { PMS_SIZE_JISB5_R,    727.20,  518.40, "JISB5_R",
    UNPRINTABLE, LOGICALPAGE59PX,
    PMS_PCL5_JISB5, PMS_PCLX_JISB5,
    "/JISB5", "(JIS B5)"},

  { PMS_SIZE_CUSTOM,    350.00,  700.00, "CUSTOM",
    UNPRINTABLE, LOGICAL_PAGE,
    PMS_PCL5_CUSTOM, PMS_PCLX_CUSTOM,
    "/Custom", "(CUSTOM)"},
  { PMS_SIZE_DONT_KNOW,  595.20,  841.68, "",
    UNPRINTABLE, LOGICAL_PAGE,
    PMS_PCL5_CUSTOM, PMS_PCLX_DEFAULT,
    "/Letter", ""},
};
#define PMS_NUM_PAPERS (sizeof(l_apPapers) / sizeof(l_apPapers[0]))

static PMS_TyMediaType l_apMediaType[] = {
  { PMS_TYPE_PLAIN,"Plain","PLAIN",                       /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_PLAIN, "Plain", "Plain"},                /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_BOND,"Bond","BOND",                          /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_BOND, "Bond", "Bond"},                   /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_SPECIAL,"Special","SPECIAL",                 /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_SPECIAL, "Special", "Special"},          /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_GLOSSY,"Glossy","GLOSSY",                    /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_GLOSSY, "Glossy", "Glossy"},             /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_TRANSPARENCY,"Transparency","TRANSPARENCY",  /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_TRANSPARENCY, "Transparency", "Transparency"},  /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_RECYCLED,"Recycled","RECYCLED",              /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_RECYCLED, "Recycled", "Recycled"},       /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_THICK,"Thick","THICK",                       /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_THICK, "Thick", "Thick"},                /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_ENVELOPE,"Envelope","ENVELOPE",              /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_ENVELOPE, "Envelope", "Envelope"},       /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_POSTCARD,"Postcard","POSTCARD",              /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_POSTCARD, "Postcard", "Postcard"},       /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_THIN,"Thin","THIN",                          /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_THIN, "Thin", "Thin"},                   /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_LABEL,"Label","LABEL",                       /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_LABEL, "Label", "Label"},                /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_PREPRINTED,"Preprinted","PREPRINTED",        /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_PREPRINTED, "Preprinted", "Preprinted"}, /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_LETTERHEAD,"Letterhead","LETTERHEAD",        /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_LETTERHEAD, "Letterhead", "Letterhead"}, /** PCL5 value, PCL5 string, PCLXL string **/
  { PMS_TYPE_DONT_KNOW,"","",                             /** PMS value, PS string, PJL string **/
    PMS_PCL5TYPE_DONT_KNOW, "", ""},                      /** PCL5 value, PCL5 string, PCLXL string **/
};
#define PMS_NUM_MEDIATYPES (sizeof(l_apMediaType) / sizeof(l_apMediaType[0]))

static PMS_TyMediaSource l_apMediaSource[] = {
  { PMS_TRAY_AUTO, PMS_PSTRAY_AUTO, "AUTO",                   /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_AUTO, PMS_PCLXTRAY_AUTO},                    /** PCL5 value, PCLXL value **/
  { PMS_TRAY_BYPASS, PMS_PSTRAY_BYPASS, "BYPASS",             /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_BYPASS, PMS_PCLXTRAY_BYPASS},                /** PCL5 value, PCLXL value **/
  { PMS_TRAY_MANUALFEED, PMS_PSTRAY_MANUALFEED, "MANUALFEED", /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_MANUALFEED, PMS_PCLXTRAY_MANUALFEED},        /** PCL5 value, PCLXL value **/
  { PMS_TRAY_TRAY1, PMS_PSTRAY_TRAY1, "TRAY1",                /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_TRAY1, PMS_PCLXTRAY_TRAY1},                  /** PCL5 value, PCLXL value **/
  { PMS_TRAY_TRAY2, PMS_PSTRAY_TRAY2, "TRAY2",                /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_TRAY2, PMS_PCLXTRAY_TRAY2},                  /** PCL5 value, PCLXL value **/
  { PMS_TRAY_TRAY3, PMS_PSTRAY_TRAY3, "TRAY3",                /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_TRAY3, PMS_PCLXTRAY_TRAY3},                  /** PCL5 value, PCLXL value **/
  { PMS_TRAY_ENVELOPE, PMS_PSTRAY_ENVELOPE, "ENVELOPE",             /** PMS value, PS value, PJL string **/
    PMS_PCL5TRAY_ENVELOPE, PMS_PCLXTRAY_ENVELOPE},                  /** PCL5 value, PCLXL value **/
};
#define PMS_NUM_MEDIASOURCES (sizeof(l_apMediaSource) / sizeof(l_apMediaSource[0]))

static PMS_TyMediaDest l_apMediaDest[] = {
  { PMS_OUTPUT_TRAY_AUTO,"","AUTO",                         /** PMS value, PS string, PJL string **/
    PMS_PCL5OUTPUT_TRAY_AUTO, PMS_PCLXOUTPUT_TRAY_AUTO},    /** PCL5 value, PCLXL value **/
  { PMS_OUTPUT_TRAY_UPPER,"Upper","UPPER",                  /** PMS value, PS string, PJL string **/
    PMS_PCL5OUTPUT_TRAY_UPPER, PMS_PCLXOUTPUT_TRAY_UPPER},  /** PCL5 value, PCLXL value **/
  { PMS_OUTPUT_TRAY_LOWER,"Lower","LOWER",                  /** PMS value, PS string, PJL string **/
    PMS_PCL5OUTPUT_TRAY_LOWER, PMS_PCLXOUTPUT_TRAY_LOWER},  /** PCL5 value, PCLXL value **/
  { PMS_OUTPUT_TRAY_EXTRA,"Extra","EXTRA",                  /** PMS value, PS string, PJL string **/
    PMS_PCL5OUTPUT_TRAY_EXTRA, PMS_PCLXOUTPUT_TRAY_EXTRA},  /** PCL5 value, PCLXL value **/
};
#define PMS_NUM_MEDIADESTS (sizeof(l_apMediaDest) / sizeof(l_apMediaDest[0]))

static PMS_TyMediaColor l_apMediaColor[] = {
  {PMS_COLOR_WHITE, "White", "WHITE"},
  {PMS_COLOR_RED, "Red", "RED"},
  {PMS_COLOR_YELLOW, "Yellow", "YELLOW"},
  {PMS_COLOR_GREEN, "Green", "GREEN"},
};
#define PMS_NUM_MEDIACOLORS (sizeof(l_apMediaColor) / sizeof(l_apMediaColor[0]))

/* We use these globals rather than, for instance, the
 * RasterDescription, purely because this code is called before such
 * things are set up in the first place. */

/** Set of a maxiumum of 4 framebuffers */
unsigned char *gFrameBuffer = NULL;
/** The size in bytes of each framebuffer, if present. */
static size_t gFrameBufferSize = 0;
/** The page size in pixels. */
static unsigned int gnPageHeightPixels = 0;
/** The height in lines of each RIP band destined for the framebuffer,
    if present. */
static unsigned int guRIPBandHeight = 0;
/** The number of bytes per line in each RIP band destined for the
    framebuffer, if present. */
static unsigned int guRIPBytesPerLine = 0;
/** The number of color components in the framebuffer, if present. */
static unsigned int guRIPComponents = 0;
/** The number of color components in each RIP band destined for the
    framebuffer, if present. */
static unsigned int guRIPComponentsPerBand = 0;

/**
 * \brief PMS Callback routine to enable OIL to look at the Data Stream chunk.
 *
 * This routine reads the requested number of bytes from the input stream and puts them
 * in the buffer.\n
 */
int PMS_PeekDataStream(unsigned char *pubBuffer, int nBytesToRead)
{
  return PMS_IM_PeekActiveDataStream(pubBuffer, nBytesToRead);
}

/**
 * \brief PMS Callback routine to enable OIL to open a file on persistent storage.
 *
 * This routine opens a file on persistent storage \n
 */
int PMS_FileOpen(char * pzPath, char * flags, void ** pHandle)
{
  return (File_Open( pzPath, flags, pHandle ));
}

/**
 * \brief PMS Callback routine to enable OIL to close a file on persistent storage.
 *
 * This routine closes a file on persistent storage \n
 */
int PMS_FileClose(void * handle)
{
  return (File_Close( handle ));
}

/**
 * \brief PMS Callback routine to enable OIL to read a file from persistent storage.
 *
 * This routine reads a file from persistent storage \n
 */
int PMS_FileRead(unsigned char * buffer, int nBytesToRead, void * handle)
{
  return (File_Read(buffer, nBytesToRead, handle));
}

/**
 * \brief PMS Callback routine to enable OIL to seek a file from persistent storage.
 *
 * This routine seek a file from persistent storage \n
 */
int PMS_FileSeek(void * handle, long *pPosition, int nWhence)
{
  return (File_Seek(handle, pPosition, nWhence));
}

/**
 * \brief PMS Callback routine to enable OIL to get the size of a file in bytes.
 *
 * This routine gets the file size in bytes \n
 */
int PMS_FileBytes(void * handle)
{
  return (File_Bytes(handle));
}

/**
 * \brief PMS Callback routine to enable OIL to consume the Data Stream chunk.
 *
 * This routine reads the requested number of bytes from the input stream and puts them
 * in the buffer.\n
 */
int PMS_ConsumeDataStream(int nBytesToConsume)
{
  return PMS_IM_ConsumeActiveDataStream(nBytesToConsume);
}

/**
 * \brief PMS Callback routine to enable OIL to read and consume the Data Stream chunk.
 *
 * This routine reads the requested number of bytes from the input stream and puts them
 * in the buffer.\n
 */
int PMS_ReadDataStream(unsigned char *pubBuffer, int nBytesToRead)
{
  int nRead;
  nRead = PMS_IM_PeekActiveDataStream(pubBuffer, nBytesToRead);
  PMS_IM_ConsumeActiveDataStream(nRead);
  return (nRead);
}

/**
 * \brief PMS routine to output a message to console.
 *
 */
int PMS_WriteStdOut(unsigned char * pBuffer, int nBytesToWrite, int * pnBytesWritten)
{
  switch(g_nPMSStdOutMethod)
  {
    default:
    /* No output... fastest */
    case 0:
      *pnBytesWritten = nBytesToWrite;
      break;
    /* Console output... slow if serial port.
       Consider buffering and outputting in a lower priority task/thread. */
    case 1:
      {
        int nWritten;
        nWritten = (int)fwrite( pBuffer, 1, nBytesToWrite, stdout );
        fflush( stdout );
        if(nWritten<0)
        {
          *pnBytesWritten = 0;
          return nWritten;
        }
        *pnBytesWritten = nBytesToWrite;
      }
      break;
  }
  return 0;
}

/**
 * \brief PMS Callback routine to enable OIL to write to the stream.
 *
 * \param eType Type of data content to be output.
 * \param pContext Pointer to structure to provide extra parameters for type of data.
 * \param pubBuffer Actual data to write.
 * \param nBytesToWrite Number of bytes to write.
 * \param pnBytesWritten Address of int to store number of bytes written.
 * \return 0 if successful, otherwise error.
 */
int PMS_WriteDataStream(PMS_eBackChannelWriteTypes eType, void *pContext, unsigned char *pubBuffer, int nBytesToWrite, int *pnBytesWritten)
{
  int nResult = 0;
  int nTagWritten;

  if(g_bTaggedBackChannel)  {
    PMS_WaitOnSemaphore_Forever(g_semTaggedOutput);
  }

  switch(eType)
  {
  case PMS_WRITE_BACK_CHANNEL: /* always */
    {
      if(g_bTaggedBackChannel)
      {
        nResult = PMS_IM_WriteToActiveDataStream((unsigned char*)"<GGEBDBackChannel>", 18, &nTagWritten);
        PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
        PMS_ASSERT(nTagWritten==18,("PMS_WriteDataStream failed\n"));
        if(nResult)
          break;
      }

      PMS_WriteStdOut(pubBuffer, nBytesToWrite, pnBytesWritten);

      nResult = PMS_IM_WriteToActiveDataStream(pubBuffer, nBytesToWrite, pnBytesWritten);
      PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
      PMS_ASSERT(*pnBytesWritten==nBytesToWrite,("PMS_WriteDataStream failed\n"));
      if(nResult)
        break;

      if(g_bTaggedBackChannel)
      {
        nResult = PMS_IM_WriteToActiveDataStream((unsigned char*)"</GGEBDBackChannel>\n", 20, &nTagWritten);
        PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
        PMS_ASSERT(nTagWritten==20,("PMS_WriteDataStream failed\n"));
        if(nResult)
          break;
      }

      break;
    }
  case PMS_WRITE_DEBUG_MESSAGE:
    {
      /* only if logging, since the receiving end might not be able to handle
      the extra information. */
      if(g_printPMSLog)
      {
        if(g_bTaggedBackChannel)
        {
          nResult = PMS_IM_WriteToActiveDataStream((unsigned char*)"<GGEBDDebugMsg>", 15, &nTagWritten);
          PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
          PMS_ASSERT(nTagWritten==15,("PMS_WriteDataStream failed\n"));
          if(nResult)
            break;
        }

        PMS_WriteStdOut(pubBuffer, nBytesToWrite, pnBytesWritten);

        nResult = PMS_IM_WriteToActiveDataStream(pubBuffer, nBytesToWrite, pnBytesWritten);
        PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
        PMS_ASSERT(*pnBytesWritten==nBytesToWrite,("PMS_WriteDataStream failed\n"));
        if(nResult)
          break;

        if(g_bTaggedBackChannel)
        {
          nResult = PMS_IM_WriteToActiveDataStream((unsigned char*)"</GGEBDDebugMsg>\n", 17, &nTagWritten);
          PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
          PMS_ASSERT(nTagWritten==17,("PMS_WriteDataStream failed\n"));
          if(nResult)
            break;
        }
      }

      break;
    }
  case PMS_WRITE_FILE_OUT:
    {
      PMS_TyBackChannelWriteFileOut *p = (PMS_TyBackChannelWriteFileOut *)pContext;
      unsigned char szCmd[1024];

      PMS_ASSERT(g_bBackChannelPageOutput==1, ("PMS_WriteDataStream... Sending page output without g_bBackChannelPageOutput set?"));

      if(g_bTaggedBackChannel)
      {
        PMS_ASSERT(p, ("PMS_WriteDataStream NULL Context"));
        if(p->uFlags & 0x1)
        {
          sprintf((char*)szCmd, "<GGEBDFile name=%s pos=%lu>", p->szFilename, p->ulAbsPos);
        }
        else
        {
          sprintf((char*)szCmd, "<GGEBDFile name=%s>", p->szFilename);
        }
        nResult = PMS_IM_WriteToActiveDataStream(szCmd, (int)strlen((const char*)szCmd), &nTagWritten);
        PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
        PMS_ASSERT(nTagWritten==(int)strlen((const char*)szCmd),("PMS_WriteDataStream failed\n"));
        if(nResult)
          break;
      }

      nResult = PMS_IM_WriteToActiveDataStream(pubBuffer, nBytesToWrite, pnBytesWritten);
      PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
      PMS_ASSERT(*pnBytesWritten==nBytesToWrite,("PMS_WriteDataStream failed\n"));
      if(nResult)
        break;

      if(g_bTaggedBackChannel)
      {
        nResult = PMS_IM_WriteToActiveDataStream((unsigned char*)"</GGEBDFile>\n", 13, &nTagWritten);
        PMS_ASSERT(nResult==0,("PMS_WriteDataStream failed\n"));
        PMS_ASSERT(nTagWritten==13,("PMS_WriteDataStream failed\n"));
        if(nResult)
          break;
      }

      break;
    }
  default:
    {
      PMS_FAIL("PMS_WriteDataStream unknown data type, %d\n", eType);
      nResult = -1;
    }
    break;
  }

  if(g_bTaggedBackChannel) {
    PMS_IncrementSemaphore(g_semTaggedOutput);
  }

  return nResult;
}

/**
 * \brief PMS Callback routine to enable OIL to check-in the RIPped page.
 *
 * This routine passes on the RIPped page to PMS.\n
 * If RIP ahead is enabled, this page is appended to the PMS page queue
 * If RIP ahead is not enabled, the page is directly passed on to the Print
 */
int PMS_CheckinPage(PMS_TyPage *ptPMSPage)
{
  g_pstCurrentPMSPage = ptPMSPage;

  /* A zero ptPMSPage indicates end of job */
  if(ptPMSPage != NULL) {
    if (g_tSystemInfo.uUseRIPAhead)
    {
      /* add to queue and let engine simulator determine when to output */
      AppendToPageQueue(ptPMSPage);
    }
    else
    {
      if(g_tSystemInfo.eBandDeliveryType == PMS_PUSH_PAGE ||
         g_tSystemInfo.eBandDeliveryType == PMS_PULL_BAND)
      {
        /* No RIP ahead, output the page and signal pagedone before returning */
        PrintPage(ptPMSPage);
        OIL_PageDone(ptPMSPage);
      }
    }
  } else { /* else job done */
    PrintPage(NULL);
  }
  /* clear frame buffer was done here.
   * Should be done at the end of the page in PMS_CheckinBand
   */
  return TRUE;
}

/**
 * \brief PMS Callback routine to enable OIL to check-in the RIPped band.
 *
 * This routine passes on the band packet to PMS.\n
 * This band packet is appended to the PMS page structure
 */
int PMS_CheckinBand(PMS_TyBandPacket *ThisBand)
{
  int i, nColorant;
  unsigned char   *pRasterBuffer;
  PMS_TyPage *pstPageToPrint;

  pstPageToPrint = g_pstCurrentPMSPage;

  if(ThisBand == NULL) /* last band */
  {
    if (g_tSystemInfo.uUseRIPAhead)
    {
      PMS_IncrementSemaphore(g_semPageComplete);
    }
    else
    {
      /* No RIP ahead, output the page and signal pagedone before returning */
      PrintPage(g_pstCurrentPMSPage);
      OIL_PageDone(g_pstCurrentPMSPage);
      /* clear frame buffer so next page set up is called from oil */
      if ( gFrameBuffer != NULL )
      {
        OSFree(gFrameBuffer,PMS_MemoryPoolPMS);
        gFrameBuffer = NULL;
        gFrameBufferSize = 0;
      }
    }
    g_pstCurrentPMSPage = NULL;
    return TRUE;
  }

  if(g_tSystemInfo.eOutputType != PMS_NONE)
  {
    /* How much data do we need to copy for each line? */
    int lineSize = pstPageToPrint->nRasterWidthBits / 8;

    for(i=0; i < PMS_MAX_PLANES_COUNT; i++)
    {
      /* determine valid rasters and hook them to PMS page */
      if(ThisBand->atColoredBand[i].ePlaneColorant != PMS_INVALID_COLOURANT)
      {
        int height = ThisBand->atColoredBand[i].uBandHeight;
        int bytesPerBand = height * lineSize;

        nColorant = (int)ThisBand->atColoredBand[i].ePlaneColorant;
        nColorant = nColorant % PMS_MAX_PLANES_COUNT; /* shortcut to map RGB to correct plane */
        pstPageToPrint->atPlane[nColorant].atBand[ThisBand->uBandNumber-1].uBandNumber = ThisBand->uBandNumber;
        pstPageToPrint->atPlane[nColorant].atBand[ThisBand->uBandNumber-1].uBandHeight = height;
        pstPageToPrint->atPlane[nColorant].atBand[ThisBand->uBandNumber-1].cbBandSize = bytesPerBand;

        /* allocate memory for rasters if we're not in direct mode, or if the
         * output from the core is scan-line interleaved. */
        if (g_tSystemInfo.eBandDeliveryType != PMS_PUSH_BAND_DIRECT_FRAME ||
            g_tSystemInfo.bScanlineInterleave)
        {
          int y;
          unsigned char *src;
          int srcStride = lineSize;

          if(g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE ||
             g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME)
          {
#ifdef GG_64395
              srcStride = sRasterLayout.lineStride;
#endif
          }

          pRasterBuffer = (unsigned char*)OSMalloc(bytesPerBand, PMS_MemoryPoolPMS);
          if(!pRasterBuffer)
          {
            PMS_SHOW_ERROR("\n**** Page Handler: Memory allocation failed **** \n\n");
            return FALSE;
          }
          src = ThisBand->atColoredBand[i].pBandRaster;
          for(y = 0; y < height; y ++) {
            /* Note that we need to copy all the data produced by the rip
             * (including padding) because bit-depths less than 8-bits need
             * their byte order rearranging. */
            memcpy(pRasterBuffer + (y * lineSize), src, lineSize);
            src += srcStride;
          }
          pstPageToPrint->atPlane[nColorant].atBand[ThisBand->uBandNumber-1].pBandRaster = pRasterBuffer;
        }
        else
        {
          pstPageToPrint->atPlane[nColorant].atBand[ThisBand->uBandNumber-1].pBandRaster = ThisBand->atColoredBand[i].pBandRaster;
        }
        pstPageToPrint->atPlane[nColorant].uBandTotal++;
      }
    }
  }
  return TRUE;
}

/**
 * \brief Remove the Page raster data from PMS's page.
 *
 * Remove the Page raster data from PMS's current page.\n
 */
int PMS_DeletePrintedPage(PMS_TyPage *pstPageToDelete)
{
  unsigned int i, j;

  for(i = 0; i < PMS_MAX_PLANES_COUNT; i++)
  {
    for(j = 0; j < PMS_BAND_LIMIT; j++)
    {
      if(pstPageToDelete->atPlane[i].atBand[j].pBandRaster != NULL)
      {
        OSFree(pstPageToDelete->atPlane[i].atBand[j].pBandRaster, PMS_MemoryPoolPMS);
        pstPageToDelete->atPlane[i].atBand[j].pBandRaster = NULL;
      }
    }
  }

  /* now allow the page list in PMSOutput to be cleared
  currently has an issue with cclearing part complete pages */ 
/*
  if (g_tSystemInfo.uUseRIPAhead)
  {
    PMS_IncrementSemaphore(g_semPageComplete);
  }
  */
  return TRUE ;
 
}

/**
 * \brief PMS Callback routine to enable PMS to select Media Source and Media Size.
 *
 * This routine passes on the job media attributes from RIP to PMS.\n
 * PMS can query the engine and check if the requested media attributes can be satisfied \n
 *
 * \param[in] pstPMSMedia Job has requested this information so far.
 * \param[out] ppTrayInfo Pointer to a tray information structure.
 * \return 1 if tray/media has been selected, 0 otherwise.
 */
int PMS_MediaSelect(PMS_TyMedia *pstPMSMedia, PMS_TyTrayInfo **ppTrayInfo, int *rotate)
{
  int bTraySelected=0;
  *rotate = 0;
  *ppTrayInfo = NULL;

  /* Implement your paper selection algorithm here */
  /* 1. loop for each tray in engine look for a tray paper size within a tolerance.
   * 2..if no match, check against the default paper size.
   * 3..if still no match, use the first tray.
   *
   * Of course, this simple example can be reworked to include
   * other media parameters and tray information such as media type,
   * weight, color, sheets available in tray, etc.
  */
  if(g_nInputTrays > 0)
  {
    int i;
    PMS_TyPaperInfo *pPaperInfo = NULL;
    PMS_TyPaperInfo *pDefaultPaperInfo = NULL;
    double dTolerance = 5.0;
    /* Simple example. Match ePaperSize - ignore other parameters */
    /* Try based on size within a tolerance.
       This example only looks for a paper size that is within
       tolerance, it does not look for the closest match. */

    /*  if the function is called from end of page detection then we don't need the actualMedia selection */
/* Check the Media Position selection first */
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);

        if((pstPMSMedia->uInputTray != 0) && ((int)pstPMSMedia->uInputTray == g_pstTrayInfo[i].eMediaSource)){
          if((fabs(pstPMSMedia->dWidth - pPaperInfo->dWidth) < dTolerance) &&
             (fabs(pstPMSMedia->dHeight - pPaperInfo->dHeight) < dTolerance )) {
            bTraySelected = 1;
            *ppTrayInfo = &g_pstTrayInfo[i];
            PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on size within tolerance\n", i+1, i);
            break;
          }
        }
      }
    }
/* if no match to a tray, then see if a rotated match can be found */
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);

        if((pstPMSMedia->uInputTray != 0) && ((int)pstPMSMedia->uInputTray == g_pstTrayInfo[i].eMediaSource)){
          if((fabs(pstPMSMedia->dWidth - pPaperInfo->dHeight) < dTolerance) &&
               (fabs(pstPMSMedia->dHeight - pPaperInfo->dWidth) < dTolerance )) {
              bTraySelected = 1;
              *rotate = 1;
              *ppTrayInfo = &g_pstTrayInfo[i];
              PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on rotated size within tolerance\n", i+1, i);
              break;
          }
        }
      }
    }
    /* Now check for the page size match only */
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);
        
        if((fabs(pstPMSMedia->dWidth - pPaperInfo->dWidth) < dTolerance) &&
           (fabs(pstPMSMedia->dHeight - pPaperInfo->dHeight) < dTolerance )) {
          bTraySelected = 1;
          *ppTrayInfo = &g_pstTrayInfo[i];
          PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on size within tolerance\n", i+1, i);
          break;
        }
      }
    }
/* if no match to a tray, then see if a rotated match can be found */
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);

        if((fabs(pstPMSMedia->dWidth - pPaperInfo->dHeight) < dTolerance) &&
           (fabs(pstPMSMedia->dHeight - pPaperInfo->dWidth) < dTolerance )) {
          bTraySelected = 1;
          *rotate = 1;
          *ppTrayInfo = &g_pstTrayInfo[i];
          PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on rotated size within tolerance\n", i+1, i);
          break;
        }
      }
    }
    /* if no match to a tray, then see if the current default size is available */
    PMS_GetPaperInfo(pstPMSMedia->ePaperSize, &pDefaultPaperInfo);
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);

        if((fabs(pDefaultPaperInfo->dWidth - pPaperInfo->dWidth) < dTolerance) &&
           (fabs(pDefaultPaperInfo->dHeight - pPaperInfo->dHeight) < dTolerance )) {
          bTraySelected = 1;
          *ppTrayInfo = &g_pstTrayInfo[i];
          PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on default size within tolerance\n", i+1, i);
          break;
        }
      }
    }
/* if no match to a tray, then see if a rotated match can be found */
    if(!bTraySelected) {
      for(i=0; i<g_nInputTrays; i++) {
        PMS_GetPaperInfo(g_pstTrayInfo[i].ePaperSize, &pPaperInfo);

        if((fabs(pDefaultPaperInfo->dWidth - pPaperInfo->dHeight) < dTolerance) &&
           (fabs(pDefaultPaperInfo->dHeight - pPaperInfo->dWidth) < dTolerance )) {
          bTraySelected = 1;
          *rotate = 1;
          *ppTrayInfo = &g_pstTrayInfo[i];
          PMS_TRAY_TRACE("PMS_MediaSelect, Tray %d (index %d) selected based on default rotated size within tolerance\n", i+1, i);
          break;
        }
      }
    }
    /* If still no match, just use the first tray, or implement something else
       like closest area with correct orientation, closest aspect ratio etc.
      */
    if(!bTraySelected) {
      *ppTrayInfo = &g_pstTrayInfo[0];
      bTraySelected = 1;
      PMS_TRAY_TRACE("PMS_MediaSelect, Tray 1 (index 0) selected (no media matched)\n");
    }
  }

  return bTraySelected;
}

/**
 * \brief PMS Callback routine to return the media information.
 *
 */
int PMS_GetPaperInfo(PMS_ePaperSize ePaperSize, PMS_TyPaperInfo** ppPaperInfo)
{
int i;
PMS_TyPaperInfo* pDontKnowPaperInfo = NULL;

    for(i=0; i<PMS_NUM_PAPERS; i++)
    {
      if(l_apPapers[i].ePaperSize == ePaperSize)
      {
        *ppPaperInfo = &l_apPapers[i];
        return TRUE;
      }
      else if(l_apPapers[i].ePaperSize == PMS_SIZE_DONT_KNOW)
        pDontKnowPaperInfo = &l_apPapers[i];
    }
    if(pDontKnowPaperInfo != NULL)
      *ppPaperInfo = pDontKnowPaperInfo;
    return FALSE;
}

/**
 * \brief PMS Callback routine to enable OIL to update the PMS media information.
 *
 */
int PMS_SetPaperInfo(PMS_ePaperSize ePaperSize, PMS_TyPaperInfo** ppPaperInfo)
{
    int i;

    for(i=0; i<PMS_NUM_PAPERS; i++)
    {
        if(l_apPapers[i].ePaperSize == ePaperSize)
        {
            l_apPapers[i] = **ppPaperInfo;
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * \brief PMS Callback routine to return information about the available Trays and papers in them.
 *
 */
int PMS_GetTrayInfo(void** pstTrayInfo)
{
  *pstTrayInfo = g_pstTrayInfo;

  return g_nInputTrays;
}
/**
 * \brief PMS Callback routine to enable OIL to set information about Trays and papers in them.
 *
 */
int PMS_SetTrayInfo(void** pstTrayInfo)
{
  g_pstTrayInfo = *pstTrayInfo;

  return 1;
}

/**
 * \brief PMS Callback routine to return information about the available Trays and papers in them.
 *
 */
int PMS_GetOutputInfo(void** pstOutputInfo)
{
  *pstOutputInfo = g_pstOutputInfo;

  return g_nOutputTrays;
}

int PMS_GetMediaType(PMS_eMediaType eMediaType, PMS_TyMediaType** ppMediaType)
{
    int i;

    for(i=0; i<PMS_NUM_MEDIATYPES; i++)
    {
        if(l_apMediaType[i].eMediaType == eMediaType)
        {
            *ppMediaType = &l_apMediaType[i];
            return TRUE;
        }
    }
    return FALSE;
}

int PMS_GetMediaSource(PMS_eMediaSource eMediaSource, PMS_TyMediaSource** ppMediaSource)
{
    int i;

    for(i=0; i<PMS_NUM_MEDIASOURCES; i++)
    {
        if(l_apMediaSource[i].eMediaSource == eMediaSource)
        {
            *ppMediaSource = &l_apMediaSource[i];
            return TRUE;
        }
    }
    return FALSE;
}

int PMS_GetMediaDest(PMS_eOutputTray eMediaDest, PMS_TyMediaDest** ppMediaDest)
{
    int i;

    for(i=0; i<PMS_NUM_MEDIADESTS; i++)
    {
        if(l_apMediaDest[i].eMediaDest == eMediaDest)
        {
            *ppMediaDest = &l_apMediaDest[i];
            return TRUE;
        }
    }
    return FALSE;
}

int PMS_GetMediaColor(PMS_eMediaColor eMediaColor, PMS_TyMediaColor** ppMediaColor)
{
    int i;

    for(i=0; i<PMS_NUM_MEDIACOLORS; i++)
    {
        if(l_apMediaColor[i].eMediaColor == eMediaColor)
        {
            *ppMediaColor = &l_apMediaColor[i];
            return TRUE;
        }
    }
    return FALSE;
}
/**
 * \brief PMS Callback routine to return the embedded screen information.
 *
 */
int PMS_GetScreenInfo(PMS_eScreens eScreen, void** ppScreenInfo)
{
    int i;

    for(i=0; i<PMS_NUM_SCREENS; i++)
    {
        if(l_apScreens[i]->eScreen == eScreen)
        {
            *ppScreenInfo = l_apScreens[i];
            return TRUE;
        }
    }

    return FALSE;
}


/**
 * \brief PMS Callback routine to enable OIL to get system parameters, e.g. the amount of
 * allocated memory defined by user (-m option on main arg pms_entry.c).
 *
 */
int PMS_GetSystemInfo(PMS_TySystem *PMS_tTySystem , int flag)
{
    if(flag == PMS_CurrentSettings)
    {
      *PMS_tTySystem = g_tSystemInfo;
    }
    else if(flag == PMS_NextSettings)
    {
      *PMS_tTySystem = g_tNextSystemInfo;
    }
    return TRUE;
}

/**
 * \brief PMS Callback routine to enable OIL to set system parameters, e.g. default PDL.
 *
 */
int PMS_SetSystemInfo(PMS_TySystem *PMS_tTySystem , int flag)
{
    if(flag == PMS_CurrentSettings)
    {
      g_tSystemInfo = *PMS_tTySystem;
    }
    else if(flag == PMS_NextSettings)
    {
      g_tNextSystemInfo = *PMS_tTySystem;
    }
    return TRUE;
}

/**
 * \brief PMS Callback routine to enable PMS to modify the color mode and bit depth for the current job being processed.
 *
 * This routine can override the job set bit depth and color mode \n
 */
/* this function currently uses oil variable values, which is not ideal for a PMS callback !!!*/
int PMS_SetJobRIPConfig(int PDL, int *bitDepth, int *colorMode)
{
/* the PDL is the OIL enumeration */
/* The bit depth is the actual number in bits per pixel */
/* The color mode is the oil_ color mode*/
#ifdef DIRECTPRINTPCLOUT
  PMS_TySystem ptPMSSysInfo;
#endif

  UNUSED_PARAM(int, PDL);
#ifndef DIRECTPRINTPCLOUT
  UNUSED_PARAM(int *, bitDepth);
  UNUSED_PARAM(int *, colorMode);
#endif

#ifdef DIRECTPRINTPCLOUT
  PMS_GetSystemInfo(&ptPMSSysInfo , PMS_CurrentSettings);
  /* Check that the setup is compatible with the available PCL output formats */
  if((ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINT) || (ptPMSSysInfo.eOutputType == PMS_DIRECT_PRINTXL))
  {
    if(*colorMode == 1)/* mono */
    {
      if((*bitDepth != 1) && (*bitDepth != 8))
        *bitDepth = 1;
    }
    else  /* Make RGB Composite Contone*/
    {
      *colorMode = 5;
      *bitDepth = 8;
    }
  }
#endif
  return 1;
}
/**
 * \brief PMS Callback routine to write job data through a back channel to the host system.
 *
 * Return value is the number of bytes successfully sent.
 */
int PMS_PutBackChannel(unsigned char * buff, int len)
{
  int nWritten;
  int nWrittenDontCare;
  int nResult;

  PMS_WriteDataStream(PMS_WRITE_DEBUG_MESSAGE, NULL, (unsigned char*)"PDL Back channel+++\n", 20, &nWrittenDontCare);
  nResult = PMS_WriteDataStream(PMS_WRITE_BACK_CHANNEL, NULL, buff, len, &nWritten);
  PMS_WriteDataStream(PMS_WRITE_DEBUG_MESSAGE, NULL, (unsigned char*)"\nPDL Back channel---\n", 21, &nWrittenDontCare);

  /* If logging pms messages then append back channel message to log file */
  if( g_bLogPMSDebugMessages )
  {
    if( l_pLogFile == NULL )
    {
      l_pLogFile = fopen( "DebugMessages.log", "a" );
    }

    if( l_pLogFile != NULL )
    {
      fprintf(l_pLogFile, "%.*s", len, buff);
      fflush(l_pLogFile);
    }
  }

  return (nWritten);
}

/**
 * \brief PMS Callback routine to wriet a debug message.
 *
 * Typically these messages are from the RIP monitor callback.
 */
int PMS_PutDebugMessage(unsigned char * message)
{
  int nWritten;
  int nResult;

  nResult = PMS_WriteDataStream(PMS_WRITE_DEBUG_MESSAGE, NULL, message, (int)strlen((const char*)message), &nWritten);

  /* If logging pms messages then append pms debug messages to log file */
  if( g_bLogPMSDebugMessages )
  {
    if( l_pLogFile == NULL )
    {
      l_pLogFile = fopen( "DebugMessages.log", "a" );
    }

    if( l_pLogFile != NULL )
    {
      fputs( (const char *) message, l_pLogFile );
      fflush(l_pLogFile);
    }
  }

  return TRUE;
}

/**
 * \brief Free the PMS framebuffer, if present.
 */
void FreePMSFramebuffer(void)
{
  if (gFrameBuffer != NULL)
  {
    OSFree(gFrameBuffer,PMS_MemoryPoolPMS);
    gFrameBuffer = NULL;
  }

  gnPageHeightPixels = 0;
  gFrameBufferSize = 0;
  guRIPBandHeight = 0;
  guRIPBytesPerLine = 0;
  guRIPComponents = 0;
  guRIPComponentsPerBand = 0;
}

/**
 * \brief If we are providing raster memory, this routine allows us to
 * specify the layout for that memory, namely the line size, line stride,
 * and colorant stride. See the RASTER_LAYOUT structure for more details.
 *
 * \param pValid This is defaulted to false by the caller; this function
 * has no effect unless this is set to true.
 *
 * \param puLineBytes Should be set to the number of bytes in a line of raster.
 *
 * \param pLineStride Should be set to the number of bytes between successive
 * lines of the same colorant.
 *
 * \param pColorantStride Should be set to the number of bytes between lines of
 * successive colorants. This may be set to zero, which will cause the core to
 * use "band height * line bytes".
 *
 * This function will almost always return 0, meaning success, but a
 * skin implementation might return -1 signifying an error if the
 * stride value is outside of an acceptable range.
 */
int PMS_RasterLayout(int nPageWidthPixels,
                     int nPageHeightPixels,
                     unsigned int uBitDepth,
                     unsigned int uComponents,
                     int *pValid,
                     unsigned int *puLineBytes,
                     int *pLineStride,
                     int *pColorantStride)
{
  UNUSED_PARAM(int, nPageHeightPixels);

  if (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE ||
      g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME) {
    /* Make the line a multiple of 4 bytes wide. */
    int lineBytes = (((nPageWidthPixels * uBitDepth) + 31) / 32) * 4;

    *pValid = TRUE;

    if (g_tSystemInfo.bScanlineInterleave || (g_tJob.eColorMode == PMS_RGB_PixelInterleaved)) {
      /* For scan-line interleaved the stride between lines of the same colorant
       * is lineBytes * num components. The stride between colorants is one line.
       */
      sRasterLayout.lineStride = *pLineStride = *puLineBytes = lineBytes * uComponents;
      sRasterLayout.colorantStride = *pColorantStride = lineBytes;
    }
    else {
      /* For band/frame interleaved the stride between lines of the same
       * colorant is lineBytes. The stride between colorants is band-height
       * lines; since we don't know the band height here we can use the special
       * value of zero, which means the core will set the colorant stride to the
       * line stride * band height. */
      sRasterLayout.lineStride = *pLineStride = *puLineBytes = lineBytes;
      sRasterLayout.colorantStride = *pColorantStride = 0;
    }
  }

  return 0;
}

/**
 * \brief Routine which the RIP uses to inform the skin of the
 * parameters influencing the raster buffers, and for the skin to
 * inform the RIP how it will proceed. Only used in \c
 * PMS_PUSH_BAND_DIRECT_(SINGLE|BAND) band delivery mode.
 *
 * There are two points in the execution of a job when this routine is
 * called. The first is when, during the setup phase, the page device
 * changes such that one or more of the parameters influencing the
 * nature of the raster that the RIP will generate have changed. When
 * this is so, the \c fRenderingStarting flag will be FALSE.
 *
 * Second, this routine will be called when the job has finished being
 * interpreted, and rendering is about to start. At that point, no
 * further changes to the raster parameters are possible, and this
 * routine should allocate the buffers to receive raster data if it
 * hasn't done so in the earlier call.
 *
 * It's preferable to allocate buffers only when \c fRenderingStarting
 * is TRUE, provided that it is possible to guarantee that such
 * allocations will succeed. If we can test the amount of free memory
 * when \c fRenderingStarting is FALSE and know that the processes
 * such as interpretation which will take place in the meantime will
 * not take up too much memory, that is the case. Otherwise, it might
 * be necessary to allocate during the earlier calls when \c
 * fRenderingStarting is FALSE. One of the reasons why this is less
 * desirable is that such calls are quite frequent: page device
 * changes happen fairly often during the initial phases of processing
 * a job - perhaps ten or twenty times.
 *
 * This function should return 0 for success, -1 for failure, and 1 to
 * indicate that it wishes for the RIP to call it back again with the
 * same data. This provides for the skin to be able to delay the RIP
 * in order for some asynchronous process to be allowed to
 * complete. Note that the maximum acceptable delay here before
 * returning 1 should be 100 milliseconds.
 */
int PMS_RasterRequirements( int nPageWidthPixels, int nPageHeightPixels,
                            unsigned int uBitDepth, unsigned int uComponents,
                            unsigned int uComponentsPerBand, unsigned int uMinBands,
                            unsigned int uRIPBandHeight, unsigned int uRIPBytesPerLine,
                            int fRenderingStarting, int *fHaveFrameBuffer )
{
  size_t uFrameBufferSize ;

  UNUSED_PARAM( int * , fHaveFrameBuffer ) ;
  UNUSED_PARAM( unsigned int , uMinBands ) ;
  UNUSED_PARAM( unsigned int , uBitDepth ) ;
  UNUSED_PARAM( unsigned int , uComponents ) ;
  UNUSED_PARAM( unsigned int , nPageWidthPixels ) ;

  if (!fRenderingStarting)
  {
    /* This is where a customer implementation might calculate whether
     * there is enough memory to continue. Also it might decide to
     * allocate raster memory here in order to be sure the allocation
     * succeeds (to prevent memory contention later), but note that
     * this function will be called many times - perhaps 10 or 20 -
     * before the final page device parameters are established in the
     * RIP. Also don't forget we have the option here to delay and
     * return 1, meaning "call me back". */
    return 0;
  }
  else if (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE)
  {
    uFrameBufferSize = uRIPBandHeight * uRIPBytesPerLine * uComponents ;
  }
  else if (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME)
  {
    uFrameBufferSize = (( nPageHeightPixels / uRIPBandHeight ) + 1 ) * uRIPBandHeight * uRIPBytesPerLine * uComponents ;
  }
  else
  {
    return 0;
  }

  if (gFrameBufferSize != uFrameBufferSize)
  {
    if (gFrameBuffer != NULL)
    {
      OSFree(gFrameBuffer,PMS_MemoryPoolPMS);
    }

    gFrameBuffer =
      (unsigned char*)OSMalloc(uFrameBufferSize,PMS_MemoryPoolPMS);
  }

  if (gFrameBuffer != NULL)
  {
    gnPageHeightPixels = nPageHeightPixels;
    gFrameBufferSize = uFrameBufferSize;
    guRIPBandHeight = uRIPBandHeight;
    guRIPBytesPerLine = uRIPBytesPerLine;
    guRIPComponents = uComponents;
    guRIPComponentsPerBand = uComponentsPerBand;
    *fHaveFrameBuffer = ((g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME) ||
                                                (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE));
    return 0;
  }
  else
  {
    return -1;
  }
}

/**
 * \brief Provide the memory range for the RIP to render into
 * directly.
 *
 * Only used in \c PMS_PUSH_BAND_DIRECT_(SINGLE|FRAME) band delivery mode.
 *
 * This function should return 0 for success, -1 for failure, and 1 to
 * indicate that it wishes for the RIP to call it back again with the
 * same data. This provides for the skin to be able to delay the RIP
 * in order for some asynchronous process to be allowed to
 * complete. Note that the maximum acceptable delay here before
 * returning 1 should be 100 milliseconds.
 */
int PMS_RasterDestination( int nFrameNumber, unsigned int uBandNum,
                           unsigned char **pMemoryBase,
                           unsigned char **pMemoryCeiling )
{
  size_t uBandSize = guRIPBandHeight * guRIPBytesPerLine * guRIPComponentsPerBand;

  if (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_SINGLE)
  {
    *pMemoryBase = gFrameBuffer;
    *pMemoryCeiling = *pMemoryBase + uBandSize - 1;
  }
  else if (g_tSystemInfo.eBandDeliveryType == PMS_PUSH_BAND_DIRECT_FRAME)
  {
    if (g_tSystemInfo.bScanlineInterleave || (g_tJob.eColorMode == PMS_RGB_PixelInterleaved))
    {
      *pMemoryBase = gFrameBuffer + (nFrameNumber * guRIPBytesPerLine) + (uBandSize * uBandNum);
    }
    else
    {
      *pMemoryBase = gFrameBuffer + (gnPageHeightPixels * guRIPBytesPerLine * nFrameNumber) + (uBandSize * uBandNum);
    }
    *pMemoryCeiling = *pMemoryBase + uBandSize - 1;
  }

  return 0;
}

/**
 * \brief PMS Callback routine to enable PMS to decide band size.
 *
 * This routine passes on the required band specification to OIL.\n
 * PMS can decide the band size based on page parameters like pagesize, etc and memory it can spare. \n
 */
int PMS_GetBandInfo( int nPageWidthPixels,  int nPageHeightPixels,
                     int bit_depth,
                     PMS_TyBandInfo *ptPMSBandInfo)
{
  UNUSED_PARAM(int, nPageHeightPixels);

  /* total memory required for the page would be -
     ((nPageWidthPixels * bit_depth) + padding if any)/8  x nPageHeightPixels

     TODO: calculate following band height based on total memory required by the page
  */
  if ( guRIPBandHeight > 0 ) {
    ptPMSBandInfo->LinesPerBand = guRIPBandHeight;
  }
  else {
    ptPMSBandInfo->LinesPerBand = 128;
  }

  if ( guRIPBytesPerLine > 0 ) {
    ptPMSBandInfo->BytesPerLine = guRIPBytesPerLine;
  }
  else {
    /* Calculate Bytes per line.
       Be sure that all output methods can cope with different alignment.
       oil_interface_oil2pms.c can cope with 8bit
       pms_tiff_out requires 32bit
       ggetiff requires 32bit
       no output can cope with 8 bit
    */
    if (g_tJob.eColorMode == PMS_RGB_PixelInterleaved) {
      ptPMSBandInfo->BytesPerLine = (((nPageWidthPixels * bit_depth * 3) + 31) / 32) * 4;
    } else {
      ptPMSBandInfo->BytesPerLine = (((nPageWidthPixels * bit_depth) + 31) / 32) * 4;
    }
  }

  return 0;
}

/**
 * \brief Callback routine to inform PMS that RIP has finished ripping all jobs.
 *
 * This routine informs PMS that RIP has completed all jobs.\n
 */
int PMS_RippingComplete()
{
  g_eRipState = PMS_Rip_Finished;
  PMS_IncrementSemaphore(g_semPageQueue);
  return 0;
}

/**
 * \brief Callback routine to transform CMYK values.
 *
 * This routine takes changes the CMYK values.
 * In this example, each colorant is squared... cyan = cyan * cyan; etc.
 * This gives a lighter appearance on non-solid colors as each colorant
 * is in the range 0 to 1.
 *
 * This function is called by oil_cmm when AlternateCMM is enabled, see
 * GG_CMMInstall in oil_psconfig.
 *
 */
int PMS_CMYKtoCMYK(float aCMYKInOut[])
{
  aCMYKInOut[0] = aCMYKInOut[0] * aCMYKInOut[0];
  aCMYKInOut[1] = aCMYKInOut[1] * aCMYKInOut[1];
  aCMYKInOut[2] = aCMYKInOut[2] * aCMYKInOut[2];
  aCMYKInOut[3] = aCMYKInOut[3] * aCMYKInOut[3];

  return 0;
}

/**
 * \brief Callback routine to transform RGB values to CMYK values.
 *
 * This routine takes transforms the RGB color value into CMYK.
 * 1) Performs a very simple RGB to CMY conversion.
 * 2) Does maximum black generation.
 * 3) Transforms the resulting CMYK color value using the PMS_CMYKtoCMYK
 *    function above.
 *
 * This function is called by oil_cmm when AlternateCMM is enabled, see
 * GG_CMMInstall in oil_psconfig.
 *
 */
int PMS_RGBtoCMYK(float rgbValueIn[], float cmykValueOut[])
{
  float fK;

  /* Very simple RGB to CMY conversion */
  cmykValueOut[0] = 1.0f - rgbValueIn[0];
  cmykValueOut[1] = 1.0f - rgbValueIn[1];
  cmykValueOut[2] = 1.0f - rgbValueIn[2];


  /* Find the minimum CMY value for maximum black generation */
  if(cmykValueOut[0] < cmykValueOut[1])
  {
    if(cmykValueOut[0] < cmykValueOut[2])
    {
      fK = cmykValueOut[0];
    }
    else
    {
      fK = cmykValueOut[2];
    }
  }
  else
  {
    if(cmykValueOut[1] < cmykValueOut[2])
    {
      fK = cmykValueOut[1];
    }
    else
    {
      fK = cmykValueOut[2];
    }
  }

  cmykValueOut[0] -= fK;
  cmykValueOut[1] -= fK;
  cmykValueOut[2] -= fK;
  cmykValueOut[3] = fK;


  /* Finally send it though CMYKtoCMYK so that the same tranformation applies */
  return PMS_CMYKtoCMYK(cmykValueOut);
}


/**
* \brief PMS Callback routine to return UFST resource data.
*
*/
int PMS_GetResource(int eResourceType, int index, PMS_TyResource * pResource)
{
  int result = 0;

#ifndef USE_UFST5
#ifndef USE_UFST7
#ifndef USE_FF
  UNUSED_PARAM(int, index);
#endif
#endif
#endif

  PMS_ASSERT(pResource, ("PMS_GetResource: pResource is NULL\n"));

  switch( eResourceType )
  {
  case EPMS_FONT_PCL_BITMAP:
  case EPMS_PCL_XLMAP:
  case EPMS_PCL_SSMAP:
  case EPMS_PCL_GLYPHS:
    result = -1;
    break;

#ifdef UFST_ENABLED
  case EPMS_UFST_FCO:
    switch( index )
    {
    case 0:
      pResource->pData = (unsigned char *) g_fontset_fco;
      pResource->length = 0;
      pResource->id = 0;
      pResource->ePriority = EPMS_Priority_Normal;
      break;
    case 1:
      pResource->pData = (unsigned char *) g_wd____xh_fco;
      pResource->length = 0;
      pResource->id = 1;
      pResource->ePriority = EPMS_Priority_Normal;
      break;
    case 2:
      pResource->pData = (unsigned char *) g_plug__xi_fco;
      pResource->length = 0;
      pResource->id = 2;
      pResource->ePriority = EPMS_Priority_Normal;
      break;
    default:
      result = -1;
    }
    break;

  case EPMS_UFST_SS:
    switch( index )
    {
    case 0:
      pResource->pData = (g_uif_ss) ? (unsigned char *) g_uif_ss :
                                      (unsigned char *) g_utt_ss;
      pResource->length = 0;
      pResource->id = 0;
      pResource->ePriority = EPMS_Priority_Normal;
      break;
    case 1:
      pResource->pData = (g_uif_ss) ? (unsigned char *) g_utt_ss :
                                      (unsigned char *) g_umt_ss;
      pResource->length = 0;
      pResource->id = 1;
      pResource->ePriority = EPMS_Priority_Normal;
      break;
    case 2:
      if (g_uif_ss) {
        pResource->pData = (unsigned char *) g_umt_ss;
        pResource->length = 0;
        pResource->id = 2;
        pResource->ePriority = EPMS_Priority_Normal;
        break;
      } /* else drop through to failure */
    default:
      result = -1;
    }
    break;

  case EPMS_UFST_MAP:
    switch( index )
    {
    case 0:
      if(g_fontsetmap_len != 0)
      {
        pResource->pData = (unsigned char *) g_fontsetmap;
        pResource->length = g_fontsetmap_len;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;
      }
      /* drop through if no fontsetmap */
    default:
      result = -1;
    }
    break;

#endif

#ifdef USE_FF
  case EPMS_FF_FIT:
    switch (index)
    {
      case 0:
        pResource->pData = (unsigned char *)g_fontset_fit;
        pResource->length = g_fontset_fit_len;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      case 1:
        pResource->pData = (unsigned char *)g_ffrom_support_fit;
        pResource->length = ffrom_support_fit_len;
        pResource->id = 1;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      default:
        result = -1;
    }
    break;

  case EPMS_FF_PFR:
    switch (index)
    {
      case 0:
        pResource->pData = (unsigned char *) g_fontset_pfr;
        pResource->length = g_fontset_pfr_len;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      case 1:
        pResource->pData = (unsigned char*) g_ffrom_support_pfr;
        pResource->length = ffrom_support_pfr_len;
        pResource->id = 1;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      default:
        result = -1;
    }
    break;

  case EPMS_FF_MAP:
    switch (index)
    {
      case 0:
      if(g_fontsetmap_len != 0)
      {
        pResource->pData = (unsigned char *) g_fontsetmap;
        pResource->length = g_fontsetmap_len;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;
      }
      /* drop through if no fontsetmap */
      default:
        result = -1;
    }
    break;

  case EPMS_FF_SYMSET:
    switch (index)
    {
      case 0:
        pResource->pData = (unsigned char *) ff_ss_data1;
        pResource->length = ff_ss_data_length;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      default:
        result = -1;
    }
    break;

  case EPMS_FF_SYMSET_MAPDATA:
    switch (index)
    {
      case 0:
        pResource->pData = (unsigned char *)ff_ss_mapdata1;
        pResource->length = ff_ss_mapdata_length;
        pResource->id = 0;
        pResource->ePriority = EPMS_Priority_Normal;
        break;

      default:
        result = -1;
    }
    break;

#endif

  default:
    result = -1;
  }

  return result;
}

/* -------------------------------------------------------------------------- */
#ifdef UFST_ENABLED
/**
* \brief PMS Callback routine to access UFST fco data.
*/
int PMS_CGIFfco_Access( FSP LPUB8 fcPathName, UW16 fco_Index, UW16 Info_Key, LPUW16 pSize, LPSB8 pBuffer )
{
  return CGIFfco_Access(FSA fcPathName, fco_Index, Info_Key, pSize, pBuffer);
}

/**
* \brief PMS Callback routine to install fco plugin.
*
*/
int PMS_CGIFfco_Plugin( FSP SW16 FChandle )
{
  return CGIFfco_Plugin(FSA FChandle);
}

/**
* \brief PMS Callback routine to open UFST fco data.
*
*/
int PMS_CGIFfco_Open( FSP LPUB8 fcName, LPSW16 FChandle )
{
  return CGIFfco_Open(FSA fcName, FChandle);
}


/**
* \brief PMS Callback routine to configure UFST module.
*
*/
int PMS_CGIFinitRomInfo(FSP LPUB8 fblock_ptr, LPUB8 * ss_ptr)
{
  return CGIFinitRomInfo(FSA fblock_ptr, ss_ptr);
}


/**
* \brief PMS Callback routine to enter UFST module.
*
*/
int PMS_CGIFenter( FSP0 )
{
  return CGIFenter(FSA0);
}


/**
* \brief PMS Callback routine to config UFST font.
*
*/
int PMS_CGIFconfig( FSP PIFCONFIG cfg )
{
  return CGIFconfig(FSA cfg);
}


/**
* \brief PMS Callback routine to initialize UFST module.
*
*/
int PMS_CGIFinit( FSP0 )
{
  return CGIFinit(FSA0);
}


/**
* \brief PMS Callback routine to close UFST fco data.
*
*/
int PMS_CGIFfco_Close( FSP SW16 FChandle )
{
  return CGIFfco_Close(FSA FChandle);
}

/**
* \brief PMS Callback routine to make a UFST character.
*
*/
int PMS_CGIFmakechar( FSP PIFBITMAP bm, LPUB8 nzw_buf )
{
  return CGIFmakechar(FSA bm, nzw_buf);
}


/**
* \brief PMS Callback routine to obtain the memory size of a UFST character.
*
*/
int PMS_CGIFchar_size( FSP UL32 chId, LPSL32 size, SW16 alt_width )
{
  return CGIFchar_size(FSA chId, size, alt_width);
}


/**
* \brief PMS Callback routine to access UFST font data.
*
*/
int PMS_CGIFfont( FSP PFONTCONTEXT fc )
{
  return CGIFfont(FSA fc);
}

/**
* \brief PMS Callback routine to endian-swap PCL font headers.
*
*/
int PMS_PCLswapHdr( FSP LPUB8 p, UW16 gifct )
{
#ifdef PMS_LOWBYTEFIRST
  return PCLswapHdr(FSA p, gifct);
#else
  UNUSED_PARAM(FSP LPUB8, p);
  UNUSED_PARAM(UW16, gifct);

  return 0;
#endif
}

/**
* \brief PMS Callback routine to endian-swap PCL glyph headers.
*
*/
int PMS_PCLswapChar( FSP LPUB8 p )
{
#ifdef PMS_LOWBYTEFIRST
  return PCLswapChar(FSA p);
#else
  UNUSED_PARAM(FSP LPUB8, p);

  return 0;
#endif
}

/* -------------------------------------------------------------------------- */
/**
* \brief PMS Callback routine to return the UFST PS3 fco data.
*
*/
int PMS_UFSTGetPS3FontDataPtr(const unsigned char **ppData)
{
  PMS_ASSERT(ppData, ("PMS_UFSTGetPS3FontDataPtr: ppData is NULL\n"));
  *ppData = g_fontset_fco;

  if(*ppData==NULL)
    return -1;

  return 0;
}

/**
* \brief PMS Callback routine to return the UFST WingDings fco data.
*
*/
int PMS_UFSTGetWingdingFontDataPtr(const unsigned char **ppData)
{
  PMS_ASSERT(ppData, ("PMS_UFSTGetWingdingFontDataPtr: ppData is NULL\n"));
  *ppData = g_wd____xh_fco;

  if(*ppData==NULL)
    return -1;

  return 0;
}

/**
* \brief PMS Callback routine to return the UFST fco plugin data.
*
* \param ppData[in] ptr to ptr to be filled in
* \param which[in]  starting at 0
*/
int PMS_UFSTGetSymbolSetDataPtr(const unsigned char **ppData, int which)
{
  PMS_ASSERT(ppData, ("PMS_UFSTGetSymbolSetDataPtr: ppData is NULL\n"));
  *ppData = NULL;

  switch (which) {
  case 0:
    *ppData = g_umt_ss;
    break ;
  case 1:
    *ppData = g_utt_ss;
    break ;
  }

  if(*ppData==NULL)
    return -1;

  return 0;
}

/**
* \brief PMS Callback routine to return the UFST symbolset data.
*
*/
int PMS_UFSTGetPluginDataPtr(const unsigned char **ppData)
{
  PMS_ASSERT(ppData, ("PMS_UFSTGetPluginDataPtr: ppData is NULL\n"));
  *ppData = g_plug__xi_fco;

  if(*ppData==NULL)
    return -1;

  return 0;
}


/**
* \brief PMS Callback routine to set the UFST callback functions.
*
*/
int PMS_UFSTSetCallbacks( PCLchId2PtrFn *chId2Ptr
                        , PCLchId2PtrFn *glyphId2Ptr
#if GG_CUSTOM
                        , PCLallocFn    *PCLalloc
                        , PCLfreeFn     *PCLfree
#endif
                        )
{
  g_pmsPCLchId2Ptr    = chId2Ptr ;
  g_pmsPCLglyphId2Ptr = glyphId2Ptr ;
#if GG_CUSTOM
  g_pmsPCLalloc       = PCLalloc ;
  g_pmsPCLfree        = PCLfree ;
#endif

  return 0;
}

#endif

/* -------------------------------------------------------------------------- */

#ifdef GG_DEBUG
static int PMS_InitFail()
{
  PMS_FAIL("PMS_InitAPI: API function pointer not initialised\n");
  return 0;
}
#endif

/**
 * \brief Allocate memory from PMS SYSTEM pool.
 *
 * Allocate the requested bytes from PMS SYSTEM memory pool and pass back the start address
 * as one of the argument and return TRUE/FALSE to indicate success/failure.\n
 */
int PMS_SysMalloc(size_t size, void **pptr)
{
  PMS_ASSERT(pptr!=NULL,("PMS_SysMalloc : pptr value is NULL\n"));
  *pptr=(void *)OSMalloc(size,PMS_MemoryPoolSys);
  if(*pptr==NULL)
    return (0);
  return (1);
}

/**
 * \brief Deallocate memory from PMS SYSTEM pool.
 *
 * Deallocate the memory chunk from PMS SYSTEM memory pool pointed to by the start address.\n
 */
int PMS_SysFree(void *ptr)
{
  OSFree(ptr,PMS_MemoryPoolSys);
  return 1;
}

/**
 * \brief Allocate memory from PMS APPLICATION pool.
 *
 * Allocate the requested bytes from PMS APPLICATION memory pool and pass back the start address
 * as one of the argument and return TRUE/FALSE to indicate success/failure.\n
 */
int PMS_AppMalloc(size_t size, void **pptr)
{
  PMS_ASSERT(pptr!=NULL,("PMS_AppMalloc : pptr value is NULL\n"));
  *pptr=(void *)OSMalloc(size,PMS_MemoryPoolApp);
  if(*pptr==NULL)
    return (0);
  return (1);
}

/**
 * \brief Allocate memory from a pool.
 *
 * Allocate the requested bytes from a memory pool and pass back the start address
 * as one of the argument and return TRUE/FALSE to indicate success/failure.\n
 */
#ifdef SDK_MEMTRACE
int PMS_Malloc(size_t size, void **pptr, int pool, char *pszFile, int nLine)
#else
int PMS_Malloc(size_t size, void **pptr, int pool)
#endif
{
  PMS_ASSERT(pptr!=NULL,("PMS_Malloc : pptr value is NULL\n"));
#ifdef SDK_MEMTRACE
  *pptr=OSMallocEx(size,pool,pszFile,nLine);
#else
  *pptr=OSMalloc(size,pool);
#endif
/*  PMS_SHOW("PMS_Malloc *pptr %p, %u, %d\n", *pptr, size, pool); */
  if(*pptr==NULL)
    return (0);
  return (1);
}

/**
 * \brief Deallocate memory from a pool.
 *
 * Deallocate the memory chunk from a memory pool pointed to by the start address.\n
 */
int PMS_Free(void *ptr, int pool)
{
  OSFree(ptr,pool);
  return 1;
}

/**
 * \brief Provide a copy of the job settings.
 */
int PMS_GetJobSettings(void** ppstJob)
{
  *ppstJob = &g_tJob;
  return 1;
}

/**
 * \brief Note changes to the job settings.
 */
int PMS_SetJobSettings(void* pstJob)
{
  PMS_TyJob * pNewSettings = (PMS_TyJob *) pstJob;

  memcpy( &g_tJob, pNewSettings, sizeof(PMS_TyJob) );

  return 1;
}

/**
 * \brief Reset job settings to their defaults.
 */
int PMS_SetJobSettingsToDefaults(void)
{
  EngineSetJobDefaults();
  return 1;
}

/**
 * \brief Initialise a file system volume.
 */
int PMS_FSInitVolume(unsigned char * pzVolume)
{
  return PMS_FS_InitVolume(pzVolume);
}

/**
 * \brief Create a directory.
 */
int PMS_FSMkDir(unsigned char * pzName)
{
  return PMS_FS_MakeDir(pzName);
}

/**
 * \brief Open a file.
 */
int PMS_FSOpen(unsigned char * pzPath, int flags, void ** pHandle)
{
  return PMS_FS_Open(pzPath, flags, pHandle);
}

/**
 * \brief Read data from a file.
 */
int PMS_FSRead(void * handle, unsigned char * buffer, int bytes, int * pcbRead)
{
  return PMS_FS_Read(handle, buffer, bytes, pcbRead);
}

/**
 * \brief Write data to a file.
 */
int PMS_FSWrite(void * handle, unsigned char * buffer, int bytes, int * pcbWritten)
{
  return PMS_FS_Write(handle, buffer, bytes, pcbWritten);
}

/**
 * \brief Close a file.
 */
int PMS_FSClose(void * handle)
{
  return PMS_FS_Close(handle);
}

/**
 * \brief Set position in a file.
 */
int PMS_FSSeek(void * handle, int offset, int whence)
{
  return PMS_FS_Seek(handle, offset, whence);
}

/**
 * \brief Append data to a file
 */
int PMS_FSAppend(unsigned char * pzName, unsigned char * pData, int cbData, int * pcbWritten)
{
  int eError;

  void * handle;

  eError = PMS_FS_Open( pzName, PMS_FS_WRITE | PMS_FS_CREAT, &handle );

  if( eError == 0 )
  {
    eError = PMS_FS_Seek( handle, 0, PMS_FS_SEEK_END );

    if( eError == 0  )
    {
      eError = PMS_FS_Write( handle, pData, cbData, pcbWritten );
    }

    PMS_FS_Close( handle );
  }

  return eError;
}

/**
 * \brief Delete a file or empty directory.
 */
int PMS_FSDelete(unsigned char * pzName)
{
  return PMS_FS_Delete(pzName);
}

/**
 * \brief Report information about an entry in a directory.
 */
int PMS_FSDirEntryInfo(unsigned char * pzName, int nEntry, PMS_TyStat * pStat)
{
  return PMS_FS_DirEntryStat(pzName, nEntry, pStat);
}

/**
 * \brief Download data to a file.
 */
int PMS_FSDownload(unsigned char * pzName, unsigned char * pData, int cbData, int * pcbWritten)
{
  int eError;

  void * handle;

  eError = PMS_FS_Open( pzName, PMS_FS_WRITE | PMS_FS_CREAT | PMS_FS_TRUNC, &handle );

  if( eError == 0 )
  {
    eError = PMS_FS_Write( handle, pData, cbData, pcbWritten );

    PMS_FS_Close( handle );
  }

  return eError;
}

/**
 * \brief Report information about file system entry.
 */
int PMS_FSQuery(unsigned char * pzName, PMS_TyStat * pStat)
{
  return PMS_FS_Stat(pzName, pStat);
}

/**
 * \brief Upload data from a file.
 */
int PMS_FSUpload(unsigned char * pzName, int offset, int cbData, unsigned char * pData, int * pcbRead)
{
  int eError;

  void * handle;

  eError = PMS_FS_Open( pzName, PMS_FS_READ, &handle );

  if( eError == 0 )
  {
    eError = PMS_FS_Seek( handle, offset, PMS_FS_SEEK_SET );

    if( eError == 0 )
    {
      eError = PMS_FS_Read( handle, pData, cbData, pcbRead );
    }

    PMS_FS_Close( handle );
  }

  return eError;
}

/**
 * \brief Get file system info.
 */
int PMS_FSFileSystemInfo( int iVolume, int * pnVolumes, PMS_TyFileSystem * pFileSystemInfo )
{
  return PMS_FS_FileSystemInfo( iVolume, pnVolumes, pFileSystemInfo );
}

/**
 * \brief Get file system disk lock status.
 */
int PMS_FSGetDisklock( void )
{
  return PMS_FS_GetDisklock();
}

/**
 * \brief Set file system disk lock status.
 */
int PMS_FSSetDisklock( int fLocked )
{
  PMS_FS_SetDisklock( fLocked );

  return 1;
}

/**
 * \brief Array of function pointers to PMS callback functions.
 *
 */
PMS_API_FNS PMS_InitAPI()
{
#ifdef GG_DEBUG
  /* Initialise the api function so that an assert is called if the
   * function array is not setup correctly
   */
  {
    int i;
    for(i = 0; i < PMS_TOTAL_NUMBER_OF_API_FUNCTIONS; i++)
    {
      apfnRip_IF_KcCalls[i] = PMS_InitFail;
    }
  }
#endif

  apfnRip_IF_KcCalls[EPMS_FN_ReadDataStream]    = PMS_ReadDataStream; /* TODO: This is going to be removed very soon */
  apfnRip_IF_KcCalls[EPMS_FN_PeekDataStream]    = PMS_PeekDataStream;
  apfnRip_IF_KcCalls[EPMS_FN_ConsumeDataStream] = PMS_ConsumeDataStream;
  apfnRip_IF_KcCalls[EPMS_FN_WriteDataStream]   = PMS_WriteDataStream;
  apfnRip_IF_KcCalls[EPMS_FN_CheckinPage]       = PMS_CheckinPage;
  apfnRip_IF_KcCalls[EPMS_FN_CheckinBand]       = PMS_CheckinBand;
  apfnRip_IF_KcCalls[EPMS_FN_DeletePrintedPage] = PMS_DeletePrintedPage;
  apfnRip_IF_KcCalls[EPMS_FN_CMYKtoCMYK]        = PMS_CMYKtoCMYK;
  apfnRip_IF_KcCalls[EPMS_FN_RGBtoCMYK]         = PMS_RGBtoCMYK;
  apfnRip_IF_KcCalls[EPMS_FN_MediaSelect]       = PMS_MediaSelect;
  apfnRip_IF_KcCalls[EPMS_FN_GetPaperInfo]      = PMS_GetPaperInfo;
  apfnRip_IF_KcCalls[EPMS_FN_SetPaperInfo]      = PMS_SetPaperInfo;
  apfnRip_IF_KcCalls[EPMS_FN_GetMediaType]      = PMS_GetMediaType;
  apfnRip_IF_KcCalls[EPMS_FN_GetMediaSource]    = PMS_GetMediaSource;
  apfnRip_IF_KcCalls[EPMS_FN_GetMediaDest]      = PMS_GetMediaDest;
  apfnRip_IF_KcCalls[EPMS_FN_GetMediaColor]     = PMS_GetMediaColor;
  apfnRip_IF_KcCalls[EPMS_FN_GetTrayInfo]       = PMS_GetTrayInfo;
  apfnRip_IF_KcCalls[EPMS_FN_SetTrayInfo]       = PMS_SetTrayInfo;
  apfnRip_IF_KcCalls[EPMS_FN_GetOutputInfo]     = PMS_GetOutputInfo;
  apfnRip_IF_KcCalls[EPMS_FN_GetScreenInfo]     = PMS_GetScreenInfo;
  apfnRip_IF_KcCalls[EPMS_FN_GetSystemInfo]     = PMS_GetSystemInfo;
  apfnRip_IF_KcCalls[EPMS_FN_SetSystemInfo]     = PMS_SetSystemInfo;
  apfnRip_IF_KcCalls[EPMS_FN_PutBackChannel]    = PMS_PutBackChannel;
  apfnRip_IF_KcCalls[EPMS_FN_PutDebugMessage]   = PMS_PutDebugMessage;
  apfnRip_IF_KcCalls[EPMS_FN_GetBandInfo]       = PMS_GetBandInfo;
  apfnRip_IF_KcCalls[EPMS_FN_RasterLayout]      = PMS_RasterLayout;
  apfnRip_IF_KcCalls[EPMS_FN_RasterRequirements] = PMS_RasterRequirements;
  apfnRip_IF_KcCalls[EPMS_FN_RasterDestination] = PMS_RasterDestination;
  apfnRip_IF_KcCalls[EPMS_FN_RippingComplete]   = PMS_RippingComplete;
  apfnRip_IF_KcCalls[EPMS_FN_Malloc]            = PMS_Malloc;
  apfnRip_IF_KcCalls[EPMS_FN_Free]              = PMS_Free;
  apfnRip_IF_KcCalls[EPMS_FN_GetJobSettings]    = PMS_GetJobSettings;
  apfnRip_IF_KcCalls[EPMS_FN_SetJobSettings]    = PMS_SetJobSettings;
  apfnRip_IF_KcCalls[EPMS_FN_SetJobSettingsToDefaults] = PMS_SetJobSettingsToDefaults;
  apfnRip_IF_KcCalls[EPMS_FN_FSInitVolume]      = PMS_FSInitVolume;
  apfnRip_IF_KcCalls[EPMS_FN_FSMkDir]           = PMS_FSMkDir;
  apfnRip_IF_KcCalls[EPMS_FN_FSOpen]            = PMS_FSOpen;
  apfnRip_IF_KcCalls[EPMS_FN_FSRead]            = PMS_FSRead;
  apfnRip_IF_KcCalls[EPMS_FN_FSWrite]           = PMS_FSWrite;
  apfnRip_IF_KcCalls[EPMS_FN_FSClose]           = PMS_FSClose;
  apfnRip_IF_KcCalls[EPMS_FN_FSSeek]            = PMS_FSSeek;
  apfnRip_IF_KcCalls[EPMS_FN_FSDelete]          = PMS_FSDelete;
  apfnRip_IF_KcCalls[EPMS_FN_FSDirEntryInfo]    = PMS_FSDirEntryInfo;
  apfnRip_IF_KcCalls[EPMS_FN_FSQuery]           = PMS_FSQuery;
  apfnRip_IF_KcCalls[EPMS_FN_FSFileSystemInfo]  = PMS_FSFileSystemInfo;
  apfnRip_IF_KcCalls[EPMS_FN_FSGetDisklock]     = PMS_FSGetDisklock;
  apfnRip_IF_KcCalls[EPMS_FN_FSSetDisklock]     = PMS_FSSetDisklock;
  apfnRip_IF_KcCalls[EPMS_FN_GetResource]       = PMS_GetResource;
  apfnRip_IF_KcCalls[EPMS_FN_FileOpen]          = PMS_FileOpen;
  apfnRip_IF_KcCalls[EPMS_FN_FileRead]          = PMS_FileRead;
  apfnRip_IF_KcCalls[EPMS_FN_FileClose]         = PMS_FileClose;
  apfnRip_IF_KcCalls[EPMS_FN_FileSeek]          = PMS_FileSeek;
  apfnRip_IF_KcCalls[EPMS_FN_FileBytes]         = PMS_FileBytes;
  apfnRip_IF_KcCalls[EPMS_FN_SetJobRIPConfig]   = PMS_SetJobRIPConfig;
#ifdef UFST_ENABLED
  /* The following CGIF calls simply give us exposure to
     the UFST module. */
  apfnRip_IF_KcCalls[EPMS_FN_CGIFfco_Access]   = PMS_CGIFfco_Access;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFfco_Plugin]   = PMS_CGIFfco_Plugin;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFfco_Open]     = PMS_CGIFfco_Open;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFenter]        = PMS_CGIFenter;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFconfig]       = PMS_CGIFconfig;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFinit]         = PMS_CGIFinit;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFinitRomInfo]  = PMS_CGIFinitRomInfo;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFfco_Close]    = PMS_CGIFfco_Close;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFmakechar]     = PMS_CGIFmakechar;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFchar_size]    = PMS_CGIFchar_size;
  apfnRip_IF_KcCalls[EPMS_FN_CGIFfont]         = PMS_CGIFfont;
  apfnRip_IF_KcCalls[EPMS_FN_PCLswapHdr]       = PMS_PCLswapHdr;
  apfnRip_IF_KcCalls[EPMS_FN_PCLswapChar]      = PMS_PCLswapChar;

#ifdef USEUFSTCALLBACKS
  apfnRip_IF_KcCalls[EPMS_FN_UFSTSetCallbacks] = PMS_UFSTSetCallbacks;
#endif

#ifdef USEPMSDATAPTRFNS
  /* The following UFST functions get the raw data stored
     in the PS3, Wingdings and plugin fco support files. */
  apfnRip_IF_KcCalls[EPMS_FN_UFSTGetPS3FontDataPtr]      = PMS_UFSTGetPS3FontDataPtr;
  apfnRip_IF_KcCalls[EPMS_FN_UFSTGetWingdingFontDataPtr] = PMS_UFSTGetWingdingFontDataPtr;
  apfnRip_IF_KcCalls[EPMS_FN_UFSTGetPluginDataPtr]       = PMS_UFSTGetPluginDataPtr;
  apfnRip_IF_KcCalls[EPMS_FN_UFSTGetSymbolSetDataPtr]    = PMS_UFSTGetSymbolSetDataPtr;
#endif
#endif

  return ((PMS_API_FNS)&apfnRip_IF_KcCalls);
};

