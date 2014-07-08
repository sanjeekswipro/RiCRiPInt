/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_media.c(EBDSDK_P.1) $
 * 
 *  \brief This file contains the implementation of the functions 
 *   used to retrieve information about the available media input trays.
 */

#include "oil_media.h"
#include "oil_interface_oil2pms.h"
#include <string.h> /* for strlen */
#include <stdio.h>  /* for sprintf */

#ifndef NULL
#define NULL ((void *)0)
#endif

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern int SwLePs(unsigned int cbBuffer, unsigned char * pBuffer );

/* Internal Functions */
static unsigned char *GetMediaTypeString(PMS_eMediaType eMediaType);
static unsigned char *GetMediaDestString(PMS_eOutputTray eMediaDest);
static unsigned char *GetMediaColorString(PMS_eMediaColor eMediaColor);
static int GetMediaLeadingEdge(PMS_ePaperSize ePaperSize);
/**
 * \brief Retrieve the input tray information from the PMS.
 *
 * The PMS is queried for information about the number of input trays available in the system.
 * The information also includes the attributes of the paper present in each tray.  A snippet of 
 * PostScript language code describing the input trays is built up in the character buffer passed 
 * in to the function.  This code snippet could be passed to the RIP to configure it with the 
 * tray information, if required.
 * \param[in,out]   pBuf    Pointer to a character buffer used to build the PostScript language
                            configuration string.
 */
void GetTrayInformation(char *pBuf)
{
  int nAvailableTraysCount, i;
  int bNeedAttrib0, bNeedAttrib1;
  char szLine[OIL_TMPSTR_SIZE];
  PMS_TyTrayInfo *pstPMSTrays = NULL;
  PMS_TyPaperInfo *pstPMSPaper = NULL;
  PMS_TyMediaSource *ThisMediaSource;
  int InputTrayPriority[NUMFIXEDMEDIASOURCES];

  static char pszInputAttributesStartPS[] =
       "<< /InputAttributes                 \n"
       "  <<                                \n";
  /* In order to change the Tray Priority, change the following Priority sequence (from highest to lowest) */
  static char pszInputAttributesEndPS[] =
       "  >>                                \n"
       "  /Policies <</PageSize 5 >>        \n"
       ">> setpagedevice                    \n";

  nAvailableTraysCount = PMS_GetTrayInfo(&pstPMSTrays);
  InputTrayPriority[0] = 0;
  bNeedAttrib0 = TRUE;
  bNeedAttrib1 = TRUE;

  strcat(pBuf, pszInputAttributesStartPS);
  for (i=0; i < nAvailableTraysCount; i++)
  {
    /* Get the media source definition so we can use the pds defined values for trays */
    PMS_GetMediaSource(pstPMSTrays[i].eMediaSource, &ThisMediaSource);
    /* remember if InputAttributes 0 and 1 get initialised */
    if (ThisMediaSource->ePSMediaSource == 0) {
        bNeedAttrib0 = FALSE;
    }
    if (ThisMediaSource->ePSMediaSource == 1) {
        bNeedAttrib1 = FALSE;
    }

    if((pstPMSTrays[i].nPriority > 0) && (pstPMSTrays[i].nPriority <= nAvailableTraysCount))
      InputTrayPriority[pstPMSTrays[i].nPriority - 1] = ThisMediaSource->ePSMediaSource;

/* use the postscript defined values required for media sources so in rip tray selection works automatically */
/* pcl values will be converted to ps values in the pcl sensepagedevice routine */
    sprintf(szLine, "    %d << \n", ThisMediaSource->ePSMediaSource);
    strcat(pBuf, szLine);

    if(pstPMSTrays[i].ePaperSize != PMS_SIZE_DONT_KNOW)
    {
      PMS_GetPaperInfo( pstPMSTrays[i].ePaperSize, &pstPMSPaper );
      sprintf(szLine, "      /PageSize [%4.3f %4.3f] \n",
                          pstPMSPaper->dWidth , pstPMSPaper->dHeight);
      strcat(pBuf, szLine);

      if(g_ConfigurableFeatures.ePrintableMode == OIL_RIPRemovesUnprintableArea)
      {
        double ClipLeft, ClipWidth, ClipTop, ClipHeight;

        ClipLeft = pstPMSPaper->nLeftUnprintable * 0.000072;
        ClipTop = pstPMSPaper->nTopUnprintable * 0.000072;
        ClipWidth = pstPMSPaper->dWidth - ClipLeft - (pstPMSPaper->nRightUnprintable * 0.000072);
        ClipHeight = pstPMSPaper->dHeight - ClipTop - (pstPMSPaper->nBottomUnprintable * 0.000072);

        sprintf(szLine, "      /RasterBBox [%4.3f %4.3f %4.3f %4.3f] \n",
                           ClipLeft,
                           ClipTop,
                           ClipLeft + PADDED_ALIGNEMENT(ClipWidth, g_pstCurrentJob->uRIPDepth, g_pstCurrentJob->uXResolution),
                           ClipTop + ClipHeight);
      }
      else
      {
        sprintf(szLine, "      /RasterBBox [%4.3f %4.3f %4.3f %4.3f] \n",
                           0.0,
                           0.0,
                           PADDED_ALIGNEMENT(pstPMSPaper->dWidth, g_pstCurrentJob->uRIPDepth, g_pstCurrentJob->uXResolution),
                           pstPMSPaper->dHeight);
      }
      strcat(pBuf, szLine);
    }

    if(pstPMSTrays[i].ePaperSize != PMS_SIZE_DONT_KNOW)
    {
      sprintf(szLine, "      /LeadingEdge %d \n",
                          GetMediaLeadingEdge(pstPMSTrays[i].ePaperSize));
      strcat(pBuf, szLine);
    }

    if(pstPMSTrays[i].eMediaType != PMS_TYPE_DONT_KNOW)
    {
      sprintf(szLine, "      /MediaType (%s)   \n",
                          GetMediaTypeString(pstPMSTrays[i].eMediaType));
      strcat(pBuf, szLine);
    }

    if(pstPMSTrays[i].eMediaColor != PMS_COLOR_DONT_KNOW)
    {
      sprintf(szLine, "      /MediaColor (%s)   \n",
                          GetMediaColorString(pstPMSTrays[i].eMediaColor));
      strcat(pBuf, szLine);
    }

    if(pstPMSTrays[i].uMediaWeight > 0)
    {
      sprintf(szLine, "      /MediaWeight %d   \n", pstPMSTrays[i].uMediaWeight);
      strcat(pBuf, szLine);
    }
    strcat(pBuf, "     >> \n");
  }

  /* check to verify InputAttribute 0 and 1 have been included, if not set to null to overwrite RIP defaults */
  if (bNeedAttrib0) {
    strcat(pBuf, "    0 null\n");
  }
  if (bNeedAttrib1) {
    strcat(pBuf, "    1 null\n");
  }
  /* add the priority if required */
  if(InputTrayPriority[0] != 0)
  {
    char *pszLine = &szLine[0];
    strcat(pBuf,"    /Priority [ ");
    for (i=0; i < nAvailableTraysCount; i++)
    {
      sprintf(pszLine,"%d ",InputTrayPriority[i]);
      pszLine += 2;
    }
    sprintf(pszLine, "] \n");
    strcat(pBuf, szLine);
  }
  strcat(pBuf, pszInputAttributesEndPS);

  HQASSERT ((strlen(szLine) < OIL_TMPSTR_SIZE),
      ("oil_media.c: szLine overflow - string larger than OIL_TMPSTR_SIZE"));
  HQASSERT ((strlen(pBuf) < OIL_TMPSTR_SIZE),
      ("SendTrayInformation: pBuf overflow - string larger than OIL_TMPSTR_SIZE"));

  GG_SHOW(GG_SHOW_MEDIA, "Tray Configuration PS:\n%s\n\n", pBuf);  
}

