/* Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This file contains definitions of various structures, constants and 
 *   other data types used by the OIL.
 *
 */

#ifndef _OIL_H_
#define _OIL_H_

/* includes needed to enable timing features */
#include "oil_platform.h"   /* platform specific timing support */
#include "oil_timing.h"
#include <stddef.h>
#include <stdio.h> /* for printf in debug messages */

/* Macro definition of TRUE
 */
#ifndef TRUE
#define TRUE 1
#endif

/* Macro definition of FALSE
 */
#ifndef FALSE
#define FALSE 0
#endif

/* Macro definition of NULL
 */
#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef UNUSED_PARAM
#define UNUSED_PARAM(_type_, _param_)  ((void)_param_)
#endif

/* oil-wide defines */
#define OIL_READBUFFER_LEN          16 * 1024      /* may need to change later */
#define LINEBUFF_LEN                1024

#define OIL_BAND_LIMIT              512
#define OIL_MAX_HOSTNAME_LENGTH     100
#define OIL_MAX_USERNAME_LENGTH     100
#define OIL_MAX_JOBNAME_LENGTH      256
#define OIL_MAX_IMAGEFILENAME_LENGTH  256
#define OIL_MEDIA_STRINGS           100
#define OIL_MAX_PLANES_COUNT        4
#define OIL_MAX_VALID_PLANES_COUNT(x)   ((x==OIL_ColorantFamily_RGB)? 3:4)
#define OIL_MAX_MEDIATYPE_LENGTH     32
#define OIL_MAX_MEDIACOLOR_LENGTH    32
#define OIL_MAX_COMMENTPARSER_LENGTH 50
#define OIL_MAX_ERRORMESSAGE_LENGTH  100
#ifdef USE_PJL
#define MAX_PJL_JOBNAME_LEN          80
#endif

#ifndef VXWORKS
typedef int BOOL;
#endif

/* \brief This flag will enable informative messages relating to memory */
#define GG_SHOW_MEMORY    (1<<0)

/* \brief This flag will enable informative messages relating to the color management module */
#define GG_SHOW_CMM       (1<<1)

#define GG_SHOW_SCREENING (1<<2)
#define GG_SHOW_FONTS     (1<<3)
#define GG_SHOW_OIL       (1<<4)
#define GG_SHOW_EBDDEV    (1<<5)
#define GG_SHOW_MEDIA     (1<<6)
#define GG_SHOW_THREADING (1<<7)
#define GG_SHOW_PSCONFIG  (1<<8)
#define GG_SHOW_TEST      (1<<9)
#define GG_SHOW_1BPP      (1<<10)
#define GG_SHOW_TIMING    (1<<11)
#define GG_SHOW_OILVER    (1<<12)
#define GG_SHOW_COMMENTPARSER   (1<<13)
#define GG_SHOW_PJL       (1<<14)
#define GG_SHOW_VIRTFILE  (1<<15)
#define GG_SHOW_CHECKSUM  (1<<16)
#define GG_SHOW_JOBCFG    (1<<17)

/*! \brief Output a message.
 *
 * This macro accepts a flag, a string message and an optional set of parameters to format the string. 
 * The message is only output if the expression @c g_uGGShow @c & @c  _flag_  evaluates to true.
 * The string uses the same formatting commands as the standard @c printf() function.
 *
 */
#define GG_SHOW(_flag_, _x_, ...) { if(g_ConfigurableFeatures.g_uGGShow & _flag_) oil_printf (_x_, ## __VA_ARGS__ ); }
void oil_printf(char *str,...);


typedef unsigned char * RIP_Memory;

/*! \brief An enumeration defining the various states that can apply to the system.
*/
enum {
    OIL_Sys_Uninitialised,                  /*!< Power on but system completely uninitialized */
    OIL_Sys_Inactive,                       /*!< Essential initialization complete */
    OIL_Sys_Active,                         /*!< RIP initialized */
    OIL_Sys_JobActive,                      /*!< RIP working - job active */
    OIL_Sys_JobCancel,                      /*!< RIP working - job cancelling */
    OIL_Sys_Suspended,                      /*!< RIP suspended */
};
typedef int OIL_eTySystemState;

