/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pjl.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This file contains functions which encapsulate the PJL 
 *  parser for use with the OIL.
 *
 *  The PJL parser passes extracted PJL commands back to the OIL 
 * as a \c PjlCommand structure, which contains the name of the command,
 * as well as the command options and command modifier, both of which
 * are modelled by the \c PjlOption structure.  A command is passed to the 
 * OIL by calling OIL_PjlCommandCallback(), which uses the name specified
 * in the \c PjlOption to look up the appropriate OIL function to handle the 
 * command.
 * 
 * This file defines the \c OilPjlCommand structure to map a command name
 * to a function pointer, denoting the function which is to handle that 
 * command.  Handler functions must be of type \c OilPjlCommandFn.
 * If no action is required for a given command, it may be mapped to a \c NULL
 * function pointer.  An array of \c OilPjlCommand structures, \c gOilPjlCommands,
 * holds the command name/function pointer mappings for all commands known and
 * supported by this implementation.
 *
 * Setting print environment variables is an important use of PJL.  The OIL PJL
 * wrapper holds a record of both the user default settings (as distinct from the 
 * factory default settings, which are stored by the device itself), and the current 
 * settings.  The user default settings are those to which the printer will 
 * revert between jobs; they are based on the factory default settings, modified
 * by changes that have been made to those settings, either via PJL \c DEFAULT commands
 * or via the printer control panel.  The current settings are based on the user default 
 * settings, modified by any further alterations that have been made in the course
 * of the current job.
 *
 * The environment variables known to the OIL PJL wrapper are also defined in this file.
 * Each \c PjlEnvVar variable has a name, a type and, optionally, a modifier. It also points
 * to an \c OilEnvVar variable, which holds further type information, the a pointer to data 
 * indicating the possible values the variable can take (or NULL if the variable is not 
 * restricted, as is the case for job names, for example), and pointers to the getter and 
 * setter methods for the variable.
 * \note The getter and setter methods for the envrironment variables are not detailed in 
 * this documentation. They are simple, highly repetitive and do not form a part of the 
 * public interface to the PJL wrapper.  If the reader is interested in these functions, 
 * they can be found within the source code of oil_pjl.c, and all have the form
 *  \c OIL_Set<variable name> for setter functions, or \c OIL_Get<variable name> for getter 
 * functions.
 */

#ifdef USE_PJL

#include "oil_pjl.h"

#include "oil_interface_oil2pms.h"
#include "oil_job_handler.h"
#include "oil_malloc.h"
#include "pjlparser.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;

/* Table of commands we recognise and handle */
typedef int32 (OilPjlCommandFn)( PjlCommand * pCommand );

typedef struct OilPjlCommand
{
  uint8 * pzName;

  OilPjlCommandFn * pFn;

} OilPjlCommand;

static int32 OIL_PjlDoUEL( PjlCommand * pCommand );
static int32 OIL_PjlDoComment( PjlCommand * pCommand );
static int32 OIL_PjlDoDefault( PjlCommand * pCommand );
static int32 OIL_PjlDoDinquire( PjlCommand * pCommand );
static int32 OIL_PjlDoEcho( PjlCommand * pCommand );
static int32 OIL_PjlDoEoj( PjlCommand * pCommand );
static int32 OIL_PjlDoFSAppend( PjlCommand * pCommand );
static int32 OIL_PjlDoFSDelete( PjlCommand * pCommand );
static int32 OIL_PjlDoFSDirList( PjlCommand * pCommand );
static int32 OIL_PjlDoFSDownload( PjlCommand * pCommand );
static int32 OIL_PjlDoFSInit( PjlCommand * pCommand );
static int32 OIL_PjlDoFSMkDir( PjlCommand * pCommand );
static int32 OIL_PjlDoFSQuery( PjlCommand * pCommand );
static int32 OIL_PjlDoFSUpload( PjlCommand * pCommand );
static int32 OIL_PjlDoInfo( PjlCommand * pCommand );
static int32 OIL_PjlDoInitialize( PjlCommand * pCommand );
static int32 OIL_PjlDoInquire( PjlCommand * pCommand );
static int32 OIL_PjlDoJob( PjlCommand * pCommand );
static int32 OIL_PjlDoOpmsg( PjlCommand * pCommand );
static int32 OIL_PjlDoRdymsg( PjlCommand * pCommand );
static int32 OIL_PjlDoReset( PjlCommand * pCommand );
static int32 OIL_PjlDoSet( PjlCommand * pCommand );
static int32 OIL_PjlDoStmsg( PjlCommand * pCommand );
static int32 OIL_PjlDoUstatus( PjlCommand * pCommand );
static int32 OIL_PjlDoUstatusoff( PjlCommand * pCommand );

/** \brief This array of OilPjlCommand structures maps each PJL
 *         command string to the function pointers which is 
 *         used to handle that command in the OIL.
*/
static OilPjlCommand gOilPjlCommands[] =
{
  { (uint8 *) "\033%-12345X", OIL_PjlDoUEL        },
  { (uint8 *) "COMMENT",      OIL_PjlDoComment    },
  { (uint8 *) "DEFAULT",      OIL_PjlDoDefault    },
  { (uint8 *) "DINQUIRE",     OIL_PjlDoDinquire   },
  { (uint8 *) "DMCMD",        NULL                },
  { (uint8 *) "DMINFO",       NULL                },
  { (uint8 *) "ECHO",         OIL_PjlDoEcho       },
  { (uint8 *) "ENTER",        NULL                },
  { (uint8 *) "EOJ",          OIL_PjlDoEoj        },
  { (uint8 *) "FSAPPEND",     OIL_PjlDoFSAppend   },
  { (uint8 *) "FSDELETE",     OIL_PjlDoFSDelete   },
  { (uint8 *) "FSDIRLIST",    OIL_PjlDoFSDirList  },
  { (uint8 *) "FSDOWNLOAD",   OIL_PjlDoFSDownload },
  { (uint8 *) "FSINIT",       OIL_PjlDoFSInit     },
  { (uint8 *) "FSMKDIR",      OIL_PjlDoFSMkDir    },
  { (uint8 *) "FSQUERY",      OIL_PjlDoFSQuery    },
  { (uint8 *) "FSUPLOAD",     OIL_PjlDoFSUpload   },
  { (uint8 *) "INFO",         OIL_PjlDoInfo       },
  { (uint8 *) "INITIALIZE",   OIL_PjlDoInitialize },
  { (uint8 *) "INQUIRE",      OIL_PjlDoInquire    },
  { (uint8 *) "JOB",          OIL_PjlDoJob        },
  { (uint8 *) "OPMSG",        OIL_PjlDoOpmsg      },
  { (uint8 *) "RDYMSG",       OIL_PjlDoRdymsg     },
  { (uint8 *) "RESET",        OIL_PjlDoReset      },
  { (uint8 *) "SET",          OIL_PjlDoSet        },
  { (uint8 *) "STMSG",        OIL_PjlDoStmsg      },
  { (uint8 *) "USTATUS",      OIL_PjlDoUstatus    },
  { (uint8 *) "USTATUSOFF",   OIL_PjlDoUstatusoff }
};
#define N_OIL_PJL_COMMANDS ( sizeof( gOilPjlCommands ) / sizeof( gOilPjlCommands[ 0 ] ) )


/* Environment variables, set by DEFAULT & SET, reported by DINQUIRE and INQUIRE */
enum {
  eRangeInt = 1,
  eRangeDouble,
  eEnumInt,
  eEnumString,
  eString,
  eEnumMappedValue
};

typedef void (OilEnvVarGetFn)( PjlValue * pValue, int fGetDefault );
typedef int32 (OilEnvVarSetFn)( PjlValue * pValue, int fSetDefault );

typedef struct OilEnvVarRangeInt
{
  int32 min;
  int32 max;
  int32 fSpecial;
  int32 special;

} OilEnvVarRangeInt;

typedef struct OilEnvVarRangeDouble
{
  double min;
  double max;

} OilEnvVarRangeDouble;

typedef struct OilEnvVarEnumInt
{
  int32   nValues;

  int32 * pValues;

} OilEnvVarEnumInt;

typedef struct OilEnvVarEnumString
{
  int32    nValues;

  uint8 ** ppValues;

} OilEnvVarEnumString;

typedef struct OilMappedValue
{
  uint8 * pValue;
  int32   value;

} OilMappedValue;

typedef struct OilEnvVarEnumMappedValue
{
  int32            nValues;

  OilMappedValue * pMappedValues;

} OilEnvVarEnumMappedValue;

/* An instance of this structure is pointed to by the pPrivate
 * field of PjlEnvVar.
 */
typedef struct OilEnvVar
{
  int32 eType;

  void * pValues;

  OilEnvVarGetFn * pGetFn;
  OilEnvVarSetFn * pSetFn;

} OilEnvVar;

enum {
  eoilpjl_Off = 1,
  eoilpjl_On
};


static uint8 *             gOffOnValues[] = { (uint8 *) "OFF", (uint8 *) "ON" };
static OilEnvVarEnumString gOffOnEnum[] = { 2, gOffOnValues };

static uint8 *             gPaperSizeValues[] =
{
  (uint8 *) "LETTER",
  (uint8 *) "A4",
  (uint8 *) "LEGAL",
  (uint8 *) "EXECUTIVE",
  (uint8 *) "A3",
  (uint8 *) "TABLOID",
  (uint8 *) "A5",
  (uint8 *) "A6",
  (uint8 *) "C5",
  (uint8 *) "DL",
  (uint8 *) "LEDGER",
  (uint8 *) "OFUKU",
  (uint8 *) "JISB4",
  (uint8 *) "JISB5",
  (uint8 *) "CUSTOM",
};

#define N_PAPER_SIZE_VALUES ( sizeof( gPaperSizeValues ) / sizeof( gPaperSizeValues[ 0 ] ) )

static uint8 * gPaperSizeUnknown = (uint8 *) "UNKNOWN";
static uint8 * gMediaTypeUnknown = (uint8 *) "UNKNOWN";

static uint8 *              gBindingValues[] =
{
  (uint8 *) "LONGEDGE",
  (uint8 *) "SHORTEDGE"
};
static OilEnvVarEnumString  gBindingEnum = { sizeof( gBindingValues ) / sizeof( gBindingValues[ 0 ] ), gBindingValues };

static int32                gBitsPerPixelValues[] = { 16, 8, 4, 2, 1 };
static OilEnvVarEnumInt     gBitsPerPixelEnum = { sizeof( gBitsPerPixelValues ) / sizeof( gBitsPerPixelValues[ 0 ] ), gBitsPerPixelValues };

static OilMappedValue       gColorModeValues[] =
{
  { (uint8 *) "BW",  1 },
  { (uint8 *) "COLOR",     2 }
};

static OilEnvVarEnumMappedValue  gColorModeEnum = { sizeof( gColorModeValues ) / sizeof( gColorModeValues[ 0 ] ), gColorModeValues };

static OilEnvVarRangeInt    gCopiesRange = { 1, 32000, FALSE, 0 };

static OilMappedValue       gCourierValues[] =
{
  { (uint8 *) "REGULAR",  0 },
  { (uint8 *) "DARK",     1 }
};

static OilEnvVarEnumMappedValue  gCourierEnum = { sizeof( gCourierValues ) / sizeof( gCourierValues[ 0 ] ), gCourierValues };
static OilEnvVarRangeDouble gCustLengthRange = { 3600, 17007 }; /* in decipoints (127-600 mm) */

static OilEnvVarRangeDouble gCustWidthRange = { 2154, 8645 }; /* in decipoints (76-305 mm) */
static OilMappedValue       gEngineSimulatorValues[] =
{
  { (uint8 *) "ON",  0 },
  { (uint8 *) "OFF", 1 },
  { (uint8 *) "BYPASS", 2 }
};

enum {
  eEngSimOn = 0,
  eEngSimOff,
  eEngSimByPass
};
static OilEnvVarEnumMappedValue  gEngineSimulatorEnum = { sizeof( gEngineSimulatorValues ) / sizeof( gEngineSimulatorValues[ 0 ] ), gEngineSimulatorValues };

static OilEnvVarRangeInt    gFontnumberRange = { 0, 50, FALSE, 0 };

static uint8 *              gFontsourceValues[] =
{
  (uint8 *) "I"
};
static OilEnvVarEnumString  gFontsourceEnum = { sizeof( gFontsourceValues ) / sizeof( gFontsourceValues[ 0 ] ), gFontsourceValues };

static OilEnvVarRangeInt    gFormlinesRange = { 5, 128, FALSE, 0 };

static OilEnvVarEnumString  gInTray1SizeEnum = { N_PAPER_SIZE_VALUES, gPaperSizeValues };

static OilEnvVarEnumString  gInMTraySizeEnum = { N_PAPER_SIZE_VALUES, gPaperSizeValues };

static OilMappedValue       gJobOffsetValues[] =
{
  { (uint8 *) "OFF",  0 },
  { (uint8 *) "ON",   1 }
};
static OilEnvVarEnumMappedValue  gJobOffsetEnum = { sizeof( gJobOffsetValues ) / sizeof( gJobOffsetValues[ 0 ] ), gJobOffsetValues };

static OilEnvVarRangeInt    gLineTerminationRange = { 0, 3, FALSE, 0 };

static OilMappedValue       gMediaSourceValues[] =
{
  { (uint8 *) "AUTOSELECT",  PMS_TRAY_AUTO },
  { (uint8 *) "BYPASS",  PMS_TRAY_BYPASS },
  { (uint8 *) "MANUALFEED",  PMS_TRAY_MANUALFEED },
  { (uint8 *) "TRAY1",  PMS_TRAY_TRAY1 },
  { (uint8 *) "TRAY2",  PMS_TRAY_TRAY2 },
  { (uint8 *) "TRAY3",  PMS_TRAY_TRAY3 }
};
static OilEnvVarEnumMappedValue  gMediaSourceEnum = { sizeof( gMediaSourceValues ) / sizeof( gMediaSourceValues[ 0 ] ), gMediaSourceValues };

static uint8 *              gMediaTypeValues[] =
{
  (uint8 *) "PLAIN",
  (uint8 *) "THICK",
  (uint8 *) "THIN",
  (uint8 *) "BOND",
  (uint8 *) "LABEL",
  (uint8 *) "TRANSPARENCY",
  (uint8 *) "ENVELOPE",
  (uint8 *) "PREPRINTED",
  (uint8 *) "LETTERHEAD",
  (uint8 *) "RECYCLED"
};

static OilEnvVarEnumString  gMediaTypeEnum = { sizeof( gMediaTypeValues ) / sizeof( gMediaTypeValues[ 0 ] ), gMediaTypeValues };

static OilEnvVarRangeInt    gRenderThreadsRange = { 1, 32000, FALSE, 0 };

static OilEnvVarRangeInt gRipMemRange = { 10 ,  9999999 };

static OilMappedValue       gOrientationValues[] =
{
  { (uint8 *) "PORTRAIT",  0 },
  { (uint8 *) "LANDSCAPE", 1 }
};
static OilEnvVarEnumMappedValue  gOrientationEnum = { sizeof( gOrientationValues ) / sizeof( gOrientationValues[ 0 ] ), gOrientationValues };

static OilMappedValue       gOutputTypeValues[] =
{
  { (uint8 *) "TIFF",     PMS_TIFF },
  { (uint8 *) "TIFFSEP",  PMS_TIFF_SEP },
  { (uint8 *) "TIFFVIEW", PMS_TIFF_VIEW },
  { (uint8 *) "PDF",      PMS_PDF },
  { (uint8 *) "PDFVIEW",  PMS_PDF_VIEW },
  { (uint8 *) "PRINT",    PMS_DIRECT_PRINT },
  { (uint8 *) "PRINTX",   PMS_DIRECT_PRINTXL },
  { (uint8 *) "NONE",     PMS_NONE }
};

static OilEnvVarEnumMappedValue  gOutputTypeEnum = { sizeof( gOutputTypeValues ) / sizeof( gOutputTypeValues[ 0 ] ), gOutputTypeValues };

static OilMappedValue       gOutBinValues[] =
{
  { (uint8 *) "AUTO", PMS_OUTPUT_TRAY_AUTO },
  { (uint8 *) "UPPER", PMS_OUTPUT_TRAY_UPPER },
  { (uint8 *) "LOWER", PMS_OUTPUT_TRAY_LOWER },
  { (uint8 *) "EXTRA", PMS_OUTPUT_TRAY_EXTRA }
};
static OilEnvVarEnumMappedValue  gOutBinEnum = { sizeof( gOutBinValues ) / sizeof( gOutBinValues[ 0 ] ), gOutBinValues };

static OilEnvVarRangeInt    gPagesRange = { 0, 9999999, FALSE, 0 };

static OilEnvVarEnumString  gPaperEnum = { N_PAPER_SIZE_VALUES, gPaperSizeValues };

static uint8 * gPasswordDisabled = (uint8 *) "DISABLED";
static uint8 * gPasswordEnabled = (uint8 *) "ENABLED";
static OilEnvVarRangeInt    gPasswordRange = { 0, 65535, FALSE, 0 };

static uint8 *              gPersonalityValues[] = { (uint8 *) "AUTO", (uint8 *) "PCL", (uint8 *) "PDF", (uint8 *) "POSTSCRIPT", (uint8 *) "XPS" };
static OilEnvVarEnumString  gPersonalityEnum = { sizeof( gPersonalityValues ) / sizeof( gPersonalityValues[ 0 ] ), gPersonalityValues };

static OilEnvVarRangeDouble gPitchRange = { 0.44, 99.99 };

static OilEnvVarRangeDouble gPtsizeRange = { 4.0, 999.75 };

static OilMappedValue       gRenderModeValues[] =
{
  { (uint8 *) "COLOR",      PMS_RenderMode_Color },
  { (uint8 *) "GRAYSCALE",  PMS_RenderMode_Grayscale }
};
static OilEnvVarEnumMappedValue  gRenderModeEnum = { sizeof( gRenderModeValues ) / sizeof( gRenderModeValues[ 0 ] ), gRenderModeValues };

static OilMappedValue       gRenderModelValues[] =
{
  { (uint8 *) "CMYK8B",     PMS_RenderModel_CMYK8B },
  { (uint8 *) "G1",         PMS_RenderModel_G1 }
};
static OilEnvVarEnumMappedValue  gRenderModelEnum = { sizeof( gRenderModelValues ) / sizeof( gRenderModelValues[ 0 ] ), gRenderModelValues };

static int32                gResolutionValues[] = { 1200, 600, 300 };
static OilEnvVarEnumInt     gResolutionEnum = { sizeof( gResolutionValues ) / sizeof( gResolutionValues[ 0 ] ), gResolutionValues };

static OilMappedValue       gSymsetValues[] =
{
  { (uint8 *) "PC8",      341  },        /* 10U - IBM PC-8 */
  { (uint8 *) "ROMAN8",   277  },        /* 8U - HP Roman-8 */
  { (uint8 *) "ROMAN9",   245  },        /* 7U */
  { (uint8 *) "ISOL1",    14   },        /* 0N - ISO Latin 1 */
  { (uint8 *) "ISOL2",    78   },        /* 2N - ISO Latin 2 */
  { (uint8 *) "ISOL5",    174  },        /* 5N - ISO Latin 5 */
  { (uint8 *) "ISOL6",    206  },        /* 6N - ISO Latin 6 */
  { (uint8 *) "ISOL9",    302  },        /* 9N - ISO Latin 9 */
  { (uint8 *) "PC775",    853  },        /* 26U - PC-775 */
  { (uint8 *) "PC8DN",    373  },        /* 11U - IBM PC-8 D/N */
  { (uint8 *) "PC850",    405  },        /* 12U - IBM PC-850 Multilingual */
  { (uint8 *) "PC852",    565  },        /* 17U - PC-852 Latin 2 */
  { (uint8 *) "PC858",    437  },        /* 13U */
  { (uint8 *) "PC8TK",    308  },        /* 9T - PC Turkish */
  { (uint8 *) "PC1004",   298  },        /* 9T - PC-1004 */
  { (uint8 *) "WINL1",    629  },        /* 19U - Windows 3.1 Latin 1 */
  { (uint8 *) "WINL2",    293  },        /* 9E - Windows 3.1 Latin 2 */
  { (uint8 *) "WINL5",    180  },        /* 5T - Windows 3.1 Latin 5 */
  { (uint8 *) "WINBALT",  620  },        /* 19L */
  { (uint8 *) "DESKTOP",  234  },        /* 7J */
  { (uint8 *) "PSTEXT",   330  },        /* 10J - PS text */
  { (uint8 *) "LEGAL",    53   },        /* 1U - US Legal */
  { (uint8 *) "ISO4",     37   },        /* 1E - ISO 4 UK */
  { (uint8 *) "ISO6",     21   },        /* 0U - ISO 6 ASCII */
  { (uint8 *) "ISO11",    19   },        /* 0S - ISO 11 Swedish */
  { (uint8 *) "ISO15",    9    },        /* 0I - ISO 15 Italian */
  { (uint8 *) "ISO17",    83   },        /* 2S - ISO 17 Spanish */
  { (uint8 *) "ISO21",    39   },        /* 1G - ISO 21 German */
  { (uint8 *) "ISO60",    4    },        /* 0D - ISO 60 Danish/Norwegian */
  { (uint8 *) "ISO69",    38   },        /* 1F - ISO 69 French */
  { (uint8 *) "WIN30",    309  },        /* 9U - Windows 3.0 Latin 1 */
  { (uint8 *) "MCTEXT",   394  }         /* 12J - Macintosh */
};
static OilEnvVarEnumMappedValue  gSymsetEnum = { sizeof( gSymsetValues ) / sizeof( gSymsetValues[ 0 ] ), gSymsetValues };

static OilMappedValue       gWideA4Values[] =
{
  { (uint8 *) "NO",  0 },
  { (uint8 *) "YES",   1 }
};
static OilEnvVarEnumMappedValue  gWideA4Enum = { sizeof( gWideA4Values ) / sizeof( gWideA4Values[ 0 ] ), gWideA4Values };

