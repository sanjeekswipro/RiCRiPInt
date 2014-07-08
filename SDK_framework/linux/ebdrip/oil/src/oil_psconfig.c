/* Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_psconfig.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief OIL RIP configuration Handler
 *
 * This file contains functions that manage configuration of the RIP for every job.
 *
 */
#include "oil.h"
#include "oil_psconfig.h"
#include "skinkit.h"
#include <string.h> /* for strlen */
#include <stdio.h>  /* for sprintf */
#include <math.h>  /* for pow */
#include "oil_stream.h"
#include "oil_malloc.h"
#include "oil_media.h"
#include "oil_interface_oil2pms.h"

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;

/*! \brief Holds Job's comment value which preceed with %% sign */
OIL_TyCommentParser g_CommentParser;
/*! \brief Holds Job's default value which preceed with %% sign */
OIL_TyCommentParser g_CommentParserDefault;
/**
 * \brief PostScript language snippet to configure XPS processing.
 */
static char pszConfigXPS1[] =
      "%!PS                                       \n"
      "/unmountZIP {                              \n"
      "   mark (%XPSStream%) devstatus            \n"
      "   {cleartomark (%XPSStream%) devdismount} \n"
      "   { pop } ifelse                          \n"
      "} bind def                                 \n"
      "                \n"
      "/openZIP {                                 \n"
      "   (%XPSStream%) dup devmount pop          \n"
      "   << /DeviceType 26                       \n"
      "      /Password 0                          \n"
      "      /Enable true                         \n"
      "      /CheckCRC32 true                     \n"
      "      /OpenPackage true                    \n"
      "      /Streamed true                       \n"
      "   >> setdevparams                         \n"
      "                \n"
      "   (%XPSStream%)                           \n"
      "   << /DataSource ((%pipeline%/input) (r) file)\n"
      "   >> setdevparams                         \n"
      "} bind def                                 \n"
      "                                           \n"
      "/doXPS {                                   \n"
      "   (%XPSStream%_rels/.rels) (r) file xmlexec \n"
      "   (%XPSStream%) << /Flush true >> setdevparams \n"
      "   (%XPSStream%) << /Close true >> setdevparams \n"
      "} bind def      \n"
      "                \n"
      "unmountZIP      \n"
      "openZIP         \n"
      "doXPS           \n";
static char pszConfigXPS2[] =
      "%!PS\n"

      "/unmountZIP {"
          "mark (%XPSDirect%) devstatus"
          "{cleartomark (%XPSDirect%) devdismount}"
          "{pop} ifelse"
      "} bind def"

      "/openZIP {"
          "(%XPSDirect%) dup devmount pop"
          "<< /DeviceType 26"
              "/Password 0"
              "/Enable true"
              "/CheckCRC32 true"
              "/OpenPackage true"
              "/Streamed false"
          ">> setdevparams"

          "(%XPSDirect%)"
          "<< /Filename (%embedded%/XPSdirect) >> setdevparams"
      "} bind def"

      "/doXPS {"
          "(%XPSDirect%_rels/.rels) (r) file xmlexec"
          "(%XPSDirect%) << /Close true >> setdevparams"
      "} bind def "

      "unmountZIP "
      "openZIP "
      "doXPS\n";

/**
 * \brief PostScript language snippet to configure PDF processing.
 *
 * Specifically, this snippet directs the RIP to read data from the
 * PDF spooler device or direct from file with a password option.
 */
static char pszConfigPDF1[] =
      "(%%pdfspool%%) (r) file << /OwnerPasswords [ (%s) ] >> pdfexec \n";
static char pszConfigPDF2[] =
      "(%%embedded%%/PDFdirect) (r) file << /OwnerPasswords [ (%s) ] >> pdfexec \n";

/**
 * \brief PostScript language snippet to configure PCL5 processing.
 */
static char pszConfigPCL5[] =
      "(%%pipeline%%/input) (r) file"
      " <<"
      " /BackChannel (%%embedded%%/BackChannel)"
      " /Copies %d"
      " /Courier %d"
      " /FontNumber %d"
      " /FontSource (%s)"
      " /LineTermination %d"
      " /PCL5Mode %s"
      " /Pitch %.2f"
      " /PointSize %.2f"
      " /SymbolSet %d"
      " >> pcl5exec\n";

