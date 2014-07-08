/* Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_ebddev.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief OIL implementation of the Embedded device.
 *
 *  The Embedded device fulfils certain data storage and handling requirements
 * in the OIL.  It models screen tables, the PostScript backchannel and
 * temporary page buffer data as virtual files, which it stores.  The interface
 * to the embedded device allows data input and output to and from these virtual
 * files, as appropriate to the file type.  For example, PostScript backchannel
 * virtual files can be written to but do not support the seek operation.  Virtual
 * files representing screening tables, however, do support seeking and reading,
 * but cannot be written to.
 *
 * The embedded device also defines a set of parameters, held in a
 * \c OIL_TyEBDDeviceParameters structure, which are used to hold various settings
 * requested both by the job and by the PMS.
 */

#include "oil_interface_oil2pms.h"
#include "oil_ebddev.h"
#include "ripthread.h"
#include "skinkit.h"
#include "devparam.h"
#include "ripcall.h"
#include <string.h>
#include <stdlib.h>
#include "pms_export.h"
#ifdef SDK_SUPPORT_1BPP_EXT_EG
#include "oil_ebd1bpp.h"
#endif
#include "oil_test.h"
#if defined(SDK_SUPPORT_2BPP_EXT_EG) || defined(SDK_SUPPORT_4BPP_EXT_EG)
#include "oil_htm.h"
#endif
#include "oil_psconfig.h"
#include "oil_virtualfile.h"
#include "oil_main.h"
#ifdef USE_PJL
#include "oil_pjl.h"
#endif
#include "oil_media.h"

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyPage *g_pstCurrentPage;

static void * pPdfImgFileHandle;

/** \brief The structure which holds the parameter settings for the embedded device.*/
static OIL_TyEBDDeviceParameters stEBDDeviceParams =
{
  0, 0,                   /* PageWidthFromJob, PageHeighthFromJob */
  0, 0, 0,                /* xResolutionFromJob, yResolutionFromJob, RasterDepthFromJob */
  "", "", "",             /* MediaTypeFromJob, MediaColorFromJob, MediaDestFromJob */
  0, 0,                   /* MediaWeightFromJob, MediaSourceFromJob */
  0, 0, 0,                /* DuplexFromJob, TumbleFromJob, OrientationFromJob */
  612.0f, 792.0f,         /* PageWidthFromPMS, PageHeighthFromPMS */
  0.0f, 0.0f, 0.0f, 0.0f, /* Clip left, top, width and height */
  0.0f, 0.0f, 0.0f, 0.0f, /* Imaging box left, top, right, bottom */
  -1, -1,                 /* PCL page size enumerations */
  0, 0,                   /* MediaSourceFromPMS, TumbleFromPMS */
  0, 0,                   /* bMediaChangeNotify, page ready for checkin */
  0, 0,                   /* ColorMode, ScreenMode */
  0, 0, 0,                /* ImageScreenQuality, GraphicsScreenQuality, TextScreenQuality */
  0,                      /* Test configuration */
  0, 0, 0,                /* PureBlackText, AllTextBlack, BlackSubstitute */
  "", 0, 0, "",           /* ActiveScreenName, ScreenWidth, ScreenHeight, ScreenPSFileName*/
  "",                     /* Parse Comment */
  0,                      /* bForceMonoIfNoCMY */
};