/*! \brief An enumeration defining the various raster states that the system can be in.
*/
enum {
    OIL_Ras_ProcessNewPage,                 /*!< RasterState to process new page */
    OIL_Ras_ProcessNewSeparation,           /*!< RasterState to process new separation */
    OIL_Ras_ProcessBands,                   /*!< RasterState to process bands */
    OIL_Ras_ProcessEndOfBands,              /*!< RasterState to process end of bands */
};
typedef int OIL_eTyRasterState;

/*! \brief An enumeration defining the various states that the job can be in.
*/
enum {
    OIL_Job_StreamStart,
    OIL_Job_StreamDone,
    OIL_Job_RipDone,
    OIL_Job_Abort,
};
typedef int OIL_eTyJobState;

/*! \brief An enumeration defining the colorant families supported by the OIL.
*/
enum {
    OIL_ColorantFamily_CMYK = 1,              /*!< CMYK */
    OIL_ColorantFamily_RGB,                   /*!< RGB */
    OIL_ColorantFamily_Gray,                  /*!< Monochrome */
    OIL_ColorantFamily_Unsupported,           /*!< Unsupported colorant family */
};
typedef int OIL_eTyColorantFamily;                      

/*! \brief An enumeration defining the PDLs supported by the RIP.
*/
enum {
    OIL_PDL_Unknown,                      /*!< PDL type undetermined */
    OIL_PDL_PS,                           /*!< PostScript language*/
    OIL_PDL_XPS,                          /*!< XPS */
    OIL_PDL_PDF,                          /*!< PDF */
    OIL_PDL_PCL5c,                        /*!< PCL5c */
    OIL_PDL_PCL5e,                        /*!< PCL5e */
    OIL_PDL_PCLXL,                        /*!< PCL-XL */
    OIL_IMG,                              /*!< TIFF/JPG input */
};
typedef int OIL_eTyPDLType;

/*! \brief Enumeration of test pages
 */
enum {
  OIL_TESTPAGE_NONE = 0,
  OIL_TESTPAGE_CFG,
  OIL_TESTPAGE_PS,
  OIL_TESTPAGE_PCL
};
typedef int OIL_eTestPage;

/*! \brief An enumeration defining shutdown modes supported by the OIL.
*/
enum {
    OIL_RIPShutdownPartial,               /*!< for shutting RIP down at the last job (multithreaded only) */
    OIL_RIPShutdownTotal,                 /*!< for shutting RIP down at the end of every job */
};
typedef int OIL_eShutdownMode;

/*! \brief An enumeration defining the colorants recognised by the OIL.
*/
enum {
    OIL_InvalidColor = -1,                /*!< Invalid Color */
    OIL_Cyan,                             /*!< Cyan */
    OIL_Magenta,                          /*!< Magenta */
    OIL_Yellow,                           /*!< Yellow */
    OIL_Black,                            /*!< Black */
    OIL_Red,                              /*!< Red */
    OIL_Green,                            /*!< Green */
    OIL_Blue,                             /*!< Blue */
};
typedef int OIL_eColorant;

#define CMYK_OFFSET 0
#define RGB_OFFSET 4
#define GRAY_OFFSET 0

/*! \brief An enumeration defining the color modes supported in the OIL for jobs.
*/
enum {
    OIL_Mono = 1,                         /*!< Monochrome job */
    OIL_CMYK_Separations,                 /*!< CMYK Color separation job */
    OIL_CMYK_Composite,                   /*!< CMYK Color composite job */
    OIL_RGB_Separations,                  /*!< RGB Color separation job */
    OIL_RGB_Composite,                    /*!< RGB Color composite job */
    OIL_RGB_PixelInterleaved,             /*!< RGB Color pixel interleaved job */
};
typedef int OIL_eColorMode;