/**
 * \brief PostScript language snippet to configure PCL XL processing.
 */
static char pszConfigPCLXL[] =
      "(%%pipeline%%/input) (r) file"
      " <<"
      " /BackChannel (%%embedded%%/BackChannel)"
      " /Copies %d"
      " /Courier %d"
      " /FontNumber %d"
      " /FontSource (%s)"
      " /LineTermination %d"
      " /Pitch %.2f"
      " /PointSize %.2f"
      " /SymbolSet %d"
      " >> pclxlexec\n";

static char pszMediaPCL_1[] =
"statusdict begin\n"
"  /GGEBDPCL5PageSizeValues\n"
"  <<\n";
static char pszMediaPCL_2[] =
"  >> def\n"
"  /GGEBDPCLXLPageSizeValues\n"
"  <<\n";
static char pszMediaPCL_3[] =
"  >> def\n"
"  /GGEBDPCLXLPageSizeStrings\n"
"  <<\n";
static char pszMediaPCL_4[] =
"  >> def\n"
"  /GGEBDPCLPageSizes\n"
"  <<\n";
static char pszMediaPCL_5[] =
"  >> def\n"
"  /GGEBDPCL5PageLengthSizes\n"
"  [\n";
static char pszMediaPCL_6[] =
"  ] def\n"
"  /GGEBDPCL5MediaTypeValues\n"
"  <<\n";
static char pszMediaPCL_7[] =
"  >> def\n"
"  /GGEBDPCL5MediaTypeStrings\n"
"  <<\n";
static char pszMediaPCL_8[] =
"  >> def\n"
"  /GGEBDPCLXMediaTypeStrings\n"
"  <<\n";
static char pszMediaPCL_9[] =
"  >> def\n"
"  /GGEBDPCL5MediaSourceValues\n"
"  <<\n";
static char pszMediaPCL_10[] =
"  >> def\n"
"  /GGEBDPCLXMediaSourceValues\n"
"  <<\n";
static char pszMediaPCL_11[] =
"  >> def\n"
"  /GGEBDPCL5MediaDestStrings\n"
"  <<\n";
static char pszMediaPCL_12[] =
"  >> def\n"
"  /GGEBDPCLXMediaDestStrings\n"
"  <<\n";
static char pszMediaPCL_13[] =
"  >> def\n"
"  /GGEBDPCLXLCustomPageSizes\n"
"  [\n";
static char pszMediaPCL_14[] =
"  ] def\n"
"end\n";

static size_t scopy(char *dest, const char *src)
{
  strcpy(dest, src) ;
  return strlen(dest) ;
}

/* change /JobTiming true to /JobTiming false to disable job timing output in monitor window */
#define JOB_TIMING "(%progress%) dup devstatus { 8 { pop } repeat << /JobTiming true >> setdevparams }{ pop }ifelse "

#define MAKESETREALDEVICE "/HqnEmbedded /ProcSet findresource /makesetrealdevice get exec "

#define LASTCONFIG_PS "(%pipeline%/input) (r) file (%embedded%/BackChannel) (w) file true "
#define LASTCONFIG_NON_PS "(%console%) (r) file (%embedded%/BackChannel) (w) file true "

unsigned char szJobStartPS[] = JOB_TIMING MAKESETREALDEVICE LASTCONFIG_PS ;
unsigned char szJobStartNonPS[] = JOB_TIMING MAKESETREALDEVICE LASTCONFIG_NON_PS ;

unsigned char szQuitPS[] = "$printerdict /superstop dup put systemdict begin quit" ;

/**
 * \brief Get the unformatted PostScript language snippet which forms the start of the RIP
          configuration string.
 *
 * If the PDL type is not recognized, it is assumed to be a PostScript language job.
 * \param[in]   ePDLType   The PDL of the current job.
 * \return      A pointer to the appropriate PostScript language snippet.
 */