/*! \brief Embedded-device parameters array.
*
* Parameters addresses that are NULL are set in SetJobParams which is called from InitDevParams.
* The addresses are cleared, in EBDdev_ClearJobParams, when the current job pointer is cleared.
*/
PARAM ebd_devparams[] =
{
#define EBD_JOB_PAGEWIDTH   0      /* 0 */
  {
    "PageWidthFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE | PARAM_SET,
    ParamFloat, & stEBDDeviceParams.PageWidthFromJob,  0, MAXINT
  },
#define EBD_JOB_PAGEHEIGHT    EBD_JOB_PAGEWIDTH+1   /* 1 */
  {
    "PageHeightFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE | PARAM_SET,
    ParamFloat, & stEBDDeviceParams.PageHeightFromJob,  0, MAXINT
  },
#define EBD_JOB_XRES        EBD_JOB_PAGEHEIGHT+1   /* 2 */
  {
    "xResolutionFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & stEBDDeviceParams.xResolutionFromJob,  0, MAXINT
  },
#define EBD_JOB_YRES      EBD_JOB_XRES+1      /* 3 */
  {
    "yResolutionFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & stEBDDeviceParams.yResolutionFromJob,  0, MAXINT
  },
#define EBD_JOB_VPC   EBD_JOB_YRES+1      /* 4 */
  {
    "VPCFromJob", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.VPCFromJob,  0, 0
  },
#define EBD_JOB_MEDIATYPE   EBD_JOB_VPC+1    /* 5 */
  {
    "MediaTypeFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE | PARAM_SET,
    ParamString, & stEBDDeviceParams.MediaTypeFromJob,  0, sizeof(stEBDDeviceParams.MediaTypeFromJob) - 1
  },
#define EBD_JOB_MEDIACOLOR   EBD_JOB_MEDIATYPE+1
  {
    "MediaColorFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE | PARAM_SET,
    ParamString, & stEBDDeviceParams.MediaColorFromJob,  0, sizeof(stEBDDeviceParams.MediaColorFromJob) - 1
  },
#define EBD_JOB_MEDIADEST   EBD_JOB_MEDIACOLOR+1
  {
    "MediaDestFromJob", 0,    PARAM_WRITEABLE | PARAM_RANGE | PARAM_SET,
    ParamString, & stEBDDeviceParams.MediaDestFromJob,  0, sizeof(stEBDDeviceParams.MediaDestFromJob) - 1
  },
#define EBD_JOB_MEDIAWEIGHT   EBD_JOB_MEDIADEST+1
  {
    "MediaWeightFromJob", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.MediaWeightFromJob,  0, 0
  },
#define EBD_JOB_MEDIASOURCE   EBD_JOB_MEDIAWEIGHT+1
  {
    "MediaSourceFromJob", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.MediaSourceFromJob,  0, 0
  },
#define EBD_JOB_DUPLEX   EBD_JOB_MEDIASOURCE+1  /* 10 */
  {
    "DuplexFromJob", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & stEBDDeviceParams.bDuplexFromJob,  0, 0
  },
#define EBD_JOB_TUMBLE   EBD_JOB_DUPLEX+1  /* 11 */
  {
    "TumbleFromJob", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & stEBDDeviceParams.bTumbleFromJob,  0, 0
  },
#define EBD_JOB_ORIENTATION   EBD_JOB_TUMBLE+1  /* 12 */
  {
    "OrientationFromJob", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.OrientationFromJob,  0, 0
  },
#define EBD_PMS_PAGEWIDTH    EBD_JOB_ORIENTATION+1
  {
    "PageWidthFromPMS", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & stEBDDeviceParams.PageWidthFromPMS,  0, MAXINT
  },
#define EBD_PMS_PAGEHEIGHT    EBD_PMS_PAGEWIDTH+1
  {
    "PageHeightFromPMS", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamFloat, & stEBDDeviceParams.PageHeightFromPMS,  0, MAXINT
  },
#define EBD_PMS_CLIP_LEFT    EBD_PMS_PAGEHEIGHT+1
  {
    "MediaClipLeftFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.MediaClipLeftFromPMS,  0, 0
  },
#define EBD_PMS_CLIP_TOP    EBD_PMS_CLIP_LEFT+1
  {
    "MediaClipTopFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.MediaClipTopFromPMS,  0, 0
  },
#define EBD_PMS_CLIP_WIDTH    EBD_PMS_CLIP_TOP+1
  {
    "MediaClipWidthFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.MediaClipWidthFromPMS,  0, 0
  },
#define EBD_PMS_CLIP_HEIGHT    EBD_PMS_CLIP_WIDTH+1
  {
    "MediaClipHeightFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.MediaClipHeightFromPMS,  0, 0
  },
#define EBD_PMS_IMAGING_BOX_LEFT    EBD_PMS_CLIP_HEIGHT+1
  {
    "ImagingBoxLeftFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.ImagingBoxLeftFromPMS,  0, 0
  },
#define EBD_PMS_IMAGING_BOX_TOP    EBD_PMS_IMAGING_BOX_LEFT+1
  {
    "ImagingBoxTopFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.ImagingBoxTopFromPMS,  0, 0
  },
#define EBD_PMS_IMAGING_BOX_RIGHT    EBD_PMS_IMAGING_BOX_TOP+1
  {
    "ImagingBoxRightFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.ImagingBoxRightFromPMS,  0, 0
  },
#define EBD_PMS_IMAGING_BOX_BOTTOM    EBD_PMS_IMAGING_BOX_RIGHT+1
  {
    "ImagingBoxBottomFromPMS", 0,    PARAM_WRITEABLE,
    ParamFloat, & stEBDDeviceParams.ImagingBoxBottomFromPMS,  0, 0
  },
#define EBD_PMS_MEDIASOURCE    EBD_PMS_IMAGING_BOX_BOTTOM+1
  {
    "MediaSourceFromPMS", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.MediaSourceFromPMS,  0, 0
  },
#define EBD_PMS_TUMBLE    EBD_PMS_MEDIASOURCE+1
  {
    "TumbleFromPMS", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & stEBDDeviceParams.bTumbleFromPMS,  0, 0
  },
#define EBD_PMS_PCL5PAGESIZE    EBD_PMS_TUMBLE+1
  {
    "PCL5PageSizeFromPMS", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.PCL5PageSizeFromPMS,  0, 0
  },
#define EBD_PMS_PCLXLPAGESIZE    EBD_PMS_PCL5PAGESIZE+1
  {
    "PCLXLPageSizeFromPMS", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.PCLXLPageSizeFromPMS,  0, 0
  },
#define EBD_MEDIA_CHANGE_NOTIFY    EBD_PMS_PCLXLPAGESIZE+1
  {
    "MediaChangeNotify",   0, PARAM_WRITEABLE,
    ParamBoolean, & stEBDDeviceParams.bMediaChangeNotify, 0, 0
  },
#define EBD_PAGE_RENDERING_COMPLETE    EBD_MEDIA_CHANGE_NOTIFY+1
  {
    "EndRender",   0, PARAM_WRITEABLE | PARAM_SET,
    ParamBoolean, & stEBDDeviceParams.bEndRender, 0, 0
  },
#define EBD_JOB_COLORMODE      EBD_PAGE_RENDERING_COMPLETE+1
  {
    "GGColorMode", 0,    PARAM_WRITEABLE | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.ColorModeFromJob,  0, MAXINT
  },
#define EBD_JOB_SCREENMODE      EBD_JOB_COLORMODE+1
  {
    "GGScreenMode", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.ScreenModeFromJob,  0, MAXINT
  },
#define EBD_JOB_IMAGESCREEN     EBD_JOB_SCREENMODE+1
  {
    "GGImageScreenQuality", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.ImageScreenQualityFromJob,  0, MAXINT
  },
#define EBD_JOB_GRAPHICSSCREEN  EBD_JOB_IMAGESCREEN+1
  {
    "GGGraphicsScreenQuality", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.GraphicsScreenQualityFromJob,  0, MAXINT
  },
#define EBD_JOB_TEXTSCREEN      EBD_JOB_GRAPHICSSCREEN+1
  {
    "GGTextScreenQuality", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.TextScreenQualityFromJob,  0, MAXINT
  },
#define EBD_TEST_CONFIGURATION  EBD_JOB_TEXTSCREEN+1
  {
    "GGTestConfig", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.TestConfiguration,  0, MAXINT
  },
#define EBD_SUPPRESS_BLANK      EBD_TEST_CONFIGURATION+1
  {
    "Job_SuppressBlank", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_PUREBLACKTEXT      EBD_SUPPRESS_BLANK+1
  {
    "GGPureBlackText", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.bPureBlackText,  0, MAXINT
  },
#define EBD_ALLTEXTBLACK      EBD_PUREBLACKTEXT+1
  {
    "GGAllTextBlack", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.bAllTextBlack,  0, MAXINT
  },
#define EBD_BLACKSUBSTITUE     EBD_ALLTEXTBLACK+1
  {
    "GGBlackSubstitute", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.bBlackSubstitute,  0, MAXINT
  },
#define EBD_SCRN_ACTIVESCREEN     EBD_BLACKSUBSTITUE+1
  {
    "ActiveScreenName", 0,    PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & stEBDDeviceParams.ActiveScreenName,  0, sizeof(stEBDDeviceParams.ActiveScreenName) - 1
  },
#define EBD_SCRN_SCREENWIDTH    EBD_SCRN_ACTIVESCREEN+1
  {
    "ScreenWidth", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.ScreenTableWidth,  0, MAXINT
  },
#define EBD_SCRN_SCREENHEIGHT     EBD_SCRN_SCREENWIDTH+1
  {
    "ScreenHeight", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, & stEBDDeviceParams.ScreenTableHeight,  0, MAXINT
  },
#define EBD_SCRN_SCREENFILENAME    EBD_SCRN_SCREENHEIGHT+1
  {
    "ScreenPSFileName", 0,    PARAM_READONLY | PARAM_SET | PARAM_RANGE,
    ParamString, & stEBDDeviceParams.ScreenFile,  0, sizeof(stEBDDeviceParams.ScreenFile) - 1
  },
#define EBD_PARSECOMMENT   EBD_SCRN_SCREENFILENAME+1
  {
    "ParseComment", 0, PARAM_WRITEABLE | PARAM_RANGE,
    ParamString, & stEBDDeviceParams.ParseComment, 0, sizeof(stEBDDeviceParams.ParseComment) - 1
  },
#define EBD_FORCE_MONO_IF_NO_CMY      EBD_PARSECOMMENT+1
  {
    "GGForceMonoIfNoCMY", 0,    PARAM_WRITEABLE,
    ParamInteger, & stEBDDeviceParams.bForceMonoIfNoCMY,  0, MAXINT
  },
#define EBD_JOB_PDL     EBD_FORCE_MONO_IF_NO_CMY+1
  {
    "Job_PDL", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, &stEBDDeviceParams.nJobPDL,  0, MAXINT
  },
#define EBD_JOB_IMAGE_INPUT     EBD_JOB_PDL+1
  {
    "Job_ImageInput", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTRIPDEPTH     EBD_JOB_IMAGE_INPUT+1
  {
    "Job_DefaultRIPDepth", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTRESX     EBD_JOB_DEFAULTRIPDEPTH+1
  {
    "Job_DefaultResX", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTRESY     EBD_JOB_DEFAULTRESX+1
  {
    "Job_DefaultResY", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTDUPLEX     EBD_JOB_DEFAULTRESY+1
  {
    "Job_DefaultDuplex", 0,    PARAM_READONLY,
    ParamBoolean, NULL,  0, 1
  },
#define EBD_JOB_DEFAULTTUMBLE     EBD_JOB_DEFAULTDUPLEX+1
  {
    "Job_DefaultTumble", 0,    PARAM_READONLY,
    ParamBoolean, NULL,  0, 1
  },
#define EBD_JOB_DEFAULTCOLLATE     EBD_JOB_DEFAULTTUMBLE+1
  {
    "Job_DefaultCollate", 0,    PARAM_READONLY,
    ParamBoolean, NULL,  0, 1
  },
#define EBD_JOB_DEFAULTJOBNAME     EBD_JOB_DEFAULTCOLLATE+1
  {
    "Job_DefaultJobName", 0,    PARAM_READONLY,
    ParamString,  NULL,  0, OIL_MAX_JOBNAME_LENGTH
  },
#define EBD_JOB_DEFAULTCOPIES     EBD_JOB_DEFAULTJOBNAME+1
  {
    "Job_DefaultCopies", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTORIENTATION     EBD_JOB_DEFAULTCOPIES+1
  {
    "Job_DefaultOrientation", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTSCREENMODE     EBD_JOB_DEFAULTORIENTATION+1
  {
    "Job_DefaultScreenMode", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTWIDEA4     EBD_JOB_DEFAULTSCREENMODE+1
  {
    "Job_DefaultWideA4", 0,    PARAM_READONLY,
    ParamBoolean, NULL,  0, 1
  },

#define EBD_JOB_DEFAULTMEDIATYPE     EBD_JOB_DEFAULTWIDEA4+1
  {
    "Job_DefaultMediaType", 0,    PARAM_READONLY,
    ParamString, NULL,  0, 32
  },

#define EBD_JOB_DEFAULTPCL5OUTPUTTRAY     EBD_JOB_DEFAULTMEDIATYPE+1
  {
    "Job_DefaultPCL5OutputTray", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_DEFAULTPCL5PAPERSIZE     EBD_JOB_DEFAULTPCL5OUTPUTTRAY+1
  {
    "Job_DefaultPCL5PaperSize", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_PCL5CUSTPAPERWIDTH     EBD_JOB_DEFAULTPCL5PAPERSIZE+1
  {
    "Job_PCL5CustomPaperWidth", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_PCL5CUSTPAPERHEIGHT     EBD_JOB_PCL5CUSTPAPERWIDTH+1
  {
    "Job_PCL5CustomPaperHeight", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_DEFAULTPCL5MEDIASOURCE     EBD_JOB_PCL5CUSTPAPERHEIGHT+1
  {
    "Job_DefaultPCL5MediaSource", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_DEFAULTVMI     EBD_JOB_DEFAULTPCL5MEDIASOURCE+1
  {
    "Job_DefaultVMI", 0,    PARAM_READONLY,
    ParamFloat, NULL,  0, 0
  },

#define EBD_JOB_DEFAULTPCLXLOUTPUTTRAY     EBD_JOB_DEFAULTVMI+1
  {
    "Job_DefaultPCLXLOutputTray", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_DEFAULTPCLXLMEDIASOURCE     EBD_JOB_DEFAULTPCLXLOUTPUTTRAY+1
  {
    "Job_DefaultPCLXLMediaSource", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },

#define EBD_JOB_DEFAULTPCLXLPAPERSIZE     EBD_JOB_DEFAULTPCLXLMEDIASOURCE+1
  {
    "Job_DefaultPCLXLPaperSize", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTJOBOFFSET     EBD_JOB_DEFAULTPCLXLPAPERSIZE+1
  {
    "Job_DefaultJobOffset", 0,    PARAM_READONLY,
    ParamInteger, NULL,  0, MAXINT
  },
#define EBD_JOB_DEFAULTSYMBOLSET     EBD_JOB_DEFAULTJOBOFFSET+1
  {
    "Job_DefaultSymbolSet", 0,    PARAM_READONLY,
    ParamString, NULL,  0, 6
  },
#define EBD_CFG_COLORMANAGEMODE     EBD_JOB_DEFAULTSYMBOLSET+1
  {
    "Cfg_ColorManageMode", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, &g_ConfigurableFeatures.uColorManagement,  0, MAXINT
  },
#define EBD_CFG_SWINRAM     EBD_CFG_COLORMANAGEMODE+1
  {
    "Cfg_SWinRAM", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bSWinRAM,  0, 1
  },
#define EBD_CFG_RETAINEDRASTER     EBD_CFG_SWINRAM+1
  {
    "Cfg_RetainedRaster", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bRetainedRaster,  0, 1
  },
#define EBD_CFG_PRINTABLEAREAMODE     EBD_CFG_RETAINEDRASTER+1
  {
    "Cfg_PrintableAreaMode", 0, PARAM_READONLY | PARAM_SET,
    ParamInteger, &g_ConfigurableFeatures.ePrintableMode,  0, MAXINT
  },
#define EBD_CFG_PAPERSELECTMODE     EBD_CFG_PRINTABLEAREAMODE+1
  {
    "Cfg_PaperSelectMode", 0, PARAM_READONLY | PARAM_SET,
    ParamInteger, &g_ConfigurableFeatures.g_ePaperSelectMode,  0, MAXINT
  },
#define EBD_CFG_GENOACOMPLIANCE     EBD_CFG_PAPERSELECTMODE+1
  {
    "Cfg_GenoaCompliance", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bGenoaCompliance,  0, 1
  },
#define EBD_CFG_TRAPPINGWIDTH     EBD_CFG_GENOACOMPLIANCE+1
  {
    "Cfg_TrappingWidth", 0, PARAM_READONLY | PARAM_SET,
    ParamFloat, &g_ConfigurableFeatures.fEbdTrapping,  0, 0
  },
#define EBD_CFG_DEFAULTCOLORMODE     EBD_CFG_TRAPPINGWIDTH+1
  { /* If you want the actual colormode being used then use GGColorMode instead */
    "Cfg_DefaultColorMode", 0, PARAM_READONLY | PARAM_SET,
    ParamInteger, &g_ConfigurableFeatures.nDefaultColorMode,  0, MAXINT
  },
#define EBD_CFG_USEUFST5     EBD_CFG_DEFAULTCOLORMODE+1
  {
    "Cfg_UseUFST5", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bUseUFST5,  0, 1
  },
#define EBD_CFG_USEUFST7     EBD_CFG_USEUFST5+1
  {
    "Cfg_UseUFST7", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bUseUFST7,  0, 1
  },
#define EBD_CFG_SCALABLECONSUMPTION     EBD_CFG_USEUFST5+1
  {
    "Cfg_ScalableConsumption", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bScalableConsumption,  0, 1
  },
#define EBD_CFG_IMAGEDECIMATON     EBD_CFG_SCALABLECONSUMPTION+1
  {
    "Cfg_ImageDecimation", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bImageDecimation,  0, 1
  },
#define EBD_CFG_USEFF EBD_CFG_IMAGEDECIMATON+1
  {
    "Cfg_UseFF", 0, PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bUseFF,  0, 1
  },
#define EBD_CFG_BANDDELIVERYTYPE     EBD_CFG_USEFF+1
  {
    "Cfg_BandDeliveryType", 0,    PARAM_READONLY | PARAM_SET,
    ParamInteger, &g_ConfigurableFeatures.eBandDeliveryType,  0, MAXINT
  },
#define EBD_CFG_PIXELINTERLEAVING   EBD_CFG_BANDDELIVERYTYPE+1
  {
    "Cfg_PixelInterleaving", 0,    PARAM_READONLY | PARAM_SET,
    ParamBoolean, &g_ConfigurableFeatures.bPixelInterleaving,  0, 1
  },
#define EBD_SYS_HDDPDFSPOOLDIR     EBD_CFG_PIXELINTERLEAVING+1
  {
    "HDD_PDFSpoolDir", 0,    PARAM_READONLY  | PARAM_SET,
    ParamString, &g_tSystemInfo.szPDFSpoolDir,  0, 256
  }
};

/** \brief The number of parameters in ebd_devparams. */
#define EBD_MAX_PARAMS (sizeof(ebd_devparams)/sizeof(ebd_devparams[0]))

static uint32 ebd_devparams_order[EBD_MAX_PARAMS];

/* return index into devparams[] for name, or EBD_MAX_PARAMS if not found. */
static inline uint32 ebd_find_param(char *name, int32 len)
{
  int32 lo = 0, hi = EBD_MAX_PARAMS - 1;
  do { /* Binary search for parameter. */
    int32 mid = (lo + hi) / 2;
    uint32 i = ebd_devparams_order[mid];
    int32 diff = len - ebd_devparams[i].namelen;
    int32 cmp = strncmp(name, ebd_devparams[i].name,
                        diff > 0 ? len - diff : len);
    if ( cmp == 0 ) /* Same up to minimum length, so compare on length */
      cmp = diff;
    if ( cmp == 0 ) /* Same length and content too. */
      return i;
    if ( cmp < 0 ) {
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  } while ( lo <= hi );
  /* Failed to find parameter. */
  return EBD_MAX_PARAMS;
}

/** \brief Test whether a value has been set for a parameter. */
#define EBD_PARAM_IS_SET(_index_)  ((ebd_devparams[_index_].flags & PARAM_SET) != 0)

/** \brief File descriptors are defined within this range */
#define EBD_FILEDESCMASK  0x00ffffff

/** \brief File descriptors with the top byte set to this value are screen tables */
#define EBD_FILEDESC_SCRN 0x01000000

/** \brief File descriptors with the top byte set to this value are PS backchannel */
#define EBD_FILEDESC_BKCHN 0x02000000

/** \brief File descriptors with the top byte set to this value are pagebuffer temporary data.
* PGB files such as PartialPaint and Compositing.
* Other virtual files such as tray information and paper sizes ps configuration.
*/
#define EBD_FILEDESC_PGB 0x04000000

/** \brief File descriptors with the top byte set to this value are image input */
#define EBD_FILEDESC_PDFIMGINPUT 0x08000000

/** \brief File descriptors with the top byte set to this value are files that are intended
to be written to disk via a receiver application that understands the tagged format.
*/
#define EBD_FILEDESC_BCFILE 0x10000000

#define DESC_TO_STR(_desc_) (((_desc_&~EBD_FILEDESCMASK)==EBD_FILEDESC_SCRN)?"Screening file": \
                             ((_desc_&~EBD_FILEDESCMASK)==EBD_FILEDESC_PGB)?"PGB": \
                             ((_desc_&~EBD_FILEDESCMASK)==EBD_FILEDESC_BKCHN)?"Backchannel": \
                             ((_desc_&~EBD_FILEDESCMASK)==EBD_FILEDESC_BCFILE)?"BCFile":\
                             ((_desc_&~EBD_FILEDESCMASK)==EBD_FILEDESC_PDFIMGINPUT)?"InputImageFormat": \
                              "don't know")

/* forward declarations */
static void ebd_RemapHTTables(OIL_eScreenMode eScreenMode);
static void ebd_process_param(int index);
static void RIPCALL ebd_set_last_error( DEVICELIST *dev, int nError );
static int RIPCALL ebd_get_last_error  ( DEVICELIST *dev );
static int RIPCALL ebd_init_device     ( DEVICELIST *dev );
static DEVICE_FILEDESCRIPTOR RIPCALL ebd_open_file ( DEVICELIST *dev ,
                                                     unsigned char *filename ,
                                                     int openflags);
static int RIPCALL ebd_read_file       ( DEVICELIST *dev ,
                                           DEVICE_FILEDESCRIPTOR descriptor ,
                                           unsigned char *buff ,
                                           int len);
static int RIPCALL ebd_write_file      ( DEVICELIST *dev,
                                           DEVICE_FILEDESCRIPTOR descriptor,
                                           unsigned char *buff,
                                           int len );
static int RIPCALL ebd_close_file      ( DEVICELIST *dev ,
                                           DEVICE_FILEDESCRIPTOR descriptor);
static int RIPCALL ebd_abort_file      ( DEVICELIST *dev ,
                                           DEVICE_FILEDESCRIPTOR descriptor);
static int RIPCALL ebd_seek_file       ( DEVICELIST *dev ,
                                           DEVICE_FILEDESCRIPTOR descriptor,
                                           Hq32x2 *destination ,
                                           int whence);
static int RIPCALL ebd_bytes_file      ( DEVICELIST *dev ,
                                           DEVICE_FILEDESCRIPTOR descriptor,
                                           Hq32x2 *bytes,
                                           int reason);
static int RIPCALL ebd_status_file     ( DEVICELIST *dev ,
                                           unsigned char *filename ,
                                           STAT *statbuff );
static void* RIPCALL ebd_start_file_list ( DEVICELIST *dev ,
                                           unsigned char *pattern);
static int RIPCALL ebd_next_file       ( DEVICELIST *dev ,
                                           void **handle ,
                                           unsigned char *pattern,
                                           FILEENTRY *entry);
static int RIPCALL ebd_end_file_list ( DEVICELIST *dev ,
                                         void * handle);
static int RIPCALL ebd_rename_file     ( DEVICELIST *dev,
                                           unsigned char *file1 ,
                                           unsigned char *file2);
static int RIPCALL ebd_delete_file     ( DEVICELIST *dev ,
                                           unsigned char *filename );
static int RIPCALL ebd_set_param       ( DEVICELIST *dev ,
                                           DEVICEPARAM *param);
static int RIPCALL ebd_start_param     ( DEVICELIST *dev );
static int RIPCALL ebd_get_param       ( DEVICELIST *dev ,
                                           DEVICEPARAM *param);
static int RIPCALL ebd_status_device   ( DEVICELIST *dev ,
                                           DEVSTAT *devstat);

static int RIPCALL ebd_noerror         ( DEVICELIST *dev );
static int RIPCALL ebd_ioerror         ( DEVICELIST *dev );
static void* RIPCALL ebd_void           ( DEVICELIST *dev );
static int32 RIPCALL ebd_spare           ( void );
static int RIPCALL ebd_undefined( DEVICELIST *dev );

static void SetJobParams();
void EBDdev_ClearJobParams();
static int MapOutputTray( int ePDL, unsigned int eOutputTray, char *szOutputTray );
static void MapMediaSource( int * pPCL5MediaSource, int * pPCLXLMediaSource );

/**
 * \brief  Embedded Device Type List Structure.
 */

DEVICETYPE Embedded_Device_Type = {
  EMBEDDED_DEVICE_TYPE, /**< The device ID number */
  DEVICERELATIVE,         /**< Flags to indicate specifics of device */
  (int)(sizeof (OIL_Ty_EBDDeviceState)), /**< The size of the private data */
  0,                      /**< Minimum ticks between tickle functions */
  NULL,                   /**< Procedure to service the device */
  ebd_get_last_error,     /**< Return last error for this device */
  ebd_init_device,        /**< Call to initialize device */
  ebd_open_file,          /**< Call to open file on device */
  ebd_read_file,          /**< Call to read data from file on device */
  ebd_write_file,         /**< Call to write data to file on device */
  ebd_close_file,         /**< Call to close file on device */
  ebd_abort_file,         /**< Call to abort action on the device */
  ebd_seek_file,          /**< Call to seek file on device */
  ebd_bytes_file,         /**< Call to get bytes avail on an open file */
  ebd_status_file,        /**< Call to check status of file */
  ebd_start_file_list,    /**< Call to start listing files */
  ebd_next_file,          /**< Call to get next file in list */
  ebd_end_file_list,      /**< Call to end listing */
  ebd_rename_file,        /**< Rename file on the device */
  ebd_delete_file,        /**< Remove file from device */
  ebd_set_param,          /**< Call to set device parameter */
  ebd_start_param,        /**< Call to start getting device parameters */
  ebd_get_param,          /**< Call to get the next device parameter */
  ebd_status_device,      /**< Call to get the status of the device */
  ebd_noerror,            /**< Call to dismount the device */
  ebd_ioerror,            /**< Call to return buffer size */
  NULL,                   /**< ioctl slot */
  ebd_spare,              /**< Spare slot */
};

/**
 * \brief Map the media source as defined in the OIL job to PDL-specific equivalents.
 *
 * This function is used when generating configuration information for PCL5 or PCL XL input.
 * It effectively translates the current job's media source setting into the corresponding
 * value for PCL5 and PCL XL constants.  If necessary, it will retrieve the input tray
 * information from the PMS to complete the mapping
 * \param[out]   pPCL5MediaSource   A pointer to receive the PCL5-specific input tray setting.
 * \param[out]   pPCLXLMediaSource  A pointer to receive the PCL XL-specific input tray setting.
 */
static void MapMediaSource( int * pPCL5MediaSource, int * pPCLXLMediaSource )
{
  PMS_TyMediaSource *ThisMediaSource;
  unsigned int uMediaSource;

  if( g_pstCurrentJob->bManualFeed )
    uMediaSource = PMS_TRAY_MANUALFEED;
  else
    uMediaSource = g_pstCurrentJob->tCurrentJobMedia.uInputTray ;
  if(PMS_GetMediaSource(uMediaSource, &ThisMediaSource))
  {
    *pPCL5MediaSource = ThisMediaSource->ePCL5MediaSource;
    *pPCLXLMediaSource = ThisMediaSource->ePCLXLMediaSource;
  }
  else
    /* Unhandled PMS value */
    HQFAILV(("MapMediaSource: Unknown media source %d", uMediaSource));
}

/**
 * \brief Map paper trays as defined in the OIL to a PDL-specific equivalent.
 *
 * This function is used when generating configuration information for PCL5 or PCL XL input.
 * It effectively translates an OIL_eOutputTray value into the corresponding value for PCL5
 * or PCL XL constants
 * \param[in]   ePDL        An OIL_eTyPDLType value.  Valid values for calling this function
                            are be OIL_PDL_PCL5c, OIL_PDL_PCL5e and OIL_PDL_PCLXL.
 * \param[in]   eOutputTray An OIL_eOutputTray value, to be translated into a PDL-specific equivalent.
 * \return      Returns the PDL-specific equivalent of eOutputTray, or 0 (the default or automatically
                selected tray) if either the ePDL or the eOutputTray are not recognized.
 */
static int MapOutputTray( int ePDL, unsigned int eOutputTray,char *szOutputTray )
{
  int iPCLOutputTray = 0;
  PMS_TyMediaDest *ThisMediaDest;

  if(PMS_GetMediaDest(eOutputTray, &ThisMediaDest))
  {
    if( ePDL == OIL_PDL_PCL5c || ePDL == OIL_PDL_PCL5e )
      iPCLOutputTray = ThisMediaDest->ePCL5Mediadest;
    else if( ePDL == OIL_PDL_PCLXL )
      iPCLOutputTray = ThisMediaDest->ePCLXLMediadest;
    else if( ePDL == OIL_PDL_PS )
      strcpy(szOutputTray, (const char *)ThisMediaDest->szPSMediaDest);
    else
      HQFAILV(("MapOutputTray: Unknown PDL %d", ePDL));
  }
  else
    HQFAILV(("MapOutputTray: Unknown output tray %d", eOutputTray));

  return iPCLOutputTray;
}

void ConvertSymbolSet(int kindvalue, char *szSymbolID)
{
  int length;

  sprintf(szSymbolID, "%d", kindvalue/32);
  length = (int)strlen(szSymbolID);
  szSymbolID[length] = (char)((kindvalue%32) + 64);
  szSymbolID[length + 1] = '\0';
}

/* \brief Set parameter addresses and flags.
 *
 * This applies to the parameters that point to dynamically created values.
 * The job structure only exists for the life of the job.
 */
static void SetJobParams() {
  if(!g_pstCurrentJob) {
    return;
  }

  /* Job PDL parameter is stored in the stEBDDeviceParams structure
     to abstract it away from the OIL enumeration values
     HqnEmbedded procset relies upon these numbers.
   */
  ebd_devparams[EBD_JOB_PDL].flags |= PARAM_SET;
  switch(g_pstCurrentJob->ePDLType)
  {
    case OIL_PDL_PS:
      stEBDDeviceParams.nJobPDL = 1;
    break;
    case OIL_PDL_XPS:
      stEBDDeviceParams.nJobPDL = 2;
    break;
    case OIL_PDL_PDF:
      stEBDDeviceParams.nJobPDL = 3;
    break;
    case OIL_PDL_PCL5e:
      stEBDDeviceParams.nJobPDL = 4;
    break;
    case OIL_PDL_PCL5c:
      stEBDDeviceParams.nJobPDL = 5;
    break;
    case OIL_PDL_PCLXL:
      stEBDDeviceParams.nJobPDL = 6;
    break;

    default:
      stEBDDeviceParams.nJobPDL = 0;
      ebd_devparams[EBD_JOB_PDL].flags &= ~PARAM_SET;
    break;
  }

#define OIL_SET_PARAM(_id_, _addr_) \
  ebd_devparams[_id_].addr = _addr_; \
  ebd_devparams[_id_].flags |= PARAM_SET; \

  OIL_SET_PARAM(EBD_JOB_IMAGE_INPUT,          &g_pstCurrentJob->bInputIsImage);
  OIL_SET_PARAM(EBD_JOB_DEFAULTRIPDEPTH,      &g_pstCurrentJob->uRIPDepth);
  OIL_SET_PARAM(EBD_JOB_DEFAULTRESX,          &g_pstCurrentJob->uXResolution);
  OIL_SET_PARAM(EBD_JOB_DEFAULTRESY,          &g_pstCurrentJob->uYResolution);
  OIL_SET_PARAM(EBD_JOB_DEFAULTDUPLEX,        &g_pstCurrentJob->bDuplex);
  OIL_SET_PARAM(EBD_JOB_DEFAULTTUMBLE,        &g_pstCurrentJob->bTumble);
  OIL_SET_PARAM(EBD_JOB_DEFAULTCOLLATE,       &g_pstCurrentJob->bCollate);
  OIL_SET_PARAM(EBD_JOB_DEFAULTJOBNAME,       &g_pstCurrentJob->szJobName);
  OIL_SET_PARAM(EBD_JOB_DEFAULTCOPIES,        &g_pstCurrentJob->uCopyCount);
  OIL_SET_PARAM(EBD_JOB_DEFAULTORIENTATION,   &g_pstCurrentJob->uOrientation);
  OIL_SET_PARAM(EBD_JOB_DEFAULTSCREENMODE,    &g_pstCurrentJob->eScreenMode);
  OIL_SET_PARAM(EBD_SUPPRESS_BLANK,           &g_pstCurrentJob->bSuppressBlank);

  if((g_pstCurrentJob->ePDLType == OIL_PDL_PCL5e) ||
     (g_pstCurrentJob->ePDLType == OIL_PDL_PCL5c) ||
     (g_pstCurrentJob->ePDLType == OIL_PDL_PCLXL) ) {
    /* \todo Decided what to do with these statics... put them in stEbd structure? */
    static int iPCL5MediaSource = 0;
    static int iPCLXLMediaSource = 0;

    static int iPCL5CustomPaperWidth;
    static int iPCL5CustomPaperHeight;

    static int iPCL5OutputTray;
    static int iPCLXLOutputTray;
    static int nJog;

    static char szSymbolSet[6];

    iPCL5OutputTray = MapOutputTray( OIL_PDL_PCL5c, g_pstCurrentJob->tCurrentJobMedia.uOutputTray, NULL );
    iPCLXLOutputTray = MapOutputTray( OIL_PDL_PCLXL, g_pstCurrentJob->tCurrentJobMedia.uOutputTray, NULL );
    nJog = (g_pstCurrentJob->uJobOffset)?2:0;

    MapMediaSource( &iPCL5MediaSource, &iPCLXLMediaSource );
    iPCL5CustomPaperWidth = (int)(g_pstCurrentJob->dUserPaperWidth * 10) ; /* convert to decipoints */
    iPCL5CustomPaperHeight = (int)(g_pstCurrentJob->dUserPaperHeight * 10) ; /* convert to decipoints */

    OIL_SET_PARAM(EBD_JOB_DEFAULTWIDEA4, &g_pstCurrentJob->bWideA4);
    OIL_SET_PARAM(EBD_JOB_DEFAULTMEDIATYPE, &g_pstCurrentJob->tCurrentJobMedia.szMediaType);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCL5OUTPUTTRAY, &iPCL5OutputTray);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCL5PAPERSIZE, &g_pstCurrentJob->uPCL5PaperSize);
    OIL_SET_PARAM(EBD_JOB_PCL5CUSTPAPERWIDTH, &iPCL5CustomPaperWidth);
    OIL_SET_PARAM(EBD_JOB_PCL5CUSTPAPERHEIGHT, &iPCL5CustomPaperHeight);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCL5MEDIASOURCE, &iPCL5MediaSource);
    OIL_SET_PARAM(EBD_JOB_DEFAULTVMI, &g_pstCurrentJob->dVMI);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCLXLOUTPUTTRAY, &iPCLXLOutputTray);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCLXLMEDIASOURCE, &iPCLXLMediaSource);
    OIL_SET_PARAM(EBD_JOB_DEFAULTPCLXLPAPERSIZE, &g_pstCurrentJob->uPCLXLPaperSize);
    OIL_SET_PARAM(EBD_JOB_DEFAULTJOBOFFSET, &nJog);

    ConvertSymbolSet(g_pstCurrentJob->uSymbolSet, szSymbolSet);
    OIL_SET_PARAM(EBD_JOB_DEFAULTSYMBOLSET, szSymbolSet);
  }
}

/* \brief Clear parameter addresses and flags.
*/
void ebddev_ClearJobParams() {
  stEBDDeviceParams.nJobPDL = 0;
  ebd_devparams[EBD_JOB_PDL].flags &= ~PARAM_SET;

#define OIL_CLEAR_PARAM(_id_) \
  ebd_devparams[_id_].addr = NULL; \
  ebd_devparams[_id_].flags &= ~PARAM_SET; \

  OIL_CLEAR_PARAM(EBD_JOB_IMAGE_INPUT);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTRIPDEPTH);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTRESX);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTRESY);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTDUPLEX);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTTUMBLE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTCOLLATE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTJOBNAME);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTCOPIES);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTORIENTATION);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTSCREENMODE);
  OIL_CLEAR_PARAM(EBD_SUPPRESS_BLANK);

  /* PCL config params */
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTWIDEA4);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTMEDIATYPE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCL5OUTPUTTRAY);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCL5PAPERSIZE);
  OIL_CLEAR_PARAM(EBD_JOB_PCL5CUSTPAPERWIDTH);
  OIL_CLEAR_PARAM(EBD_JOB_PCL5CUSTPAPERHEIGHT);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCL5MEDIASOURCE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTVMI);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCLXLOUTPUTTRAY);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCLXLMEDIASOURCE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTPCLXLPAPERSIZE);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTJOBOFFSET);
  OIL_CLEAR_PARAM(EBD_JOB_DEFAULTSYMBOLSET);
}