/*! \brief An enumeration defining the screen modes supported by the OIL.
*/
enum {
    OIL_Scrn_Auto = 0,                    /*!< Auto screen mode*/
    OIL_Scrn_Photo,                       /*!< Photo screen mode */
    OIL_Scrn_Graphics,                    /*!< Graphics screen mode */
    OIL_Scrn_Text,                        /*!< Text screen mode */
    OIL_Scrn_ORIPDefault,                 /*!< ORIP default screen mode */
    OIL_Scrn_Job,                         /*!< Job settings screen mode */
    OIL_Scrn_Module,                      /*!< Screening module screen mode */
};
typedef int OIL_eScreenMode;

/*! \brief An enumeration defining the screen qualities recognised by the OIL.
*/
enum {
    OIL_Scrn_LowLPI = 0,                  /*!< Low res screen */
    OIL_Scrn_MediumLPI,                   /*!< Medium res screen */
    OIL_Scrn_HighLPI,                     /*!< High res screen */
};
typedef int OIL_eScreenQuality;

/*! \brief An enumeration defining constants used to denote either an enabled
* or a disabled feature in the OIL.
*/
enum {
    OIL_Enabled,                         /*!< for a enabled feature */
    OIL_Disabled,                        /*!< for a disabled feature */
};
typedef int OIL_eFeatureState;

/*! \brief An enumeration defining the paper selection modes used by the OIL.
*/
enum {
    OIL_PaperSelNone,                    /*!< no media selection */
    OIL_PaperSelRIP,                     /*!< for RIP defined paper selection */
    OIL_PaperSelPMS,                     /*!< for PMS defined paper selection */
};
typedef int OIL_ePaperSelectMode;

/*! \brief An enumeration defining the printable region modes used by the OIL.
*/
enum {
    OIL_DefaultPrintableArea,            /*!< Let the job or PMS decide. */
    OIL_RIPRemovesUnprintableArea,       /*!< The RIP removes the unprintable pixels */
    OIL_RIPSuppliesUnprintableArea,      /*!< The RIP will supply white (or black) pixels */
    /* Other methods could be implemented, such as OIL provides padded area, or 
       OIL does clipping, or PMS does the work. It will however, be generally most efficient
       for the RIP to take care of it. */
};
typedef int OIL_ePrintableMode;

/*! \brief A structure representing the current system state.
 * 
*/
typedef struct tySystem{
    OIL_eTySystemState   eCurrentState;     /*!< Current state of system */
    int                  bJobCancelReq;     /*!< True if job cancellation has been requested */
    void                 *pUserData;        /*!< Available for implementation data */
    RIP_Memory           pRIPMemory;        /*!< Pointer to memory used by RIP */
    size_t               cbmaxAddressSpace; /*!< The size of the virtual address space allocated for RIP usage */
    size_t               cbRIPMemory;       /*!< The size of the commitable memory allocated for RIP usage */
    unsigned int         nOILconfig ;       /*!< custom configuration for OIL - 0=none */
    unsigned int         nOutputType ;      /*!< raster output type  */
} OIL_TySystem;

/*! \brief A structure representing a band. 
*/
typedef struct tyBand{
    unsigned int    uBandNumber;          /*!< Band number within page */
    unsigned int    uBandHeight;          /*!< Height of band in pixels */
    unsigned int    cbBandSize;           /*!< Size of raster in bytes */
    unsigned char   *pBandRaster;         /*!< Uncompressed Band data */
} OIL_TyBand;                             /*!< OIL Band Structure */

/*! \brief A structure representing a plane. 
*/
typedef struct tyPlane{
    OIL_eColorant   ePlaneColorant;         /*!< C=0, M=1, Y=2, K=3 Invalid = -1 */
    unsigned int    uBandTotal;             /*!< Total number of bands in the page */
    OIL_TyBand      atBand[OIL_BAND_LIMIT]; /*!< Pointer to band data */
    unsigned char   bBlankPlane;            /*!< True if plane has no data OR blank data */
} OIL_TyPlane;