unsigned char * GetConfigPS( OIL_eTyPDLType ePDLType )
{
  uint8 * pConfigPS = NULL;

  switch( ePDLType )
  {
  case OIL_PDL_PS:
    pConfigPS = szJobStartPS;
    break;
  case OIL_PDL_XPS:
  case OIL_PDL_PDF:
  case OIL_PDL_PCL5c:
  case OIL_PDL_PCL5e:
  case OIL_PDL_PCLXL:
    pConfigPS = szJobStartNonPS;
    break;
  case OIL_PDL_Unknown:
  default:
    GG_SHOW(GG_SHOW_OIL, "GetConfigPS: Unknown PDL - ASSUME PS\n");
    pConfigPS = szJobStartPS;
    break;
  }

  return pConfigPS;
}


/**
 * \brief Prepare a PostScript language snippet to configure the RIP for PCL5 input
 *
 * This function creates a PostScript language snippet based upon the preformatted string
 * pszConfigPCL5 and the settings of the current job and writes it to the buffer supplied.
 * \param[out]   pBuff       A pointer to a character buffer to receive the PostScript language snippet.
 */
static void SetupPCL5( char *pBuff )
{
  sprintf( pBuff, pszConfigPCL5,
    g_pstCurrentJob->uCopyCount,
    g_pstCurrentJob->bCourierDark ? 1 : 0,
    g_pstCurrentJob->uFontNumber,
    g_pstCurrentJob->szFontSource,
    g_pstCurrentJob->uLineTermination,
    g_pstCurrentJob->ePDLType == OIL_PDL_PCL5c ? "/PCL5c" : "/PCL5e",
    g_pstCurrentJob->dPitch,
    g_pstCurrentJob->dPointSize,
    g_pstCurrentJob->uSymbolSet
    );
}


/**
 * \brief Prepare a PostScript language snippet to configure the RIP for PCL XL input
 *
 * This function creates a PostScript language snippet based upon the preformatted string
 * pszConfigPCLXL and the settings of the current job and writes it to the buffer supplied.
 * \param[out]   pBuff       A pointer to a character buffer to receive the PostScript language snippet.
 */
static void SetupPCLXL( char *pBuff )
{
  sprintf( pBuff, pszConfigPCLXL,
    g_pstCurrentJob->uCopyCount,
    g_pstCurrentJob->bCourierDark ? 1 : 0,
    g_pstCurrentJob->uFontNumber,
    g_pstCurrentJob->szFontSource,
    g_pstCurrentJob->uLineTermination,
    g_pstCurrentJob->dPitch,
    g_pstCurrentJob->dPointSize,
    g_pstCurrentJob->uSymbolSet
    );
}


/**
 * \brief Prepare the RIP and OIL for the requested PDL
 *
 * \param[in]   bSendFlag   If TRUE, the PostScript language configuration code is sent
                            to the RIP before the function returns. If FALSE, the
                            PostScript language code will be filled into the buffer supplied
                            by the caller (pBuff), and not sent to the RIP.
 * \param[out]  pBuff       If bSendFlag is TRUE, this parameter is ignored. If bSendFlag
                            is FALSE, the PostScript language code will be written into this
                            buffer instead of being sent to the RIP.
 * \param[in]   nBuffSize   If bSendFlag is TRUE, this parameter is ignored. It specifies the
                            maximum size of pBuff.
 * \return      Returns TRUE if the function completes successfully, or FALSE if an error occurs.
 */