static int CRT_API param_sort(const void *a, const void *b)
{
  const uint32 *pa = a;
  const uint32 *pb = b;
  return strcmp(ebd_devparams[*pa].name, ebd_devparams[*pb].name);
}

/**
 * \brief The initialization routine for the embedded device type.
 *
 * This function sets up the name length for all the parameters registered
 * for the device, initializes the screen tables and clears the device
 * error status.
 * \param[in]   dev     A pointer to the device to be initialized.
 * \return      This function always returns zero.
 */
static int RIPCALL ebd_init_device( DEVICELIST *dev )
{
  unsigned int i;

  for ( i = 0; i < EBD_MAX_PARAMS; i++ ) {
    ebd_devparams[i].namelen = (int)strlen( ebd_devparams[i].name );
    ebd_devparams_order[i] = i;
  }
  qsort(ebd_devparams_order, EBD_MAX_PARAMS, sizeof(uint32), param_sort);

#ifdef SDK_SUPPORT_1BPP_EXT_EG
  ebd_scrn_init();
#endif

  ebd_set_last_error( dev, DeviceNoError );
  return 0;
}

/**
 * \brief Set a device parameter to the given value.
 *
 * This function searches the device's parameter list for the specified parameter,
 * ensures that the parameter is writeable, validates the value passed in and, if
 * all tests are passed, sets the device parameter to the specified value and calls
 * ebd_process_param() to ensure any required downstream processing is carried out.
 *
 * \param[in]   dev     A pointer to the device that owns the parameter.
 * \param[in]   param   A pointer to a parameter structure which contains both the
                        name of the parameter and the value it is to be set to.
 * \return      Returns \c ParamAccepted if the new value is accepted and set
 *              successfully. Other values indicate an error, as defined in
 *              swdevice.h.
 */
