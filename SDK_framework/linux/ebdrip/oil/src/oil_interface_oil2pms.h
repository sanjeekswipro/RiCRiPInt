/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_interface_oil2pms.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This header file declares the OIL functions that support communication
 *   between the OIL and the PMS.
 *
 */


#ifndef _OIL_INTERFACE_OIL2PMS_H_
#define _OIL_INTERFACE_OIL2PMS_H_

#include "oil.h"
#include "pms_export.h"
#include "oil_ebddev.h"

/* skinkit and swdevice required for DEVICE_FILEDESCRIPTOR */
#include "skinkit.h"
#include "swdevice.h"

/** Array of PMS function pointers is declared in oil_interface_oil2pms.c */
extern int(**g_apfn_pms_calls[PMS_TOTAL_NUMBER_OF_API_FUNCTIONS])();

/* Sensible macros to the PMS API functions. */
#define PMS_ReadDataStream        (int)(*(*g_apfn_pms_calls)[EPMS_FN_ReadDataStream]) /* TODO: This is going to be removed very soon */
#define PMS_PeekDataStream        (int)(*(*g_apfn_pms_calls)[EPMS_FN_PeekDataStream])
#define PMS_ConsumeDataStream     (int)(*(*g_apfn_pms_calls)[EPMS_FN_ConsumeDataStream])
#define PMS_WriteDataStream       (int)(*(*g_apfn_pms_calls)[EPMS_FN_WriteDataStream])
#define PMS_CheckinPage           (int)(*(*g_apfn_pms_calls)[EPMS_FN_CheckinPage])
#define PMS_CheckinBand           (int)(*(*g_apfn_pms_calls)[EPMS_FN_CheckinBand])
#define PMS_DeletePrintedPage     (int)(*(*g_apfn_pms_calls)[EPMS_FN_DeletePrintedPage])
#define PMS_CMYKtoCMYK            (int)(*(*g_apfn_pms_calls)[EPMS_FN_CMYKtoCMYK])
#define PMS_RGBtoCMYK             (int)(*(*g_apfn_pms_calls)[EPMS_FN_RGBtoCMYK])
#define PMS_MediaSelect           (int)(*(*g_apfn_pms_calls)[EPMS_FN_MediaSelect])
#define PMS_GetPaperInfo          (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetPaperInfo])
#define PMS_SetPaperInfo          (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetPaperInfo])
#define PMS_GetMediaType          (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetMediaType])
#define PMS_GetMediaSource        (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetMediaSource])
#define PMS_GetMediaDest          (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetMediaDest])
#define PMS_GetMediaColor         (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetMediaColor])
#define PMS_GetTrayInfo           (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetTrayInfo])
#define PMS_SetTrayInfo           (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetTrayInfo])
#define PMS_GetOutputInfo         (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetOutputInfo])
#define PMS_GetScreenInfo         (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetScreenInfo])
#define PMS_GetSystemInfo         (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetSystemInfo])
#define PMS_SetSystemInfo         (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetSystemInfo])
#define PMS_PutBackChannel        (int)(*(*g_apfn_pms_calls)[EPMS_FN_PutBackChannel])
#define PMS_PutDebugMessage       (int)(*(*g_apfn_pms_calls)[EPMS_FN_PutDebugMessage])
#define PMS_GetBandInfo           (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetBandInfo])
#define PMS_RasterLayout          (int)(*(*g_apfn_pms_calls)[EPMS_FN_RasterLayout])
#define PMS_RasterRequirements    (int)(*(*g_apfn_pms_calls)[EPMS_FN_RasterRequirements])
#define PMS_RasterDestination     (int)(*(*g_apfn_pms_calls)[EPMS_FN_RasterDestination])
#define PMS_RippingComplete       (int)(*(*g_apfn_pms_calls)[EPMS_FN_RippingComplete])
#define PMS_Malloc                (int)(*(*g_apfn_pms_calls)[EPMS_FN_Malloc])
#define PMS_Free                  (int)(*(*g_apfn_pms_calls)[EPMS_FN_Free])
#define PMS_GetJobSettings        (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetJobSettings])
#define PMS_SetJobSettings        (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetJobSettings])
#define PMS_SetJobSettingsToDefaults (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetJobSettingsToDefaults])
#define PMS_FSInitVolume          (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSInitVolume])
#define PMS_FSMkDir               (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSMkDir])
#define PMS_FSOpen                (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSOpen])
#define PMS_FSRead                (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSRead])
#define PMS_FSWrite               (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSWrite])
#define PMS_FSClose               (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSClose])
#define PMS_FSSeek                (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSSeek])
#define PMS_FSDelete              (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSDelete])
#define PMS_FSDirEntryInfo        (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSDirEntryInfo])
#define PMS_FSQuery               (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSQuery])
#define PMS_FSFileSystemInfo      (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSFileSystemInfo])
#define PMS_FSGetDisklock         (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSGetDisklock])
#define PMS_FSSetDisklock         (int)(*(*g_apfn_pms_calls)[EPMS_FN_FSSetDisklock])
#define PMS_GetResource           (int)(*(*g_apfn_pms_calls)[EPMS_FN_GetResource])
#define PMS_FileOpen              (int)(*(*g_apfn_pms_calls)[EPMS_FN_FileOpen])
#define PMS_FileRead              (int)(*(*g_apfn_pms_calls)[EPMS_FN_FileRead])
#define PMS_FileClose             (int)(*(*g_apfn_pms_calls)[EPMS_FN_FileClose])
#define PMS_FileSeek              (int)(*(*g_apfn_pms_calls)[EPMS_FN_FileSeek])
#define PMS_FileBytes             (int)(*(*g_apfn_pms_calls)[EPMS_FN_FileBytes])
#define PMS_SetJobRIPConfig       (int)(*(*g_apfn_pms_calls)[EPMS_FN_SetJobRIPConfig])