/*! \brief A structure representing various media attributes.
 *
 * Media dimensions, including unprintable areas, are stored in this structure, along
 * with the selected input and output trays for the medium.
*/
typedef struct tyMedia{
    unsigned int    uInputTray;          /*!< Input tray */
    unsigned int    uOutputTray;         /*!< Output tray */
    unsigned char   szMediaType[OIL_MAX_MEDIATYPE_LENGTH];     /**< Media type string */
    unsigned char   szMediaColor[OIL_MAX_MEDIACOLOR_LENGTH];   /**< Media color string */
    unsigned int    uMediaWeight;        /**< Media Weight */
    double          dWidth;              /**< Media width in ps points */
    double          dHeight;             /**< Media height in ps points */
    void            *pUser;              /**< User data, typically PMS media reference */
} OIL_TyMedia;


/*! \brief A structure representing a page.
 *
 * This structure contains a pointer to the array of planes, represented
 * by the @c OIL_TyPlane type, which make up the page. It also contains 
 * page dimensions, resolution, color depth, colorant and status information
 * along with settings for various features such as duplex printing.
*/
typedef struct tyPage{
    void            *ptPMSpage ;         /*!< Pointer to the equivalent PMS Page structure */
    unsigned int    uPageNo;             /*!< Page number */
    struct TyPage   *pNext;              /*!< Pointer to next page */
    int             nPageWidthPixels;    /*!< Page width in pixels (user page width not inc padding) */
    int             nPageHeightPixels;   /*!< Page height in pixels */
    int             nRasterWidthData;    /*!< Number of bits (padded) in 1 scanline in the raster */
    double          dXRes;               /*!< X Resolution */
    double          dYRes;               /*!< Y Resolution */
    unsigned int    uRIPDepth;           /*!< Bits per pixel for ripping */
    unsigned int    uOutputDepth;        /*!< Bits per pixel for output */
    OIL_eTyColorantFamily eColorantFamily; /*!< Colorant Family (1-cmyk, 2-rgb, 3-gray, 4-unsupported) */
    int             nColorants;          /*!< Number of colorants */
    OIL_TyPlane     atPlane[OIL_MAX_PLANES_COUNT]; /*!< The array of the planes that make up the page */
    void *ptBandPacket; /*!< pointer to band packet. used only in push band delivery model*/
    unsigned int    uCopies;             /*!< Number of copies for the page - typically inherited from job */
    OIL_TyMedia     stMedia;             /*!< Media attributes like Mediatype, MediaColor etc */
    int             nTopMargin;          /*!< Top margin (pixels) */
    int             nBottomMargin;       /*!< Bottom margin (pixels) */
    int             nLeftMargin;         /*!< Left margin (pixels) */
    int             nRightMargin;        /*!< Right margin (pixels) */
    int             bIsRLE;              /*!< True if raster is run-length encoded? */
    BOOL            bDuplex;             /*!< @c true = duplex, @c false = simplex */
    BOOL            bTumble;             /*!< @c true = tumble, @c false = no tumble */
    BOOL            bCollate;            /*!< @c true = collate, @c false = no collate */
    int             uOrientation;        /*!< Orientation of image on page */
    BOOL            bFaceUp;             /*!< Output Order @c true = faceup, @c false=facedown */
    unsigned char   nBlankPage;          /*!< 0=not blank, 1=blank in job, 2=blank created by OIL to resolve duplex/media issues */
    unsigned int    bPageComplete;       /*!< Flag to indicate if page is completely rendered. */
    struct tyJob    *pstJob;             /*!< Pointer to job details */
} OIL_TyPage;

/*! \brief Structure for error handling.
*/
typedef struct{
int Code;   /**< Error code, 0 no error, 1 message detected, 2 message printing */
char szData[OIL_MAX_ERRORMESSAGE_LENGTH + 1];/**< Captured Error Message */
BOOL   bErrorPageComplete;  /**< Error Page printed */
}OIL_TyError;