static int RIPCALL ebd_set_param ( DEVICELIST *dev , DEVICEPARAM *param )
{
  int *int_ptr;
  unsigned char *str_ptr;
  float *flt_ptr;
  char *lname;
  int len;
  unsigned int i;

  ebd_set_last_error( dev, DeviceNoError );

  len = param->paramnamelen;
  lname = (char *) param->paramname;

  if ( lname == NULL || len <= 0 )
    return ParamConfigError;

  for (i = 0; i < EBD_MAX_PARAMS; i++ )
  {
    if ( len == ebd_devparams [i].namelen &&
         strncmp ( lname , ebd_devparams [i].name, (size_t)(len) ) == 0 )
      break;
  }
  if ( i >= EBD_MAX_PARAMS )
    return ParamIgnored;

  if ((ebd_devparams [i].flags & PARAM_WRITEABLE) == 0)
    /* The caller is not allowed to set this parameter */
    return ParamConfigError;

  if ( ebd_devparams [i].type != param->type )
    return ParamTypeCheck;

  switch ( param->type )
  {
    case ParamBoolean:
      int_ptr = (int *) ebd_devparams [i].addr;
      *int_ptr = param->paramval.boolval;
      break;

    case ParamInteger:
      if ( ebd_devparams [i].flags & PARAM_RANGE )
      {
        if ( param->paramval.intval < ebd_devparams [i].minval ||
             param->paramval.intval > ebd_devparams [i].maxval )
          return ParamRangeCheck;
      }
      int_ptr = (int *) ebd_devparams [i].addr;
      *int_ptr = param->paramval.intval;
      break;

    case ParamString:
      if(strncmp ( lname , "ParseComment", (size_t)(len) ) != 0 )
      {
        /* Don't do a range check for parsed Comments */
        if ( ebd_devparams [i].flags & PARAM_RANGE )
        {
          if ( param->strvallen > ebd_devparams [i].maxval )
            return ParamRangeCheck;
        }
        str_ptr = (unsigned char *) ebd_devparams [i].addr;
        /* use memcpy because the string may have ascii NUL in it */
        memcpy( str_ptr , param->paramval.strval , CAST_SIGNED_TO_SIZET(param->strvallen) );
        /* and null terminate our local copy */
        str_ptr [ param->strvallen ] = 0;
      }
      else
      {
        /* Allow string to be truncated if a parsed Comment */
        str_ptr = (unsigned char *) ebd_devparams [i].addr;
        /* use memcpy because the string may have ascii NUL in it */
        if(param->strvallen > ebd_devparams [i].maxval)
        {
          memcpy( str_ptr , param->paramval.strval , CAST_SIGNED_TO_SIZET(ebd_devparams [i].maxval) );
          /* and null terminate our local copy */
          str_ptr [ ebd_devparams [i].maxval ] = 0;
        }
        else
        {
          memcpy( str_ptr , param->paramval.strval , CAST_SIGNED_TO_SIZET(param->strvallen) );
          /* and null terminate our local copy */
          str_ptr [ param->strvallen ] = 0;
        }
      }

      break;

    case ParamFloat:
      if ( ebd_devparams [i].flags & PARAM_RANGE )
      {
        if ((double)param->paramval.floatval < (double)ebd_devparams [i].minval ||
            (double)param->paramval.floatval > (double)ebd_devparams [i].maxval )
          return ParamRangeCheck;
      }
      /* There are no writable params that point to doubles. If there were
         then a conversion is required. e.g. see EBD_JOB_DEFAULTVMI in ebd_get_param. */
      flt_ptr = (float *) ebd_devparams [i].addr;
      *flt_ptr = param->paramval.floatval;
      break;

    default:
      return ParamIgnored;
  }

  ebd_devparams [i].flags |= PARAM_SET;
  ebd_process_param(i);
  return ParamAccepted;
}