static OilMappedValue       gBlackDetectValues[] =
{
  { (uint8 *) "NO",  0 },
  { (uint8 *) "YES",   1 }
};
static OilEnvVarEnumMappedValue  gBlackDetectEnum = { sizeof( gBlackDetectValues ) / sizeof( gBlackDetectValues[ 0 ] ), gBlackDetectValues };

static OilMappedValue       gTestPageValues[] =
{
  { (uint8 *) "CONFIG",  PMS_TESTPAGE_CFG },
  { (uint8 *) "PSFONTLIST",  PMS_TESTPAGE_PS },
  { (uint8 *) "PCLFONTLIST", PMS_TESTPAGE_PCL }
};
static OilEnvVarEnumMappedValue  gTestPageEnum = { sizeof( gTestPageValues ) / sizeof( gTestPageValues[ 0 ] ), gTestPageValues };

static OilMappedValue       gPrintErrorPageValues[] =
{
  { (uint8 *) "NO",  0 },
  { (uint8 *) "YES",   1 }
};
static OilEnvVarEnumMappedValue  gPrintErrorPageEnum = { sizeof( gPrintErrorPageValues ) / sizeof( gPrintErrorPageValues[ 0 ] ), gPrintErrorPageValues };

/* GGEBDSDK specific options */
static OilEnvVarRangeInt    gCmdIntRange = { 0, 999999, FALSE, 0 };
static OilEnvVarRangeDouble gCmdTrapRange = { 0, 999999 };