/*! \brief A structure representing a job.
 *
 * This structure contains a pointer to a list of pages in the job, represented
 * by the @c OIL_TyPage type. It also contains fields storing a wide range of job
 * attributes, including both feature settings and status fields.
*/
typedef struct tyJob
{
    void            *pJobPMSData;        /*!< Pointer to PMS job structure */
    unsigned int    uJobId;              /*!< Job ID number */
    OIL_eTyJobState eJobStatus;          /*!< Job status */
    char szHostName[OIL_MAX_HOSTNAME_LENGTH]; /*!< Printer hostname */
    char szUserName[OIL_MAX_USERNAME_LENGTH]; /*!< Job user name */
    char szJobName[OIL_MAX_JOBNAME_LENGTH];   /*!< Job name */
    OIL_eTyPDLType  ePDLType;            /*!< PDL to use for job, PS, XPS etc */
    BOOL            bPDLInitialised;     /*!< Set to @c true when PDL init sequence sent to RIP */
    unsigned int    uCopyCount;          /*!< Total copies to print */
    unsigned int    uPagesInOIL ;        /*!< Total number of pages currently held in OIL */
    unsigned int    uPagesParsed;        /*!< Total number of pages ripped */
    unsigned int    uPagesToPrint;       /*!< Total number of pages submitted for output */
    unsigned int    uPagesPrinted;       /*!< Total number of physical pages output */
    unsigned int    uXResolution;         /*!< Horizontal Resolution */
    unsigned int    uYResolution;         /*!< Vertical Resolution */
    unsigned int    uOrientation;        /*!< Orientation, portrait or landscape */
    OIL_TyMedia     tCurrentJobMedia;    /*!< Current media settings, initially PMS default page */
    BOOL            bAutoA4Letter;       /*!< Automatic A4/letter switching */
    double          dUserPaperWidth;     /*!< User defined paper width */
    double          dUserPaperHeight;    /*!< User defined paper height */
    unsigned char   bManualFeed;         /**< true = Manual feed */
    unsigned int    uPunchType;          /*!< Punch type, single double etc */
    unsigned int    uStapleType;         /*!< Staple operation, 1 hole, 2 hole, centre etc */
    BOOL            bDuplex;             /*!< true = duplex, false = simplex */
    BOOL            bTumble;             /*!< true = tumble, false = no tumble */
    BOOL            bCollate;            /*!< true = collate, false = no collate */
    BOOL            bReversePageOrder;   /*!< Reverse page order */
    unsigned int    uBookletType;        /*!< Booklet binding, left, right */
    unsigned int    uOhpMode;            /*!< OHP interleaving mode */
    unsigned int    uOhpType;            /*!< OHP interleaving media type */
    unsigned int    uOhpInTray;          /*!< OHP interleaving feed tray */
    unsigned int    uCollatedCount;      /*!< Total collated copies in a job */
    OIL_eColorMode  eColorMode;          /*!< Default color (1-mono, 2-CMYKsep, 3-CMYKcomp, 4-RGBsep, 5-RGBcomp) */
    BOOL            bForceMonoIfCMYblank;/*!< true = force monochrome if CMY absent, false = output all planes even if blank*/
    unsigned int    uRIPDepth;           /*!< Default bit depth used by rip */
    unsigned int    bOutputDepthMatchesRIP; /*!< Set true if bit depth conversion may be required, otherwise no conversion */
    unsigned int    uOutputDepth;        /*!< Default bit depth for final output */
    OIL_eScreenMode    eScreenMode;      /*!< Screening type auto, photo, text etc */
    OIL_eScreenQuality eImageScreenQuality;     /*!< Screen quality from job for images */
    OIL_eScreenQuality eGraphicsScreenQuality;  /*!< Screen quality from job for graphics */
    OIL_eScreenQuality eTextScreenQuality;      /*!< Screen quality from job for images */
    BOOL            bSuppressBlank;      /*!< true = suppress blank pages, false = let blank pages pass through */
    BOOL            bPureBlackText;      /*!< true = Pure Black Text enabled */
    BOOL            bAllTextBlack;       /*!< true = All Text Black Enabled */
    BOOL            bBlackSubstitute;    /*!< true = Black Substitute enabled */
    unsigned int    uFontNumber;         /*!< PCL font number */
    char            szFontSource[4];     /*!< PCL font source */
    unsigned int    uLineTermination;    /*!< PCL line termination */
    double          dPitch;              /*!< Pitch of the default PCL font in characters per inch */
    double          dPointSize;          /*!< Height of the default PCL font in points */
    unsigned int    uSymbolSet;          /*!< PCL symbol set */
    unsigned int    uPCL5PaperSize;      /*!< PCL5 paper size */
    unsigned int    uPCLXLPaperSize;     /*!< PCLXL paper size */
    unsigned int    uJobOffset;          /*!< Job offset */
    BOOL            bCourierDark;        /*!< PCL Courier set */
    BOOL            bWideA4;             /**< Wide A4 */
    double          dVMI;                /*!< PCL vertical motion index */
    BOOL            bInputIsImage;       /**< Image Input FALSE=no, TRUE=yes */
    char            szImageFile[OIL_MAX_IMAGEFILENAME_LENGTH];   /**< Image File Path. */
    OIL_eTestPage   eTestPage;           /**< Selected Test Page to print */
    BOOL            bTestPagesComplete;  /**< All Test Pages  printed */
    unsigned int    uPrintErrorPage;     /**< Print error page selected  0 or 1*/
    unsigned char   bFileInput;          /**< File Input FALSE=no, TRUE=yes */
    char            szJobFilename[OIL_MAX_JOBNAME_LENGTH]; /**< Job file name */
    char            szPDFPassword[32];   /*!< PDF File Password */
#ifdef USE_PJL
    unsigned int    uJobStart;           /*!< Value of START parameter to JOB command, or 0 if none */
    unsigned int    uJobEnd;             /*!< Value of END parameter to JOB command, or 0 if none */
    BOOL            bEojReceived;        /*!< true = EOJ command received */
    char szEojJobName[MAX_PJL_JOBNAME_LEN+1];  /*!< Value of NAME parameter to EOJ command */
#endif
    struct tyPage   *pPage;           /*!< Pointer to a list of pages in the job */
} OIL_TyJob;