/** \brief The start_param and get_param functions for the embedded
   device type use last_param to maintain the local state.
*/
static unsigned int last_param = 0;

/**
 * \brief Counts the number of parameters belonging to the device that have valid
 *        values set.
 * \param[in]   dev A pointer to the device.
 * \return      Returns the number of parameters with valid values set.
 */
static int RIPCALL ebd_start_param ( DEVICELIST *dev )
{
  unsigned int param;
  int count;

  ebd_set_last_error( dev, DeviceNoError );
  last_param = 0;

  /* count the number of parameters with a valid value */
  count = 0;
  for ( param = 0; param < EBD_MAX_PARAMS; param++ ) {
    if ( EBD_PARAM_IS_SET ( param ))
      count++;
  }

  return count;
}

/**
 * \brief Retrieve the value of a device parameter.
 *
 * This function can either be called for a specific parameter, or without specifying
 * a particular parameter, as part of an iteration through the parameter list. It
 * searches the device's parameter list for the specified parameter, or for the next
 * parameter that has a valid value.  Both the parameter name and the parameter
 * value are copied into the structure supplied, and the index marking the position
 * in the device list is moved to point to the next parameter.
 *
 * \param[in]       dev     A pointer to the device that owns the parameter.
 * \param[in,out]   param   A pointer to a parameter structure which contains both the
                            name of the parameter and the value it is to be set to.
 * \return          Returns \c ParamAccepted if the requested parameter is found and
 *                  a valid value successfully retrieved. Other values indicate an
 *                  error, as defined in swdevice.h.
 */
static int RIPCALL ebd_get_param ( DEVICELIST *dev , DEVICEPARAM *param )
{
  int *int_ptr;
  unsigned char *str_ptr;
  int len;
  char *lname;

  ebd_set_last_error( dev, DeviceNoError );

  len = param->paramnamelen;
  lname = (char *) param->paramname;

  if (lname == NULL) {
    /* return with the next parameter in the list */
    while ( last_param < EBD_MAX_PARAMS && ! EBD_PARAM_IS_SET ( last_param )) {
      ++last_param;
    }
    if ( last_param >= EBD_MAX_PARAMS )
      return ParamIgnored;
  } else {
    if ( len < 0 )
      return ParamConfigError;

    /* search for the name */
    last_param = ebd_find_param(lname, len);
    if ( last_param >= EBD_MAX_PARAMS )
      return ParamIgnored;

    if ( ! EBD_PARAM_IS_SET ( last_param )) {
      return ParamIgnored;
    }
  }

  param->paramname = (unsigned char *) ebd_devparams [last_param].name;
  param->paramnamelen = ebd_devparams [last_param].namelen;
  param->type = ebd_devparams [last_param].type;
  if(ebd_devparams [last_param].addr == NULL) {
    SetJobParams();
  }
  if(ebd_devparams [last_param].addr == NULL) {
    return ParamIgnored;
  }
  switch (param->type) {
    case ParamBoolean:
      int_ptr = (int *) ebd_devparams [last_param].addr;
      param->paramval.boolval = *int_ptr;
      break;

    case ParamInteger:
      int_ptr = (int *) ebd_devparams [last_param].addr;
      param->paramval.intval = *int_ptr;
      break;

    case ParamString:
      str_ptr = (unsigned char *) ebd_devparams [last_param].addr;
      param->paramval.strval = str_ptr;
      param->strvallen = (int)strlen( (char *) str_ptr );
      break;

    case ParamFloat:
      /* Convert doubles to floats */
      switch ( last_param )
      {
      case EBD_JOB_DEFAULTVMI: /* Pointer to double */
        param->paramval.floatval = (float)(*(double*)ebd_devparams [last_param].addr);
        break;
      default: /* Others are float pointers */
        param->paramval.floatval = *(float *)ebd_devparams [last_param].addr;
        break;
      }
      break;

    default:
      return ParamIgnored;
  }

  ++ last_param;
  return ParamAccepted;
}

/**
 * \brief Get the integer value of a parameter specified by name.
 *
 * \param[in]       szParamName  A string holding the parameter name.
 * \param[out]      pValue       A pointer to an integer to receive the parameter value.
 * \return          Returns zero if the requested parameter is found and the
 *                  value successfully retrieved. Returns -1 if the function is called
 *                  without specifying a parameter name, if the name if of zero length, if the
 *                  parameter cannot be found or if the parameter is not of an integer type.
 */
int ebd_get_int_value(char * szParamName, int * pValue)
{
  int *int_ptr;
  int len, last_param;
  char *lname;

  len = (int)strlen(szParamName);
  lname = szParamName;

  if(lname == NULL || len == 0)
    return -1;

  /* search for the name */
  last_param = ebd_find_param(lname, len);
  if ( last_param >= EBD_MAX_PARAMS )
    return -1;

  if ( ebd_devparams [last_param].type != ParamInteger )
    return -1;

  int_ptr = (int *) ebd_devparams [last_param].addr;
  *pValue = *int_ptr;
  return 0;
}

/**
 * \brief Routine to take some action in response to setting of a parameter by RIP.
 *
 * \param[in]       index       Indicates which parameter (from the ebd_devparams_array)
 *                              has changed.
 */
static void ebd_process_param(int index)
{
  switch(index)
  {
  case EBD_JOB_PAGEWIDTH:
    GG_SHOW(GG_SHOW_EBDDEV, "PageWidthFromJob = %f\n", stEBDDeviceParams.PageWidthFromJob);
    g_pstCurrentJob->tCurrentJobMedia.dWidth = stEBDDeviceParams.PageWidthFromJob;
    break;
  case EBD_JOB_PAGEHEIGHT:
    GG_SHOW(GG_SHOW_EBDDEV, "PageHeightFromJob = %f\n", stEBDDeviceParams.PageHeightFromJob);
    g_pstCurrentJob->tCurrentJobMedia.dHeight = stEBDDeviceParams.PageHeightFromJob;
    break;
  case EBD_JOB_MEDIATYPE:
    GG_SHOW(GG_SHOW_EBDDEV, "MediaTypeFromJob = %s\n", stEBDDeviceParams.MediaTypeFromJob);
    strcpy((char*)g_pstCurrentJob->tCurrentJobMedia.szMediaType, (char*)stEBDDeviceParams.MediaTypeFromJob);
    break;
  case EBD_JOB_MEDIACOLOR:
    GG_SHOW(GG_SHOW_EBDDEV, "MediaColorFromJob = %s\n", stEBDDeviceParams.MediaColorFromJob);
    strcpy((char*)g_pstCurrentJob->tCurrentJobMedia.szMediaColor, (char*)stEBDDeviceParams.MediaColorFromJob);
    break;
  case EBD_JOB_MEDIADEST:
    GG_SHOW(GG_SHOW_EBDDEV, "MediaDestFromJob = %s\n", stEBDDeviceParams.MediaDestFromJob);
    /* The selected output tray is passed to the oil page via the raster description */
    break;
  case EBD_JOB_MEDIAWEIGHT:
    GG_SHOW(GG_SHOW_EBDDEV, "MediaWeightFromJob = %d\n", stEBDDeviceParams.MediaWeightFromJob);
    g_pstCurrentJob->tCurrentJobMedia.uMediaWeight = stEBDDeviceParams.MediaWeightFromJob;
    break;
  case EBD_JOB_MEDIASOURCE:
    GG_SHOW(GG_SHOW_EBDDEV, "MediaSourceFromJob = %d\n", stEBDDeviceParams.MediaSourceFromJob);
    g_pstCurrentJob->tCurrentJobMedia.uInputTray = stEBDDeviceParams.MediaSourceFromJob;
    break;
  case EBD_JOB_COLORMODE:
    g_pstCurrentJob->eColorMode = stEBDDeviceParams.ColorModeFromJob;
    break;
  case EBD_JOB_SCREENMODE:
    g_pstCurrentJob->eScreenMode = stEBDDeviceParams.ScreenModeFromJob;
    ebd_RemapHTTables(g_pstCurrentJob->eScreenMode);
    break;
  case EBD_JOB_IMAGESCREEN:
    g_pstCurrentJob->eImageScreenQuality = stEBDDeviceParams.ImageScreenQualityFromJob;
    if( g_pstCurrentJob->eScreenMode == OIL_Scrn_Job )
    {
      ebd_RemapHTTables(g_pstCurrentJob->eScreenMode);
    }
    break;
  case EBD_JOB_GRAPHICSSCREEN:
    g_pstCurrentJob->eGraphicsScreenQuality = stEBDDeviceParams.GraphicsScreenQualityFromJob;
    if( g_pstCurrentJob->eScreenMode == OIL_Scrn_Job )
    {
      ebd_RemapHTTables(g_pstCurrentJob->eScreenMode);
    }
    break;
  case EBD_JOB_TEXTSCREEN:
    g_pstCurrentJob->eTextScreenQuality = stEBDDeviceParams.TextScreenQualityFromJob;
    if( g_pstCurrentJob->eScreenMode == OIL_Scrn_Job )
    {
      ebd_RemapHTTables(g_pstCurrentJob->eScreenMode);
    }
    break;
  case EBD_MEDIA_CHANGE_NOTIFY:
    GG_SHOW(GG_SHOW_EBDDEV, "Got Media change Notification = %s\n", stEBDDeviceParams.bMediaChangeNotify ? "true" : "false");
    if(g_ConfigurableFeatures.g_ePaperSelectMode == OIL_PaperSelPMS)
    {
      GetMediaFromPMS(&stEBDDeviceParams);
      /* All parameters changeable in GetMediaFromPMS() must be flagged as CHANGED here */
      /* \todo Consider moving the line below to the GetMediaFromPMS function  */
      ebd_devparams [EBD_PMS_PAGEWIDTH].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_PAGEHEIGHT].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_MEDIASOURCE].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_CLIP_TOP].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_CLIP_LEFT].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_CLIP_WIDTH].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_CLIP_HEIGHT].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_IMAGING_BOX_TOP].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_IMAGING_BOX_LEFT].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_IMAGING_BOX_RIGHT].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_IMAGING_BOX_BOTTOM].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_PCLXLPAGESIZE].flags |= PARAM_SET;
      ebd_devparams [EBD_PMS_PCL5PAGESIZE].flags |= PARAM_SET;
    }
    break;
  case EBD_PAGE_RENDERING_COMPLETE:
    break;
  case EBD_TEST_CONFIGURATION:
    GG_SHOW(GG_SHOW_EBDDEV, "TestConfiguration = 0x%x\n", stEBDDeviceParams.TestConfiguration);
    TestSetupConfiguration(stEBDDeviceParams.TestConfiguration);
    break;
  case EBD_PUREBLACKTEXT:
    g_pstCurrentJob->bPureBlackText = stEBDDeviceParams.bPureBlackText;
    break;
  case EBD_ALLTEXTBLACK:
    g_pstCurrentJob->bAllTextBlack = stEBDDeviceParams.bAllTextBlack;
    break;
  case EBD_BLACKSUBSTITUE:
    g_pstCurrentJob->bBlackSubstitute = stEBDDeviceParams.bBlackSubstitute;
    break;
  case EBD_SCRN_ACTIVESCREEN:
#ifdef SDK_SUPPORT_1BPP_EXT_EG
  stEBDDeviceParams.ScreenTableWidth = Get1bppScreenWidth((char*)stEBDDeviceParams.ActiveScreenName);
    stEBDDeviceParams.ScreenTableHeight = Get1bppScreenHeight((char*)stEBDDeviceParams.ActiveScreenName);
    Get1bppScreenFileName((char*)stEBDDeviceParams.ScreenFile, (char*)stEBDDeviceParams.ActiveScreenName);
    ebd_devparams [EBD_SCRN_SCREENWIDTH].flags |= PARAM_SET;
    ebd_devparams [EBD_SCRN_SCREENHEIGHT].flags |= PARAM_SET;
    ebd_devparams [EBD_SCRN_SCREENFILENAME].flags |= PARAM_SET;
#endif
    break;
  case EBD_PARSECOMMENT:
    GG_SHOW(GG_SHOW_EBDDEV, "ParseComment = %s\n", stEBDDeviceParams.ParseComment);
    UpdateOILComment((char *)&stEBDDeviceParams.ParseComment);
    break;
  case EBD_FORCE_MONO_IF_NO_CMY:
    g_pstCurrentJob->bForceMonoIfCMYblank = stEBDDeviceParams.bForceMonoIfNoCMY;
    break;
  default:
    break;
  }
}

void ebddev_EndRender(int page)
{
  UNUSED_PARAM(int, page);
  /* Submit the page to PMS in page delivery model*/
  if (g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND ||
      g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND_DIRECT_SINGLE ||
      g_ConfigurableFeatures.eBandDeliveryType == OIL_PUSH_BAND_DIRECT_FRAME)
  {
    /* in band delivery model non-blank page is already submitted at the start of render
    so need to submit here only if the page is completely blank*/
    if((!g_pstCurrentPage) || (g_pstCurrentPage->nBlankPage == TRUE))
    {
      /* create blank page skeleton and submit to PMS */
      SubmitPageToPMS();

      /* a null band needs to be sent to trigger the page output */
      if(
#ifdef USE_PJL
      (OIL_PjlShouldOutputPage(g_pstCurrentJob->uPagesParsed, g_pstCurrentJob)==TRUE) &&
#endif
      (g_pstCurrentJob->bSuppressBlank != TRUE))
      {
        /* notify end of bands by submitting a NULL band*/
        SubmitBandToPMS((PMS_TyBandPacket *)NULL);
      }
    }
    else /* non-blank page */
    {
      /* a null band needs to be sent to trigger the page output */
#ifdef USE_PJL
      if(OIL_PjlShouldOutputPage(g_pstCurrentJob->uPagesParsed, g_pstCurrentJob)==TRUE)
#endif
      {
        /* notify end of bands by submitting a NULL band*/
        SubmitBandToPMS((PMS_TyBandPacket *)NULL);
      }
    }
  }
  else
  {
    SubmitPageToPMS();
  }
  stEBDDeviceParams.bTumbleFromPMS = FALSE;
  if(stEBDDeviceParams.bDuplexFromJob)
  {
    if(stEBDDeviceParams.bTumbleFromJob && (g_pstCurrentJob->uPagesToPrint & 0x1))
    {
      stEBDDeviceParams.bTumbleFromPMS = TRUE;
    }
  }
  /* don't really need to set Orientation flag as the ps world only writes to this! */
  ebd_devparams [EBD_JOB_ORIENTATION].flags |= PARAM_SET;
  ebd_devparams [EBD_PMS_TUMBLE].flags |= PARAM_SET;
  g_pstCurrentPage = NULL;
}

/**
 * \brief Initialize the embedded device parameters.
 *
 * All parameters, even those that are "...FromPMS" are initialized based on the current job.
 */
void ebddev_InitDevParams()
{
  /* initialize media attributes by default attributes that we got from PMS */
  HQASSERT(g_pstCurrentJob, "InitializeDevParams: Job structure is invalid");
  stEBDDeviceParams.PageWidthFromJob = (float)g_pstCurrentJob->tCurrentJobMedia.dWidth;
  stEBDDeviceParams.PageWidthFromPMS = (float)g_pstCurrentJob->tCurrentJobMedia.dWidth;
  stEBDDeviceParams.PageHeightFromJob = (float)g_pstCurrentJob->tCurrentJobMedia.dHeight;
  stEBDDeviceParams.PageHeightFromPMS = (float)g_pstCurrentJob->tCurrentJobMedia.dHeight;
  strcpy((char*)stEBDDeviceParams.MediaTypeFromJob, (char*)g_pstCurrentJob->tCurrentJobMedia.szMediaType);
  strcpy((char*)stEBDDeviceParams.MediaColorFromJob, (char*)g_pstCurrentJob->tCurrentJobMedia.szMediaColor);
  MapOutputTray(OIL_PDL_PS, g_pstCurrentJob->tCurrentJobMedia.uOutputTray,(char*)stEBDDeviceParams.MediaDestFromJob);
  stEBDDeviceParams.MediaWeightFromJob = g_pstCurrentJob->tCurrentJobMedia.uMediaWeight;
  stEBDDeviceParams.MediaSourceFromJob = g_pstCurrentJob->tCurrentJobMedia.uInputTray;
  stEBDDeviceParams.MediaSourceFromPMS = g_pstCurrentJob->tCurrentJobMedia.uInputTray;
  stEBDDeviceParams.bDuplexFromJob = g_pstCurrentJob->bDuplex;
  stEBDDeviceParams.bTumbleFromJob = g_pstCurrentJob->bTumble;
  stEBDDeviceParams.OrientationFromJob = g_pstCurrentJob->uOrientation;
  stEBDDeviceParams.bTumbleFromPMS = FALSE;
  SetJobParams();
}

/**
 * \brief Routine to remap the screen tables based on requested screen mode.
 *
 * \param[in]     eScreenMode   The requested new screen mode.
 */
static void ebd_RemapHTTables(OIL_eScreenMode eScreenMode)
{
  void *pgbdev = NULL ;
  int nRasterDepth ;
#if defined(SDK_SUPPORT_2BPP_EXT_EG) || defined(SDK_SUPPORT_4BPP_EXT_EG)
  OIL_eScreenQuality eImageQuality, eGraphicsQuality, eTextQuality;
#else
  UNUSED_PARAM(OIL_eScreenMode, eScreenMode);
#endif

  /* get handle to the page buffer device */
  pgbdev = SwLeGetDeviceHandle( (uint8*) "pagebuffer" );
  if (pgbdev == NULL)
  {
    HQFAIL("ebd_RemapHTTables: SwLeGetDeviceHandle failed to get Pagebuffer device handle");
    return;
  }

  /* get raster depth from the page buffer device */
  SwLeGetIntDevParam( pgbdev, (unsigned char *) "RasterDepth", (int*)&nRasterDepth);

#if defined(SDK_SUPPORT_2BPP_EXT_EG) || defined(SDK_SUPPORT_4BPP_EXT_EG)
  switch(nRasterDepth)
  {
#ifdef SDK_SUPPORT_2BPP_EXT_EG
  case 2:
#endif
#ifdef SDK_SUPPORT_4BPP_EXT_EG
  case 4:
#endif
    switch( eScreenMode )
    {
    case OIL_Scrn_Auto:
    case OIL_Scrn_Module:
      eImageQuality = OIL_Scrn_LowLPI;
      eGraphicsQuality = OIL_Scrn_MediumLPI;
      eTextQuality = OIL_Scrn_HighLPI;
      break;
    case OIL_Scrn_Photo:
      eImageQuality = OIL_Scrn_LowLPI;
      eGraphicsQuality = OIL_Scrn_LowLPI;
      eTextQuality = OIL_Scrn_LowLPI;
      break;
    case OIL_Scrn_Graphics:
      eImageQuality = OIL_Scrn_MediumLPI;
      eGraphicsQuality = OIL_Scrn_MediumLPI;
      eTextQuality = OIL_Scrn_MediumLPI;
      break;
    case OIL_Scrn_Text:
      eImageQuality = OIL_Scrn_HighLPI;
      eGraphicsQuality = OIL_Scrn_HighLPI;
      eTextQuality = OIL_Scrn_HighLPI;
      break;
    case OIL_Scrn_Job:
    case OIL_Scrn_ORIPDefault:
      eImageQuality = g_pstCurrentJob->eImageScreenQuality;
      eGraphicsQuality = g_pstCurrentJob->eGraphicsScreenQuality;
      eTextQuality = g_pstCurrentJob->eTextScreenQuality;
      break;
    default:
      HQFAILV(("ebd_RemapHTTables: Unsupported ScreenMode %d", (int)eScreenMode));
      eImageQuality = OIL_Scrn_LowLPI;
      eGraphicsQuality = OIL_Scrn_MediumLPI;
      eTextQuality = OIL_Scrn_HighLPI;
      break;
    }
#ifdef SDK_SUPPORT_2BPP_EXT_EG
    if( nRasterDepth == 2 )
    {
      htm2bpp_remapHTtables( eImageQuality, eGraphicsQuality, eTextQuality );
    }
#endif
#ifdef SDK_SUPPORT_4BPP_EXT_EG
    if( nRasterDepth == 4 )
    {
      htm4bpp_remapHTtables( eImageQuality, eGraphicsQuality, eTextQuality );
    }
#endif
    break;
  default:
    /* No screening in 8bpp and 16bpp, just break. 1bpp switching example
       is implemented in configuration code. */
    break;
  }
#endif
}

/**
 * \brief Open a file on the specified device.
 *
 * All virtual files in this \c \%embedded\% device are always assumed to
 * be exclusive access. The same descriptor will be returned
 * for a specific filename regardless how many times it is opened.
 * This does mean that it is possible to maintain multiple position
 * pointers into the same file at the same time.
 * \param[in]   dev       A pointer to the device that holds the file.
 * \param[in]   filename  The name of the file to be opened.
 * \param[in]   openflags The flags to use when opening the file.  SW_CREAT
 *                        or SW_TRUNC may be specified to create an empty
 *                        virtual file; all other flags are ignored.
 * \return      Returns a file descriptor for the open file, or -1 if the
                file could not be opened.
 */