BOOL SetupPDLType(BOOL bSendFlag, char *pBuff, unsigned int nBuffSize)
{
  BOOL           bRetVal = FALSE;
  OIL_eTyPDLType ePDLType;
  char         * pszConfigPDL = NULL;
  BOOL           bLocalMemFlag = FALSE;

#ifndef GG_DEBUG
  UNUSED_PARAM(unsigned int, nBuffSize);
#endif

  ePDLType = g_pstCurrentJob->ePDLType;

  /* initialize the RIP for the PDL */
  switch (ePDLType)
  {
  case OIL_PDL_PS:
    /* All config for PS jobs is done via setrealdevice proc in HqnEmbedded procset, so
     * SetupPDLType() should not be called for PS jobs.
     */
    return FALSE;
  case OIL_PDL_XPS:
    if(g_pstCurrentJob->bFileInput)
      pszConfigPDL = pszConfigXPS2;
    else
      pszConfigPDL = pszConfigXPS1;
    break;
  case OIL_PDL_PDF:
    pszConfigPDL = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, 256);
    HQASSERT(pszConfigPDL!=NULL, ("SetupPDLType: Failed to allocate 256 bytes"));
    if(pszConfigPDL)
    {
      bLocalMemFlag = TRUE;
      if(g_pstCurrentJob->bFileInput)
        sprintf(pszConfigPDL, pszConfigPDF2, g_pstCurrentJob->szPDFPassword); /*uses embedded dev to print direct */
      else
        sprintf(pszConfigPDL, pszConfigPDF1, g_pstCurrentJob->szPDFPassword); /* uses pdfspool to spool pdf*/
    }
    break;

  case OIL_PDL_PCL5c:
  case OIL_PDL_PCL5e:
    /* Add 5000 for the PCL Media definition */
    pszConfigPDL = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, 1200 + 5000);
    HQASSERT(pszConfigPDL!=NULL, ("SetupPDLType: Failed to allocate 6200 bytes"));

    if(pszConfigPDL)
    {
      bLocalMemFlag = TRUE;

      SetupPCL5(pszConfigPDL);
    }
    break;
  case OIL_PDL_PCLXL:
    /* Add 5000 for the PCL Media definition */
    pszConfigPDL = OIL_malloc(OILMemoryPoolJob, OIL_MemBlock, 1400 + 5000);
    HQASSERT(pszConfigPDL!=NULL, ("SetupPDLType: Failed to allocate 6400 bytes"));

    if(pszConfigPDL)
    {
      bLocalMemFlag = TRUE;

      SetupPCLXL(pszConfigPDL);
    }
    break;
  case OIL_PDL_Unknown:
  default:
    GG_SHOW(GG_SHOW_PSCONFIG, "SetupPDLType: Unknown PDL - ASSUME PS\n");
    ePDLType = OIL_PDL_PS;
    bRetVal = TRUE;
    break;
  }

  if (pszConfigPDL != NULL)
  {
    if(bSendFlag)
    {
      /* prepare RIP for the requested PDL */
      if ( !SwLePs ((uint32)strlen(pszConfigPDL), (uint8*)pszConfigPDL) )
      {
        GG_SHOW(GG_SHOW_PSCONFIG, "SetupPDLType: Failed to submit PDL initialisation details.\n");
      }
      else
      {
        /* initialisation completed OK */
        g_pstCurrentJob->bPDLInitialised = TRUE;
        bRetVal = TRUE;
      }
    }
    else
    {
      HQASSERT((strlen(pszConfigPDL) <= nBuffSize), ("SetupPDLType: memory overflow"));
      strcpy(pBuff, pszConfigPDL);
      /* initialisation completed OK */
      g_pstCurrentJob->bPDLInitialised = TRUE;
      bRetVal = TRUE;
    }

    if(bLocalMemFlag)
    {
      OIL_free(OILMemoryPoolJob, pszConfigPDL);
    }
  }

  return (bRetVal);
}

/**
 * \brief Search for a specific comment and if found, update parser structure.
 *
 * \param[in]   pszComment     A pointer to the start of the comment string.
 * \param[in]   pszSearch      A pointer to the string to search for.
 * \param[out]  pszDest        A pointer to the start of the destination string.
 * \param[in]   nDestLen       The length of the destination string buffer in bytes.
 * \param[in]   bDefault       Flag to indicate which comment parser stricture is in use.
 */
static int FindAndUpdateComment(char *pszComment, char *pszSearch, char *pszDest, unsigned int nDestLen, int bDefault)
{
  char *pszContent;

  if(strncmp(pszComment,pszSearch,strlen(pszSearch))==0) {
    pszContent = &pszComment[strlen(pszSearch)];
    if(strlen(pszContent) < nDestLen ) {
      strcpy(pszDest, pszContent);
    } else {
      strncpy(pszDest, pszContent, nDestLen - 1);
      pszDest[nDestLen - 1] = '\0';
    }
    GG_SHOW(GG_SHOW_COMMENTPARSER, "%s%s : %s\n", bDefault?"Default ":"", pszSearch, pszDest);
    return 1;
  }

  return 0;
}