/**
 * \brief Retrieve the input tray information from the PMS.
 *
 * The PMS is queried for information about the number of input trays available in the system.
 * The information also includes the attributes of the paper present in each tray.  A snippet of 
 * PostScript language code describing the input trays is built up in the character buffer passed 
 * in to the function.  This code snippet could be passed to the RIP to configure it with the 
 * tray information, if required.
 * \param[in,out]   pBuf    Pointer to a character buffer used to build the PostScript language
                            configuration string.
 */
void GetOutputInformation(char *pBuf)
{
  int nAvailableOutputsCount, i;
  int bNeedAttrib0 = TRUE;
  char szLine[OIL_TMPSTR_SIZE];
  PMS_TyOutputInfo *pstPMSOutputs = NULL;
  PMS_TyMediaDest *ThisMediaDest;
  int OutputTrayPriority[NUMFIXEDMEDIADESTS];
  static char pszOutputAttributesStartPS[] =
       "<< /OutputAttributes                 \n"
       "  <<                                \n";
  /* In order to change the Tray Priority, change the following Priority sequence (from highest to lowest) */
  static char pszOutputAttributesEndPS[] =
       "  >>                                \n"
       ">> setpagedevice                    \n";

  nAvailableOutputsCount = PMS_GetOutputInfo(&pstPMSOutputs);
  OutputTrayPriority[0] = 0;

  strcat(pBuf, pszOutputAttributesStartPS);
  for (i=0; i < nAvailableOutputsCount; i++)
  {
    /* Get the media source definition so we can use the pds defined values for trays */
    PMS_GetMediaDest(pstPMSOutputs[i].eOutputTray, &ThisMediaDest);
    /* remember if InputAttributes 0 and 1 get initialised */
    if (ThisMediaDest->eMediaDest == 0) {
        bNeedAttrib0 = FALSE;
    }
    if((pstPMSOutputs[i].nPriority > 0) && (pstPMSOutputs[i].nPriority <= nAvailableOutputsCount))
        OutputTrayPriority[pstPMSOutputs[i].nPriority - 1] = ThisMediaDest->eMediaDest;

/* use the postscript defined values required for media sources so in rip tray selection works automatically */
/* pcl values will be converted to ps values in the pcl sensepagedevice routine */
    sprintf(szLine, "    %d << \n", ThisMediaDest->eMediaDest);
    strcat(pBuf, szLine);

    sprintf(szLine, "      /OutputType (%s)   \n",
                          GetMediaDestString(pstPMSOutputs[i].eOutputTray));
    strcat(pBuf, szLine);
    strcat(pBuf, "     >> \n");
  }
  /* check to verify OutputAttribute 0 has been included, if not set to null to overwrite RIP default */
  if (bNeedAttrib0) {
    strcat(pBuf, "    0 null\n");
  }
  /* add the priority if required */
  if(OutputTrayPriority[0] != 0)
  {
    char *pszLine = &szLine[0];
    strcat(pBuf,"    /Priority [ ");
    for (i=0; i < nAvailableOutputsCount; i++)
    {
      sprintf(pszLine,"%d ",OutputTrayPriority[i]);
      pszLine += 2;
    }
    sprintf(pszLine, "] \n");
    strcat(pBuf, szLine);
  }
  strcat(pBuf, pszOutputAttributesEndPS);
}