/*! \brief Band delivery methods.
*/
enum {
    OIL_PUSH_PAGE,        /**< OIL should push page into PMS */
    OIL_PULL_BAND,        /**< PMS will Pull Bands from OIL */
    OIL_PUSH_BAND,        /**< OIL should push bands(on the fly) into PMS */
    OIL_PUSH_BAND_DIRECT_SINGLE, /**< The RIP should render directly into memory provided by the PMS, one band at a time. */
    OIL_PUSH_BAND_DIRECT_FRAME,  /**< The RIP should render directly into memory provided by the PMS, which is part of a framebuffer. The framebuffer can also be used to store the results of partial paints, which has large performance advantages. */
};
typedef int OIL_eBandDeliveryType;

/*! \brief Maximum length of Probe argument string */
#define MAX_PROBE_ARG_LEN 128

/*! \brief Configurable features.
 *
 *   These features can be configured at runtime by calling the @c GG_SetTestConfiguration()
 *   procedure with appropriate values.
*/
typedef struct {
  OIL_ePaperSelectMode g_ePaperSelectMode;     /*!< Specifies the paper selection method to use */
  OIL_ePrintableMode ePrintableMode;           /*!< Specifies the paper selection method to use */
  OIL_eShutdownMode g_eShutdownMode;           /*!< Specifies the shutdown mode */
  /* initialise GG_SHOW control param to output no messages */
  /* To turn on all messages - g_uGGShow = (unsigned int)-1 */
  unsigned int   g_uGGShow;                    /*!< Specifies the level of debugging msgs to output */
  BOOL  bGenoaCompliance;                    /*!< Specifies whether GenoaCompliance settings are enabled or disabled */
  float fEbdTrapping ;                         /*!< Specifies the width of traps to be applied - a value of 0 means do no trap */
  unsigned int uColorManagement;               /*!< Specifies the color management method */
  char szProbeTrace[MAX_PROBE_ARG_LEN] ;       /*!< Specifies the probe trace parameter(s) */
  OIL_eBandDeliveryType eBandDeliveryType;   /*!< specifies raster delivery mode */
  int nRendererThreads;                        /*!< Specifies the number of RIP renderer threads */
  int nStrideBoundaryBytes;                    /*!< Allows stride boundary to be configurable which make testing easier */
  int bSWinRAM;                                /*!< SW in RAM build. Set at compilation time, i.e. not configurable at runtime */
  int bRetainedRaster;                         /*!< Retained Raster RIP feature */
  int nDefaultColorMode;                       /*!< Default color mode, not the actual color mode */
  BOOL bScalableConsumption;                   /*!< Specifies whether Scalable Consumption RIP feature is enabled or disabled */
  BOOL bImageDecimation;                       /*!< Specifies whether Image Decimation RIP feature is enabled or disabled */
  BOOL bUseUFST5;                              /*!< Specifies whether the UFST5 pfin module is used */
  BOOL bUseUFST7;                              /*!< Specifies whether the UFST7 pfin module is used */
  BOOL bUseFF;                                 /*!< Specifies whether the FF pfin module is used */
  BOOL bPixelInterleaving;                     /*!< Specifies whether the RIP should use Pixel interleaving, false = band */
  BOOL bRasterByteSWap;                        /*!< Specifies whether the RIP should byte swap the raster ooutput for little endian cpu*/
}OIL_TyConfigurableFeatures;