/**
 * \brief Comment Parser
 *
 * This function extracts information from comments in a
 * PostScript language input file and stores them in an
 * OIL_TyCommentParser structure.
 * \param[in]   comment     A pointer to the start of the comment string.
 */
void UpdateOILComment(char *comment)
{
  char *ptrToContent;
  static int beginFound = 0;
  static OIL_TyCommentParser *pParser = &g_CommentParser;

  ptrToContent = comment;

  if(!strncmp(comment,"BeginDefaults",strlen("BeginDefaults"))) {
      beginFound = 1;
      pParser = &g_CommentParserDefault;
  } else if(!strncmp(comment,"EndDefaults",strlen("EndDefaults"))) {
      beginFound = 0;
      pParser = &g_CommentParser;
  } else if(FindAndUpdateComment(comment, "Creator:",            pParser->szJobCreator,            sizeof(pParser->szJobCreator),            beginFound)) {
  } else if(g_pstCurrentJob && (strlen(pParser->szJobTitle)== 0) &&
           (FindAndUpdateComment(comment, "Title:",              pParser->szJobTitle,              sizeof(pParser->szJobTitle),              beginFound))) {
  } else if(FindAndUpdateComment(comment, "For:",                pParser->szJobFor,                sizeof(pParser->szJobFor),                beginFound)) {
  } else if(FindAndUpdateComment(comment, "CreationDate:",       pParser->szJobCreationDate,       sizeof(pParser->szJobCreationDate),       beginFound)) {
  } else if(FindAndUpdateComment(comment, "Orientation:",        pParser->szJobOrientation,        sizeof(pParser->szJobOrientation),        beginFound)) {
  } else if(FindAndUpdateComment(comment, "ViewingOrientation:", pParser->szJobViewingOrientation, sizeof(pParser->szJobViewingOrientation), beginFound)) {
  } else if(FindAndUpdateComment(comment, "BoundingBox:",        pParser->szJobBoundingBox,        sizeof(pParser->szJobBoundingBox),        beginFound)) {
  } else if(FindAndUpdateComment(comment, "PageBoundingBox:",    pParser->szJobPageBoundingBox,    sizeof(pParser->szJobPageBoundingBox),    beginFound)) {
  } else {
    GG_SHOW(GG_SHOW_COMMENTPARSER,"**Comment Parsed but ignored by data structure\n");
  }
}

/**
 * \brief Create PCL media list configuration
 *
 * \param[in]   pBuff     A pointer to a string buffer.
 * \return      Returns number of bytes written to string.
 */