static void OIL_GetBinding( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetBinding( PjlValue * pValue, int fSetDefault );
static void OIL_GetBitsPerPixel( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetBitsPerPixel( PjlValue * pValue, int fSetDefault );
static void OIL_GetColorMode( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetColorMode( PjlValue * pValue, int fSetDefault );
static void OIL_GetCopies( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCopies( PjlValue * pValue, int fSetDefault );
static void OIL_GetCourier( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCourier( PjlValue * pValue, int fSetDefault );
static void OIL_GetCustLength( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCustLength( PjlValue * pValue, int fSetDefault );
static void OIL_GetCustWidth( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCustWidth( PjlValue * pValue, int fSetDefault );
static void OIL_GetDisklock( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetDisklock( PjlValue * pValue, int fSetDefault );
static void OIL_GetDuplex( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetDuplex( PjlValue * pValue, int fSetDefault );
static void OIL_GetFontnumber( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetFontnumber( PjlValue * pValue, int fSetDefault );
static void OIL_GetFontsource( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetFontsource( PjlValue * pValue, int fSetDefault );
static void OIL_GetFormlines( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetFormlines( PjlValue * pValue, int fSetDefault );
static void OIL_GetImageFile( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetImageFile( PjlValue * pValue, int fSetDefault );
static void OIL_GetInTray1Size( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetInTray1Size( PjlValue * pValue, int fSetDefault );
static void OIL_GetJobname( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetJobname( PjlValue * pValue, int fSetDefault );
static void OIL_GetJobOffset( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetJobOffset( PjlValue * pValue, int fSetDefault );
static void OIL_GetLineTermination( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetLineTermination( PjlValue * pValue, int fSetDefault );
static void OIL_GetManualFeed( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetManualFeed( PjlValue * pValue, int fSetDefault );
static void OIL_GetMediaSource( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetMediaSource( PjlValue * pValue, int fSetDefault );
static void OIL_GetMediaType( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetMediaType( PjlValue * pValue, int fSetDefault );
static void OIL_GetMediaTypeMTray( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetMediaTypeMTray( PjlValue * pValue, int fSetDefault );
static void OIL_GetMediaTypeTray1( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetMediaTypeTray1( PjlValue * pValue, int fSetDefault );
static void OIL_GetMTraySize( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetMTraySize( PjlValue * pValue, int fSetDefault );
static void OIL_GetOrientation( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetOrientation( PjlValue * pValue, int fSetDefault );
static void OIL_GetOutBin( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetOutBin( PjlValue * pValue, int fSetDefault );
static void OIL_GetPages( PjlValue * pValue, int fGetDefault );
static void OIL_GetPaper( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPaper( PjlValue * pValue, int fSetDefault );
static void OIL_GetPassword( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPassword( PjlValue * pValue, int fSetDefault );
static void OIL_GetPersonality( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPersonality( PjlValue * pValue, int fSetDefault );
static void OIL_GetPitch( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPitch( PjlValue * pValue, int fSetDefault );
static void OIL_GetPrintBlankPage( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPrintBlankPage( PjlValue * pValue, int fSetDefault );
static void OIL_GetPrintErrorPage( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPrintErrorPage( PjlValue * pValue, int fSetDefault );
static void OIL_GetPtsize( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPtsize( PjlValue * pValue, int fSetDefault );
static void OIL_GetRenderMode( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetRenderMode( PjlValue * pValue, int fSetDefault );
static void OIL_GetRenderModel( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetRenderModel( PjlValue * pValue, int fSetDefault );
static void OIL_GetResolution( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetResolution( PjlValue * pValue, int fSetDefault );
static void OIL_GetSymset( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetSymset( PjlValue * pValue, int fSetDefault );
static void OIL_GetTestPage( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetTestPage( PjlValue * pValue, int fSetDefault );
static void OIL_GetUsername( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetUsername( PjlValue * pValue, int fSetDefault );
static void OIL_GetWideA4( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetWideA4( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_A( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_A( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_D( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_D( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_E( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_E( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_G( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_G( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_H( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_H( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_J( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_J( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_K( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_K( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_M( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_M( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MAPP( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MAPP( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MBUF( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MBUF( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MJOB( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MJOB( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MMISC( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MMISC( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MPMS( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MPMS( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_MSYS( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_MSYS( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_N( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_N( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_O( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_O( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_P( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_P( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_R( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_R( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_X( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_X( PjlValue * pValue, int fSetDefault );
static void OIL_GetCMD_Y( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetCMD_Y( PjlValue * pValue, int fSetDefault );
static void OIL_GetSYSTEM_Restart( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetSYSTEM_Restart( PjlValue * pValue, int fSetDefault );
static void OIL_GetSYSTEM_StrideBoundary( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetSYSTEM_StrideBoundary( PjlValue * pValue, int fSetDefault );
static void OIL_GetSYSTEM_PrintableMode( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetSYSTEM_PrintableMode( PjlValue * pValue, int fSetDefault );

static OilEnvVar gBinding         = { eEnumString,      &gBindingEnum,          OIL_GetBinding,         OIL_SetBinding         };
static OilEnvVar gBitsPerPixel    = { eEnumInt,         &gBitsPerPixelEnum,     OIL_GetBitsPerPixel,    OIL_SetBitsPerPixel    };
static OilEnvVar gColorMode       = { eEnumMappedValue, &gColorModeEnum,        OIL_GetColorMode,       OIL_SetColorMode       };
static OilEnvVar gCopies          = { eRangeInt,        &gCopiesRange,          OIL_GetCopies,          OIL_SetCopies          };
static OilEnvVar gCourier         = { eEnumMappedValue, &gCourierEnum,          OIL_GetCourier,         OIL_SetCourier         };
static OilEnvVar gCustLength      = { eRangeDouble,     &gCustLengthRange,      OIL_GetCustLength,      OIL_SetCustLength      };
static OilEnvVar gCustWidth       = { eRangeDouble,     &gCustWidthRange,       OIL_GetCustWidth,       OIL_SetCustWidth       };
static OilEnvVar gDisklock        = { eEnumString,      &gOffOnEnum,            OIL_GetDisklock,        OIL_SetDisklock        };
static OilEnvVar gDuplex          = { eEnumString,      &gOffOnEnum,            OIL_GetDuplex,          OIL_SetDuplex          };
static OilEnvVar gFontnumber      = { eRangeInt,        &gFontnumberRange,      OIL_GetFontnumber,      OIL_SetFontnumber      };
static OilEnvVar gFontsource      = { eEnumString,      &gFontsourceEnum,       OIL_GetFontsource,      OIL_SetFontsource      };
static OilEnvVar gFormlines       = { eRangeInt,        &gFormlinesRange,       OIL_GetFormlines,       OIL_SetFormlines       };
static OilEnvVar gImageFile       = { eString,          NULL,                   OIL_GetImageFile,       OIL_SetImageFile       };
static OilEnvVar gInTray1Size     = { eEnumString,      &gInTray1SizeEnum,      OIL_GetInTray1Size,     OIL_SetInTray1Size     };
static OilEnvVar gInMTraySize     = { eEnumString,      &gInMTraySizeEnum,      OIL_GetMTraySize,       OIL_SetMTraySize       };
static OilEnvVar gJobname         = { eString,          NULL,                   OIL_GetJobname,         OIL_SetJobname         };
static OilEnvVar gJobOffset       = { eEnumMappedValue, &gJobOffsetEnum,        OIL_GetJobOffset,       OIL_SetJobOffset       };
static OilEnvVar gLineTermination = { eRangeInt,        &gLineTerminationRange, OIL_GetLineTermination, OIL_SetLineTermination };
static OilEnvVar gManualFeed      = { eEnumString,      &gOffOnEnum,            OIL_GetManualFeed,      OIL_SetManualFeed      };
static OilEnvVar gMediaSource     = { eEnumMappedValue, &gMediaSourceEnum,      OIL_GetMediaSource,     OIL_SetMediaSource     };
static OilEnvVar gMediaType       = { eEnumString,      &gMediaTypeEnum,        OIL_GetMediaType,       OIL_SetMediaType       };
static OilEnvVar gMediaTypeMTray  = { eEnumString,      &gMediaTypeEnum,        OIL_GetMediaTypeMTray,  OIL_SetMediaTypeMTray  };
static OilEnvVar gMediaTypeTray1  = { eEnumString,      &gMediaTypeEnum,        OIL_GetMediaTypeTray1,  OIL_SetMediaTypeTray1  };
static OilEnvVar gOrientation     = { eEnumMappedValue, &gOrientationEnum,      OIL_GetOrientation,     OIL_SetOrientation     };
static OilEnvVar gOutBin          = { eEnumMappedValue, &gOutBinEnum,           OIL_GetOutBin,          OIL_SetOutBin          };
static OilEnvVar gPages           = { eRangeInt,        &gPagesRange,           OIL_GetPages,           NULL                   };
static OilEnvVar gPaper           = { eEnumString,      &gPaperEnum,            OIL_GetPaper,           OIL_SetPaper           };
static OilEnvVar gPassword        = { eRangeInt,        &gPasswordRange,        OIL_GetPassword,        OIL_SetPassword        };
static OilEnvVar gPersonality     = { eEnumString,      &gPersonalityEnum,      OIL_GetPersonality,     OIL_SetPersonality     };
static OilEnvVar gPitch           = { eRangeDouble,     &gPitchRange,           OIL_GetPitch,           OIL_SetPitch           };
static OilEnvVar gPrintBlankPage  = { eEnumString,      &gOffOnEnum,            OIL_GetPrintBlankPage,  OIL_SetPrintBlankPage  };
static OilEnvVar gPrintErrorPage  = { eEnumMappedValue, &gPrintErrorPageEnum,   OIL_GetPrintErrorPage,  OIL_SetPrintErrorPage  };
static OilEnvVar gPtsize          = { eRangeDouble,     &gPtsizeRange,          OIL_GetPtsize,          OIL_SetPtsize          };
static OilEnvVar gRenderMode      = { eEnumMappedValue, &gRenderModeEnum,       OIL_GetRenderMode,      OIL_SetRenderMode      };
static OilEnvVar gRenderModel     = { eEnumMappedValue, &gRenderModelEnum,      OIL_GetRenderModel,     OIL_SetRenderModel     };
static OilEnvVar gResolution      = { eEnumInt,         &gResolutionEnum,       OIL_GetResolution,      OIL_SetResolution      };
static OilEnvVar gSymset          = { eEnumMappedValue, &gSymsetEnum,           OIL_GetSymset,          OIL_SetSymset          };
static OilEnvVar gTestPage        = { eEnumMappedValue, &gTestPageEnum,         OIL_GetTestPage,        OIL_SetTestPage        };
static OilEnvVar gUsername        = { eString,          NULL,                   OIL_GetUsername,        OIL_SetUsername        };
static OilEnvVar gWideA4          = { eEnumMappedValue, &gWideA4Enum,           OIL_GetWideA4,          OIL_SetWideA4          };
static OilEnvVar gEBDSDKcmdA      = { eRangeDouble,     &gCmdTrapRange,         OIL_GetCMD_A,           OIL_SetCMD_A           };
static OilEnvVar gEBDSDKcmdD      = { eEnumInt,         &gBitsPerPixelEnum,     OIL_GetCMD_D,           OIL_SetCMD_D           };
static OilEnvVar gEBDSDKcmdE      = { eEnumMappedValue, &gEngineSimulatorEnum,  OIL_GetCMD_E,           OIL_SetCMD_E           };
static OilEnvVar gEBDSDKcmdG      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_G,           OIL_SetCMD_G           };
static OilEnvVar gEBDSDKcmdH      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_H,           OIL_SetCMD_H           };
static OilEnvVar gEBDSDKcmdJ      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_J,           OIL_SetCMD_J           };
static OilEnvVar gEBDSDKcmdK      = { eEnumMappedValue, &gBlackDetectEnum,      OIL_GetCMD_K,           OIL_SetCMD_K           };
static OilEnvVar gEBDSDKcmdM      = { eRangeInt,        &gRipMemRange,          OIL_GetCMD_M,           OIL_SetCMD_M           };
static OilEnvVar gEBDSDKcmdMAPP   = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MAPP,        OIL_SetCMD_MAPP        };
static OilEnvVar gEBDSDKcmdMBUF   = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MBUF,        OIL_SetCMD_MBUF        };
static OilEnvVar gEBDSDKcmdMJOB   = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MJOB,        OIL_SetCMD_MJOB        };
static OilEnvVar gEBDSDKcmdMMISC  = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MMISC,       OIL_SetCMD_MMISC       };
static OilEnvVar gEBDSDKcmdMPMS   = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MPMS,        OIL_SetCMD_MPMS        };
static OilEnvVar gEBDSDKcmdMSYS   = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_MSYS,        OIL_SetCMD_MSYS        };
static OilEnvVar gEBDSDKcmdN      = { eRangeInt,        &gRenderThreadsRange,   OIL_GetCMD_N,           OIL_SetCMD_N           };
static OilEnvVar gEBDSDKcmdO      = { eEnumMappedValue, &gOutputTypeEnum,       OIL_GetCMD_O,           OIL_SetCMD_O           };
static OilEnvVar gEBDSDKcmdP      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_P,           OIL_SetCMD_P           };
static OilEnvVar gEBDSDKcmdR      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_R,           OIL_SetCMD_R           };
static OilEnvVar gEBDSDKcmdX      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_X,           OIL_SetCMD_X           };
static OilEnvVar gEBDSDKcmdY      = { eRangeInt,        &gCmdIntRange,          OIL_GetCMD_Y,           OIL_SetCMD_Y           };
static OilEnvVar gEBDSDKsystemRestart        = { eRangeInt,    &gCmdIntRange,          OIL_GetSYSTEM_Restart,        OIL_SetSYSTEM_Restart  };
static OilEnvVar gEBDSDKsystemStrideBoundary = { eRangeInt,    &gCmdIntRange,          OIL_GetSYSTEM_StrideBoundary, OIL_SetSYSTEM_StrideBoundary  };
static OilEnvVar gEBDSDKsystemPrintableMode  = { eRangeInt,    &gCmdIntRange,          OIL_GetSYSTEM_PrintableMode,  OIL_SetSYSTEM_PrintableMode  };

static PjlOption gLparmPclModifier     = { (uint8 *) "LPARM", { eValueAlphanumeric, (uint8 *) "PCL" }, NULL };

static PjlOption gEbdsdkCmdModifier    = { (uint8 *) "GGEBDSDK", { eValueAlphanumeric, (uint8 *) "CMDLINE" }, NULL };
static PjlOption gEbdsdkSystemModifier = { (uint8 *) "GGEBDSDK", { eValueAlphanumeric, (uint8 *) "SYSTEM" }, NULL };

static PjlEnvVar gOilPjlEnvVars[] =
{
  { (uint8 *) "BINDING",         NULL,               eValueAlphanumeric, &gBinding         },
  { (uint8 *) "BITSPERPIXEL",    NULL,               eValueInt,          &gBitsPerPixel    },
  { (uint8 *) "COLORMODE",       NULL,               eValueAlphanumeric, &gColorMode       },
  { (uint8 *) "COPIES",          NULL,               eValueInt,          &gCopies          },
  { (uint8 *) "COURIER",         NULL,               eValueAlphanumeric, &gCourier         },
  { (uint8 *) "CUSTLENGTH",      NULL,               eValueDouble,       &gCustLength      },
  { (uint8 *) "CUSTWIDTH",       NULL,               eValueDouble,       &gCustWidth       },
  { (uint8 *) "DISKLOCK",        NULL,               eValueAlphanumeric, &gDisklock        },
  { (uint8 *) "DUPLEX",          NULL,               eValueAlphanumeric, &gDuplex          },
  { (uint8 *) "FONTNUMBER",      &gLparmPclModifier, eValueInt,          &gFontnumber      },
  { (uint8 *) "FONTSOURCE",      &gLparmPclModifier, eValueAlphanumeric, &gFontsource      },
  { (uint8 *) "FORMLINES",       NULL,               eValueInt,          &gFormlines       },
  { (uint8 *) "IMAGEFILE",       NULL,               eValueString,       &gImageFile       },
  { (uint8 *) "INTRAY1SIZE",     NULL,               eValueAlphanumeric, &gInTray1Size     },
  { (uint8 *) "JOBNAME",         NULL,               eValueString,       &gJobname         },
  { (uint8 *) "JOBOFFSET",       NULL,               eValueAlphanumeric, &gJobOffset       },
  { (uint8 *) "LINETERMINATION", &gLparmPclModifier, eValueInt,          &gLineTermination },
  { (uint8 *) "MANUALFEED",      NULL,               eValueAlphanumeric, &gManualFeed      },
  { (uint8 *) "MEDIASOURCE",     NULL,               eValueAlphanumeric, &gMediaSource     },
  { (uint8 *) "MEDIATYPE",       NULL,               eValueAlphanumeric, &gMediaType       },
  { (uint8 *) "MEDIATYPETRAY1",  NULL,               eValueAlphanumeric, &gMediaTypeTray1  },
  { (uint8 *) "MTRAYMEDIA",      NULL,               eValueAlphanumeric, &gMediaTypeMTray  },
  { (uint8 *) "MTRAYSIZE",       NULL,               eValueAlphanumeric, &gInMTraySize     },
  { (uint8 *) "ORIENTATION",     NULL,               eValueAlphanumeric, &gOrientation     },
  { (uint8 *) "OUTBIN",          NULL,               eValueAlphanumeric, &gOutBin          },
  { (uint8 *) "PAGES",           NULL,               eValueInt,          &gPages           },
  { (uint8 *) "PAPER",           NULL,               eValueAlphanumeric, &gPaper           },
  { (uint8 *) "PASSWORD",        NULL,               eValueInt,          &gPassword        },
  { (uint8 *) "PERSONALITY",     NULL,               eValueAlphanumeric, &gPersonality     },
  { (uint8 *) "PITCH",           &gLparmPclModifier, eValueDouble,       &gPitch           },
  { (uint8 *) "PRINTBLANKPAGE",  NULL,               eValueAlphanumeric, &gPrintBlankPage  },
  { (uint8 *) "PRINTERRORPAGE",  NULL,               eValueAlphanumeric, &gPrintErrorPage  },
  { (uint8 *) "PTSIZE",          &gLparmPclModifier, eValueDouble,       &gPtsize          },
  { (uint8 *) "RENDERMODE",      NULL,               eValueAlphanumeric, &gRenderMode      },
  { (uint8 *) "RENDERMODEL",     NULL,               eValueAlphanumeric, &gRenderModel     },
  { (uint8 *) "RESOLUTION",      NULL,               eValueInt,          &gResolution      },
  { (uint8 *) "SYMSET",          &gLparmPclModifier, eValueAlphanumeric, &gSymset          },
  { (uint8 *) "TESTPAGE",        NULL,               eValueAlphanumeric, &gTestPage        },
  { (uint8 *) "USERNAME",        NULL,               eValueString,       &gUsername        },
  { (uint8 *) "WIDEA4",          NULL,               eValueAlphanumeric, &gWideA4          },
  { (uint8 *) "A",               &gEbdsdkCmdModifier,eValueDouble      , &gEBDSDKcmdA      },
  { (uint8 *) "D",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdD      },
  { (uint8 *) "E",               &gEbdsdkCmdModifier,eValueAlphanumeric, &gEBDSDKcmdE      },
  { (uint8 *) "G",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdG      },
  { (uint8 *) "H",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdH      },
  { (uint8 *) "J",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdJ      },
  { (uint8 *) "K",               &gEbdsdkCmdModifier,eValueAlphanumeric, &gEBDSDKcmdK      },
  { (uint8 *) "M",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdM      },
  { (uint8 *) "MAPP",            &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMAPP   },
  { (uint8 *) "MBUF",            &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMBUF   },
  { (uint8 *) "MJOB",            &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMJOB   },
  { (uint8 *) "MMISC",           &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMMISC  },
  { (uint8 *) "MPMS",            &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMPMS   },
  { (uint8 *) "MSYS",            &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdMSYS   },
  { (uint8 *) "N",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdN      },

  { (uint8 *) "O",               &gEbdsdkCmdModifier,eValueAlphanumeric, &gEBDSDKcmdO      },
  { (uint8 *) "P",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdP      },
  { (uint8 *) "R",               &gEbdsdkCmdModifier,eValueInt         , &gEBDSDKcmdR      },
  { (uint8 *) "X",               &gEbdsdkCmdModifier,eValueInt,          &gEBDSDKcmdX      },
  { (uint8 *) "Y",               &gEbdsdkCmdModifier,eValueInt,          &gEBDSDKcmdY      },
  { (uint8 *) "RESTART",         &gEbdsdkSystemModifier, eValueInt      , &gEBDSDKsystemRestart },
  { (uint8 *) "STRIDEBOUNDARY",  &gEbdsdkSystemModifier, eValueInt      , &gEBDSDKsystemStrideBoundary },
  { (uint8 *) "PRINTABLEMODE",   &gEbdsdkSystemModifier, eValueInt      , &gEBDSDKsystemPrintableMode }
};
#define N_OIL_PJL_ENV_VARS ( sizeof( gOilPjlEnvVars ) / sizeof( gOilPjlEnvVars[ 0 ] ) )


/* Categories for USTATUS command */
enum {
  eDeviceStatusOff = 1,
  eDeviceStatusOn,
  eDeviceStatusVerbose
};
static uint8 *             gDeviceStatusValues[] = { (uint8 *) "OFF", (uint8 *) "ON", (uint8 *) "VERBOSE" };
static OilEnvVarEnumString gDeviceStatusEnum[] = { 3, gDeviceStatusValues };

static OilEnvVarRangeInt   gTimedStatusRange = { 5, 300, TRUE, 0 };

static void OIL_GetDeviceStatus( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetDeviceStatus( PjlValue * pValue, int fSetDefault );
static void OIL_GetJobStatus( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetJobStatus( PjlValue * pValue, int fSetDefault );
static void OIL_GetPageStatus( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetPageStatus( PjlValue * pValue, int fSetDefault );
static void OIL_GetTimedStatus( PjlValue * pValue, int fGetDefault );
static int32 OIL_SetTimedStatus( PjlValue * pValue, int fSetDefault );

static OilEnvVar gDeviceStatus = { eEnumString, &gDeviceStatusEnum, OIL_GetDeviceStatus, OIL_SetDeviceStatus };
static OilEnvVar gJobStatus    = { eEnumString, &gOffOnEnum,        OIL_GetJobStatus,    OIL_SetJobStatus    };
static OilEnvVar gPageStatus   = { eEnumString, &gOffOnEnum,        OIL_GetPageStatus,   OIL_SetPageStatus   };
static OilEnvVar gTimedStatus  = { eRangeInt,   &gTimedStatusRange, OIL_GetTimedStatus,  OIL_SetTimedStatus  };

static PjlEnvVar gOilPjlStatusCategories[] =
{
  { (uint8 *) "DEVICE", NULL, eValueAlphanumeric, &gDeviceStatus },
  { (uint8 *) "JOB",    NULL, eValueAlphanumeric, &gJobStatus    },
  { (uint8 *) "PAGE",   NULL, eValueAlphanumeric, &gPageStatus   },
  { (uint8 *) "TIMED",  NULL, eValueInt,          &gTimedStatus  }
};
/*
TODO: No TIMED USTATUS support for now
#define N_OIL_PJL_STATUS_CATS ( sizeof( gOilPjlStatusCategories ) / sizeof( gOilPjlStatusCategories[ 0 ] ) )
*/
#define N_OIL_PJL_STATUS_CATS 3


/* Settings which are not job specific, but which can be SET */
typedef struct OilPjlSettings
{
  int32 ePersonality;

} OilPjlSettings;


/* Info held about a job */
typedef struct OilPjlJobInfo
{
  uint32 uJobId;
  uint8 szJobName[ MAX_PJL_JOBNAME_LEN + 1 ];
  int32 fAllPagesOutput;
  int32 nEojPages;

  struct OilPjlJobInfo * pNext;

} OilPjlJobInfo;


/* Context for OIL PJL */
typedef struct OilPjlContext
{
  PjlParserContext * pParserContext;

  int32 eCurrentPDL;

  int32 ePJLCommentPDL;               /* Any PDL determined by parsing PJL COMMENTs */

  PMS_TyJob * pUserDefaults;
  PMS_TyJob * pCurrentEnvironment;

  OilPjlSettings currentEnvironment;

  uint32 nJobPassword;

  int32 deviceStatusValue;
  int32 jobStatusValue;
  int32 pageStatusValue;
  int32 timedStatusValue;

  OilPjlJobInfo * pJobInfo;

  int32 nJobStart;
  int32 nJobEnd;
  /* the following are used for mobile print app support */
  int32 nColorMode;
  int32 nPaperSize;
  int32 nCopies;
  char szFileName[ MAX_PJL_JOBNAME_LEN + 1];

} OilPjlContext ;

static OilPjlContext gOilPjlContext =
{
  NULL,              /* pParserContext */

  eUnknown,          /* eCurrentPDL */

  eUnknown,          /* ePJLCommentPDL */

  NULL,              /* pUserDefaults */
  NULL,              /* pCurrentEnvironment */
  
  {                  /* currentEnvironment */
    eUnknown         /* ePersonality */
  },

  0,                 /* nJobPassword */

  eDeviceStatusOff,  /* deviceStatusValue */
  eoilpjl_Off,              /* jobStatusValue */
  eoilpjl_Off,              /* pageStatusValue */
  0,                 /* timedStatusValue */

  NULL,              /* pJobInfo */

  0,                 /* nJobStart */
  0                  /* nJobEnd */
};

/* Other forward declarations */
static int32 Oil_PCL5JobIsPCL5e( void );
static int32 OIL_GetPMSPersonality( void );
static void OIL_SetPMSPersonality( int32 ePersonality );
static uint32 OIL_GetPMSPjlPassword( void );
static void OIL_SetPMSPjlPassword( uint32 password );
static int32 OIL_InSecureJob( void );

/**
 * \brief Write a message to the backchannel.
 *
 * \param[in] pText Text to report.
 *
 * \param[in] cbLen Length of text.
 *
 * \return Returns one of the error enumeration values. This function always returns \c eNoError.
 */
static int32 OIL_PjlReport( uint8 * pText, size_t cbLen )
{
  OIL_BackChannelWrite( pText, (int) cbLen );

  return eNoError;
}


/**
 * \brief Display text on the control panel.
 *
 * \param[in] pOption A PJL option structure which holds the text to display in its \c value field.
 *
 * \return Returns one of the error enumeration values. This function always returns \c eNoError.
 */
static int32 OIL_PjlDisplay( PjlOption * pOption )
{
  UNUSED_PARAM( PjlOption *, pOption );

  return eNoError;
}


/**
 * \brief Look up a status category by name.
 *
 * \param[in] pzName Name of status category.
 *
 * \return Returns a pointer to the structure representing the required status category,
           or NULL if no such status category is found.
 */
static PjlEnvVar * OIL_LookupStatusCategory( uint8 * pzName )
{
  PjlEnvVar * pEnvVar = NULL;

  int32 i;

  for( i = 0; i < N_OIL_PJL_STATUS_CATS; i++ )
  {
    if( strcmp( (const char *) pzName, (const char *) gOilPjlStatusCategories[ i ].pzName ) == 0 )
    {
      pEnvVar = &gOilPjlStatusCategories[ i ];
      break;
    }
  }

  return pEnvVar;
}


/**
 * \brief Look up an environment variable by name
 *
 * \param[in] pzName Name of environment variable.
 *
 * \return Returns a pointer to the structure representing the required environment variable.  
           This should never be NULL, thanks to the PJL parser's validation.
 */
static PjlEnvVar * OIL_LookupEnvironmentVariable( uint8 * pzName )
{
  PjlEnvVar * pEnvVar = NULL;

  int32 i;

  for( i = 0; i < N_OIL_PJL_ENV_VARS; i++ )
  {
    if( strcmp( (const char *) pzName, (const char *) gOilPjlEnvVars[ i ].pzName ) == 0 )
    {
      pEnvVar = &gOilPjlEnvVars[ i ];
      break;
    }
  }

  HQASSERT(pEnvVar != NULL , "No environment variable");

  return pEnvVar;
}

/**
 * \brief Retrieve a string representation of the value of an environment variable.
 *
 * This function can be directed to return either the current value of the variable 
 * or the user default value by setting \c fGetDefault appropriately.
 * \param[in] pOption Option holding the name of the environment variable.
 * \param[in] fGetDefault Set to TRUE to retrieve the user default value, or
 *        FALSE to retrieve the current value.
 *
 * \param[out] pValue A buffer to receive the string representation of the variable's value.
 * \param[in] cbLen The length of pValue. Unused in this implementation.
 * \return One of the error enumeration values.  This implementation always returns \c eNoError.
 */
static int32 OIL_GetEnvironmentVariable( PjlOption * pOption, int fGetDefault, uint8 * pValue, size_t cbLen )
{
  int32 result = eNoError;

  PjlEnvVar * pEnvVar;
  OilEnvVar * pOilEnvVar;
  PjlValue    value;

  UNUSED_PARAM( size_t, cbLen );

  pEnvVar = OIL_LookupEnvironmentVariable( pOption->pzName );
  pOilEnvVar = (OilEnvVar *) pEnvVar->pPrivate;
  (pOilEnvVar->pGetFn)( &value, fGetDefault );

  /* Note that value.eType may not match pEnvVar->eType,
   * e.g. PASSWORD is set as an int but reported as an alphanumeric.
   */

  switch( value.eType )
  {
  case eValueString:
  case eValueAlphanumeric:
    strcpy( (char *) pValue, (char *) value.u.pzValue );
    break;
  case eValueInt:
    sprintf( (char *) pValue, "%d", value.u.iValue );
    break;
  case eValueDouble:
    sprintf( (char *) pValue, "%.2f", value.u.dValue );
    break;
  default:
    HQFAIL("Invalid value type");
    break;
  }

  return result;
}

/**
 * \brief Set an environment variable to a new value.
 *
 * This function can be directed to change either the current value of the variable 
 * or the user default value of the variable, by setting \c fSetDefault appropriately.
 * \param pOption Option holding name and new value of environment variable.
 *
 * \param fSetDefault Set to TRUE to change the user default, or FALSE to change
 *        the current environment value.
 *
 * \return Returns \c eNoError if the variable is successfully set to the new value, or
           one of the error enumeration values if a problem occurs.
 */
static int32 OIL_SetEnvironmentVariable( PjlOption * pOption, int fSetDefault )
{
  int32 result = eNoError;

  PjlEnvVar * pEnvVar;
  OilEnvVar * pOilEnvVar;

  pEnvVar = OIL_LookupEnvironmentVariable( pOption->pzName );
  pOilEnvVar = (OilEnvVar *) pEnvVar->pPrivate;

  if( pOilEnvVar->pSetFn != NULL )
  {
    result = (pOilEnvVar->pSetFn)( &pOption->value, fSetDefault );
  }
  else
  {
    /* Variable is read only */
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eReadOnlyVariable );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


/**
 * \brief Retrieve a string that corresponds to an integer in a particular value/string map set.
 * \param[in]  pEnumMappedValue The set of mapped values to use.
 * \param[in]  value  The integer to look up.
 * \param[out] pValue A PjlValue to receive the value.
 */
static void OIL_GetMappedValue( OilEnvVarEnumMappedValue * pEnumMappedValue, int32 value, PjlValue * pValue )
{
  OilMappedValue * pMappedValues = pEnumMappedValue->pMappedValues;
  int32 i;

  pValue->eType = eValueAlphanumeric;

  for( i = 0; i < pEnumMappedValue->nValues; i++ )
  {
    if( value == pMappedValues[ i ].value )
    {
      pValue->u.pzValue = pMappedValues[ i ].pValue;
      return;
    }
  }

  HQFAIL("Unknown value");  /* Unknown value */
}


/**
 * \brief Retrieve an integer that corresponds to a string in a particular value/string map set.
 * \param[in] pEnumMappedValue The set of mapped values to use.
 * \param[in] pValue The string to look up.
 * \return Returns a poitner to the OilMappedValue corresponding to the string in pValue, or NULL 
           if no matching entry was found in pEnumMappedValue.
 */
static OilMappedValue * OIL_LookupMappedString( OilEnvVarEnumMappedValue * pEnumMappedValue, uint8 * pValue )
{
  OilMappedValue * pMappedValues = pEnumMappedValue->pMappedValues;
  OilMappedValue * pMappedValue = NULL;
  int32 i;

  for( i = 0; i < pEnumMappedValue->nValues; i++ )
  {
    if( strcmp( (const char *) pValue, (const char *) pMappedValues[ i ].pValue ) == 0 )
    {
      pMappedValue = &pMappedValues[ i ];
      break;
    }
  }

  return pMappedValue;
}


/**
 * \brief Memory allocator function for PjlParserInit.
 * 
 * This function allocates the specified amount of memory from the job memory pool and, 
 * optionally, initializes it by setting all the bytes to zero.  
 * \param[in]   cbSize      The size, in bytes, of the required memory allocation.
 * \param[in]   fZero       Set to TRUE if the memory should be initialized to all bytes zero, or
                            FALSE if no initialization is required.
 * \return Returns a pointer to the newly allocated memory, or NULL 
           if the allocation fails.
 */
static void * OIL_PjlMemAllocFn( size_t cbSize, int32 fZero )
{
  void * pPtr = OIL_malloc( OILMemoryPoolJob, OIL_MemBlock, cbSize );

  if( pPtr != NULL )
  {
    if( fZero )
    {
      memset( pPtr, 0, cbSize );
    }
  }

  return pPtr;
}


/**
 * \brief Memory de-allocation function.
 *
 * This function releases the memory back to the job memory pool.
 * \param[in]   pMem    Pointer to the memory to be freed.
 */
static void OIL_PjlMemFreeFn( void * pMem )
{
  OIL_free( OILMemoryPoolJob, pMem );
}


/**
 * \brief Create a PJL job information structure for the supplied job ID.
 *
 * Once created, the structure is added to the head of the 
 * list of job information structures held in the PJL context.
 * \param[in]   uJobId      The job ID.
 * \return      Returns a pointer to the newly-created information structure, or NULL
                if the create fails.
 */
static OilPjlJobInfo * OIL_CreateJobInfo( uint32 uJobId )
{
  OilPjlJobInfo * pJobInfo = OIL_PjlMemAllocFn( sizeof( OilPjlJobInfo ), TRUE );

  if( pJobInfo != NULL )
  {
    pJobInfo->uJobId = uJobId;

    /* Add to head of list */
    pJobInfo->pNext = gOilPjlContext.pJobInfo;
    gOilPjlContext.pJobInfo = pJobInfo;
  }

  return pJobInfo;
}


/**
 * \brief Find the PJL job information structure that is related to a given job ID.
 * \param[in]   uJobId      The job ID.
 * \return      Returns a pointer to the retrieved information structure, or NULL
                if no matching job information structure is found.
 */
static OilPjlJobInfo * OIL_GetJobInfo( uint32 uJobId )
{
  OilPjlJobInfo * pJobInfo = gOilPjlContext.pJobInfo;

  while( pJobInfo != NULL )
  {
    if( pJobInfo->uJobId == uJobId )
    {
      break;
    }

    pJobInfo = pJobInfo->pNext;
  }

  return pJobInfo;
}


/**
 * \brief Free the PJL job information structure for the job ID.
 * 
 * This function can also be called to free all PJL job information structures if an
 * error occurs and cleanup is required.  In normal usage, \c bFreeAll will be 
 * set to FALSE and only the structure pertinent to the specified job will be freed.
 *
 * For cleanup purposes, bFreeAll can be set to TRUE, which will cause all PJL job 
 * information structures to be freed.
 * \param[in]   uJobId      The job ID.
 * \param[in]   bFreeAll    Set to FALSE if only this job's data should be deleted, or
                            TRUE if data for all jobs should be freed.
 */
static void OIL_FreeJobInfo( uint32 uJobId, int32 bFreeAll )
{
  OilPjlJobInfo * pJobInfo = gOilPjlContext.pJobInfo;
  OilPjlJobInfo * pPrevJobInfo = NULL;

  while( pJobInfo != NULL )
  {
    if( bFreeAll || pJobInfo->uJobId == uJobId )
    {
      /* Remove from list */
      OilPjlJobInfo * pNextJobInfo = pJobInfo->pNext;

      if( pPrevJobInfo != NULL )
      {
        pPrevJobInfo->pNext = pNextJobInfo;
      }
      else
      {
        gOilPjlContext.pJobInfo = pNextJobInfo;
      }

      OIL_PjlMemFreeFn( pJobInfo );

      if( !bFreeAll )
      {
        break;
      }

      pJobInfo = pNextJobInfo;
    }
    else
    {
      pPrevJobInfo = pJobInfo;
      pJobInfo = pJobInfo->pNext;
    }
  }
}


/**
 * \brief Actions a PJL command.
 *
 * This function looks up the command in the list of supported PJL commands and retrieves
 * any function associated with it.  If a function is found, it is invoked, passing in the
 * command.
 * \param[in]   pCommand    The PJL command to be actioned.
 * \return      Returns the result of executing the function registered against the command, 
                if one exists.  If the command is recognised but has no registered function, this
                function returns eNoError.  If the command is not recognised, this function returns
                eUnsupportedCommand.
 */
static int32 OIL_PjlCommandCallback( PjlCommand * pCommand )
{
  int32 result = eUnsupportedCommand;

  int32 i;

  GG_SHOW(GG_SHOW_PJL, "OIL_PjlCommandCallback: %s\n", pCommand->pzName);

  for( i = 0; i < N_OIL_PJL_COMMANDS; i++ )
  {
    if( strcmp( (const char *) gOilPjlCommands[ i ].pzName, (const char *) pCommand->pzName ) == 0 )
    {
      if( gOilPjlCommands[ i ].pFn )
      {
        result = ( gOilPjlCommands[ i ].pFn )( pCommand );
      }
      else
      {
        /* Nothing to do */
        result = eNoError; 
      }

      break;
    }
  }

  gOilPjlContext.eCurrentPDL = ePJL;

  return result;
}


/**
 * \brief Error callback function.
 *
 * If the OIL PJL context is set to produce verbose errors, this function will construct an
 * error message which is then written to the PMS backchannel.
 * \param[in]   eError  The error code being reported.
 */
static void OIL_PjlErrorCallback( int32 eError )
{
  GG_SHOW(GG_SHOW_PJL, "OIL_PjlErrorCallback: %d\n", eError);

  if( gOilPjlContext.deviceStatusValue == eDeviceStatusVerbose )
  {
    uint8 aBuffer[ 50 ];
    int   cbLen;

    cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS DEVICE\r\nCODE=%d\r\n\f", eError );
    HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

    OIL_PjlReport( aBuffer, (size_t) cbLen );
  }
}


/**
 * \brief Initialize the PJL parser.
 *
 * This function initializes the OIL PJL context with the current personality adopted
 * by the PMS and passes pointers to the crucial OIL PJL callback functions into the
 * core PJL parser, along with the personality information and the environment variable
 * information.
 */
void OIL_PjlInit( void )
{
  gOilPjlContext.currentEnvironment.ePersonality = OIL_GetPMSPersonality();

  gOilPjlContext.pParserContext = PjlParserInit(
    OIL_PjlMemAllocFn, OIL_PjlMemFreeFn, OIL_PjlCommandCallback, OIL_PjlErrorCallback,
    gOilPjlContext.currentEnvironment.ePersonality,
    gOilPjlEnvVars, N_OIL_PJL_ENV_VARS );
}


/**
 * \brief Set up the user default settings and current environment variable settings.
 *
 * This function uses the settings of the current job to configure the PJL context.  The
 * environment variables are set to those of the job, and a copy of the settings is 
 * taken and held as the user default settings for this job.
 * \param[in]   pms_pstJobCurrent   A structure containing the settings for the current job.
 */
void OIL_PjlSetEnvironment( PMS_TyJob * pms_pstJobCurrent )
{
  PMS_TyJob * pms_pstJobUserDefaults;
  PMS_TyJob * pJobUserDefaultsCopy;
  PMS_TyPaperInfo * pPaperInfo = NULL;

  /* Take copy of PMS job defaults to use as user defaults environment */
  PMS_GetJobSettings( &pms_pstJobUserDefaults );
  pJobUserDefaultsCopy = OIL_malloc( OILMemoryPoolJob, OIL_MemBlock, sizeof(PMS_TyJob) );
  memcpy( pJobUserDefaultsCopy, pms_pstJobUserDefaults, sizeof(PMS_TyJob) );

  HQASSERT(gOilPjlContext.pUserDefaults == NULL , "No user defaults");
  gOilPjlContext.pUserDefaults = pJobUserDefaultsCopy;
  if(gOilPjlContext.nColorMode > 0)
  {
    pms_pstJobCurrent->tDefaultJobMedia.ePaperSize = gOilPjlContext.nPaperSize;
    PMS_GetPaperInfo(gOilPjlContext.nPaperSize, &pPaperInfo);
    pms_pstJobCurrent->tDefaultJobMedia.dWidth = pPaperInfo->dWidth;
    pms_pstJobCurrent->tDefaultJobMedia.dHeight = pPaperInfo->dHeight;
    pms_pstJobCurrent->uCopyCount = gOilPjlContext.nCopies;
    if(gOilPjlContext.nColorMode == 1)
      pms_pstJobCurrent->eColorMode = PMS_Mono;
    else
      pms_pstJobCurrent->eColorMode = PMS_RGB_Composite;
  }
  gOilPjlContext.pCurrentEnvironment = pms_pstJobCurrent;
}

/**
 * \brief Retrieve the current environment settings.
 * \return      Returns a pointer to the current environment settings as stored in the
                PJL context.
 */
PMS_TyJob *OIL_PjlGetEnvironment( void )
{
  return gOilPjlContext.pCurrentEnvironment;
}

/**
 * \brief Clear the user default settings.
 */
void OIL_PjlClearEnvironment( void )
{
  OIL_free( OILMemoryPoolJob, gOilPjlContext.pUserDefaults );
  gOilPjlContext.pUserDefaults = NULL;

  gOilPjlContext.ePJLCommentPDL = eUnknown;
}


/**
 * \brief Called when a job fails.
 *
 * This simple implementation is a wrapper to OIL_FreeJobInfo().
 * \param uJobID The ID of the failed job.
 */
void OIL_PjlJobFailed( unsigned int uJobID )
{
  OIL_FreeJobInfo( uJobID, TRUE );
}


/**
 * \brief Pass data to the PJL parser for parsing.
 *
 * \param[in]  pData Pointer to data to parse.
 * \param[in]  cbDataLen Length of data.
 * \param[out] pcbDataConsumed Filled in with amount of data consumed by parser.
 * \return     Returns one of the PDL enumeration values, or \c ePJL or \c eNeedMoreData if the PDL has
               not yet been determined.  If the function returns \c ePJL or \c eNeedMoreData, it should
               be called again, passing in any data not consumed by the previous call.
 */
int OIL_PjlParseData( unsigned char * pData, size_t cbDataLen, size_t * pcbDataConsumed )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");
  HQASSERT(gOilPjlContext.pUserDefaults , "No user defaults");
  HQASSERT(gOilPjlContext.pCurrentEnvironment , "No current environment");

  gOilPjlContext.eCurrentPDL = PjlParserParseData( gOilPjlContext.pParserContext, pData, cbDataLen, pcbDataConsumed );

  /* For PCL5 jobs check if the PJL suggests that the job is actually PCL5e */
  if( gOilPjlContext.eCurrentPDL == ePCL5c && Oil_PCL5JobIsPCL5e() )
  {
    gOilPjlContext.eCurrentPDL = ePCL5e;
  }

  return gOilPjlContext.eCurrentPDL;
}


/**
 * \brief Shut down the PJL parser.
 */
void OIL_PjlExit( void )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  PjlParserExit( gOilPjlContext.pParserContext );
}


/**
 * \brief Send an unsolicted device status message.
 * \param[in]     statusCode Status code to report.
 * \param[in]     pzDisplay  Text from any control panel to report (pass NULL if not required).
 * \param[in]     fOnline    Set to a non-zero if printer is online, or zero if it is offline.
 */
void OIL_PjlReportDeviceStatus( int statusCode, unsigned char * pzDisplay, int fOnline )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  if( gOilPjlContext.deviceStatusValue != eDeviceStatusOff )
  {
    uint8 aBuffer[ 200 ];
    int   cbLen;

    cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS DEVICE\r\nCODE=%d\r\n", statusCode );
    if( pzDisplay != NULL )
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, "DISPLAY=\"%s\"\r\n", pzDisplay );
    }
    cbLen += sprintf( (char *) aBuffer + cbLen, "ONLINE=%s\r\n\f", fOnline ? "TRUE" : "FALSE" );
    HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

    OIL_PjlReport( aBuffer, (size_t) cbLen );
  }
}


/**
 * \brief Send an unsolicted job start status message.
 * \param[in]     pJobInfo   Status code to report.
 */
static void OIL_PjlReportJobStart( OilPjlJobInfo * pJobInfo )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  if( gOilPjlContext.jobStatusValue == eoilpjl_On )
  {
    uint8 aBuffer[ 200 ];
    int   cbLen;

    cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS JOB\r\nSTART\r\n" );
    if( pJobInfo->szJobName[ 0 ] != '\0' )
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, "NAME=\"%s\"\r\n", pJobInfo->szJobName );
    }
    cbLen += sprintf( (char *) aBuffer + cbLen, "\f" );
    HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

    OIL_PjlReport( aBuffer, (size_t) cbLen );
  }
}


/**
 * \brief Send an unsolicted job cancel status message.
 * \param[in]   uJobId  The ID of the job being cancelled.
 */
void OIL_PjlReportJobCancel( unsigned int uJobId )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  if( gOilPjlContext.jobStatusValue == eoilpjl_On )
  {
    OilPjlJobInfo * pJobInfo;
    uint8           aBuffer[ 200 ];
    int             cbLen;

    pJobInfo = OIL_GetJobInfo( uJobId );

    cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS JOB\r\nCANCELED\r\n" );
    if( pJobInfo != NULL && pJobInfo->szJobName[ 0 ] != '\0' )
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, "NAME=\"%s\"\r\n", pJobInfo->szJobName );
    }
    cbLen += sprintf( (char *) aBuffer + cbLen, "\f" );
    HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

    OIL_PjlReport( aBuffer, (size_t) cbLen );
  }
}


/**
 * \brief Send an unsolicted job end status message.
 *
 * \param[in] pJob  The job which is ending.
 */
void OIL_PjlReportEoj( OIL_TyJob * pJob )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  if( pJob->bEojReceived )
  {
    /* We've already received the EOJ command so issue the EOJ
     * report now.
     */
    if( gOilPjlContext.jobStatusValue == eoilpjl_On )
    {
      uint8 aBuffer[ 200 ];
      int   cbLen;

      cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS JOB\r\nEND\r\n" );
      if( pJob->szEojJobName[ 0 ] != '\0' )
      {
        cbLen += sprintf( (char *) aBuffer + cbLen, "NAME=\"%s\"\r\n", pJob->szEojJobName );
      }
      cbLen += sprintf( (char *) aBuffer + cbLen, "PAGES=%d\r\n\f", pJob->uPagesPrinted );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

      OIL_PjlReport( aBuffer, (size_t) cbLen );
    }
  }
  else
  {
    /* We've not received the EOJ command yet so just note
     * that all pages haves now been output.
     */
    OilPjlJobInfo * pJobInfo = OIL_GetJobInfo( pJob->uJobId );

    if( pJobInfo != NULL )
    {
      pJobInfo->fAllPagesOutput = TRUE;
      pJobInfo->nEojPages = pJob->uPagesPrinted;
    }
  }
}


/**
 * \brief Send an unsolicted page status message.
 *
 * \param[in] nPages Number of pages to report.
 */
void OIL_PjlReportPageStatus( int nPages )
{
  HQASSERT(gOilPjlContext.pParserContext , "No parser context");

  if( gOilPjlContext.pageStatusValue == eoilpjl_On )
  {
    uint8 aBuffer[ 50 ];
    int   cbLen;

    cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS PAGE\r\n%d\r\n\f", nPages );
    HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

    OIL_PjlReport( aBuffer, (size_t) cbLen );
  }
}


/**
 * \brief Returns the value of the START parameter
 * passed to the current JOB command (if any).
 *
 * \return Value of the START parameter, or 0 if none.
 */
int OIL_PjlGetJobStart( void )
{
  return gOilPjlContext.nJobStart;
}


/**
 * \brief Returns the value of the END parameter
 * passed to the current JOB command (if any).
 *
 * \return Value of the END parameter, or 0 if none.
 */
int OIL_PjlGetJobEnd( void )
{
  return gOilPjlContext.nJobEnd;
}


/**
 * \brief Indicate whether a page should be output, based on the 
 * START and END values passed to the JOB command.
 * \param[in]   nPage   The current page number.
 *
 * \param[in]   pJob The job being output.
 *
 * \return      Returns TRUE if the page should be output, or FALSE 
                otherwise.
 */
int OIL_PjlShouldOutputPage( int nPage, OIL_TyJob * pJob )
{
  int fOutput = TRUE;

  int fDuplex = pJob->bDuplex;

  if( pJob->uJobStart != 0 )
  {
    int nFirstPage = pJob->uJobStart;

    if( fDuplex && ( nFirstPage % 2 ) == 0 )
    {
      /* Include page before if even and duplex */
      nFirstPage--;
    }

    if( nPage < nFirstPage )
    {
      fOutput = FALSE;
    }
  }

  if( pJob->uJobEnd != 0 && fOutput )
  {
    int nLastPage = pJob->uJobEnd;

    if( fDuplex && ( nLastPage % 2 ) == 1 )
    {
      /* Include page after if odd and duplex */
      nLastPage++;
    }

    if( nPage > nLastPage )
    {
      fOutput = FALSE;
    }
  }

  return fOutput;
}



/**
 * \brief Maps a PJL PDL enumeration value to its
 * OIL equivalent.
 *
 * \param[in] ePJL_PDL A PJL PDL enumeration value.
 *
 * \return Returns the equivalent OIL_PDL_xxx enumeration value.
 */
int OIL_PjlMapPdlValue( int ePJL_PDL )
{
  int eOIL_PDL = OIL_PDL_Unknown;

  switch( ePJL_PDL )
  {
  case eUnknown:
    eOIL_PDL = OIL_PDL_Unknown;
    break;
  case ePostScript:
    eOIL_PDL = OIL_PDL_PS;
    break;
  case eZIP:
    eOIL_PDL = OIL_PDL_XPS;
    break;
  case ePDF:
    eOIL_PDL = OIL_PDL_PDF;
    break;
  case ePCL5c:
    eOIL_PDL = OIL_PDL_PCL5c;
    break;
  case ePCL5e:
    eOIL_PDL = OIL_PDL_PCL5e;
    break;
  case ePCLXL:
    eOIL_PDL = OIL_PDL_PCLXL;
    break;
  case eImage:
    eOIL_PDL = OIL_IMG;
    break;
  default:
    HQFAIL("Unknown PDL selection");
    break;
  }

  return eOIL_PDL;
}



/**
 * \brief Reports an environment variable setting.
 *
 * \param[in] pCommand Pointer to command.
 *
 * \param[in] pzValue  Zero-terminated string representaion of value of setting.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlReportSetting( PjlCommand * pCommand, uint8 * pzValue )
{
  int32 result = eNoError;

  uint8 aModifier[ 100 ];
  uint8 aBuffer[ 200 ];
  int   cbLen;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "No command options");

  if( pCommand->pModifier )
  {
    HQASSERT(pCommand->pModifier->value.eType == eValueAlphanumeric ,
             "Invalid value type");

    cbLen = sprintf( (char *) aModifier, "%s:%s ", pCommand->pModifier->pzName, pCommand->pModifier->value.u.pzValue );
    HQASSERT(cbLen < sizeof( aModifier ) , "Buffer overflow");
  }
  else
  {
    aModifier[ 0 ] = '\0';
  }

  cbLen = sprintf( (char *) aBuffer, "@PJL %s %s%s\r\n%s\r\n\f",
    pCommand->pzName, aModifier, pCommand->pOptions->pzName, pzValue );
  HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

  result = OIL_PjlReport( aBuffer, cbLen );

  return result;
}


/**
 * \brief Report environment variable settings for INFO command.
 * Common code for INFO VARIABLES and INFO USTATUS reporting.
 *
 * \param[in] pzCategory Name of INFO category.
 *
 * \param[in] aEnvVar Array of environment variables.
 *
 * \param[in] nEnvVars Number of environment variables in array.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlEnvVarInfo( uint8 * pzCategory, PjlEnvVar * aEnvVar, int32 nEnvVars )
{
  int32 result = eNoError;

  uint8 aBuffer[ 2048 ];
  int   cbLen;
  int32 i;

  cbLen = sprintf( (char *) aBuffer, "@PJL INFO %s\r\n", pzCategory );

  for( i = 0; i < nEnvVars; i++ )
  {
    PjlEnvVar * pEnvVar;
    OilEnvVar * pOilEnvVar;
    PjlValue    value;
    uint8     * pzReadOnly;

    pEnvVar = &aEnvVar[ i ];
    pOilEnvVar = (OilEnvVar *) pEnvVar->pPrivate;

    pzReadOnly = (uint8 *) ( ( pOilEnvVar->pSetFn == NULL ) ? " READONLY" : "" ) ;

    (pOilEnvVar->pGetFn)( &value, FALSE );

    switch( value.eType )
    {
    case eValueAlphanumeric:
    case eValueString:
      cbLen += sprintf( (char *) aBuffer + cbLen, "%s=%s", pEnvVar->pzName, value.u.pzValue );
      break;
    case eValueInt:
      cbLen += sprintf( (char *) aBuffer + cbLen, "%s=%d", pEnvVar->pzName, value.u.iValue );
      break;
    case eValueDouble:
      cbLen += sprintf( (char *) aBuffer + cbLen, "%s=%.2f", pEnvVar->pzName, value.u.dValue );
      break;
    default:
      HQFAIL("Invalid value type");
      break;
    }

    switch( pOilEnvVar->eType )
    {
    case eRangeInt:
    {
      OilEnvVarRangeInt * pRange = (OilEnvVarRangeInt *) pOilEnvVar->pValues;

      cbLen += sprintf( (char *) aBuffer + cbLen, " [2 RANGE%s]\r\n\t%d\r\n\t%d\r\n",
        pzReadOnly, pRange->min, pRange->max );
      break;
    }
    case eRangeDouble:
    {
      OilEnvVarRangeDouble * pRange = (OilEnvVarRangeDouble *) pOilEnvVar->pValues;

      cbLen += sprintf( (char *) aBuffer + cbLen, " [2 RANGE%s]\r\n\t%.2f\r\n\t%.2f\r\n",
        pzReadOnly, pRange->min, pRange->max );
      break;
    }
    case eEnumInt:
    {
      int32 j;
      OilEnvVarEnumInt * pEnum = (OilEnvVarEnumInt *) pOilEnvVar->pValues;

      cbLen += sprintf( (char *) aBuffer + cbLen, " [%d ENUMERATED%s]\r\n", pEnum->nValues, pzReadOnly );

      for( j = 0; j < pEnum->nValues; j++ )
      {
        cbLen += sprintf( (char *) aBuffer + cbLen, "\t%d\r\n", pEnum->pValues[ j ] );
      }
      break;
    }
    case eEnumString:
    {
      int32 j;
      OilEnvVarEnumString * pEnum = (OilEnvVarEnumString *) pOilEnvVar->pValues;

      cbLen += sprintf( (char *) aBuffer + cbLen, " [%d ENUMERATED%s]\r\n", pEnum->nValues, pzReadOnly );

      for( j = 0; j < pEnum->nValues; j++ )
      {
        cbLen += sprintf( (char *) aBuffer + cbLen, "\t%s\r\n", pEnum->ppValues[ j ] );
      }
      break;
    }
    case eString:
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, " [1 STRING%s]\r\n\t%s\r\n", pzReadOnly, value.u.pzValue );
      break;
    }
    case eEnumMappedValue:
    {
      int32 j;
      OilEnvVarEnumMappedValue * pEnum = (OilEnvVarEnumMappedValue *) pOilEnvVar->pValues;

      cbLen += sprintf( (char *) aBuffer + cbLen, " [%d ENUMERATED%s]\r\n", pEnum->nValues, pzReadOnly );

      for( j = 0; j < pEnum->nValues; j++ )
      {
        cbLen += sprintf( (char *) aBuffer + cbLen, "\t%s\r\n", pEnum->pMappedValues[ j ].pValue );
      }
      break;
    }
    default:
      HQFAIL("Invalid value type");
      break;
    }
  }

  HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

  cbLen += sprintf( (char *) aBuffer + cbLen, "\f" );

  result = OIL_PjlReport( aBuffer, cbLen );

  return result;
}

/**
 * \brief Check if the PJL suggests that the job is actually PCL5e.
 *
 * \return Returns TRUE if the job appears to be PCL5e, FALSE otherwise.
 */
static int32 Oil_PCL5JobIsPCL5e( void )
{
  int32 fIsPCL5e = FALSE;

  HQASSERT(gOilPjlContext.eCurrentPDL == ePCL5c , "Not PCL5c");

  if( gOilPjlContext.pCurrentEnvironment->eRenderModel == PMS_RenderModel_G1 )
  {
    fIsPCL5e = TRUE;
  }
  else if( gOilPjlContext.ePJLCommentPDL == ePCL5e )
  {
    /* PJL COMMENT suggested that job is PCL5e */
    fIsPCL5e = TRUE;
  }

  return fIsPCL5e;
}

/**
 * \brief Look up the default personality of the PMS
 *
 * \return Returns the personality held by the PMS.
 */
static int32 OIL_GetPMSPersonality( void )
{
  int32 ePersonality = eUnknown;

  PMS_TySystem pmsSysInfo;

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  switch( pmsSysInfo.ePersonality )
  {
  case PMS_PERSONALITY_NONE:
    ePersonality = eNone;
    break;
  case PMS_PERSONALITY_AUTO:
    ePersonality = eAutoPDL;
    break;
  case PMS_PERSONALITY_PS:
    ePersonality = ePostScript;
    break;
  case PMS_PERSONALITY_PDF:
    ePersonality = ePDF;
    break;
  case PMS_PERSONALITY_XPS:
    ePersonality = eZIP;
    break;
  case PMS_PERSONALITY_PCL5:
    ePersonality = ePCL5c;
    break;
  case PMS_PERSONALITY_PCLXL:
    ePersonality = ePCLXL;
    break;
  default:
    HQFAIL("Invalid PMS personality");
  }

  return ePersonality;
}

/**
 * \brief Set the PMS default personality.
 *
 * \param[in] ePersonality  The personality value.
 */
static void OIL_SetPMSPersonality( int32 ePersonality )
{
  PMS_ePersonality ePmsPersonality;
  PMS_TySystem     pmsSysInfo;

  switch( ePersonality )
  {
  case eNone:
    ePmsPersonality = PMS_PERSONALITY_NONE;
    break;
  case eAutoPDL:
    ePmsPersonality = PMS_PERSONALITY_AUTO;
    break;
  case ePostScript:
    ePmsPersonality = PMS_PERSONALITY_PS;
    break;
  case ePDF:
    ePmsPersonality = PMS_PERSONALITY_PDF;
    break;
  case eZIP:
    ePmsPersonality = PMS_PERSONALITY_XPS;
    break;
  case ePCL5c:
  case ePCL5e:
    ePmsPersonality = PMS_PERSONALITY_PCL5;
    break;
  case ePCLXL:
    ePmsPersonality = PMS_PERSONALITY_PCLXL;
    break;
  default:
    HQFAIL("Unknown PDL");
    ePmsPersonality = PMS_PERSONALITY_NONE;
  }

  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  if( pmsSysInfo.ePersonality != ePmsPersonality )
  {
    pmsSysInfo.ePersonality = ePmsPersonality;

    PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  }
}

/**
 * \brief Look up the PJL password.
 *
 * \return The password held by PMS.
 */
static uint32 OIL_GetPMSPjlPassword( void )
{
  PMS_TySystem pmsSysInfo;

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  return pmsSysInfo.uPjlPassword;
}

/**
 * \brief Set the PJL password.
 *
 * \param[in] password The password value.
 */
static void OIL_SetPMSPjlPassword( uint32 password )
{
  PMS_TySystem pmsSysInfo;

  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  if( pmsSysInfo.uPjlPassword != password )
  {
    pmsSysInfo.uPjlPassword = password;

    PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  }
}

/**
 * \brief Report the current job is a secure job.
 *
 * \return TRUE if the current job is a secure job, FALSE otherwise.
 */
static int32 OIL_InSecureJob( void )
{
  int32 fSecureJob = FALSE;

  uint32 password = OIL_GetPMSPjlPassword();

  /* Secure if password is 0 (regardless of any password specified for job,
   * or if job's password matches password.
   */
  if( password == 0 || gOilPjlContext.nJobPassword == password )
  {
    fSecureJob = TRUE;
  }

  return fSecureJob;
}

/**
 * \brief Validate a file system volume name.
 *
 * \param[in]  pzVolume The volume name. This should be a sequence of digits followed by a colon (:) and
                        then either a null terminator or a backslash (\\).
 *
 * \param[out] ppNext   If not NULL, and the volume name is valid, this is 
                        set to point to the next character in \c pzVolume after the volume name.
 *
 * \return Returns \c eNoError if the volume name is valid, otherwise one of the error enumeration values.
 */
static int32 OIL_ValidateVolumeName( uint8 * pzVolume, uint8 ** ppNext )
{
  int32 result = eNoError;

  /* Validate volume name, which should be a number followed by : */
  if( *pzVolume == '\0' )
  {
    result = eNoFilename;
  }
  else
  {
    int32 nDigits = 0;
    int32 nVolume = 0;

    while( pzVolume[ nDigits ] >= '0' && pzVolume[ nDigits ] <= '9' )
    {
      nVolume *= 10;
      nVolume += pzVolume[ nDigits ] - '0';
      nDigits++;
    }

    if( nDigits == 0 )
    {
      /* No digits */
      result = eVolumeNameOutOfRange;
    }
    else if( pzVolume[ nDigits ] != ':' )
    {
      /* Digits, but not followed by : */
      result = eVolumeNameOutOfRange;
    }
    else if( pzVolume[ nDigits + 1 ] != '\0' && pzVolume[ nDigits + 1 ] != '\\' )
    {
      /* Digits, but not followed by just : */
      result = eIllegalName;
    }
    else if( ppNext != NULL )
    {
      *ppNext = pzVolume + nDigits + 1;
    }
  }

  return result;
}


/**
 * \brief Validate a file system file name.
 *
 * This function checks that the volume name at the start of a file name is valid, that it does not
 * contain too many elements, and that the elements that make up the name are themselves valid. An
 * element is a string of up to 100 characters, restricted only in that neither the first character
 * nor the last character in the element can be a space or character 229.
 *
 * The maximum number of elements allowed in a file name is nine, and the total length of the filename
 * cannot be greater than 255.
 * \param[in] pzFile The file name.
 * \return Returns \c eNoError if the file name is valid, otherwise one of the error enumeration values.
 */
static int32 OIL_ValidateFileName( uint8 * pzFile )
{
  int32 result = eNoError;

  if( strlen( (const char *) pzFile ) > PMS_FSENTRY_PATHLEN )
  {
    /* File name is too long */
    result = eIllegalName;
  }
  else
  {
    uint8 * pzFileName;

    result = OIL_ValidateVolumeName( pzFile, &pzFileName );

    if( result == eNoError )
    {
      /* Name should consist of no more than nine repeats of \<element>
       * <element> is up to 100 of any char, but first and last cannot be space or 229
       */
      int32 nElements = 0;
      int32 nElementLength;

      while( *pzFileName != '\0' )
      {
        if( *pzFileName++ == '\\' )
        {
          /* Allow multiple \s */
          while( *pzFileName == '\\' )
          {
            pzFileName++;
          }

          if( *pzFileName == '\0' )
          {
            /* Trailing \s at end of name */
            break;
          }

          nElements++;
          if( nElements > 9 )
          {
            /* Too many path elements */
            result = eIllegalName;
            break;
          }

          nElementLength = 0;

          if( *pzFileName == ' ' || *pzFileName == 229 )
          {
            /* Bad first character */
            result = eIllegalName;
            break;
          }

          while( *pzFileName != '\\' && *pzFileName != '\0' )
          {
            pzFileName++;
            nElementLength++;
          }

          if( *(pzFileName - 1) == ' ' || *(pzFileName - 1) == 229 )
          {
            /* Bad last character */
            result = eIllegalName;
            break;
          }
          else if( nElementLength > 100 )
          {
            /* Path element too long */
            result = eIllegalName;
            break;
          }
        }
        else
        {
          /* Expected \ */
          result = eIllegalName;
          break;
        }
      }
    }
  }

  return result;
}

/**
 * \brief Handle the UEL (Universal Exit Language) command.
 *
 * \param[in] pCommand Pointer to command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoUEL( PjlCommand * pCommand )
{
  int32 result = eNoError;

  if( gOilPjlContext.eCurrentPDL == ePDF || gOilPjlContext.eCurrentPDL == eZIP )
  {
    /* RIP doesn't consume trailing UEL for PJL-wrapped PDF or XPS
     * so just ignore it here.
     */
  }
  else if( PjlGetJobDepth( gOilPjlContext.pParserContext ) == 0 )
  {
    /* Set current environment to user default environment, i.e. RESET */
    result = OIL_PjlDoReset( pCommand );
  }

  return result;
}

/**
 * \brief Handle the COMMENT command.
 *
 * This function checks the value of the comment and, if it is "PCL5c", "PCL5e" 
 * or "PCL6", sets the value of ePJLCommentPDL in the OIL PJL context.
 * \param[in] pCommand Pointer to the command.
 *
 * \return This function always returns \c eNoError.
 */
static int32 OIL_PjlDoComment( PjlCommand * pCommand )
{
  int32 result = eNoError;

  uint8 * pzComment;

  HQASSERT(pCommand , "No command");

  if( pCommand->pOptions )
  {
    /* Comment text, if any, will be the value of a single WORDS option */
    HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

    pzComment = pCommand->pOptions->value.u.pzValue;

    if( strstr( (const char *) pzComment, "PCL5c" ) || strstr( (const char *) pzComment, "PCL 5c" ))
    {
      gOilPjlContext.ePJLCommentPDL = ePCL5c;
    }
    else if( strstr( (const char *) pzComment, "PCL5e" ) || strstr( (const char *) pzComment, "PCL 5e" ) )
    {
      gOilPjlContext.ePJLCommentPDL = ePCL5e;
    }
    else if( strstr( (const char *) pzComment, "PCL6" ) || strstr( (const char *) pzComment, "PCL 6" ))
    {
      gOilPjlContext.ePJLCommentPDL = ePCLXL;
    }
  }

  return result;
}

/**
 * \brief Handle the DEFAULT command.
 *
 * This function sets the specified variable to the specified value in the user default settings.
 * \param[in] pCommand Pointer to command.
 * \return Returns \c eNoError if the value is successfully set, 
           otherwise an error enumeration value.
 */
static int32 OIL_PjlDoDefault( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  if( OIL_InSecureJob() )
  {
    result = OIL_SetEnvironmentVariable( pCommand->pOptions, TRUE );
  }
  else
  {
    result = ePasswordProtected;
  }

  return result;
}

/**
 * \brief Handle the DINQUIRE command.
 *
 * This function reports the value of the specified variable in the user default settings. The 
 * value is extracted in a string format and is posted to the backchannel to the PMS.
 * \param[in] pCommand Pointer to command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoDinquire( PjlCommand * pCommand )
{
  int32 result = eNoError;

  uint8 aValue[ 100 ];

  result = OIL_GetEnvironmentVariable( pCommand->pOptions, TRUE, aValue, sizeof( aValue ) );

  if( result == eNoError )
  {
    result = OIL_PjlReportSetting( pCommand, aValue );
  }

  return result;
}

/**
 * \brief Handle the ECHO command.
 *
 * This function uses the value of the command (which must be of type \c eValueString and 
 * less than 80 characters long) to construct a message which is then posted to the PMS backchannel.
 * \param[in] pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoEcho( PjlCommand * pCommand )
{
  int32 result = eNoError;

  uint8 * pzText;
  uint8   aBuffer[ 100 ];
  int     cbLen;

  HQASSERT(pCommand , "No command");

  if( pCommand->pOptions )
  {
    HQASSERT(pCommand->pOptions->value.eType == eValueString , "Invalid value type");

    pzText = pCommand->pOptions->value.u.pzValue;
    HQASSERT(strlen( (const char *) pzText ) <= 80 , "String too long");

    cbLen = sprintf( (char *) aBuffer, "@PJL ECHO %s\r\n\f", pzText );
  }
  else
  {
    /* No option means that no text was provided for the ECHO command.
     * We still need to generate an empty response.
     */
    strcpy( (char *) aBuffer, "@PJL ECHO\r\n\f" );
    cbLen = 12;
  }

  HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

  result = OIL_PjlReport( aBuffer, cbLen );

  return result;
}

/**
 * \brief Handle the EOJ (end of job) command.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoEoj( PjlCommand * pCommand )
{
  int32 result = eNoError;

  OilPjlJobInfo * pJobInfo;
  PjlOption     * pOption;
  char            szEojName[ MAX_PJL_JOBNAME_LEN + 1 ];

  HQASSERT(pCommand , "No command");

  /* This EOJ corresponds the last JOB command we received,
   * i.e. its OilPjlJobInfo is the first one in the list because
   * OIL_CreateJobInfo() adds each new entry to the start of the list.
   */
  pJobInfo = gOilPjlContext.pJobInfo;
  HQASSERT(gOilPjlContext.pJobInfo , "No job info");

  szEojName[ 0 ] = '\0';

  for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
  {
    if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

      if( strlen( (const char *) pOption->value.u.pzValue ) <= MAX_PJL_JOBNAME_LEN )
      {
        strcpy( szEojName, (char *) pOption->value.u.pzValue );
      }
      else
      {
        strncpy( szEojName, (char *) pOption->value.u.pzValue, MAX_PJL_JOBNAME_LEN );
        szEojName[ MAX_PJL_JOBNAME_LEN ] = '\0';

        result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eStringTooLong );
      }
    }
    else
    {
      HQFAIL("Invalid options");  /* Parser should only pass us valid options */
    }
  }

  if( pJobInfo->fAllPagesOutput )
  {
    if( gOilPjlContext.jobStatusValue == eoilpjl_On )
    {
      uint8 aBuffer[ 200 ];
      int   cbLen;

      cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS JOB\r\nEND\r\n" );
      if( szEojName[ 0 ] != '\0' )
      {
        cbLen += sprintf( (char *) aBuffer + cbLen, "NAME=\"%s\"\r\n", szEojName );
      }
      cbLen += sprintf( (char *) aBuffer + cbLen, "PAGES=%d\r\n\f", pJobInfo->nEojPages );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

      OIL_PjlReport( aBuffer, (size_t) cbLen );
    }
  }
  else
  {
    /* Note any name to include in report once output is complete */
    OIL_TyJob * pJob;

    pJob = GetJobByJobID( pJobInfo->uJobId );

    if( pJob != NULL )
    {
      pJob->bEojReceived = TRUE;
      strcpy( pJob->szEojJobName, szEojName );
    }
    else
    {
      if( gOilPjlContext.jobStatusValue == eoilpjl_On )
      {
        /* No OIL job => nothing to rip in job, issue report now */
        uint8 aBuffer[ 200 ];
        int   cbLen;

        HQASSERT(pJobInfo->nEojPages == 0 , "Pages still at end of job");

        cbLen = sprintf( (char *) aBuffer, "@PJL USTATUS JOB\r\nEND\r\n" );
        if( szEojName[ 0 ] != '\0' )
        {
          cbLen += sprintf( (char *) aBuffer + cbLen, "NAME=\"%s\"\r\n", szEojName );
        }
        cbLen += sprintf( (char *) aBuffer + cbLen, "PAGES=0\r\n\f" );
        HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");

        OIL_PjlReport( aBuffer, (size_t) cbLen );
      }
    }
  }

  OIL_FreeJobInfo( pJobInfo->uJobId, FALSE );

  /* Set current environment to user default environment, i.e. RESET */
  result = OIL_PjlDoReset( pCommand );

  /* Reset page number associated with unsolicited page status - TODO */

  /* Terminate the non-printing mode */
  gOilPjlContext.nJobStart = 0;
  gOilPjlContext.nJobEnd = 0;
  gOilPjlContext.nColorMode = 0;
  /* Reset other JOB values */
  gOilPjlContext.nJobPassword = 0;

  return result;
}

/**
 * \brief Utility function to map a PMS error code to a PJL error code.
 *
 * This is a trivial implementation, because this PJL parser implementation is
 * designed to work with the sample PMS code, which uses the same error codes#
 * as PJL.  Therefore, no mapping is required.
 * \param[in] ePMSFSError Error value from a PMS file system API.
 *
 * \return This implementation returns the error value that was passed in.
 */
static int32 OIL_MapFSError( int32 ePMSFSError )
{
  /* PMS example file system uses PJL error codes so no mapping required */
  return ePMSFSError;
}

/**
 * \brief Utility function to append or download data to a PMS file
 * \param[in] pzPath Path to file.
 * \param[in] pData Data to write to file.
 * \param[in] cbData Length of data to write to file.
 * \param[in] fAppend TRUE if appending to file, FALSE if overwriting existing data.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_FSDownload( unsigned char * pzPath, unsigned char * pData, int cbData, int fAppend )
{
  int32 eError;

  int    flags;
  void * handle;

  flags = PMS_FS_WRITE | PMS_FS_CREAT;
  if( fAppend )
  {
    flags |= PMS_FS_APPEND;
  }
  else
  {
    flags |= PMS_FS_TRUNC;
  }
  
  eError = PMS_FSOpen( pzPath, flags, &handle );

  if( eError == 0 )
  {
    int cbWritten;

    eError = PMS_FSWrite( handle, pData, cbData, &cbWritten );

    PMS_FSClose( handle );
  }

  return OIL_MapFSError( eError );
}

/**
 * \brief Handle the FSAPPEND command.
 *
 * This function appends data to an existing file, or writes data to a new file.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSAppend( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pModifier , "No comand modifier");
  HQASSERT(pCommand->pOptions &&
           pCommand->pOptions->pNext &&
           pCommand->pOptions->pNext->pNext, "Not enough command options") ;
  HQASSERT(pCommand->pOptions->pNext->pNext->pNext == NULL,
           "Too many command options");

  if( strcmp( (const char *) pCommand->pModifier->pzName, "FORMAT" ) != 0
    || strcmp( (const char *) pCommand->pModifier->value.u.pzValue, "BINARY" ) != 0 )
  {
    /* Bad modifier */
    result = eUnsupportedModifer;
  }
  else
  {
    PjlOption * pOption;
    uint8     * pzName = NULL;
    int32       cbSize = 0;
    uint8     * pData = NULL;
    int32       eValidation;

    for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
    {
      if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
      {
        HQASSERT(pzName == NULL , "No name");
        HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

        pzName = pOption->value.u.pzValue;
      }
      else if( strcmp( (const char *) pOption->pzName, "SIZE" ) == 0 )
      {
        HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

        cbSize = pOption->value.u.iValue;
      }
      else if( strcmp( (const char *) pOption->pzName, "BINARYDATA" ) == 0 )
      {
        HQASSERT(pData == NULL , "No option data");
        HQASSERT(pOption->value.eType == eValueBinaryData , "Invalid value type");

        pData = pOption->value.u.pzValue;
      }
      else
      {
        HQFAIL("Invalid options");  /* Parser should only pass us valid options */
      }
    }

    HQASSERT(pzName , "No name");
    HQASSERT(pData , "No data");

    eValidation = OIL_ValidateFileName( pzName );

    if( eValidation != eNoError )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
    }
    else if( cbSize < 0 )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
    else
    {
      result = OIL_FSDownload( pzName, pData, cbSize, TRUE );
    }
  }

  return result;
}

/**
 * \brief Handle the FSDELETE command.
 *
 * This function deletes a file or empty directory.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSDelete( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlValue * pValue;
  uint8    * pzName;
  int32      eValidation;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  /* Validate directory name */
  pValue = &pCommand->pOptions->value;
  pzName = pValue->u.pzValue;

  eValidation = OIL_ValidateFileName( pzName );

  if( eValidation != eNoError )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
  }
  else
  {
    int eError = PMS_FSDelete( pzName );

    result = OIL_MapFSError( eError );
  }

  return result;
}

/**
 * \brief Handle the FSDIRLIST command.
 *
 * This function lists the entries in a directory.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSDirList( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlOption * pOption;
  uint8     * pzName = NULL;
  int32       nEntry = 0;
  int32       nCount = 0;
  int32       eValidation;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions &&
           pCommand->pOptions->pNext &&
           pCommand->pOptions->pNext->pNext , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext->pNext->pNext == NULL ,
           "Too many command options");

  for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
  {
    if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
    {
      HQASSERT(pzName == NULL , "No name");
      HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

      pzName = pOption->value.u.pzValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "ENTRY" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      nEntry = pOption->value.u.iValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "COUNT" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      nCount = pOption->value.u.iValue;
    }
    else
    {
      HQFAIL("Invalid options");  /* Parser should only pass us valid options */
    }
  }

  HQASSERT(pzName , "No name");
  eValidation = OIL_ValidateFileName( pzName );

  if( eValidation != eNoError )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
  }
  else if( nEntry < 1 )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else if( nCount < 0 )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else
  {
    int32      eError;
    int32      i;
    PMS_TyStat stat;
    uint8      aBuffer[ 1024 ];
    int        cbLen;

    eError = PMS_FSQuery( pzName, &stat );
    result = OIL_MapFSError( eError );

    if( result != eNoError )
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL FSDIRLIST NAME=\"%s\" FILEERROR=%d\r\n\f", pzName, result % 1000 );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");
      OIL_PjlReport( aBuffer, cbLen );
      result = eNoError;
    }
    else if( stat.type == ePMS_FS_File )
    {
      /* Trying to list a file */
      cbLen = sprintf( (char *) aBuffer, "@PJL FSDIRLIST NAME=\"%s\" FILEERROR=%d\r\n\f", pzName, eDirectoryOpOnFile % 1000 );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");
      OIL_PjlReport( aBuffer, cbLen );
    }
    else if( nEntry >= (int) stat.cbSize )
    {
      /* Nothing to read */
      cbLen = sprintf( (char *) aBuffer, "@PJL FSDIRLIST NAME=\"%s\" ENTRY=%d\r\n\f", pzName, nEntry );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");
      OIL_PjlReport( aBuffer, cbLen );
    }
    else
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL FSDIRLIST NAME=\"%s\" ENTRY=%d\r\n", pzName, nEntry );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");
      OIL_PjlReport( aBuffer, cbLen );

      for( i = nEntry; i < nEntry + nCount; i++ )
      {
        eError = PMS_FSDirEntryInfo( pzName, i, &stat );
        result = OIL_MapFSError( eError );

        if( result != eNoError )
        {
          break;
        }

        if( stat.type == ePMS_FS_None )
        {
          break;
        }

        if( stat.type == ePMS_FS_Dir )
        {
          cbLen = sprintf( (char *) aBuffer, "%s TYPE=DIR\r\n", stat.aName );
        }
        else
        {
          cbLen = sprintf( (char *) aBuffer, "%s TYPE=FILE SIZE=%ld\r\n", stat.aName, stat.cbSize );
        }

        HQASSERT(cbLen < sizeof( aBuffer ) , "Buffer overflow");
        OIL_PjlReport( aBuffer, cbLen );
      }

      OIL_PjlReport( (uint8 *) "\f", 1 );
    }
  }

  return result;
}

/**
 * \brief Handle the FSDOWNLOAD command.
 *
 * This function writes data to a file.  It will overwrite any existing data in the destination file.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSDownload( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pModifier , "No command modifier");
  HQASSERT(pCommand->pOptions &&
           pCommand->pOptions->pNext &&
           pCommand->pOptions->pNext->pNext , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext->pNext->pNext == NULL,
           "Too many command options");

  if( strcmp( (const char *) pCommand->pModifier->pzName, "FORMAT" ) != 0
    || strcmp( (const char *) pCommand->pModifier->value.u.pzValue, "BINARY" ) != 0 )
  {
    /* Bad modifier */
    result = eUnsupportedModifer;
  }
  else
  {
    PjlOption * pOption;
    uint8     * pzName = NULL;
    int32       cbSize = 0;
    uint8     * pData = NULL;
    int32       eValidation;

    for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
    {
      if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
      {
        HQASSERT(pzName == NULL , "No name");
        HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

        pzName = pOption->value.u.pzValue;
      }
      else if( strcmp( (const char *) pOption->pzName, "SIZE" ) == 0 )
      {
        HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

        cbSize = pOption->value.u.iValue;
      }
      else if( strcmp( (const char *) pOption->pzName, "BINARYDATA" ) == 0 )
      {
        HQASSERT(pData == NULL , "No name");
        HQASSERT(pOption->value.eType == eValueBinaryData , "Invalid value type");

        pData = pOption->value.u.pzValue;
      }
      else
      {
        HQFAIL("Invalid options");  /* Parser should only pass us valid options */
      }
    }

    HQASSERT(pzName , "No name");
    HQASSERT(pData , "No data");

    eValidation = OIL_ValidateFileName( pzName );

    if( eValidation != eNoError )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
    }
    else if( cbSize < 0 )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
    else
    {
      result = OIL_FSDownload( pzName, pData, cbSize, FALSE );
    }
  }

  return result;
}

/**
 * \brief Handle the FSINIT command.
 *
 * This function initializes a volume. It validates the volume name, then hands it to the PMS to 
 * complete the initialization.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSInit( PjlCommand * pCommand )
{
  int32 result = eNoError;

  if( OIL_InSecureJob() )
  {
    PjlValue * pValue;
    uint8    * pzVolume;
    uint8    * pzVolumeEnd;
    int32      eValidation;

    HQASSERT(pCommand , "No command");
    HQASSERT(pCommand->pOptions , "Not enough command options");
    HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

    /* Validate volume name */
    pValue = &pCommand->pOptions->value;
    pzVolume = pValue->u.pzValue;

    eValidation = OIL_ValidateVolumeName( pzVolume, &pzVolumeEnd );

    if( eValidation != eNoError )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
    }
    else if( *pzVolumeEnd != '\0' )
    {
      /* Not just a volume name.  (Some printers don't care and do it anyway.) */
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eVolumeNameOutOfRange );
    }
    else
    {
      int32 eError = PMS_FSInitVolume( pzVolume );

      result = OIL_MapFSError( eError );
    }
  }
  else
  {
    result = ePasswordProtected;
  }

  return result;
}

/**
 * \brief Handle the FSMKDIR command.
 *
 * This function creates a directory.  It validates the name of the required directory, as specified
 * in the command, then passes it to the PMS, where the directory is actually created.
 * \param[in] pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSMkDir( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlValue * pValue;
  uint8    * pzName;
  int32      eValidation;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  /* Validate directory name */
  pValue = &pCommand->pOptions->value;
  pzName = pValue->u.pzValue;

  eValidation = OIL_ValidateFileName( pzName );

  if( eValidation != eNoError )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
  }
  else
  {
    int32 eError = PMS_FSMkDir( pzName );

    result = OIL_MapFSError( eError );
  }

  return result;
}

/**
 * \brief Handle FSQUERY command.
 *
 * Determine details of a file system entry.
 * \param[in] pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSQuery( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlValue * pValue;
  uint8    * pzName;
  int32      eValidation;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  /* Validate directory name */
  pValue = &pCommand->pOptions->value;
  pzName = pValue->u.pzValue;

  eValidation = OIL_ValidateFileName( pzName );

  if( eValidation != eNoError )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
  }
  else
  {
    int32      eError;
    PMS_TyStat stat;
    uint8      aBuffer[ 1024 ];
    int        cbLen;

    eError = PMS_FSQuery( pzName, &stat );
    result = OIL_MapFSError( eError );

    if( result != eNoError )
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL FSQUERY NAME=\"%s\" FILEERROR=%d\r\n\f", pzName, result % 1000 );
      result = eNoError;
    }
    else if( stat.type == ePMS_FS_Dir )
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL FSQUERY NAME=\"%s\" TYPE=DIR\r\n\f", pzName );
    }
    else
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL FSQUERY NAME=\"%s\" TYPE=FILE SIZE=%ld\r\n\f", pzName, stat.cbSize );
    }

    HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
    OIL_PjlReport( aBuffer, cbLen );
  }

  return result;
}

/**
 * \brief Utility function to read data from a PMS file into the buffer supplied.
 * \param[in]  pzPath Path to file.
 * \param[in]  offset The point in the file that the read operation should start at.
 * \param[in]  cbData Length of data to read from file.
 * \param[out] pData Buffer to read data in to.
 * \param[out] pcbRead Integer to receive the actual number of bytes read from file.
 * \return Returns \c eNoError if the read succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_FSUpload(unsigned char * pzPath, int offset, int cbData, unsigned char * pData, int * pcbRead)
{
  int32 eError;

  void * handle;
  
  eError = PMS_FSOpen( pzPath, PMS_FS_READ, &handle );

  if( eError == 0 )
  {
    eError = PMS_FSSeek( handle, offset, PMS_FS_SEEK_SET );

    if( eError == 0 )
    {
      eError = PMS_FSRead( handle, pData, cbData, pcbRead );
    }

    PMS_FSClose( handle );
  }

  return OIL_MapFSError( eError );
}

/**
 * \brief Handle the FSUPLOAD command.
 *
 * This function uploads (reads) data from a file.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoFSUpload( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlOption * pOption;
  uint8     * pzName = NULL;
  int32       cbSize = 0;
  int32       nOffset = 0;
  int32       eValidation;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions &&
           pCommand->pOptions->pNext &&
           pCommand->pOptions->pNext->pNext , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext->pNext->pNext == NULL,
           "Too many command options");

  for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
  {
    if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
    {
      HQASSERT(pzName == NULL , "No name");
      HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

      pzName = pOption->value.u.pzValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "SIZE" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      cbSize = pOption->value.u.iValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "OFFSET" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      nOffset = pOption->value.u.iValue;
    }
    else
    {
      HQFAIL("Invalid options");  /* Parser should only pass us valid options */
    }
  }

  HQASSERT(pzName , "No name");
  eValidation = OIL_ValidateFileName( pzName );

  if( eValidation != eNoError )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eValidation );
  }
  else if( cbSize < 0 )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else if( nOffset < 0 )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else
  {
    int32      eError;
    PMS_TyStat stat;
    uint8      aBuffer[ 1024 ];
    int        cbLen;

    eError = PMS_FSQuery( pzName, &stat );
    result = OIL_MapFSError( eError );

    if( result != eNoError )
    {
      cbLen = sprintf( (char *) aBuffer, "@PJL UPLOAD NAME=\"%s\" FILEERROR=%d\r\n\f", pzName, result % 1000 );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
      OIL_PjlReport( aBuffer, cbLen );
      result = eNoError;
    }
    else if( stat.type == ePMS_FS_Dir )
    {
      /* Trying to upload a directory */
      cbLen = sprintf( (char *) aBuffer, "@PJL UPLOAD NAME=\"%s\" FILEERROR=%d\r\n\f", pzName, eFileNotFound % 1000 );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
      OIL_PjlReport( aBuffer, cbLen );
    }
    else if( cbSize == 0 || nOffset >= (int) stat.cbSize )
    {
      /* Nothing to read */
      cbLen = sprintf( (char *) aBuffer, "@PJL FSUPLOAD FORMAT:BINARY NAME=\"%s\" OFFSET=%d SIZE=0\r\n\f",
                       pzName, nOffset );
      HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
      OIL_PjlReport( aBuffer, cbLen );
    }
    else
    {
      uint8 aDataBuffer[ 1024 ];
      int32 cbBytesRead;

      if( nOffset + cbSize > (int) stat.cbSize )
      {
        cbSize = stat.cbSize - nOffset;
      }

      if( cbSize <= sizeof( aBuffer ) )
      {
        result = OIL_FSUpload( pzName, nOffset, cbSize, aDataBuffer, &cbBytesRead );

        if( result == eNoError )
        {
          cbLen = sprintf( (char *) aBuffer, "@PJL FSUPLOAD FORMAT:BINARY NAME=\"%s\" OFFSET=%d SIZE=%d\r\n",
                           pzName, nOffset, cbBytesRead );
          HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
          OIL_PjlReport( aBuffer, cbLen );

          OIL_PjlReport( aDataBuffer, cbBytesRead );
          OIL_PjlReport( (uint8 *) "\f", 1 );
        }
      }
      else
      {
        result = OIL_FSUpload( pzName, nOffset, sizeof(aDataBuffer), aDataBuffer, &cbBytesRead );

        if( result == eNoError )
        {
          cbLen = sprintf( (char *) aBuffer, "@PJL FSUPLOAD FORMAT:BINARY NAME=\"%s\" OFFSET=%d SIZE=%d\r\n",
                           pzName, nOffset, cbSize );
          HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");
          OIL_PjlReport( aBuffer, cbLen );

          OIL_PjlReport( aDataBuffer, cbBytesRead );

          nOffset += cbBytesRead;
          cbSize -= cbBytesRead;

          while( cbSize > sizeof( aBuffer ) && cbBytesRead > 0 )
          {
            result = OIL_FSUpload( pzName, nOffset, sizeof(aDataBuffer), aDataBuffer, &cbBytesRead );

            if( result != eNoError )
            {
              break;
            }

            OIL_PjlReport( aDataBuffer, cbBytesRead );

            nOffset += cbBytesRead;
            cbSize -= cbBytesRead;
          }

          if( cbSize > 0 && result == eNoError )
          {
            result = OIL_FSUpload( pzName, nOffset, cbSize, aDataBuffer, &cbBytesRead );
            OIL_PjlReport( aDataBuffer, cbSize );
          }

          OIL_PjlReport( (uint8 *) "\f", 1 );
        }
      }
    }
  }

  return result;
}

/**
 * \brief Handle the INFO command.
 *
 * This function reports information about the printer.  The information gathered is used to construct
 * a string which is then passed back to the PMS via the PMS backchannel.
 * \param[in] pCommand Pointer to the command.
 * \return    Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoInfo( PjlCommand * pCommand )
{
  int32 result = eNoError;

  uint8   aBuffer[ 1024 ];
  int     cbLen;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");

  if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "ID" ) == 0 )
  {
    uint8 * pzPrinterModel = (uint8 *) "<Insert printer model here>";   /* TODO */

    cbLen = sprintf( (char *) aBuffer, "@PJL INFO ID\r\n\"%s\"\r\n\f", pzPrinterModel );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "CONFIG" ) == 0 )
  {
    int32            i;
    int32            nTrays;
    PMS_TySystem     pmsSysInfo;
    PMS_TyTrayInfo * pPMSTrays;

    cbLen = sprintf( (char *) aBuffer, "@PJL INFO CONFIG\r\n" );

    nTrays = PMS_GetTrayInfo( &pPMSTrays );
    cbLen += sprintf( (char *) aBuffer + cbLen, "IN TRAYS [%d ENUMERATED]\r\n", nTrays );
    for( i = 0; i < nTrays; i++ )
    {
      switch( pPMSTrays[ i ].eMediaSource )
      {
      case PMS_TRAY_BYPASS:
        cbLen += sprintf( (char *) aBuffer + cbLen, "\tINTRAYBYPASS\r\n" );
        break;
      case PMS_TRAY_MANUALFEED:
        cbLen += sprintf( (char *) aBuffer + cbLen, "\tINTRAYMANUALFEED\r\n" );
        break;
      case PMS_TRAY_TRAY1:
        cbLen += sprintf( (char *) aBuffer + cbLen, "\tINTRAY1\r\n" );
        break;
      case PMS_TRAY_TRAY2:
        cbLen += sprintf( (char *) aBuffer + cbLen, "\tINTRAY2\r\n" );
        break;
      case PMS_TRAY_TRAY3:
        cbLen += sprintf( (char *) aBuffer + cbLen, "\tINTRAY3\r\n" );
        break;
      default:
        HQFAIL("Invalid tray setting");
        break;
      }
    }

    cbLen += sprintf( (char *) aBuffer + cbLen, "DUPLEX\r\n" );

    /* TODO
    cbLen += sprintf( (char *) aBuffer + cbLen, "OUTPUT BINS\r\n" );
    */

    cbLen += sprintf( (char *) aBuffer + cbLen, "PAPERS [%d ENUMERATED]\r\n", (int) N_PAPER_SIZE_VALUES );
    for( i = 0; i < N_PAPER_SIZE_VALUES; i++ )
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, "\t%s\r\n", gPaperSizeValues[ i ] );
    }

    cbLen += sprintf( (char *) aBuffer + cbLen, "LANGUAGES [4 ENUMERATED]\r\n" );
    cbLen += sprintf( (char *) aBuffer + cbLen, "\tPCL\r\n" );
    cbLen += sprintf( (char *) aBuffer + cbLen, "\tPDF\r\n" );
    cbLen += sprintf( (char *) aBuffer + cbLen, "\tPOSTSCRIPT\r\n" );
    cbLen += sprintf( (char *) aBuffer + cbLen, "\tXPS\r\n" );

    cbLen += sprintf( (char *) aBuffer + cbLen, "USTATUS [%d ENUMERATED]\r\n", (int) N_OIL_PJL_STATUS_CATS );
    for( i = 0; i < N_OIL_PJL_STATUS_CATS; i++ )
    {
      cbLen += sprintf( (char *) aBuffer + cbLen, "\t%s\r\n", gOilPjlStatusCategories[ i ].pzName );
    }

    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
    cbLen += sprintf( (char *) aBuffer + cbLen, "MEMORY=%u\r\n", pmsSysInfo.cbRIPMemory );

    cbLen += sprintf( (char *) aBuffer + cbLen, "\f" );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "FILESYS" ) == 0 )
  {
    int nVolumes;

    PMS_FSFileSystemInfo( 0, &nVolumes, NULL );

    if( nVolumes == 0 )
    {
      /* No file system */
      cbLen = sprintf( (char *) aBuffer, "@PJL INFO FILESYS\r\n\"?\"\r\n\f" );
    }
    else
    {
      int32            i;
      PMS_TyFileSystem fileSystemInfo;
      int32            fDisklock = PMS_FSGetDisklock();

      cbLen = sprintf( (char *) aBuffer, "@PJL INFO FILESYS [ %d TABLE ]\r\n", (int) nVolumes + 1 );
      cbLen += sprintf( (char *) aBuffer + cbLen, "\tVOLUME\tTOTAL SIZE\tFREE SPACE\tLOCATION\tLABEL\tSTATUS\r\n" );

      for( i = 0; i < nVolumes; i++ )
      {
        PMS_FSFileSystemInfo( i, NULL, &fileSystemInfo );
        cbLen += sprintf( (char *) aBuffer + cbLen, "\t%d:\t%d\t%d\t%s\t%s\t%s\r\n",
          i,
          fileSystemInfo.cbCapacity,
          fileSystemInfo.cbFree,
          fileSystemInfo.pLocation,
          fileSystemInfo.pLabel,
          i == 0 && fDisklock ? "READ-ONLY" : "READ-WRITE" );
      }

      cbLen += sprintf( (char *) aBuffer + cbLen, "\f" );
    }
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "MEMORY" ) == 0 )
  {
    PMS_TySystem pmsSysInfo;
    uint32 cbLargestFree = 0;   /* TODO */

    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
    cbLen = sprintf( (char *) aBuffer, "@PJL INFO MEMORY\r\nTOTAL=%u\r\nLARGEST=%u\r\n\f",
      pmsSysInfo.cbRIPMemory, cbLargestFree );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "PAGECOUNT" ) == 0 )
  {
    PMS_TySystem pmsSysInfo;

    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

    cbLen = sprintf( (char *) aBuffer, "@PJL INFO PAGECOUNT\r\nPAGECOUNT=%u\r\n\f", pmsSysInfo.cPagesPrinted );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "STATUS" ) == 0 )
  {
    int32 statusCode = 10001;                  /* TODO */
    uint8 * pzDisplay = (uint8 *) "Message";   /* TODO */
    int32 fOnline = TRUE;                      /* TODO */

    cbLen = sprintf( (char *) aBuffer, "@PJL INFO STATUS\r\nCODE=%d\r\nDISPLAY=\"%s\"\r\nONLINE=%s\r\n\f",
      statusCode, pzDisplay, fOnline ? "TRUE" : "FALSE" );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "VARIABLES" ) == 0 )
  {
    return OIL_PjlEnvVarInfo( pCommand->pOptions->pzName, gOilPjlEnvVars, N_OIL_PJL_ENV_VARS );
  }
  else if( strcmp( (const char *) pCommand->pOptions->pzName, (const char *) "USTATUS" ) == 0 )
  {
    return OIL_PjlEnvVarInfo( pCommand->pOptions->pzName, gOilPjlStatusCategories, N_OIL_PJL_STATUS_CATS );
  }
  else
  {
    /* Unknown category (not an error) */
    cbLen = sprintf( (char *) aBuffer, "@PJL INFO %s\r\n\"?\"\r\n\f", pCommand->pOptions->pzName );
  }

  HQASSERT(cbLen < sizeof( aBuffer ) , "Overflowed buffer");

  result = OIL_PjlReport( aBuffer, cbLen );

  return result;
}

/**
 * \brief Handle the INITIALIZE command.
 *
 * This function sets the current and user default environment settings to the factory defaults.
 * \param[in]  pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoInitialize( PjlCommand * pCommand )
{
  int32 result = eNoError;

  if( OIL_InSecureJob() )
  {
    /* Set user default environment to factory settings */
    PMS_TyJob * pFactorySettings;

    PMS_SetJobSettingsToDefaults();
    PMS_GetJobSettings( &pFactorySettings );
    memcpy( gOilPjlContext.pUserDefaults, pFactorySettings, sizeof(PMS_TyJob) );

    /* Set current environment to user default environment, i.e. RESET */
    result = OIL_PjlDoReset( pCommand );
  }
  else
  {
    result = ePasswordProtected;
  }

  return result;
}

/**
 * \brief Handle the INQUIRE command.
 *
 * This function reports the value of the specified variable in the current environment settings.
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoInquire( PjlCommand * pCommand )
{
  int32 result = eNoError;

  uint8 aValue[ 100 ];

  result = OIL_GetEnvironmentVariable( pCommand->pOptions, FALSE, aValue, sizeof( aValue ) );

  if( result == eNoError )
  {
    result = OIL_PjlReportSetting( pCommand, aValue );
  }

  return result;
}

/**
 * \brief Handle the JOB command.
 *
 * \param[in] pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoJob( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlOption * pOption;
  OilPjlJobInfo * pJobInfo;

  HQASSERT(pCommand , "No command");

  pJobInfo = OIL_CreateJobInfo( gOilPjlContext.pCurrentEnvironment->uJobId );

  /* Set current environment to user default environment, i.e. RESET */
  result = OIL_PjlDoReset( pCommand );

  gOilPjlContext.nJobStart = 0;
  gOilPjlContext.nJobEnd = 0;
  gOilPjlContext.nJobPassword = 0;
  gOilPjlContext.nColorMode = 0;
  gOilPjlContext.szFileName[0] = '\0';

  for( pOption = pCommand->pOptions; pOption != NULL; pOption = pOption->pNext )
  {
    if( strcmp( (const char *) pOption->pzName, "NAME" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueString , "Invalid value type");

      if( strlen( (const char *) pOption->value.u.pzValue ) <= MAX_PJL_JOBNAME_LEN )
      {
        strcpy( (char *) pJobInfo->szJobName, (char *) pOption->value.u.pzValue );
        strcpy( (char *) gOilPjlContext.szFileName, (char *) pOption->value.u.pzValue);
      }
      else
      {
        strncpy( (char *) pJobInfo->szJobName, (char *) pOption->value.u.pzValue, MAX_PJL_JOBNAME_LEN );
        pJobInfo->szJobName[ MAX_PJL_JOBNAME_LEN ] = '\0';
        strncpy( (char *) gOilPjlContext.szFileName, (char *) pOption->value.u.pzValue, MAX_PJL_JOBNAME_LEN );
        gOilPjlContext.szFileName[ MAX_PJL_JOBNAME_LEN ] = '\0';

        result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eStringTooLong );
      }
    }
    else if( strcmp( (const char *) pOption->pzName, "START" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      gOilPjlContext.nJobStart = pOption->value.u.iValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "END" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      gOilPjlContext.nJobEnd = pOption->value.u.iValue;
    }
    else if( strcmp( (const char *) pOption->pzName, "PASSWORD" ) == 0 )
    {
      HQASSERT(pOption->value.eType == eValueInt , "Invalid value type");

      gOilPjlContext.nJobPassword = pOption->value.u.iValue;
    }
    else
    {
      HQFAIL("Invalid options");  /* Parser should only pass us valid options */
    }
  }

  OIL_PjlReportJobStart( pJobInfo );

  return result;
}

/**
 * \brief Handle the OPMSG command.
 *
 * This function displays the message and returns.  It should prompt the system to go offline, but
 * this has not been implemented in this sample code.
 * \param[in] pCommand Pointer to command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoOpmsg( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");
  HQASSERT(pCommand->pOptions->value.eType == eValueString , "Invalid value type");

  if( pCommand->pOptions->value.u.pzValue[ 0 ] == '\0' )
  {
    result = eNullString;
  }
  else
  {
    result = OIL_PjlDisplay( pCommand->pOptions );

    /* TODO - go offline */
  }

  return result;
}

/**
 * \brief Handle the RDYMSG command.
 *
 * This function causes the "ready" message to be displayed, via the panel if panel use has been
 * configured.
 * \param[in] pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoRdymsg( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");
  HQASSERT(pCommand->pOptions->value.eType == eValueString , "Invalid value type");

  if( pCommand->pOptions->value.u.pzValue[ 0 ] != '\0' )
  {
    result = OIL_PjlDisplay( pCommand->pOptions );
  }

  return result;
}

/**
 * \brief Handle the RESET command.
 *
 * This function sets the current environment settings to the user default settings.
 * \param[in]  pCommand Pointer to the command.
 * \return This function always returns \c eNoError.
 */
static int32 OIL_PjlDoReset( PjlCommand * pCommand )
{
  int32 result = eNoError;

  unsigned int uJobId;
  char szJobName[PMS_MAX_JOBNAME_LENGTH];

  UNUSED_PARAM( PjlCommand *, pCommand );

  /* Set current environment to user default environment,
   * but preserving uJobId, szJobName
   */
  uJobId = gOilPjlContext.pCurrentEnvironment->uJobId;
  strcpy( szJobName, gOilPjlContext.pCurrentEnvironment->szJobName );

  *gOilPjlContext.pCurrentEnvironment = *gOilPjlContext.pUserDefaults;

  gOilPjlContext.pCurrentEnvironment->uJobId = uJobId;
  strcpy( gOilPjlContext.pCurrentEnvironment->szJobName, szJobName );

  gOilPjlContext.currentEnvironment.ePersonality = OIL_GetPMSPersonality();
  PjlSetDefaultPersonality( gOilPjlContext.pParserContext, gOilPjlContext.currentEnvironment.ePersonality );

  return result;
}

/**
 * \brief Handle the SET command.
 *
 * This function sets the value of an environment variable in the current environment settings.
 * \param[in]  pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoSet( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  result = OIL_SetEnvironmentVariable( pCommand->pOptions, FALSE );

  return result;
}

/**
 * \brief Handle the STMSG command.
 *
 * This function should cause the system to display the message provided, then go offline and wait 
 * for a key press to signal that the system should go back online. This sample implementation 
 * only displays the message; it does not go offline.
 * \param[in]   pCommand Pointer to the command.
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoStmsg( PjlCommand * pCommand )
{
  int32 result = eNoError;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  HQASSERT(pCommand->pOptions->value.eType == eValueString , "Invalid value type");

  if( pCommand->pOptions->value.u.pzValue[ 0 ] == '\0' )
  {
    result = eNullString;
  }
  else
  {
    result = OIL_PjlDisplay( pCommand->pOptions );

    /* TODO - go offline, wait for key press, report it */
  }

  return result;
}

/**
 * \brief Handle the USTATUS command.
 *
 * This function handles requests to change the unsolicited status reporting activity of the system.
 * \param[in]  pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoUstatus( PjlCommand * pCommand )
{
  int32 result = eNoError;

  PjlEnvVar * pStatusCategory;

  HQASSERT(pCommand , "No command");
  HQASSERT(pCommand->pOptions , "Not enough command options");
  HQASSERT(pCommand->pOptions->pNext == NULL , "Too many command options");

  pStatusCategory = OIL_LookupStatusCategory( pCommand->pOptions->pzName );

  if( pStatusCategory != NULL )
  {
    PjlValue * pValue = &pCommand->pOptions->value;

    if( pValue->eType == pStatusCategory->eType )
    {
      OilEnvVar * pOilEnvVar = pStatusCategory->pPrivate;

      result = (pOilEnvVar->pSetFn)( pValue, FALSE );
    }
    else
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionWrongValueType );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

/**
 * \brief Handle the USTATUSOFF command.
 *
 * This function turns off all unsolicited status reporting.
 * \param[in]  pCommand Pointer to the command.
 *
 * \return Returns \c eNoError if the call succeeds, otherwise one of the error enumeration values.
 */
static int32 OIL_PjlDoUstatusoff( PjlCommand * pCommand )
{
  int32 result = eNoError;

  UNUSED_PARAM( PjlCommand *, pCommand );

  gOilPjlContext.deviceStatusValue = eDeviceStatusOff;
  gOilPjlContext.jobStatusValue = eoilpjl_Off;
  gOilPjlContext.pageStatusValue = eoilpjl_Off;
  gOilPjlContext.timedStatusValue = 0;

  return result;
}



/**
 * \brief Retrieve the user default or current environment settings.
 *
 * This function can retrieve either the current environmnent settings or the user default 
 * settings.  
 * \param[in] fDefault      Set to TRUE to return the user default settings, or FALSE to return
 *                          the current environment settings.
 *
 * \return Returns a pointer to a structure containing the requested settings.
 */
static PMS_TyJob * OIL_GetEnvironment( int32 fDefault )
{
  return fDefault ? gOilPjlContext.pUserDefaults : gOilPjlContext.pCurrentEnvironment;
}

/**
 * \brief Set the user default or current environment settings.
 *
 * This function can updateeither the current environmnent settings or the user default 
 * settings.  
 * \param[in] pEnvironment  A pointer to a job structure containing the new settings.
 * \param[in] fDefault      Set to TRUE to update the user default settings, or FALSE to update
 *                          the current environment settings.
 * \todo If fDefault is FALSE, the call returns silently without doing any work.  Should it 
 *       actually be updating the current settings?
 */
static void OIL_UpdateEnvironment( PMS_TyJob * pEnvironment, int32 fDefault )
{
  if( fDefault )
  {
    PMS_SetJobSettings( pEnvironment );
  }
}


static void OIL_GetBinding( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( pEnvironment->bTumble )
  {
    /* Tumble: Landscape (orientation = 1) -> long edge (0), Portrait (orientation = 0) -> short edge (1) */
    pValue->u.pzValue = gBindingValues[ pEnvironment->uOrientation ? 0 : 1 ];
  }
  else
  {
    /* No tumble: Landscape (orientation = 1) -> short edge (1), Portrait (orientation = 0) -> long edge (0) */
    pValue->u.pzValue = gBindingValues[ pEnvironment->uOrientation ? 1 : 0 ];
  }
}


static int32 OIL_SetBinding( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gBindingValues[ 0 ] ) == 0 )
  {
    /* Long edge: Landscape -> tumble, portrait -> no tumble */
    pEnvironment->bTumble = pEnvironment->uOrientation ? 1 : 0;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gBindingValues[ 1 ] ) == 0 )
  {
    /* Short edge: Landscape -> no tumble, portrait -> tumble */
    pEnvironment->bTumble = pEnvironment->uOrientation ? 0 : 1;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetBitsPerPixel( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;

  switch( pEnvironment->eImageQuality )
  {
  case PMS_1BPP:
    pValue->u.iValue = 1;
    break;
  case PMS_2BPP:
    pValue->u.iValue = 2;
    break;
  case PMS_4BPP:
    pValue->u.iValue = 4;
    break;
  case PMS_8BPP_CONTONE:
    pValue->u.iValue = 8;
    break;
  case PMS_16BPP_CONTONE:
    pValue->u.iValue = 16;
    break;
  default:
    HQFAIL("Invalid image quality");
    break;
  }
}


static int32 OIL_SetBitsPerPixel( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( pValue->u.iValue == gBitsPerPixelValues[ 0 ] )
  {
    pEnvironment->eImageQuality = PMS_16BPP_CONTONE;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 1 ] )
  {
    pEnvironment->eImageQuality = PMS_8BPP_CONTONE;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 2 ] )
  {
    pEnvironment->eImageQuality = PMS_4BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 3 ] )
  {
    pEnvironment->eImageQuality = PMS_2BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 4 ] )
  {
    pEnvironment->eImageQuality = PMS_1BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

static void OIL_GetCopies( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uCopyCount;
}


static int32 OIL_SetCopies( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 value = pValue->u.iValue;

  if( value < gCopiesRange.min )
  {
    value = gCopiesRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCopiesRange.max )
  {
    value = gCopiesRange.max;
    result = eOptionValueDataLossRange;
  }
  gOilPjlContext.nCopies = value;
  pEnvironment->uCopyCount = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetCourier( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gCourierEnum, pEnvironment->bCourierDark, pValue );
}


static int32 OIL_SetCourier( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gCourierEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->bCourierDark = (unsigned char)pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetCustLength( PjlValue * pValue, int fGetDefault )
{
  PMS_TyPaperInfo * pPaperInfo = NULL;

  
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueDouble;
  PMS_GetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  /* convert from points to decipoints */
  pValue->u.dValue = pPaperInfo->dHeight * 10;
  
}


static int32 OIL_SetCustLength( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyPaperInfo * pPaperInfo = NULL;
  double value = pValue->u.dValue;
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( value < gCustLengthRange.min )
  {
    value = gCustLengthRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCustLengthRange.max )
  {
    value = gCustLengthRange.max;
    result = eOptionValueDataLossRange;
  }

  PMS_GetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  /* convert from decipoints to points */
  pPaperInfo->dHeight = value * 0.1;
  PMS_SetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  pEnvironment->dUserPaperHeight = pPaperInfo->dHeight;
  return result;
}


static void OIL_GetCustWidth( PjlValue * pValue, int fGetDefault )
{
  PMS_TyPaperInfo * pPaperInfo = NULL;

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueDouble;
  PMS_GetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  /* convert from points to decipoints */
  pValue->u.dValue = pPaperInfo->dWidth * 10;
  
}


static int32 OIL_SetCustWidth( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyPaperInfo * pPaperInfo = NULL;
  double value = pValue->u.dValue;
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );



  if( value < gCustWidthRange.min )
  {
    value = gCustWidthRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCustWidthRange.max )
  {
    value = gCustWidthRange.max;
    result = eOptionValueDataLossRange;
  }

  PMS_GetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  /* convert from decipoints to points */
  pPaperInfo->dWidth = value * 0.1;
  PMS_SetPaperInfo( PMS_SIZE_CUSTOM, &pPaperInfo );
  pEnvironment->dUserPaperWidth = pPaperInfo->dWidth;

  return result;
}


static void OIL_GetDisklock( PjlValue * pValue, int fGetDefault )
{
  int32 fDisklock = PMS_FSGetDisklock();

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( fDisklock )
  {
    pValue->u.pzValue = gOffOnValues[ 1 ];
  }
  else
  {
    pValue->u.pzValue = gOffOnValues[ 0 ];
  }
}


static int32 OIL_SetDisklock( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  if( fSetDefault )
  {
    if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
    {
      PMS_FSSetDisklock( FALSE );
    }
    else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
    {
      PMS_FSSetDisklock( TRUE );
    }
    else
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
  }
  else
  {
    /* Silently fail for SET DISKLOCK */
  }

  return result;
}

static void OIL_GetPrintBlankPage( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( pEnvironment->bSuppressBlank )
  {
    pValue->u.pzValue = gOffOnValues[ 0 ];
  }
  else
  {
    pValue->u.pzValue = gOffOnValues[ 1 ];
  }
}

static int32 OIL_SetPrintBlankPage( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
  {
    pEnvironment->bSuppressBlank = 1;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
  {
    pEnvironment->bSuppressBlank = 0;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

static void OIL_GetColorMode( PjlValue * pValue, int fGetDefault )
{
  UNUSED_PARAM( int, fGetDefault );

  OIL_GetMappedValue( &gOrientationEnum, gOilPjlContext.nColorMode, pValue );
}


static int32 OIL_SetColorMode( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;
  
  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gColorModeEnum, pValue->u.pzValue );

  UNUSED_PARAM( int, fSetDefault );

  if( pMappedValue != NULL )
  {

    gOilPjlContext.nColorMode = pMappedValue->value;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

void OIL_ResetPJLAppData(void)
{
  /* reset parameters used by the mobile print app */
  gOilPjlContext.nColorMode = 0;
  gOilPjlContext.nJobStart = 0;
  gOilPjlContext.nJobEnd = 0;
  gOilPjlContext.nCopies = 1;
  gOilPjlContext.szFileName[0] = '\0';
}

char *OIL_PjlGetFileName(void)
{
  if(gOilPjlContext.szFileName[0] == '\0')
    return NULL;
  else
    return gOilPjlContext.szFileName;
}

static void OIL_GetDuplex( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( pEnvironment->bDuplex )
  {
    pValue->u.pzValue = gOffOnValues[ 1 ];
  }
  else
  {
    pValue->u.pzValue = gOffOnValues[ 0 ];
  }
}


static int32 OIL_SetDuplex( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
  {
    pEnvironment->bDuplex = 0;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
  {
    pEnvironment->bDuplex = 1;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetFontnumber( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uFontNumber;
}


static int32 OIL_SetFontnumber( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 value = pValue->u.iValue;

  if( value < gFontnumberRange.min || value > gFontnumberRange.max )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->uFontNumber = value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }

  return result;
}


static void OIL_GetFontsource( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueString;
  pValue->u.pzValue = (uint8 *) pEnvironment->szFontSource;
}


static int32 OIL_SetFontsource( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 fKnownValue = FALSE;
  int32 i;

  for( i = 0; i < gFontsourceEnum.nValues; i++ )
  {
    if( strcmp( (const char *) pValue->u.pzValue, (const char *) gFontsourceValues[ i ] ) == 0 )
    {
      if( strcmp( (const char *) pValue->u.pzValue, pEnvironment->szFontSource ) != 0 )
      {
        /* Changing value */
        strcpy( pEnvironment->szFontSource, (const char *) pValue->u.pzValue );

        /* Update fontnumber to lowest numbered font in new font source */
        pEnvironment->uFontNumber = gFontnumberRange.min;

        OIL_UpdateEnvironment( pEnvironment, fSetDefault );
      }

      fKnownValue = TRUE;
      break;
    }
  }

  if( ! fKnownValue )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueUnsupported );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static double OIL_GetMediaLength( PMS_TyJob * pEnvironment ) 
{
  double dMediaLengthPoints;

  if( pEnvironment->uOrientation == 0 )
  {
    /* Portrait
     * Assume 0.5" margin top and bottom
     */
    dMediaLengthPoints = pEnvironment->tDefaultJobMedia.dHeight - 72.0;
  }
  else
  {
    /* Landscape
     * Assume 0.5" margin top and bottom
     */
    dMediaLengthPoints = pEnvironment->tDefaultJobMedia.dWidth - 72.0;
  }

  return dMediaLengthPoints / 72.0;
}


static void OIL_GetFormlines( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );
  double dMediaLengthInches = OIL_GetMediaLength( pEnvironment );

  pValue->eType = eValueInt;
  pValue->u.iValue = (unsigned int)( dMediaLengthInches * pEnvironment->dLineSpacing );
}


static int32 OIL_SetFormlines( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 value = pValue->u.iValue;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  double dMediaLengthInches = OIL_GetMediaLength( pEnvironment );

  if( value < gFormlinesRange.min )
  {
    value = gFormlinesRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gFormlinesRange.max )
  {
    value = gFormlinesRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->dLineSpacing = ((double) value) / dMediaLengthInches;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static uint8 * OIL_GetPaperSizeName( PMS_ePaperSize ePaperSize )
{
  uint8 * pzName = gPaperSizeUnknown;

  switch( ePaperSize )
  {
  case PMS_SIZE_LETTER:
  case PMS_SIZE_LETTER_R:
    pzName = gPaperSizeValues[ 0 ];
    break;
  case PMS_SIZE_A4:
  case PMS_SIZE_A4_R:
    pzName = gPaperSizeValues[ 1 ];
    break;
  case PMS_SIZE_LEGAL:
  case PMS_SIZE_LEGAL_R:
    pzName = gPaperSizeValues[ 2 ];
    break;
  case PMS_SIZE_EXE:
  case PMS_SIZE_EXE_R:
    pzName = gPaperSizeValues[ 3 ];
    break;
  case PMS_SIZE_A3:
  case PMS_SIZE_A3_R:
    pzName = gPaperSizeValues[ 4 ];
    break;
  case PMS_SIZE_TABLOID:
  case PMS_SIZE_TABLOID_R:
    pzName = gPaperSizeValues[ 5 ];
    break;
  case PMS_SIZE_A5:
  case PMS_SIZE_A5_R:
    pzName = gPaperSizeValues[ 6 ];
    break;
  case PMS_SIZE_A6:
  case PMS_SIZE_A6_R:
    pzName = gPaperSizeValues[ 7 ];
    break;
  case PMS_SIZE_C5_ENV:
  case PMS_SIZE_C5_ENV_R:
    pzName = gPaperSizeValues[ 8 ];
    break;
  case PMS_SIZE_DL_ENV:
  case PMS_SIZE_DL_ENV_R:
    pzName = gPaperSizeValues[ 9 ];
    break;
  case PMS_SIZE_LEDGER:
  case PMS_SIZE_LEDGER_R:
    pzName = gPaperSizeValues[ 10 ];
    break;
  case PMS_SIZE_OFUKU:
  case PMS_SIZE_OFUKU_R:
    pzName = gPaperSizeValues[ 11 ];
    break;
  case PMS_SIZE_JISB4:
  case PMS_SIZE_JISB4_R:
    pzName = gPaperSizeValues[ 12 ];
    break;
  case PMS_SIZE_JISB5:
  case PMS_SIZE_JISB5_R:
    pzName = gPaperSizeValues[ 13 ];
    break;
  case PMS_SIZE_CUSTOM:
    pzName = gPaperSizeValues[ 14 ];
    break;
  case PMS_SIZE_DONT_KNOW:
    pzName = gPaperSizeUnknown;
    break;
  default:
    HQFAIL("Invalid paper size");
    break;
  }

  return pzName;
}


static uint8 * OIL_GetMediaTypeName( PMS_eMediaType eMediaType )
{
  uint8 * pzName = gMediaTypeUnknown;

  switch( eMediaType )
  {
  case PMS_TYPE_PLAIN:
    pzName = gMediaTypeValues[ 0 ];
    break;
  case PMS_TYPE_THICK:
    pzName = gMediaTypeValues[ 1 ];
    break;
  case PMS_TYPE_THIN:
    pzName = gMediaTypeValues[ 2 ];
    break;
  case PMS_TYPE_BOND:
    pzName = gMediaTypeValues[ 3 ];
    break;
  case PMS_TYPE_LABEL:
    pzName = gMediaTypeValues[ 4 ];
    break;
  case PMS_TYPE_TRANSPARENCY:
    pzName = gMediaTypeValues[ 5 ];
    break;
  case PMS_TYPE_ENVELOPE:
    pzName = gMediaTypeValues[ 6 ];
    break;
  case PMS_TYPE_PREPRINTED:
    pzName = gMediaTypeValues[ 7 ];
    break;
  case PMS_TYPE_LETTERHEAD:
    pzName = gMediaTypeValues[ 8 ];
    break;
  case PMS_TYPE_RECYCLED:
    pzName = gMediaTypeValues[ 9 ];
    break;
  case PMS_TYPE_DONT_KNOW:
    pzName = gMediaTypeUnknown;
    break;
  default:
    HQFAIL("Invalid media type");
    break;
  }

  return pzName;
}


static void OIL_GetInTray1Size( PjlValue * pValue, int fGetDefault )
{
  int32            i;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = gPaperSizeUnknown;

  nTrays = PMS_GetTrayInfo( &pPMSTrays );
  for( i = 0; i < nTrays; i++ )
  {
    if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_TRAY1 )
    {
      pValue->u.pzValue = OIL_GetPaperSizeName( pPMSTrays[ i ].ePaperSize );
      break;
    }
  }
}


static int32 OIL_SetInTray1Size( PjlValue * pValue, int fSetDefault )
{
  int32            i;
  int32            nTrays;
  int32 result = eNoError;
  PMS_TyTrayInfo * pPMSTrays;

  PMS_ePaperSize ePaperSize = PMS_SIZE_DONT_KNOW;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 0 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LETTER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 1 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 2 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEGAL;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 3 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_EXE;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 4 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A3;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 5 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_TABLOID;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 6 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 7 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A6;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 8 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_C5_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 9 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_DL_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 10 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEDGER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 11 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_OFUKU;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 12 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 13 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 14 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_CUSTOM;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueUnsupported );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( ePaperSize != PMS_SIZE_DONT_KNOW )
  {
    nTrays = PMS_GetTrayInfo(&pPMSTrays);

    for( i = 0; i < nTrays; i++ )
    {
      if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_TRAY1 )
      {
        pPMSTrays[1].ePaperSize = ePaperSize ;
        break;
      }
    }

    PMS_SetTrayInfo(&pPMSTrays);
  }

  return result;
}


static void OIL_GetMTraySize( PjlValue * pValue, int fGetDefault )
{
  int32            i;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = gPaperSizeUnknown;

  nTrays = PMS_GetTrayInfo( &pPMSTrays );

  for( i = 0; i < nTrays; i++ )
  {
    if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_MANUALFEED )
    {
      pValue->u.pzValue = OIL_GetPaperSizeName( pPMSTrays[ i ].ePaperSize );
      break;
    }
  }
}


static int32 OIL_SetMTraySize( PjlValue * pValue, int fSetDefault )
{
  int32            i;
  int32            nTrays;
  int32 result = eNoError;
  PMS_TyTrayInfo * pPMSTrays;

  PMS_ePaperSize ePaperSize = PMS_SIZE_DONT_KNOW;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 0 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LETTER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 1 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 2 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEGAL;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 3 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_EXE;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 4 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A3;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 5 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_TABLOID;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 6 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 7 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A6;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 8 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_C5_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 9 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_DL_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 10 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEDGER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 11 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_OFUKU;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 12 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 13 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 14 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_CUSTOM;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueUnsupported );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( ePaperSize != PMS_SIZE_DONT_KNOW )
  {
    nTrays = PMS_GetTrayInfo(&pPMSTrays);

    for( i = 0; i < nTrays; i++ )
    {
      if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_MANUALFEED )
      {
        pPMSTrays[1].ePaperSize = ePaperSize ;
        break;
      }
    }

    PMS_SetTrayInfo(&pPMSTrays);
  }

  return result;
}


static void OIL_GetJobname( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueString;
  pValue->u.pzValue = (uint8 *) pEnvironment->szPjlJobName;
}


static int32 OIL_SetJobname( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  strcpy( pEnvironment->szPjlJobName, (char *) pValue->u.pzValue );
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}

static void OIL_GetImageFile( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueString;
  pValue->u.pzValue = (uint8 *) pEnvironment->szImageFile;
}


static int32 OIL_SetImageFile( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  strcpy( pEnvironment->szImageFile, (char *) pValue->u.pzValue );
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}

static void OIL_GetTestPage( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gOrientationEnum, pEnvironment->eTestPage, pValue );
}


static int32 OIL_SetTestPage( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gTestPageEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->eTestPage = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

static void OIL_GetPrintErrorPage( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gOrientationEnum, pEnvironment->uPrintErrorPage, pValue );
}


static int32 OIL_SetPrintErrorPage( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gPrintErrorPageEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->uPrintErrorPage = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

static void OIL_GetJobOffset( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gJobOffsetEnum, pEnvironment->uJobOffset, pValue );
}


static int32 OIL_SetJobOffset( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gJobOffsetEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->uJobOffset = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetLineTermination( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uLineTermination;
}


static int32 OIL_SetLineTermination( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 value = pValue->u.iValue;

  if( value < gLineTerminationRange.min || value > gLineTerminationRange.max )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  else
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->uLineTermination = value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }

  return result;
}


static void OIL_GetManualFeed( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( pEnvironment->bManualFeed )
  {
    pValue->u.pzValue = gOffOnValues[ 1 ];
  }
  else
  {
    pValue->u.pzValue = gOffOnValues[ 0 ];
  }
}


static int32 OIL_SetManualFeed( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
  {
    /* Manual feed off */
    pEnvironment->bManualFeed = FALSE;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
  {
    /* Manual feed on */
    pEnvironment->bManualFeed = TRUE;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetMediaSource( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  unsigned int nTrays;
  PMS_TyTrayInfo * pPMSTrays;

  nTrays = PMS_GetTrayInfo( &pPMSTrays );

  if( pEnvironment->tDefaultJobMedia.uInputTray < nTrays )
  {
    OIL_GetMappedValue( &gMediaSourceEnum, pPMSTrays[ pEnvironment->tDefaultJobMedia.uInputTray ].eMediaSource, pValue );
  }
}


static int32 OIL_SetMediaSource( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gMediaSourceEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    int iTray;
    int nTrays;
    PMS_TyTrayInfo * pPMSTrays;

    nTrays = PMS_GetTrayInfo( &pPMSTrays );

    for( iTray = 0; iTray < nTrays; iTray++ )
    {
      if(( pPMSTrays[ iTray ].eMediaSource == pMappedValue->value )||(pMappedValue->value == PMS_TRAY_AUTO))
      {
        pEnvironment->tDefaultJobMedia.uInputTray = pMappedValue->value;
        OIL_UpdateEnvironment( pEnvironment, fSetDefault );
        break;
      }
    }

    if( iTray == nTrays )
    {
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetMediaType( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;

  if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Thick" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 1 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Thin" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 2 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Bond" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 3 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Label" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 4 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Transparency" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 5 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Envelope" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 6 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Preprinted" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 7 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Letterhead" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 8 ];
  }
  else if( strcmp( (const char *) pEnvironment->tDefaultJobMedia.szMediaType, "Recycled" ) == 0 )
  {
    pValue->u.pzValue = gMediaTypeValues[ 9 ];
  }
  else
  {
    pValue->u.pzValue = gMediaTypeValues[ 0 ];
  }
}


static int32 OIL_SetMediaType( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 0 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Plain" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 1 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Thick" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 2 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Thin" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 3 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Bond" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 4 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Label" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 5 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Transparency" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 6 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Envelope" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 7 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Preprinted" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 8 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Letterhead" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 9 ] ) == 0 )
  {
    strcpy( (char *) pEnvironment->tDefaultJobMedia.szMediaType, "Recycled" );
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetMediaTypeMTray( PjlValue * pValue, int fGetDefault )
{
  int32            i;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = gMediaTypeUnknown;

  nTrays = PMS_GetTrayInfo( &pPMSTrays );
  for( i = 0; i < nTrays; i++ )
  {
    if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_MANUALFEED )
    {
      pValue->u.pzValue = OIL_GetMediaTypeName( pPMSTrays[ i ].eMediaType );
      break;
    }
  }
}


static int32 OIL_SetMediaTypeMTray( PjlValue * pValue, int fSetDefault )
{
  int32 i;
  int32 result = eNoError;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;
  PMS_eMediaType eMediaType = PMS_TYPE_DONT_KNOW;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 0 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_PLAIN;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 1 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_THICK;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 2 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_THIN;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 3 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_BOND;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 4 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_LABEL;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 5 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_TRANSPARENCY;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 6 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_ENVELOPE;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 7 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_PREPRINTED;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 8 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_LETTERHEAD;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 9 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_RECYCLED;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( eMediaType != PMS_TYPE_DONT_KNOW )
  {
    nTrays = PMS_GetTrayInfo(&pPMSTrays);

    for( i = 0; i < nTrays; i++ )
    {
      if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_MANUALFEED )
      {
        pPMSTrays[i].eMediaType = eMediaType ;
        break;
      }
    }

    PMS_SetTrayInfo(&pPMSTrays);
  }

  return result;
}


static void OIL_GetMediaTypeTray1( PjlValue * pValue, int fGetDefault )
{
  int32            i;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;

  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = gMediaTypeUnknown;

  nTrays = PMS_GetTrayInfo( &pPMSTrays );
  for( i = 0; i < nTrays; i++ )
  {
    if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_TRAY1 )
    {
      pValue->u.pzValue = OIL_GetMediaTypeName( pPMSTrays[ i ].eMediaType );
      break;
    }
  }
}


static int32 OIL_SetMediaTypeTray1( PjlValue * pValue, int fSetDefault )
{
  int32 i;
  int32 result = eNoError;
  int32            nTrays;
  PMS_TyTrayInfo * pPMSTrays;
  PMS_eMediaType eMediaType = PMS_TYPE_DONT_KNOW;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 0 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_PLAIN;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 1 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_THICK;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 2 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_THIN;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 3 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_BOND;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 4 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_LABEL;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 5 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_TRANSPARENCY;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 6 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_ENVELOPE;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 7 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_PREPRINTED;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 8 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_LETTERHEAD;
  }
  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gMediaTypeValues[ 9 ] ) == 0 )
  {
    eMediaType = PMS_TYPE_RECYCLED;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( eMediaType != PMS_TYPE_DONT_KNOW )
  {
    nTrays = PMS_GetTrayInfo(&pPMSTrays);

    for( i = 0; i < nTrays; i++ )
    {
      if( pPMSTrays[ i ].eMediaSource == PMS_TRAY_TRAY1 )
      {
        pPMSTrays[i].eMediaType = eMediaType ;
        break;
      }
    }

    PMS_SetTrayInfo(&pPMSTrays);
  }

  return result;
}


static void OIL_GetOrientation( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gOrientationEnum, pEnvironment->uOrientation, pValue );
}