/**
 * \brief Get width of Orientation of paper
 *
 * This routine returns the orientation of the requested paper size by indicating
 * the leading edge.
 * \param[in]   ePaperSize  A PMS_ePaperSize value indicating the size of the paper.
 * \return      The function returns an integer indicating the leading edge of
 * the specified paper size.  The possible values are as follows:
 * \arg  0 Short edge; top of canonical page
 * \arg  1 Long edge; right side of canonical page
 * \arg  2 Short edge; bottom of canonical page (not used by this routine)
 * \arg  3 Long edge; left side of canonical page (not used by this routine)
 */
static int GetMediaLeadingEdge(PMS_ePaperSize ePaperSize)
{
  switch(ePaperSize)
  {
    case PMS_SIZE_LETTER:
    case PMS_SIZE_A4:
    case PMS_SIZE_LEGAL:
    case PMS_SIZE_EXE:
    case PMS_SIZE_A3:
    case PMS_SIZE_TABLOID:
    case PMS_SIZE_A5:
    case PMS_SIZE_A6:
    case PMS_SIZE_C5_ENV:
    case PMS_SIZE_DL_ENV:
    case PMS_SIZE_LEDGER:
      return 0;
    case PMS_SIZE_LETTER_R:
    case PMS_SIZE_A4_R:
    case PMS_SIZE_LEGAL_R:
    case PMS_SIZE_EXE_R:
    case PMS_SIZE_A3_R:
    case PMS_SIZE_TABLOID_R:
    case PMS_SIZE_A5_R:
    case PMS_SIZE_A6_R:
    case PMS_SIZE_C5_ENV_R:
    case PMS_SIZE_DL_ENV_R:
    case PMS_SIZE_LEDGER_R:
      return 1;
    case PMS_SIZE_DONT_KNOW:
      return 0;
    default:
      return 0;
  }
}

/**
 * \brief Get the name of the requested media type.
 *
 * This function maps the PMS_eMediaType value to a PS string as defined in the PMS media definition.
 * \param[in] A PMS_eMediaType value to be mapped to a media name.
 * \return A string holding the human-readable name of the media specified by eMediaType.
 */
static unsigned char *GetMediaTypeString(PMS_eMediaType eMediaType)
{
  PMS_TyMediaType *ThisMediaType;

  PMS_GetMediaType(eMediaType, &ThisMediaType);
  return (ThisMediaType->szPSType);
}

/**
 * \brief Get the name of the requested output tray.
 *
 * This function maps the PMS_eMediaType value to a PS string as defined in the PMS media definition.
 * \param[in] A PMS_eMediaType value to be mapped to a media name.
 * \return A string holding the human-readable name of the media specified by eMediaType.
 */
static unsigned char *GetMediaDestString(PMS_eOutputTray eMediaDest)
{
  PMS_TyMediaDest *ThisMediaDest;

  PMS_GetMediaDest(eMediaDest, &ThisMediaDest);
  return (ThisMediaDest->szPSMediaDest);
}
/**
 * \brief Get the name of the requested media color.
 *
 * This function maps PMS_eMediaColor values to strings in the PMS media definition.
 * \param[in] A PMS_eMediaColor value to be mapped to a media color name.
 * \return A string holding the human-readable name of the media color specified by eMediaColor.
 */
static unsigned char *GetMediaColorString(PMS_eMediaColor eMediaColor)
{
  PMS_TyMediaColor *ThisMediaColor;

  PMS_GetMediaColor(eMediaColor, &ThisMediaColor);
  return (ThisMediaColor->szMediaColor);
}