/*! \brief Comment Parser Structure 
 * 
*/
typedef struct tyCommentParser{
    char   szJobCreator[OIL_MAX_COMMENTPARSER_LENGTH];            /*!< %%Creator */
    char   szJobCreationDate[OIL_MAX_COMMENTPARSER_LENGTH];       /*!< %%CreationDate */
    char   szJobTitle[OIL_MAX_COMMENTPARSER_LENGTH];              /*!< %%CreationDate */
    char   szJobOrientation[OIL_MAX_COMMENTPARSER_LENGTH];        /*!< %%Orientation */
    char   szJobViewingOrientation[OIL_MAX_COMMENTPARSER_LENGTH]; /*!< %%ViewingOrientation */
    char   szJobFor[OIL_MAX_COMMENTPARSER_LENGTH];                /*!< %%For */
    char   szJobBoundingBox[OIL_MAX_COMMENTPARSER_LENGTH];        /*!< %%BoundingBox */
    char   szJobPageBoundingBox[OIL_MAX_COMMENTPARSER_LENGTH];    /*!< %%PageBoundingBox */
} OIL_TyCommentParser;

/*! \brief Supported RIP Features Structure
 *
*/
typedef struct tyRIPFeatures{
    int    bTrapping;                                            /*!< Trapping supported */
    int    bCoreTrace;                                           /*!< coretrace supported */
    int    bFontEmul;                                            /*!< Font Emulation supported */
    int    bPS;                                                  /*!< PS supported */
    int    bPDF;                                                 /*!< PDF supported */
    int    bXPS;                                                 /*!< XPS supported */
    int    bPCL;                                                 /*!< PCL supported (PCL5 and PCLXL) */
    int    bPCL5;                                                /*!< PCL5 supported */
    int    bMultiThreadedCore;                                   /*!< Multithreaded core */
    int    bFF;                                                  /*!< FontFusion library */
    int    bUFST5; /*!< UFST5 font library.*/
    int    bUFST7; /*!< UFST7 font library.*/
} OIL_tyRIPFeatures;


#endif /* _OIL_H_ */