static int32 OIL_SetOrientation( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gOrientationEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->uOrientation = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetOutBin( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gOutBinEnum, pEnvironment->tDefaultJobMedia.eOutputTray, pValue );
}


static int32 OIL_SetOutBin( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gOutBinEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->tDefaultJobMedia.eOutputTray = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetPages( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cPagesPrinted;
}


static void OIL_GetPaper( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = OIL_GetPaperSizeName( pEnvironment->tDefaultJobMedia.ePaperSize );
}


static int32 OIL_SetPaper( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_ePaperSize ePaperSize = PMS_SIZE_DONT_KNOW;

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 0 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LETTER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 1 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 2 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEGAL;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 3 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_EXE;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 4 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A3;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 5 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_TABLOID;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 6 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 7 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_A6;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 8 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_C5_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 9 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_DL_ENV;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 10 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_LEDGER;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 11 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_OFUKU;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 12 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB4;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 13 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_JISB5;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPaperSizeValues[ 14 ] ) == 0 )
  {
    ePaperSize = PMS_SIZE_CUSTOM;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueUnsupported );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( ePaperSize != PMS_SIZE_DONT_KNOW )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
    PMS_TyPaperInfo * pPaperInfo = NULL;
    PMS_TyPaperInfo * pPaperInfoDK = NULL;

    pEnvironment->tDefaultJobMedia.ePaperSize = ePaperSize;
    gOilPjlContext.nPaperSize = ePaperSize;

    PMS_GetPaperInfo( ePaperSize, &pPaperInfo );
    HQASSERT(pPaperInfo, "No paper info");
    pEnvironment->tDefaultJobMedia.dWidth = pPaperInfo->dWidth;
    pEnvironment->tDefaultJobMedia.dHeight = pPaperInfo->dHeight;

    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  /* Update the PMS definition as it is used by the PCL procset  for paper selection */
    PMS_GetPaperInfo( PMS_SIZE_DONT_KNOW, &pPaperInfoDK );
    /* update PSName only, as dimensins etc. not used for matching */
    strcpy((char *)(pPaperInfoDK->szPSName), (char *)(pPaperInfo->szPSName));
    PMS_SetPaperInfo( PMS_SIZE_DONT_KNOW, &pPaperInfoDK );
  }

  return result;
}


