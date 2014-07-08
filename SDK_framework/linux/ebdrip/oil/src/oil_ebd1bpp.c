/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
*
* This example is provided on an "as is" basis and without
* warranty of any kind. Global Graphics Software Ltd. does not
* warrant or make any representations regarding the use or results
* of use of this example.
*
* $HopeName: SWebd_OIL_example_gg!src:oil_ebd1bpp.c(EBDSDK_P.1) $
*
*/
/*! \file
*  \ingroup OIL
*  \brief OIL implementation of the screening table functions.
*
*/

#include "ripthread.h"
#include "oil_interface_oil2pms.h"
#include "swdevice.h"
#include "pms_export.h"

#include <stdio.h>
#include <string.h>

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;

typedef struct tagOILScreens
{
    PMS_eScreens ePMSScreen;
    char *pszPSName;
    int32 nCurrentPos;
    PMS_TyScreenInfo *pScreenInfo;
}OIL_SCREEN;

static OIL_SCREEN l_tOILScreens[]=
{ 
    { PMS_SCREEN_1BPP_GFX_CYAN,       "/Screen_1bpp_Gfx_C", 0, NULL },
    { PMS_SCREEN_1BPP_GFX_MAGENTA,    "/Screen_1bpp_Gfx_M", 0, NULL },
    { PMS_SCREEN_1BPP_GFX_YELLOW,     "/Screen_1bpp_Gfx_Y", 0, NULL },
    { PMS_SCREEN_1BPP_GFX_BLACK,      "/Screen_1bpp_Gfx_K", 0, NULL },
    { PMS_SCREEN_1BPP_IMAGE_CYAN,     "/Screen_1bpp_Image_C", 0, NULL },
    { PMS_SCREEN_1BPP_IMAGE_MAGENTA,  "/Screen_1bpp_Image_M", 0, NULL },
    { PMS_SCREEN_1BPP_IMAGE_YELLOW,   "/Screen_1bpp_Image_Y", 0, NULL },
    { PMS_SCREEN_1BPP_IMAGE_BLACK,    "/Screen_1bpp_Image_K", 0, NULL },
    { PMS_SCREEN_1BPP_TEXT_CYAN,      "/Screen_1bpp_Text_C", 0, NULL },
    { PMS_SCREEN_1BPP_TEXT_MAGENTA,   "/Screen_1bpp_Text_M", 0, NULL },
    { PMS_SCREEN_1BPP_TEXT_YELLOW,    "/Screen_1bpp_Text_Y", 0, NULL },
    { PMS_SCREEN_1BPP_TEXT_BLACK,     "/Screen_1bpp_Text_K", 0, NULL }
};

#define OIL_EBD_SCRN_NUM_SCREENS (sizeof(l_tOILScreens) / sizeof(l_tOILScreens[0]))

/**
* \brief Open an embedded screen table.
*
* A screen table is 'opened' by setting the current position in the file to the 
* beginning of the file.
* \param[in]   filename    The filename of the screen to open.
* \return Returns a unique number for the filename if successful, otherwise -1.
*/
DEVICE_FILEDESCRIPTOR RIPCALL ebd_scrn_open(unsigned char *filename)
{
    int i;

    GG_SHOW(GG_SHOW_1BPP, "ebd_scrn_open_file (%s)\n", filename);

    for(i = 0; i < OIL_EBD_SCRN_NUM_SCREENS; i++)
    {
        if(strcmp((char*)l_tOILScreens[i].pszPSName, (char*)filename) == 0)
        {
            /* Reset seek position to beginning of file */
            l_tOILScreens[i].nCurrentPos = 0;
            break;
        }
    }
    if(i >= OIL_EBD_SCRN_NUM_SCREENS)
      return -1;
    else
      return i;
}

/**
* \brief Read data from an embedded screen table.
*
* A screen table is 'closed' by setting the current position in the file to the 
* beginning of the file.
* \param[in]   nScreen       The identifier of the screen to close.
* \param[out]  buff          The buffer to receive the data read from the screen.
* \param[in]   len           The amount of data to read into the buffer.
* \return Returns the actual amount of data read into the buffer, or -1 if nScreen cannot be recognized.
*/
int RIPCALL ebd_scrn_read(int nScreen, unsigned char *buff , int len )
{
    int nLimit;
    GG_SHOW(GG_SHOW_1BPP, "ebd_scrn_read %d, len=%d\n", nScreen, len);

    if((nScreen < 0) || (nScreen >= OIL_EBD_SCRN_NUM_SCREENS))
        return -1;

    HQASSERT(l_tOILScreens[nScreen].pScreenInfo, "Screen table not open");
    if(l_tOILScreens[nScreen].pScreenInfo == NULL)
        return -1;

    /* do not read past the end of the screen table */
    if((len + l_tOILScreens[nScreen].nCurrentPos) > 
       (l_tOILScreens[nScreen].pScreenInfo->nCellWidth * l_tOILScreens[nScreen].pScreenInfo->nCellHeight))
    {
        nLimit = (l_tOILScreens[nScreen].pScreenInfo->nCellWidth *
                  l_tOILScreens[nScreen].pScreenInfo->nCellHeight) -
                  l_tOILScreens[nScreen].nCurrentPos;
    }
    else
    {
        nLimit = len;
    }
        
    /* copy screen data */
    memcpy(buff, l_tOILScreens[nScreen].pScreenInfo->pTable[0] + l_tOILScreens[nScreen].nCurrentPos, nLimit);

    /* move file position */
    l_tOILScreens[nScreen].nCurrentPos += nLimit;

    GG_SHOW(GG_SHOW_1BPP, "Screen %d read %d bytes\n", nScreen, l_tOILScreens[nScreen].nCurrentPos);

    return len;
}