#ifdef USE_UFST5
#define PMS_CGIFfco_Access              (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Access])
#define PMS_CGIFfco_Plugin              (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Plugin])
#define PMS_CGIFfco_Open                (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Open])
#define PMS_CGIFenter                   (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFenter])
#define PMS_CGIFconfig                  (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFconfig])
#define PMS_CGIFinit                    (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFinit])
#define PMS_CGIFinitRomInfo             (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFinitRomInfo])
#define PMS_CGIFfco_Close               (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Close])
#define PMS_CGIFmakechar                (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFmakechar])
#define PMS_CGIFchar_size               (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFchar_size])
#define PMS_CGIFfont                    (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfont])
#define PMS_UFSTGetPS3FontDataPtr       (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetPS3FontDataPtr])
#define PMS_UFSTGetWingdingFontDataPtr  (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetWingdingFontDataPtr])
#define PMS_UFSTGetPluginDataPtr        (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetPluginDataPtr])
#define PMS_UFSTGetSymbolSetDataPtr     (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetSymbolSetDataPtr])
#endif
#ifdef USE_UFST7
#define PMS_CGIFfco_Access              (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Access])
#define PMS_CGIFfco_Plugin              (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Plugin])
#define PMS_CGIFfco_Open                (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Open])
#define PMS_CGIFenter                   (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFenter])
#define PMS_CGIFconfig                  (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFconfig])
#define PMS_CGIFinit                    (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFinit])
#define PMS_CGIFinitRomInfo             (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFinitRomInfo])
#define PMS_CGIFfco_Close               (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfco_Close])
#define PMS_CGIFmakechar                (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFmakechar])
#define PMS_CGIFchar_size               (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFchar_size])
#define PMS_CGIFfont                    (int)(*(*g_apfn_pms_calls)[EPMS_FN_CGIFfont])
#define PMS_UFSTGetPS3FontDataPtr       (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetPS3FontDataPtr])
#define PMS_UFSTGetWingdingFontDataPtr  (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetWingdingFontDataPtr])
#define PMS_UFSTGetPluginDataPtr        (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetPluginDataPtr])
#define PMS_UFSTGetSymbolSetDataPtr     (int)(*(*g_apfn_pms_calls)[EPMS_FN_UFSTGetSymbolSetDataPtr])
#endif

void GetPMSSystemInfo();
PMS_TyPage* CreatePMSPage(OIL_TyPage *ptOILPage);
void DeletePMSPage(PMS_TyPage *ptPMSPage);
void SubmitPageToPMS(void);
int SubmitBandToPMS(PMS_TyBandPacket *ptBandpacket);
void GetMediaFromPMS(OIL_TyEBDDeviceParameters *stEBDDeviceParams);
void GetBandInfoFromPMS(int nPageWidth, int nPageHeight, unsigned int uRasterDepth,
                        PMS_TyBandInfo * stPMSBandInfo);
int GetScreenInfoFromPMS(int nScreen, PMS_TyScreenInfo **ppPMSScreenInfo);

/* Interface to Embedded Device for PDL Back Channel */
DEVICE_FILEDESCRIPTOR OIL_BackChannelOpen(void);
int OIL_BackChannelWrite(unsigned char * buff, int len);
int OIL_FileOpen(char * pzPath, char * flags, void ** pHandle);
int OIL_FileClose(void * handle);
int OIL_FileRead(unsigned char * buffer, int nBytesToRead, void * handle);
int OIL_FileSeek(void * handle, long *pPosition, int nWhence);
int OIL_FileBytes(void * handle);


#endif /* _OIL_INTERFACE_OIL2PMS_H_ */