int SetupPCLMedia( char *pBuff )
{
  char *pLocal = pBuff;
  PMS_TyPaperInfo *ThisPaperInfo;
  PMS_TyMediaType *ThisMediaType;
  PMS_TyMediaSource *ThisMediaSource;
  PMS_TyMediaDest *ThisMediaDest;
  PMS_TyPaperInfo *RotatedPaperInfo;
  PMS_TyPaperInfo SortedPaperInfo[NUMFIXEDMEDIASIZES +1];
  int i, j;


  pLocal += scopy(pLocal, pszMediaPCL_1);
  /* Set up the PCL 5 Page Size Values */
  for (i =0; i < NUMFIXEDMEDIASIZES; i++)
  {
    PMS_GetPaperInfo(i, &ThisPaperInfo);
    pLocal += sprintf(pLocal,"%d  %s\n",ThisPaperInfo->ePCL5size, ThisPaperInfo->szPSName);
  }
  /* now add Custom Media */
  PMS_GetPaperInfo(PMS_SIZE_CUSTOM, &ThisPaperInfo);
  pLocal += sprintf(pLocal,"%d  %s\n",ThisPaperInfo->ePCL5size, ThisPaperInfo->szPSName);
  pLocal += scopy(pLocal, pszMediaPCL_2);
  /* Set up the PCL XL Page Size Values */
  for (i =0; i < NUMFIXEDMEDIASIZES; i++)
  {
    PMS_GetPaperInfo(i, &ThisPaperInfo);
    pLocal += sprintf(pLocal,"%d  %s\n",ThisPaperInfo->ePCLXLsize, ThisPaperInfo->szPSName);
  }
  /* now add Default Media */
  PMS_GetPaperInfo(PMS_SIZE_DONT_KNOW, &ThisPaperInfo);
  pLocal += sprintf(pLocal,"%d  %s\n",ThisPaperInfo->ePCLXLsize, ThisPaperInfo->szPSName);

  pLocal += scopy(pLocal, pszMediaPCL_3);
  /* Set up the PCL XL Page Size String Values */
  for (i =0; i < NUMFIXEDMEDIASIZES; i++)
  {
    PMS_GetPaperInfo(i, &ThisPaperInfo);
    pLocal += sprintf(pLocal,"%s  %s\n",ThisPaperInfo->szXLName, ThisPaperInfo->szPSName);
  }
  /* no Default Media to add ??? */

  pLocal += scopy(pLocal, pszMediaPCL_4);
  /* Set up the PCL 5 Page Size Dictionary */
  for (i =0; i < NUMFIXEDMEDIASIZES; i++)
  {
    /* loop for each paper size */
    PMS_GetPaperInfo(i, &ThisPaperInfo);
    PMS_GetPaperInfo(i + NUMFIXEDMEDIASIZES, &RotatedPaperInfo);
    if((strcmp((char*)ThisPaperInfo->szPSName, "/Ledger")==0)||
      (strcmp((char*)ThisPaperInfo->szPSName, "/Tabloid")==0))
    {
      /* Height and Width are reversed for PCL Ledger/Tabloid */
      pLocal += sprintf(pLocal,"%s <<\n /PageSize [%4.2f %4.2f]\n",
        ThisPaperInfo->szPSName, ThisPaperInfo->dHeight,ThisPaperInfo->dWidth);
    }
    else
    {
      pLocal += sprintf(pLocal,"%s <<\n /PageSize [%4.2f %4.2f]\n",
        ThisPaperInfo->szPSName, ThisPaperInfo->dWidth,ThisPaperInfo->dHeight);
    }
    pLocal += sprintf(pLocal,"/PCL5LogicalPage [[%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f]]\n",
      ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
      ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
      RotatedPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
      RotatedPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
      ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
      ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
      RotatedPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
      RotatedPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS);
    pLocal += sprintf(pLocal,"/PCL5PrintableArea [%4.2f %4.2f %4.2f %4.2f]\n",
      ThisPaperInfo->nLeftUnprintable*MICROINCHTOPOINTS,ThisPaperInfo->nRightUnprintable*MICROINCHTOPOINTS,
      ThisPaperInfo->nTopUnprintable*MICROINCHTOPOINTS,ThisPaperInfo->nBottomUnprintable*MICROINCHTOPOINTS);
    pLocal += sprintf(pLocal,"/PCL5RequestedPageSize %d\n/PCLXLRequestedPageSize %d\n>>\n",
      ThisPaperInfo->ePCL5size,ThisPaperInfo->ePCLXLsize);
  }
  /* Now add the CUSTOM paper  */
  PMS_GetPaperInfo(PMS_SIZE_CUSTOM, &ThisPaperInfo);
  pLocal += sprintf(pLocal,"%s <<\n /PageSize [%4.2f %4.2f]\n",
    ThisPaperInfo->szPSName, ThisPaperInfo->dWidth,ThisPaperInfo->dHeight);
  pLocal += sprintf(pLocal,"/PCL5LogicalPage [[%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f] [%4.2f %4.2f %4.2f %4.2f]]\n",

    ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    ThisPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,ThisPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS);

  pLocal += sprintf(pLocal,"/PCL5PrintableArea [%4.2f %4.2f %4.2f %4.2f]\n",
    ThisPaperInfo->nLeftUnprintable*MICROINCHTOPOINTS,ThisPaperInfo->nRightUnprintable*MICROINCHTOPOINTS,
    ThisPaperInfo->nTopUnprintable*MICROINCHTOPOINTS,ThisPaperInfo->nBottomUnprintable*MICROINCHTOPOINTS);
  pLocal += sprintf(pLocal,"/PCL5RequestedPageSize %d\n/PCLXLRequestedPageSize %d\n>>\n",

    ThisPaperInfo->ePCL5size,ThisPaperInfo->ePCLXLsize);
  /* Now add the WIDE A4 */
  PMS_GetPaperInfo(PMS_SIZE_A4, &ThisPaperInfo);
  PMS_GetPaperInfo(PMS_SIZE_A4 + NUMFIXEDMEDIASIZES, &RotatedPaperInfo);
  pLocal += sprintf(pLocal,"%s <<\n /PageSize [%4.2f %4.2f]\n",
    "/WideA4", ThisPaperInfo->dWidth,ThisPaperInfo->dHeight);
  pLocal += sprintf(pLocal,"/PCL5LogicalPage [%s [%4.2f %4.2f %4.2f %4.2f] %s [%4.2f %4.2f %4.2f %4.2f]]\n",
    LOGICALPAGE40PX,
    RotatedPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    RotatedPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS,
    LOGICALPAGE40PX,
    RotatedPaperInfo->nLeftLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nRightLogicalPage*MICROINCHTOPOINTS,
    RotatedPaperInfo->nTopLogicalPage*MICROINCHTOPOINTS,RotatedPaperInfo->nBottomLogicalPage*MICROINCHTOPOINTS);
  pLocal += sprintf(pLocal,"/PCL5PrintableArea %s\n",PRINTABLEAREAWIDEA4 );
  pLocal += sprintf(pLocal,"/PCL5RequestedPageSize %d\n/PCLXLRequestedPageSize %d\n>>\n",
    ThisPaperInfo->ePCL5size,ThisPaperInfo->ePCLXLsize);


  pLocal += scopy(pLocal, pszMediaPCL_5);
  /* Set up the PCL 5 Page Length Sizes - Needs to be ordered by increasing size */
  /* copy the sorting data into a local array of paper info structures */
  for (i =0; i < NUMFIXEDMEDIASIZES; i++)
  {
    PMS_GetPaperInfo(i, &ThisPaperInfo);
    SortedPaperInfo[i].ePaperSize = ThisPaperInfo->ePaperSize;
    /*reverse the Ledger height and width as PCL uses 17x11, not 17x11 as defined in PMS*/
    if((strcmp((char*)ThisPaperInfo->szPSName, "/Ledger")==0)||
      (strcmp((char*)ThisPaperInfo->szPSName, "/Tabloid")==0))
    {
      SortedPaperInfo[i].dHeight = ThisPaperInfo->dWidth;
      SortedPaperInfo[i].dWidth = ThisPaperInfo->dHeight;
    }
    else
    {
      SortedPaperInfo[i].dHeight = ThisPaperInfo->dHeight;
      SortedPaperInfo[i].dWidth = ThisPaperInfo->dWidth;
    }
  }
  PMS_GetPaperInfo(PMS_SIZE_CUSTOM, &ThisPaperInfo);
  SortedPaperInfo[i].ePaperSize = ThisPaperInfo->ePaperSize;
  SortedPaperInfo[i].dHeight = ThisPaperInfo->dHeight;
  SortedPaperInfo[i].dWidth = ThisPaperInfo->dWidth;
  /* now sort the local arrays by height */
  for (i =0; i < NUMFIXEDMEDIASIZES + 1; i++)
  {
    PMS_TyPaperInfo temp;
    int minindex = i;

    for(j = i; j < NUMFIXEDMEDIASIZES + 1; j++)
    {
      if(SortedPaperInfo[minindex].dHeight > SortedPaperInfo[j].dHeight)
      {
        minindex = j;
      }
    }
    temp.ePaperSize = SortedPaperInfo[i].ePaperSize;
    temp.dHeight = SortedPaperInfo[i].dHeight;
    temp.dWidth = SortedPaperInfo[i].dWidth;
    SortedPaperInfo[i].ePaperSize = SortedPaperInfo[minindex].ePaperSize;
    SortedPaperInfo[i].dHeight = SortedPaperInfo[minindex].dHeight;
    SortedPaperInfo[i].dWidth = SortedPaperInfo[minindex].dWidth;
    SortedPaperInfo[minindex].ePaperSize = temp.ePaperSize;
    SortedPaperInfo[minindex].dHeight = temp.dHeight;
    SortedPaperInfo[minindex].dWidth = temp.dWidth;
  }

  for (i =0; i < NUMFIXEDMEDIASIZES + 1; i++)
  {
    PMS_GetPaperInfo(SortedPaperInfo[i].ePaperSize, &ThisPaperInfo);
    pLocal += sprintf(pLocal,"%s\n", ThisPaperInfo->szPSName);
  }
  /* Set up the PCL 5 Media Type Values */
  pLocal += scopy(pLocal, pszMediaPCL_6);
  for (i =0; i < NUMFIXEDMEDIATYPES; i++)
  {
    PMS_GetMediaType(i, &ThisMediaType);
    pLocal += sprintf(pLocal, "/%s  %d\n", ThisMediaType->szPCL5Type, ThisMediaType->ePCL5type);
  }
  pLocal += scopy(pLocal, pszMediaPCL_7);
  /* Set up the PCL 5 Media Type strings */
  for (i =0; i < NUMFIXEDMEDIATYPES; i++)
  {
    PMS_GetMediaType(i, &ThisMediaType);
    pLocal += sprintf(pLocal, "%d  (%s)\n", ThisMediaType->ePCL5type, ThisMediaType->szPSType);
  }
  pLocal += scopy(pLocal, pszMediaPCL_8);
  /* Set up the PCL XL Media Type strings */
  for (i =0; i < NUMFIXEDMEDIATYPES; i++)
  {
    PMS_GetMediaType(i, &ThisMediaType);
    pLocal += sprintf(pLocal, "/%s  (%s)\n", ThisMediaType->szXLType, ThisMediaType->szPSType);
  }
  pLocal += scopy(pLocal, pszMediaPCL_9);
  /* Set up the PCL 5 Media Source Values */
  for (i =0; i < NUMFIXEDMEDIASOURCES; i++)
  {
    PMS_GetMediaSource(i, &ThisMediaSource);
    pLocal += sprintf(pLocal, "%d  %d\n", ThisMediaSource->ePCL5MediaSource, ThisMediaSource->ePSMediaSource);
  }
  pLocal += scopy(pLocal, pszMediaPCL_10);
  /* Set up the PCL XL Media Source Values */
  for (i =0; i < NUMFIXEDMEDIASOURCES; i++)
  {
    PMS_GetMediaSource(i, &ThisMediaSource);
    pLocal += sprintf(pLocal, "%d  %d\n", ThisMediaSource->ePCLXLMediaSource, ThisMediaSource->ePSMediaSource);
  }
  pLocal += scopy(pLocal, pszMediaPCL_11);
  /* Set up the PCL 5 Media Dest strings */
  for (i =0; i < NUMFIXEDMEDIADESTS; i++)
  {
    PMS_GetMediaDest(i, &ThisMediaDest);
    pLocal += sprintf(pLocal, "%d  (%s)\n", ThisMediaDest->ePCL5Mediadest, ThisMediaDest->szPSMediaDest);
  }
  pLocal += scopy(pLocal, pszMediaPCL_12);
  /* Set up the PCL XL Media Dest strings */
  for (i =0; i < NUMFIXEDMEDIADESTS; i++)
  {
    PMS_GetMediaDest(i, &ThisMediaDest);
    pLocal += sprintf(pLocal, "%d  (%s)\n", ThisMediaDest->ePCLXLMediadest, ThisMediaDest->szPSMediaDest);
  }
  pLocal += scopy(pLocal, pszMediaPCL_13);
  /* Set up the PCL XL Custom Page Size Values  Needs to be ordered by increasing size */
  /*TODO? ordered by height only so far as the actual algorithm is not clear */
  for (i =0; i < NUMFIXEDMEDIASIZES + 1; i++)
  {
    PMS_GetPaperInfo(SortedPaperInfo[i].ePaperSize, &ThisPaperInfo);
    pLocal += sprintf(pLocal,"%s\n", ThisPaperInfo->szPSName);
  }

  pLocal += scopy(pLocal, pszMediaPCL_14);

/* #define PCLMEDIAFILE */
#ifdef PCLMEDIAFILE
  {
    FILE *hFile;
    hFile = fopen("c:\\RicohTemp\\RiftPCLMedia.txt", "w+");
    if(hFile == NULL)
        printf("failed to open Media text file");
    fprintf(hFile, "%s", pBuff);
    fclose(hFile);
  }
#endif

  return (int)(pLocal - pBuff);
}