static void OIL_GetPassword( PjlValue * pValue, int fGetDefault )
{
  /* Reports whether PMS PJL password setting is 0 (disabled)
   * or not for both DINQUIRE and INQUIRE commands.
   */
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;
  pValue->u.pzValue = ( OIL_GetPMSPjlPassword() == 0 ) ? gPasswordDisabled : gPasswordEnabled;
}


static int32 OIL_SetPassword( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 value = pValue->u.iValue;

  if( fSetDefault )
  {
    if( OIL_InSecureJob() )
    {
      if( value >= gPasswordRange.min && value <= gPasswordRange.max )
      {
        OIL_SetPMSPjlPassword( value );
      }
      else
      {
        /* Password value is out of range */
        result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueOutOfRange );
      }
    }
    else
    {
      /* Trying to change password without having passed correct value to JOB command */
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, ePasswordProtected );
      result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
    }
  }
  else
  {
    /* Silently fail for SET PASSWORD */
  }

  return result;
}


static void OIL_GetPersonality( PjlValue * pValue, int fGetDefault )
{
  int32 ePersonality;

  ePersonality = fGetDefault ? OIL_GetPMSPersonality() : gOilPjlContext.currentEnvironment.ePersonality ;

  pValue->eType = eValueAlphanumeric;

  switch( ePersonality )
  {
  case eAutoPDL:
    pValue->u.pzValue = gPersonalityValues[ 0 ];
    break;
  case ePCL5c:
  case ePCL5e:
    pValue->u.pzValue = gPersonalityValues[ 1 ];
    break;
  case ePDF:
    pValue->u.pzValue = gPersonalityValues[ 2 ];
    break;
  case ePostScript:
    pValue->u.pzValue = gPersonalityValues[ 3 ];
    break;
  case eZIP:
    pValue->u.pzValue = gPersonalityValues[ 4 ];
    break;
  default:
    HQFAIL("Invalid personality");
    break;
  }
}