static DEVICE_FILEDESCRIPTOR RIPCALL ebd_open_file( DEVICELIST *dev ,
                                                    unsigned char *filename , int openflags )
{
  DEVICE_FILEDESCRIPTOR filedesc;

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: %s, %d\n", filename, openflags);

  if(strncmp((char*)filename, "/BackChannel", 12)==0)
  {
      filedesc = OIL_BackChannelOpen();
      if(filedesc < 0)
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: Failed to get backchannel, %s\n", filename);
          return ebd_ioerror(dev);
      }
      else
      {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0,
                   "ebd_open_file: File descriptor too large for mask.");
          return filedesc | EBD_FILEDESC_BKCHN;
      }
  }
#ifdef SDK_SUPPORT_1BPP_EXT_EG
  else if(strncmp((char*)filename, "/Screening/", 11)==0)
  {
      filedesc = ebd_scrn_open(filename + 10);
      if(filedesc < 0)
      {
          HQFAILV(("ebd_open_file: Failed to get screen table, %s", filename));
          return ebd_ioerror(dev);
      }
      else
      {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0, ("ebd_open_file: File descriptor too large for mask."));
          return filedesc | EBD_FILEDESC_SCRN;
      }
  }
#endif
  else if((strncmp((char*)filename, "PGB/", 4)==0) ||
          (strncmp((char*)filename, "/VirtFile/", 10)==0) )
  {
      filedesc = OIL_VirtFileOpen((char*)filename, openflags);

      if(filedesc < 0)
      {
        /* Only create the file if it doesn't already exist and is being opened for read */
        if(openflags & (SW_RDONLY | SW_RDWR)) {
          char sz[MAX_PCLCONFIG_BUF];
          Hq32x2 hq32zero = HQ32X2_INIT_ZERO;
          int nLength;
          int nResult;

          /* These special virtual files are accessed via the %embedded% ps device in \HqnEmbeddedSetup in SW\procsets\HqnEmbedded */
          if(strcmp((char*)filename, "/VirtFile/TrayInformation")==0) {
            filedesc = OIL_VirtFileOpen((char*)filename, SW_CREAT | SW_TRUNC);
            if(filedesc >= 0) {
              sz[0] = '\0';
              GetTrayInformation(sz);
              nLength = (int)strlen(sz);
              HQASSERT((sizeof(sz) > nLength),
                       "ebd_open_file: sz buffer overrun (TrayInformation).");
              nResult = OIL_VirtFileWrite(filedesc, (unsigned char*)sz, nLength);
              HQASSERT(nResult == nLength, ("ebd_open_file: failed to write (TrayInformation)."));
              nResult = OIL_VirtFileSeek(filedesc, &hq32zero, SW_SET);
              HQASSERT(nResult >= 0, ("ebd_open_file: failed to seek (TrayInformation)."));
            }
          } else if(strcmp((char*)filename, "/VirtFile/OutputInformation")==0) {
            filedesc = OIL_VirtFileOpen((char*)filename, SW_CREAT | SW_TRUNC);
            if(filedesc >= 0) {
              sz[0] = '\0';
              GetOutputInformation(sz);
              nLength = (int)strlen(sz);
              HQASSERT((sizeof(sz) > strlen(sz)),
                       "ebd_open_file: sz buffer overrun (OutputInformation).");
              nResult = OIL_VirtFileWrite(filedesc, (unsigned char*)sz, nLength);
              HQASSERT(nResult == nLength,
                       "ebd_open_file: failed to write (OutputInformation).");
              nResult = OIL_VirtFileSeek(filedesc, &hq32zero, SW_SET);
              HQASSERT(nResult >= 0,
                       "ebd_open_file: failed to seek (OutputInformation).");
            }
          } else if(strcmp((char*)filename, "/VirtFile/PCLMedia")==0) {
            filedesc = OIL_VirtFileOpen((char*)filename, SW_CREAT | SW_TRUNC);
            if(filedesc >= 0) {
              sz[0] = '\0';
              SetupPCLMedia(sz);
              nLength = (int)strlen(sz);
              HQASSERT((sizeof(sz) > strlen(sz)),
                       "ebd_open_file: sz buffer overrun (PCLMedia).");
              nResult = OIL_VirtFileWrite(filedesc, (unsigned char*)sz, nLength);
              HQASSERT(nResult == nLength,
                       "ebd_open_file: failed to write (PCLMedia).");
              nResult = OIL_VirtFileSeek(filedesc, &hq32zero, SW_SET);
              HQASSERT(nResult >= 0,
                       "ebd_open_file: failed to seek (PCLMedia).");
            }
          }
        }
      }

      if(filedesc < 0) {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: Failed to open virtual file, %s\n", filename);
          return ebd_ioerror(dev);
      } else {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0,
                   "ebd_open_file: File descriptor too large for mask.");
          return filedesc | EBD_FILEDESC_PGB;
      }
  }
  else if(strncmp((char*)filename, "/ImageInput", 10)==0)
  {
      filedesc = OIL_FileOpen(g_pstCurrentJob->szImageFile, "rb", &pPdfImgFileHandle);
      if(filedesc < 0)
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: Failed to get imagefile, %s\n", filename);
          return ebd_ioerror(dev);
      }
      else
      {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0, ("ebd_open_file: File descriptor too large for mask."));
          return filedesc | EBD_FILEDESC_PDFIMGINPUT;
      }
  }
  else if(strncmp((char*)filename, "Files/", 6)==0)
  {
      filedesc = OIL_VirtFileOpen((char*)filename, openflags);

      if(filedesc < 0)
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: Failed to open virtual file, %s\n", filename);
          return ebd_ioerror(dev);
      }
      else
      {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0, ("ebd_open_file: File descriptor too large for mask."));
          return filedesc | EBD_FILEDESC_BCFILE;
      }
  }
  else if((strncmp((char*)filename, "/PDFdirect", 10)==0) ||
          (strncmp((char*)filename, "/XPSdirect", 10)==0))
  {
      filedesc = OIL_FileOpen(g_pstCurrentJob->szJobFilename, "rb", &pPdfImgFileHandle);
      if(filedesc < 0)
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_open_file: Failed to get pdf file, %s\n", filename);
          return ebd_ioerror(dev);
      }
      else
      {
          HQASSERT((filedesc & ~EBD_FILEDESCMASK)==0, ("ebd_open_file: File descriptor too large for mask."));
          return filedesc | EBD_FILEDESC_PDFIMGINPUT;
      }
  }

  return ebd_undefined(dev);
}

/**
 * \brief Read from a file into a buffer.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file to read from.
 * \param[out]  buff          The buffer to read data into.
 * \param[in]   len           The length of the buffer; the maximum amount of data, in bytes, that can
 *                            be read in this call.
 * \return      Returns the number of bytes actually read to the buffer, or -1 if an error occurred.
 */
static int RIPCALL ebd_read_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor ,
                                    unsigned char *buff , int len )
{
  int nRead = 0;

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_read_file: 0x%08x, %d\n", descriptor, len);

  if(descriptor & EBD_FILEDESC_PGB)
  {
    nRead = OIL_VirtFileRead(descriptor & EBD_FILEDESCMASK, buff, len);
    if(nRead < 0)
    {
      HQFAILV(("ebd_read_file: Failed to read from pgb temp file %d", descriptor & EBD_FILEDESCMASK));
      return ebd_ioerror(dev);
    }
    return nRead;
  }
#ifdef SDK_SUPPORT_1BPP_EXT_EG
  else if(descriptor & EBD_FILEDESC_SCRN)
  {
      nRead = ebd_scrn_read(descriptor & EBD_FILEDESCMASK, buff, len);
      if(nRead <= 0)
      {
          HQFAIL("ebd_read_file: Failed to read from screen table");
          return ebd_ioerror(dev);
      }
      else
      {
          return nRead;
      }
  }
#endif
  else if(descriptor & EBD_FILEDESC_PDFIMGINPUT)
  {
    nRead = OIL_FileRead(buff, len, pPdfImgFileHandle);
    if(nRead < 0)
    {
      HQFAILV(("ebd_read_file: Failed to read from image file %d", descriptor & EBD_FILEDESC_PDFIMGINPUT));
      return ebd_ioerror(dev);
    }
    return nRead;
  }

  return ebd_undefined(dev);
}

/**
 * \brief Write from a buffer into a file.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file to write to.
 * \param[out]  buff          The buffer containing the data to be written.
 * \param[in]   len           The length of the buffer; the maximum amount of data, in bytes, that can
 *                            be written in this call.
 * \return      Returns the number of bytes actually written to the file, or -1 if an error occurred.
 */
static int RIPCALL ebd_write_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                     unsigned char *buff, int len)
{
  int bytes_written;

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_write_file: 0x%08x (%s), %d bytes\n", descriptor, DESC_TO_STR(descriptor), len);

  if(descriptor & EBD_FILEDESC_BKCHN)
  {
      bytes_written = OIL_BackChannelWrite(buff, len);
      if(bytes_written <= 0)
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_write_file: Failed to write to BackChannel\n");
          return ebd_ioerror(dev);
      }
      else
      {
          return bytes_written;
      }
  }
  else if(descriptor & EBD_FILEDESC_PGB)
  {
    int nWritten;

    nWritten = OIL_VirtFileWrite(descriptor & EBD_FILEDESCMASK, buff, len);
    if(nWritten < 0)
    {
      HQFAILV(("ebd_write_file: Failed to write to virtual file %d", descriptor & EBD_FILEDESCMASK));
      return ebd_ioerror(dev);
    }

    return nWritten;
  }
  else if(descriptor & EBD_FILEDESC_BCFILE)
  {
    int nWritten;

    nWritten = OIL_VirtFileWrite(descriptor & EBD_FILEDESCMASK, buff, len);
    if(nWritten < 0)
    {
      HQFAILV(("ebd_write_file: Failed to write to virtual file %d", descriptor & EBD_FILEDESCMASK));
      return ebd_ioerror(dev);
    }

    return nWritten;
  }

  return ebd_ioerror(dev);
}

/**
 * \brief Close a file.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file to be closed.
 * \return      Returns TRUE if the file is successfully closed, or -1 if an error occurred.
 */
static int RIPCALL ebd_close_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor )
{
  GG_SHOW(GG_SHOW_EBDDEV, "ebd_close_file: 0x%08x\n", descriptor);

  if(descriptor & EBD_FILEDESC_PGB)
  {
      if(OIL_VirtFileClose(descriptor & EBD_FILEDESCMASK))
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_close_file: Failed to close virtual file\n");
          return ebd_ioerror(dev);
      }
  }
#ifdef SDK_SUPPORT_1BPP_EXT_EG
  else if(descriptor & EBD_FILEDESC_SCRN)
  {
      if(ebd_scrn_close(descriptor & EBD_FILEDESCMASK))
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_close_file: Failed to close screen table\n");
          return ebd_ioerror(dev);
      }
  }
#endif
  else if(descriptor & EBD_FILEDESC_PDFIMGINPUT)
  {
      OIL_FileClose(pPdfImgFileHandle);
  }
  else if(descriptor & EBD_FILEDESC_BCFILE)
  {
      OIL_VirtFileSendToOutput(descriptor & EBD_FILEDESCMASK);

      if(OIL_VirtFileClose(descriptor & EBD_FILEDESCMASK))
      {
          GG_SHOW(GG_SHOW_EBDDEV, "ebd_close_file: Failed to close virtual file\n");
          return ebd_ioerror(dev);
      }
  }

  return TRUE;
}