/**
* \brief Close an embedded screen table.
*
* A screen table is 'closed' by setting the current position in the file to the 
* beginning of the file.
* \param[in]   nScreen       The identifier of the screen to close.
* \return Returns 0 if the close was successful, or -1 if nScreen cannot be recognized.
*/
int RIPCALL ebd_scrn_close(int nScreen)
{
    if((nScreen < 0) || (nScreen >= OIL_EBD_SCRN_NUM_SCREENS))
        return -1;

    l_tOILScreens[nScreen].nCurrentPos = 0;

    return 0;
}

/**
* \brief Seek through one of the embedded screen tables.
*
* This function supports both seeking from the current file pointer position
* and from the beginning of the file, as specified by the whence parameter
* \param[in]   nScreen       The identifier of the screen to seek through.
* \param[in]   destination   The distance to seek, in bytes, from the specified starting point.
* \param[in]   whence        The starting point of the seek. Set to \c SW_SET to seek from the 
*                            beginning of the file, or \c SW_INCR to seek from the current 
*                            position in the file.
* \return Returns TRUE if the seek was successful, FALSE if the seek failed.
*/
int RIPCALL ebd_scrn_seek(int nScreen, Hq32x2 *destination , int whence )
{
    GG_SHOW(GG_SHOW_1BPP, "ebd_scrn_seek %d, %d\n", destination->low, whence);

    if((nScreen < 0) || (nScreen >= OIL_EBD_SCRN_NUM_SCREENS))
        return FALSE;

    /* \todo Handle 64 bit destination value... or not, as screen tables are not that large */
    HQASSERT(destination->high == 0, "ebd_scrn_seek_file: 64bit seeks not supported");

    switch( whence )
    {
    case SW_SET:   
        l_tOILScreens[nScreen].nCurrentPos = destination->low;
        break ;
    case SW_INCR:  
        l_tOILScreens[nScreen].nCurrentPos += destination->low; 
        break ;
    case SW_XTND:  
        /* \todo If ever required. Obtain the size of each screen table file to find end of file */
        HQFAIL("ebd_scrn_seek_file: SW_XTND not supported");
        return FALSE ;
    default:
        HQFAILV(("ebd_scrn_seek_file: Unknown seek method %d", whence));
        return FALSE ;
    }

    return TRUE ;
}

/**
 * \brief Initialize the screen tables.
 *
 * This routine moves through the array of screens, and initializes the information structure
 * of each by querying the PMS.
 */
void ebd_scrn_init()
{
  int i;
  for(i = 0; i < OIL_EBD_SCRN_NUM_SCREENS; i++)
  {
    if(!GetScreenInfoFromPMS(l_tOILScreens[i].ePMSScreen, &(l_tOILScreens[i].pScreenInfo)))
    {
      GG_SHOW(GG_SHOW_1BPP, "ebd_scrn_open_file: failed to get screen info, %s, %d\n", l_tOILScreens[i].pszPSName, i);
    }
  }
}

/**
 * \brief Retrieve a screen's cell width.
 * \param[in]   ActiveScreen    A pointer to a string containing the screen's name.
 * \return      Returns the cell width of the screen, or -1 if the screen could not be found.
 */
int Get1bppScreenWidth(char *ActiveScreen)
{
  int i, nScreenWidth = -1;

  for(i = 0; i < OIL_EBD_SCRN_NUM_SCREENS; i++)
  {
      if(strcmp((char*)&l_tOILScreens[i].pszPSName[1], ActiveScreen) == 0)
      {
        nScreenWidth = l_tOILScreens[i].pScreenInfo->nCellWidth;
        break;
      }
  }
  return nScreenWidth;
}

/**
 * \brief Retrieve a screen's cell height.
 * \param[in]   ActiveScreen    A pointer to a string containing the screen's name.
 * \return      Returns the cell height of the screen, or -1 if the screen could not be found.
 */
int Get1bppScreenHeight(char *ActiveScreen)
{
  int i, nScreenHeight = -1;

  for(i = 0; i < OIL_EBD_SCRN_NUM_SCREENS; i++)
  {
      if(strcmp((char*)&l_tOILScreens[i].pszPSName[1], ActiveScreen) == 0)
      {
        nScreenHeight = l_tOILScreens[i].pScreenInfo->nCellHeight;
        break;
      }
  }
  return nScreenHeight;
}

/* \brief Retrieve the filename that corresponds to a named screen.
 *
 * The virtual files holding the data for the 1 bpp screens are stored on 
 * the \c \%embedded\% PostScript device in the \c Screening folder.  This function 
 * builds a string containing the full path to the screen's virtual file.
 * \param[in]   ActiveScreen    A pointer to a string containing the screen's name.
 * \param[out]  ScreenFile      A pointer to a string to receive the specified screen's filename.
 */
void Get1bppScreenFileName(char *ScreenFile, char *ActiveScreen)
{
  int i;

  ScreenFile[0]='\0';
  for(i = 0; i < OIL_EBD_SCRN_NUM_SCREENS; i++)
  {
      if(strcmp((char*)&l_tOILScreens[i].pszPSName[1], ActiveScreen) == 0)
      {
        sprintf(ScreenFile, "%%embedded%%/Screening%s", l_tOILScreens[i].pszPSName);
        break;
      }
  }
}