static int32 OIL_SetPersonality( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 ePersonality = eUnknown;

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPersonalityValues[ 0 ] ) == 0 )
  {
    ePersonality = eAutoPDL;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPersonalityValues[ 1 ] ) == 0 )
  {
    ePersonality = ePCL5c;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPersonalityValues[ 2 ] ) == 0 )
  {
    ePersonality = ePDF;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPersonalityValues[ 3 ] ) == 0 )
  {
    ePersonality = ePostScript;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gPersonalityValues[ 4 ] ) == 0 )
  {
    ePersonality = eZIP;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  if( ePersonality != eUnknown )
  {
    if( fSetDefault )
    {
      OIL_SetPMSPersonality( ePersonality );
    }
    else
    {
      gOilPjlContext.currentEnvironment.ePersonality = ePersonality;
      PjlSetDefaultPersonality( gOilPjlContext.pParserContext, ePersonality );
    }
  }

  return result;
}


static void OIL_GetPitch( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueDouble;
  pValue->u.dValue = pEnvironment->dPitch;
}


static int32 OIL_SetPitch( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  double value = pValue->u.dValue;

  if( value < gPitchRange.min )
  {
    value = gPitchRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gPitchRange.max )
  {
    value = gPitchRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->dPitch = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetPtsize( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueDouble;
  pValue->u.dValue = pEnvironment->dPointSize;
}


static int32 OIL_SetPtsize( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  double value = pValue->u.dValue;

  if( value < gPtsizeRange.min )
  {
    value = gPtsizeRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gPtsizeRange.max )
  {
    value = gPtsizeRange.max;
    result = eOptionValueDataLossRange;
  }
  else
  {
    /* In range - round down to multiple of 0.25 */
    double roundedValue = (double)((int32)(value * 4.0)) / 4.0;
    if( fabs( value - roundedValue ) > 0.005 )
    {
      value = roundedValue;
      result = eOptionValueDataLossConversion;
    }
  }

  pEnvironment->dPointSize = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetRenderMode( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gRenderModeEnum, pEnvironment->eRenderMode, pValue );
}


static int32 OIL_SetRenderMode( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gRenderModeEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->eRenderMode = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetRenderModel( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gRenderModelEnum, pEnvironment->eRenderModel, pValue );
}


static int32 OIL_SetRenderModel( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gRenderModelEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->eRenderModel = pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetResolution( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uXResolution; /* TODO: do we need to separate X and Y ? */
}


static int32 OIL_SetResolution( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 fKnownValue = FALSE;
  int32 i;

  for( i = 0; i < gResolutionEnum.nValues; i++ )
  {
    if( pValue->u.iValue == gResolutionValues[ i ] )
    {
      /* TODO: do we need to separate X and Y ? 
      For the time being, let us set both X and Y to be the same*/
      pEnvironment->uXResolution = pValue->u.iValue;
      pEnvironment->uYResolution = pEnvironment->uXResolution;

      OIL_UpdateEnvironment( pEnvironment, fSetDefault );

      fKnownValue = TRUE;
      break;
    }
  }

  if( ! fKnownValue )
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetSymset( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gSymsetEnum, pEnvironment->uSymbolSet, pValue );
}


static int32 OIL_SetSymset( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gSymsetEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    if( pEnvironment->uSymbolSet != (unsigned int) pMappedValue->value  )
    {
      /* Changing value */
      pEnvironment->uSymbolSet = (unsigned int) pMappedValue->value;

      /* Update fontsource (TODO) and fontnumber to highest priority default marked font */
      pEnvironment->uFontNumber = gFontnumberRange.min;

      OIL_UpdateEnvironment( pEnvironment, fSetDefault );
    }
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionValueUnsupported );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetUsername( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueString;
  pValue->u.pzValue = (uint8 *) pEnvironment->szUserName;
}


static int32 OIL_SetUsername( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  strcpy( pEnvironment->szUserName, (char *) pValue->u.pzValue );
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetWideA4( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gWideA4Enum, pEnvironment->bWideA4, pValue );
}


static int32 OIL_SetWideA4( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gWideA4Enum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->bWideA4 = (unsigned char)pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}

/* Ustatus category getters and setters */

static void OIL_GetDeviceStatus( PjlValue * pValue, int fGetDefault )
{
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;

  switch( gOilPjlContext.deviceStatusValue )
  {
  case eDeviceStatusOff:
    pValue->u.pzValue = gDeviceStatusValues[ 0 ];
    break;
  case eDeviceStatusOn:
    pValue->u.pzValue = gDeviceStatusValues[ 1 ];
    break;
  case eDeviceStatusVerbose:
    pValue->u.pzValue = gDeviceStatusValues[ 2 ];
    break;
  default:
    HQFAIL("Invalid device status");
    break;
  }
}


static int32 OIL_SetDeviceStatus( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gDeviceStatusValues[ 0 ] ) == 0 )
  {
    gOilPjlContext.deviceStatusValue = eDeviceStatusOff;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gDeviceStatusValues[ 1 ] ) == 0 )
  {
    gOilPjlContext.deviceStatusValue = eDeviceStatusOn;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gDeviceStatusValues[ 2 ] ) == 0 )
  {
    gOilPjlContext.deviceStatusValue = eDeviceStatusVerbose;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetJobStatus( PjlValue * pValue, int fGetDefault )
{
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;

  switch( gOilPjlContext.jobStatusValue )
  {
  case eoilpjl_Off:
    pValue->u.pzValue = gOffOnValues[ 0 ];
    break;
  case eoilpjl_On:
    pValue->u.pzValue = gOffOnValues[ 1 ];
    break;
  default:
    HQFAIL("Invalid job status");
    break;
  }
}


static int32 OIL_SetJobStatus( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
  {
    gOilPjlContext.jobStatusValue = eoilpjl_Off;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
  {
    gOilPjlContext.jobStatusValue = eoilpjl_On;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetPageStatus( PjlValue * pValue, int fGetDefault )
{
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueAlphanumeric;

  switch( gOilPjlContext.pageStatusValue )
  {
  case eoilpjl_Off:
    pValue->u.pzValue = gOffOnValues[ 0 ];
    break;
  case eoilpjl_On:
    pValue->u.pzValue = gOffOnValues[ 1 ];
    break;
  default:
    HQFAIL("Invalid page status");
    break;
  }
}


static int32 OIL_SetPageStatus( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  UNUSED_PARAM( int, fSetDefault );

  if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 0 ] ) == 0 )
  {
    gOilPjlContext.pageStatusValue = eoilpjl_Off;
  }
  else if( strcmp( (const char *) pValue->u.pzValue, (const char *) gOffOnValues[ 1 ] ) == 0 )
  {
    gOilPjlContext.pageStatusValue = eoilpjl_On;
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetTimedStatus( PjlValue * pValue, int fGetDefault )
{
  UNUSED_PARAM( int, fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = gOilPjlContext.timedStatusValue;
}


static int32 OIL_SetTimedStatus( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );

  if( value != gTimedStatusRange.special )
  {
    if( value < gTimedStatusRange.min )
    {
      value = gTimedStatusRange.min;
      result = eOptionValueDataLossRange;
    }
    else if( value > gTimedStatusRange.max )
    {
      value = gTimedStatusRange.max;
      result = eOptionValueDataLossRange;
    }
  }

  gOilPjlContext.timedStatusValue = value;

  return result;
}

static void OIL_GetCMD_A( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.dValue = pmsSysInfo.fTrapWidth;
}


static int32 OIL_SetCMD_A( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  double value = pValue->u.dValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdTrapRange.min )
  {
    value = gCmdTrapRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdTrapRange.max )
  {
    value = gCmdTrapRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.fTrapWidth = (float) value;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_D( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;

  switch( pEnvironment->eImageQuality )
  {
  case PMS_1BPP:
    pValue->u.iValue = 1;
    break;
  case PMS_2BPP:
    pValue->u.iValue = 2;
    break;
  case PMS_4BPP:
    pValue->u.iValue = 4;
    break;
  case PMS_8BPP_CONTONE:
    pValue->u.iValue = 8;
    break;
  case PMS_16BPP_CONTONE:
    pValue->u.iValue = 16;
    break;
  default:
    HQFAIL("Invalid depth");
    break;
  }
}


static int32 OIL_SetCMD_D( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

  if( pValue->u.iValue == gBitsPerPixelValues[ 0 ] )
  {
    pEnvironment->eImageQuality = PMS_16BPP_CONTONE;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 1 ] )
  {
    pEnvironment->eImageQuality = PMS_8BPP_CONTONE;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 2 ] )
  {
    pEnvironment->eImageQuality = PMS_4BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 3 ] )
  {
    pEnvironment->eImageQuality = PMS_2BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else if( pValue->u.iValue == gBitsPerPixelValues[ 4 ] )
  {
    pEnvironment->eImageQuality = PMS_1BPP;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetCMD_E( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  pValue->eType = eValueAlphanumeric;

  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  if((pmsSysInfo.uUseEngineSimulator == FALSE) && (pmsSysInfo.uUseRIPAhead == TRUE))
  {
    pValue->u.iValue = eEngSimOff;
    OIL_GetMappedValue( &gEngineSimulatorEnum, pValue->u.iValue, pValue );
  }
  else if((pmsSysInfo.uUseEngineSimulator == TRUE) && (pmsSysInfo.uUseRIPAhead == TRUE))
  {
    pValue->u.iValue = eEngSimOn;
    OIL_GetMappedValue( &gEngineSimulatorEnum, pValue->u.iValue, pValue );
  }
  else if((pmsSysInfo.uUseEngineSimulator == FALSE) && (pmsSysInfo.uUseRIPAhead == FALSE))
  {
    pValue->u.iValue = eEngSimByPass;
    OIL_GetMappedValue( &gEngineSimulatorEnum, pValue->u.iValue, pValue );
  }
  else
  {
    HQFAIL("Invalid configuration");
  }
}


static int32 OIL_SetCMD_E( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gEngineSimulatorEnum, pValue->u.pzValue );
  UNUSED_PARAM( int, fSetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( pMappedValue != NULL )
  {
    if(pMappedValue->value == eEngSimOff)
    {
      pmsSysInfo.uUseEngineSimulator = FALSE ;
      pmsSysInfo.uUseRIPAhead = TRUE ;
    }
    else if(pMappedValue->value == eEngSimOn)
    {
      pmsSysInfo.uUseEngineSimulator = TRUE ;
      pmsSysInfo.uUseRIPAhead = TRUE ;
    }
    else
    {
      pmsSysInfo.uUseEngineSimulator = FALSE ;
      pmsSysInfo.uUseRIPAhead = FALSE ;
    }
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  return result;
}


static void OIL_GetCMD_G( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.uColorManagement;
}


static int32 OIL_SetCMD_G( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.uColorManagement = value;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_H( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->eScreenMode;
}


static int32 OIL_SetCMD_H( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 value = pValue->u.iValue;

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->eScreenMode = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetCMD_J( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;

  /* The J command line parameter controls two variables */
  switch(pmsSysInfo.eBandDeliveryType) {
  case 3: /* direct single scanline interleaved */
    if(pmsSysInfo.bScanlineInterleave) {
      pValue->u.iValue = 5;
    } else {
      pValue->u.iValue = 3;
    }
    break;
  case 4: /* direct frame scanline interleaved */
    if(pmsSysInfo.bScanlineInterleave) {
      pValue->u.iValue = 6;
    } else {
      pValue->u.iValue = 4;
    }
    break;
  default: /* 0 to 4 map directly without the scanline interleave flag */
    pValue->u.iValue = pmsSysInfo.eBandDeliveryType;
    break;
  }
}


static int32 OIL_SetCMD_J( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gRipMemRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  /* The J command line parameter controls two variables */
  /* \todo It's not ideal to implement a duplicate mapping here
           as well as in the command line parser (pms_main.c).
           Consider implementing a common mapping function. */
  switch(value) {
  case 5: /* direct single scanline interleaved */
    pmsSysInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_SINGLE;
    pmsSysInfo.bScanlineInterleave = TRUE;
    break;
  case 6: /* direct frame scanline interleaved */
    pmsSysInfo.eBandDeliveryType = PMS_PUSH_BAND_DIRECT_FRAME;
    pmsSysInfo.bScanlineInterleave = TRUE;
    break;
  default: /* 0 to 4 map directly */
    pmsSysInfo.eBandDeliveryType = value;
    pmsSysInfo.bScanlineInterleave = FALSE;
    break;
  }

  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_K( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  OIL_GetMappedValue( &gBlackDetectEnum, pEnvironment->bForceMonoIfCMYblank, pValue );
}


static int32 OIL_SetCMD_K( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gBlackDetectEnum, pValue->u.pzValue );

  if( pMappedValue != NULL )
  {
    PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );

    pEnvironment->bBlackSubstitute = (unsigned char)pMappedValue->value;
    OIL_UpdateEnvironment( pEnvironment, fSetDefault );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }

  return result;
}


static void OIL_GetCMD_M( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbRIPMemory;
}


static int32 OIL_SetCMD_M( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gRipMemRange.min )
  {
    value = gRipMemRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gRipMemRange.max )
  {
    value = gRipMemRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbRIPMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MAPP( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbAppMemory;
}


static int32 OIL_SetCMD_MAPP( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbAppMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MBUF( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbReceiveBuffer;
}


static int32 OIL_SetCMD_MBUF( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbReceiveBuffer = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MJOB( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbJobMemory;
}


static int32 OIL_SetCMD_MJOB( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbJobMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MMISC( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbMiscMemory;
}


static int32 OIL_SetCMD_MMISC( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbMiscMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MPMS( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbPMSMemory;
}


static int32 OIL_SetCMD_MPMS( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbPMSMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_MSYS( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.cbSysMemory;
}


static int32 OIL_SetCMD_MSYS( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.cbSysMemory = value * 1024 * 1024;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}



static void OIL_GetCMD_N( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.nRendererThreads;
}


static int32 OIL_SetCMD_N( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gRenderThreadsRange.min )
  {
    value = gRenderThreadsRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gRenderThreadsRange.max )
  {
    value = gRenderThreadsRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.nRendererThreads= value;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_O( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;
  UNUSED_PARAM( int, fGetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  
  pValue->eType = eValueAlphanumeric;
  pValue->u.iValue = pmsSysInfo.eOutputType;
}


static int32 OIL_SetCMD_O( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;

  OilMappedValue * pMappedValue = OIL_LookupMappedString( &gOutputTypeEnum, pValue->u.pzValue );
  UNUSED_PARAM( int, fSetDefault );

  if( pMappedValue != NULL )
  {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
    pmsSysInfo.eOutputType = pMappedValue->value;
    PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  }
  else
  {
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eUnsupportedOption );
    result = PjlCombineErrors( gOilPjlContext.pParserContext, result, eOptionMissing );
  }
  
  return result;
}


static void OIL_GetCMD_P( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.ePaperSelectMode;
}


static int32 OIL_SetCMD_P( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  UNUSED_PARAM( int, fSetDefault );
  PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.ePaperSelectMode = value;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );

  return result;
}


static void OIL_GetCMD_R( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->eColorMode;
}


static int32 OIL_SetCMD_R( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 value = pValue->u.iValue;

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->eColorMode = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetCMD_X( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uXResolution;
}


static int32 OIL_SetCMD_X( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 value = pValue->u.iValue;

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->uXResolution = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetCMD_Y( PjlValue * pValue, int fGetDefault )
{
  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fGetDefault );

  pValue->eType = eValueInt;
  pValue->u.iValue = pEnvironment->uYResolution;
}


static int32 OIL_SetCMD_Y( PjlValue * pValue, int fSetDefault )
{
  int32 result = eNoError;

  PMS_TyJob * pEnvironment = OIL_GetEnvironment( fSetDefault );
  int32 value = pValue->u.iValue;

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pEnvironment->uYResolution = value;
  OIL_UpdateEnvironment( pEnvironment, fSetDefault );

  return result;
}


static void OIL_GetSYSTEM_Restart( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  /* INQUIRE and DINQUIRE both return the same PMS system value */
  UNUSED_PARAM( int, fGetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.nRestart;
}

static int32 OIL_SetSYSTEM_Restart( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  /* Both SET and DEFAULT are implemented in the same way.
     Restart once only at the end of the current job. It is 
     technically possible to implement DEFAULT as restart after
     every job, and does make sense to do so in some implementations.
     However, it has been decided that we don't want make it available
     as a feature as putting the rip into 'restart every job' mode
     will impact performance.
     So, 'Restart' can only happen after a job that contains the
     restart command. */
  UNUSED_PARAM( int, fSetDefault );

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.nRestart = value;
  PMS_SetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  return result;
}


static void OIL_GetSYSTEM_StrideBoundary( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  if(fGetDefault) {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.nStrideBoundaryBytes;
}

static int32 OIL_SetSYSTEM_StrideBoundary( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  /* Note that both SET and DEFAULT have been implement here, however only DEFAULT 
     will have an effect. The system parameters are only read once, when the 
     PMS starts the OIL, so using PJL SET will be too late. */
  if(fSetDefault) {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.nStrideBoundaryBytes = value;
  if(fSetDefault) {
    PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_SetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }

  return result;
}


static void OIL_GetSYSTEM_PrintableMode( PjlValue * pValue, int fGetDefault )
{
  PMS_TySystem pmsSysInfo;

  if(fGetDefault) {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }

  PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);

  pValue->eType = eValueInt;
  pValue->u.iValue = pmsSysInfo.nPrintableMode;
}

static int32 OIL_SetSYSTEM_PrintableMode( PjlValue * pValue, int fSetDefault )
{
  PMS_TySystem pmsSysInfo;
  int32 result = eNoError;
  int32 value = pValue->u.iValue;

  /* Note that both SET and DEFAULT have been implement here, however only DEFAULT 
     will have an effect. The system parameters are only read once, when the 
     PMS starts the OIL, so using PJL SET will be too late. */
  if(fSetDefault) {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_GetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }

  if( value < gCmdIntRange.min )
  {
    value = gCmdIntRange.min;
    result = eOptionValueDataLossRange;
  }
  else if( value > gCmdIntRange.max )
  {
    value = gCmdIntRange.max;
    result = eOptionValueDataLossRange;
  }

  pmsSysInfo.nPrintableMode = value;
  if(fSetDefault) {
    PMS_SetSystemInfo( &pmsSysInfo , PMS_NextSettings );
  } else {
    PMS_SetSystemInfo( &pmsSysInfo , PMS_CurrentSettings);
  }
  return result;
}

#endif