/**
 * \brief Abort a file.  This device only supports aborting of temporary page buffer files.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file to be aborted.
 * \return      Returns zero if the file is successfully aborted, or -1 if an error occurred.
 */
static int RIPCALL ebd_abort_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor )
{
  GG_SHOW(GG_SHOW_EBDDEV, "ebd_abort_file: 0x%08x\n", descriptor);

  if(descriptor & EBD_FILEDESC_PGB)
  {
    return 0;
  }

  return ebd_ioerror(dev);
}

/**
 * \brief Seek through a file.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file.
 * \param[in]   destination   The distance to seek, in bytes, from the specified starting point.
 * \param[in]   whence        The starting point of the seek. See OIL_VirtFileSeek() for a
 *                            description of valid values.
 * \return Returns TRUE if the seek was successful, or FALSE if the seek failed.
 */
static int RIPCALL ebd_seek_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor,
                                    Hq32x2 *destination , int whence )
{
  GG_SHOW(GG_SHOW_EBDDEV, "ebd_seek_file: 0x%08x, %d, %d\n", descriptor, whence, destination->low);

  (void) ebd_noerror(dev) ; /* clear error flag first */
  if(descriptor & EBD_FILEDESC_PGB)
  {
    int nResult;
    nResult = OIL_VirtFileSeek(descriptor & EBD_FILEDESCMASK, destination, whence);
    if(nResult < 0)
    {
      HQFAILV(("ebd_seek_file: Failed OIL_VirtFileSeek %d", descriptor & EBD_FILEDESCMASK));
      ebd_set_last_error(dev, DeviceIOError);
      return FALSE ;
    }
  }
#ifdef SDK_SUPPORT_1BPP_EXT_EG
  else if(descriptor & EBD_FILEDESC_SCRN)
  {
      if(!ebd_scrn_seek(descriptor & EBD_FILEDESCMASK, destination, whence))
      {
          HQFAIL("ebd_seek_file: Failed to seek screen table");
          ebd_set_last_error(dev, DeviceIOError);
          return FALSE ;
      }
  }
#endif
  else if(descriptor & EBD_FILEDESC_PDFIMGINPUT)
  {
    int nResult;

    /* \todo Handle 64 bit destination value... or not?*/
    HQASSERT(destination->high == 0, ("ebd_seek_file: 64bit seeks not supported in image input"));

    switch( whence )
    {
    case SW_SET:
      nResult = OIL_FileSeek(pPdfImgFileHandle, (long *)&destination->low, SEEK_SET);
      break ;
    case SW_INCR:
      nResult = OIL_FileSeek(pPdfImgFileHandle, (long *)&destination->low, SEEK_CUR);
      break ;
    case SW_XTND:
      nResult = OIL_FileSeek(pPdfImgFileHandle, (long *)&destination->low, SEEK_END);
      break ;
    default:
      HQFAILV(("ebd_seek_file: image input: Unknown seek method %d", whence));
      return FALSE ;
      break;
    }
    if(nResult < 0)
    {
      HQFAIL("ebd_seek_file: image input: Failed to seek");
      ebd_set_last_error(dev, DeviceIOError);
      return FALSE ;
    }
  }

  return TRUE ;
}

/**
 * \brief Get either the total length or remaining bytes available in the file.
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   descriptor    The descriptor of the file.
 * \param[out]  bytes         The distance to seek, in bytes, from the specified starting point.
 * \param[in]   reason        Indicates whether the function is to retrieve the total length of
 *                            the file or the number of available bytes in the file.  Valid
 *                            values are:
 * \arg       SW_BYTES_AVAIL_REL   Retrieve the remaining available bytes in the file.
 * \arg       SW_BYTES_TOTAL_ABS   Retrieve the total length of the file
 *
 * \return Returns TRUE if the enquiry was successful, or FALSE if it failed.
 */
static int RIPCALL ebd_bytes_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor,
                                     Hq32x2 *bytes, int reason )
{
  int nResult = 0;

  if(descriptor & EBD_FILEDESC_PGB)
  {
    nResult = OIL_VirtFileBytes(descriptor & EBD_FILEDESCMASK, bytes, reason);
    if(nResult < 0)
    {
      HQFAILV(("ebd_bytes_file: Failed OIL_VirtFileBytes %d", descriptor & EBD_FILEDESCMASK));
      if(nResult < 0)
      {
        ebd_set_last_error(dev, DeviceIOError);
        return FALSE ;
      }
    }
  }
  else if(descriptor & EBD_FILEDESC_PDFIMGINPUT)
  {
    if( reason == SW_BYTES_AVAIL_REL )
    {
      /* We indicate that bytes are immediately available, but not
         how many by setting pHQ32x2 to 0. */
      bytes->high = 0;
      bytes->low = 0;

      return TRUE;
    }
    else if( reason == SW_BYTES_TOTAL_ABS )
    {
      bytes->high = 0;
      bytes->low = OIL_FileBytes(pPdfImgFileHandle);
      return TRUE;
    }
    return FALSE;
  }

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_bytes_file: 0x%08x, %d, result=%d, length=%d\n", descriptor, reason, nResult, bytes->low);

  return TRUE;
}

/**
 * \brief Get the status of a file.
 *
 * \param[in]   dev           A pointer to the device that holds the file.
 * \param[in]   filename      The name of the file.
 * \param[out]  statbuff      Pointer to a STAT structure to receive status.
 * \return      Returns zero if the data is successfully retrived, or -1 if an error occurred.
*/
static int RIPCALL ebd_status_file( DEVICELIST *dev,
                                    unsigned char *filename,
                                    STAT *statbuff )
{
  int nResult;

  (void) ebd_noerror(dev) ; /* clear error flag first */

  if(strncmp("PGB/", (char*)filename, 4)==0)
  {
    nResult = OIL_VirtFileStatus((char*)filename, statbuff);
    if(nResult < 0)
    {
      GG_SHOW(GG_SHOW_EBDDEV, "ebd_status_file: %s, doesn't exist\n", filename);
      return ebd_ioerror(dev);
    }
  }

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_status_file: %s, %d\n", filename, statbuff?statbuff->bytes.low:-1);

  return 0;
}

/**
 * \brief Not implemented for this device type.
 * \param[in]   dev           Unused parameter.
 * \param[in]   pattern       Unused parameter.
 * \return      Returns NULL.
 */
static void* RIPCALL ebd_start_file_list ( DEVICELIST *dev ,
                                           unsigned char *pattern)
{
  UNUSED_PARAM(unsigned char *, pattern);

  return ebd_void(dev);
}

/**
 * \brief Not implemented for this device type.
 * \param[in]   dev           Unused parameter.
 * \param[in]   handle        Unused parameter.
 * \param[in]   pattern       Unused parameter.
 * \param[in]   entry         Unused parameter.
 * \return      Returns zero.
 */
static int RIPCALL ebd_next_file( DEVICELIST *dev ,
                                    void **handle ,
                                    unsigned char *pattern,
                                    FILEENTRY *entry)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(unsigned char *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return FileNameNoMatch;
}

/**
 * \brief Not implemented for this device type.
 * \param[in]   dev           Unused parameter.
 * \param[in]   handle        Unused parameter.
 * \return      Returns 0.
 */
static int RIPCALL ebd_end_file_list ( DEVICELIST *dev ,
                                         void * handle)
{
  UNUSED_PARAM(void *, handle);

  return ebd_noerror(dev);
}

/**
 * \brief Not implemented for this device type.
 * \param[in]   dev           Unused parameter.
 * \param[in]   file1         Unused parameter.
 * \param[in]   file2         Unused parameter.
 * \return      Returns -1.
 */
static int RIPCALL ebd_rename_file( DEVICELIST *dev,
                                      unsigned char *file1 ,
                                      unsigned char *file2)
{
  UNUSED_PARAM(unsigned char *, file1);
  UNUSED_PARAM(unsigned char *, file2);

  return ebd_ioerror(dev);
}

/**
 * \brief Delete a file.
 *
 * Action is only taken for temporary pagebuffer files. The temopary
 * pagebuffer files are stored in the virtual sub-folder "PGB".
 *
 * \param[in]   dev       A pointer to the device that holds the file.
 * \param[in]   filename  The name of the file to be deleted.
 * \return      Returns zero if the file was successfully deleted, or
 *              -1 if an error occurred.
 */
static int RIPCALL ebd_delete_file( DEVICELIST *dev ,
                                    unsigned char *filename )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  GG_SHOW(GG_SHOW_EBDDEV, "ebd_delete_file: %s\n", filename);

  if((strncmp((char*)filename, "PGB/", 4)==0) ||
     (strncmp((char*)filename, "/VirtFile/", 10)==0))
  {
    return OIL_VirtFileDelete((char*)filename);
  }

  return 0;
}

/**
 * \brief Not implemented for this device type.
 * \param[in]   dev           Unused parameter.
 * \param[in]   devstat       Unused parameter.
 * \return      Returns -1.
 */
static int RIPCALL ebd_status_device ( DEVICELIST *dev , DEVSTAT *devstat )
{
  UNUSED_PARAM(DEVSTAT *, devstat);

  return ebd_ioerror(dev);
}

/**
 * \brief Not implemented for this device type.
 * \return      Returns zero.
 */
static int RIPCALL ebd_spare (void)
{
  return 0;
}

/* ----------------------------------------------------------------------
 *
 * \brief Set the last error status.
 * \param[in]   dev           A pointer to the device.
 * \param[in]   nError        The error code to be set.
 */

static void RIPCALL ebd_set_last_error( DEVICELIST *dev, int nError )
{
  OIL_Ty_EBDDeviceState* pDeviceState = (OIL_Ty_EBDDeviceState*) dev->private_data;
  pDeviceState->last_error = nError;
}

/**
 * \brief Retrieve the last error code.
 * \param[in]   dev           A pointer to the device.
 * \return      Returns the last error code stored in the device.
 */
static int RIPCALL ebd_get_last_error( DEVICELIST *dev )
{
  OIL_Ty_EBDDeviceState* pDeviceState = (OIL_Ty_EBDDeviceState*) dev->private_data;
  return pDeviceState->last_error;
}

/* ----------------------------------------------------------------------
   Stub functions for those with no other purpose
*/

/**
 * \brief Set the last error to DeviceIOError.
 * \param[in]   dev    A pointer to the device.
 * \return This function always returns -1.
 */
static int RIPCALL ebd_ioerror( DEVICELIST *dev )
{
  ebd_set_last_error( dev, DeviceIOError );
  return -1;
}

/**
 * \brief Set the last error to DeviceUndefined.
 * \param[in]   dev    A pointer to the device.
 * \return This function always returns -1.
 */
static int RIPCALL ebd_undefined( DEVICELIST *dev )
{
  ebd_set_last_error( dev, DeviceUndefined );
  return -1;
}

/**
 * \brief Clear the last error setting (no error).
 * \param[in]   dev    A pointer to the device.
 * \return This function always returns zero.
 */
static int RIPCALL ebd_noerror( DEVICELIST *dev )
{
  ebd_set_last_error( dev, DeviceNoError );
  return 0;
}

/**
 * \brief The void routine for the embedded device type.
 * \param[in]   dev    A pointer to the device.
 * \return This function always returns NULL.
 */
static void* RIPCALL ebd_void( DEVICELIST *dev )
{
  ebd_set_last_error( dev, DeviceNoError );
  return NULL;
}

