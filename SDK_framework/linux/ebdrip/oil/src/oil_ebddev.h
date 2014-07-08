/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_ebddev.h(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief Header file for the Embedded device type. Defines Embedded device parameters.
 *
 */

#ifndef _OIL_EBDDEV_H_
#define _OIL_EBDDEV_H_

#include "std.h"
#include "swdevice.h"

#define EBD_MEDIATYPE_MAXLENGTH 20
#define EBD_MEDIACOLOR_MAXLENGTH 20
#define EBD_MEDIADEST_MAXLENGTH 32
#define EBD_SCREENNAME_MAXLENGTH 20
#define EBD_SCREENFILE_MAXLENGTH 50
#define EBD_COMMENTPARSER_MAXLENGTH 100
#define EBD_MAX_PDFSPOOLDIR_LENGTH 256
/*! \brief Structure to hold embedded-device parameter values.
*/
typedef struct {
  float PageWidthFromJob;         /**< Pagewidth requested by the job */
  float PageHeightFromJob;        /**< Pageheight requested by the job */
  float xResolutionFromJob;       /**< x resolution requested by the job */
  float yResolutionFromJob;       /**< y resolution requested by the job */
  int VPCFromJob;                 /**< Values Per Component requested by the job */
  unsigned char MediaTypeFromJob[EBD_MEDIATYPE_MAXLENGTH];      /**< MediaType requested by the job */
  unsigned char MediaColorFromJob[EBD_MEDIACOLOR_MAXLENGTH];    /**< MediaColor requested by the job */
  unsigned char MediaDestFromJob[EBD_MEDIADEST_MAXLENGTH];    /**< MediaDest requested by the job */
  unsigned int MediaWeightFromJob;       /**< MediaWeight requested by the job */
  int MediaSourceFromJob;       /**< MediaSource requested by the job */
  int bDuplexFromJob;            /**< Duplex requested by the job */
  int bTumbleFromJob;            /**< Tumble requested by the job */
  int OrientationFromJob;       /**< Orientation requested by the job */
  float PageWidthFromPMS;       /**< Pagewidth requested by pms/engine */
  float PageHeightFromPMS;      /**< Pageheight requested by the job */
  float MediaClipLeftFromPMS;   /**< Clip size specified by the PMS */
  float MediaClipTopFromPMS;    /**< Clip size specified by the PMS */
  float MediaClipWidthFromPMS;  /**< Clip size specified by the PMS */
  float MediaClipHeightFromPMS; /**< Clip size specified by the PMS */
  float ImagingBoxLeftFromPMS;  /**< Imaging size specified by the PMS */
  float ImagingBoxTopFromPMS;   /**< Imaging size specified by the PMS */
  float ImagingBoxRightFromPMS; /**< Imaging size specified by the PMS */
  float ImagingBoxBottomFromPMS;/**< Imaging size specified by the PMS */
  int PCL5PageSizeFromPMS;      /**< PCL5 page size enumeration specified by the PMS */
  int PCLXLPageSizeFromPMS;     /**< PCLXL page size enumeration specified by the PMS */
  int MediaSourceFromPMS;       /**< MediaSource requested by the PMS */
  int bTumbleFromPMS;           /**< Tumble requested by the OIL/PMS */
  int bMediaChangeNotify;       /**< True if all job media parameters are sent to embedded device by RIP */
  int bEndRender;               /**< True if rendering is complete and page is ready for checkin */
  int ColorModeFromJob;         /**< Color Mode requested by the job */
  int ScreenModeFromJob;        /**< Screen Mode requested by the job */
  int ImageScreenQualityFromJob;     /**< Image screen quality requested by the job */
  int GraphicsScreenQualityFromJob;  /**< Graphics screen quality requested by the job */
  int TextScreenQualityFromJob;      /**< Text screen quality requested by the job */
  int TestConfiguration;        /**< Configuration for testing the coming jobs */
  int bPureBlackText;           /**< True Pure Black Text enabled */
  int bAllTextBlack;            /**< True All Text Black enabled */
  int bBlackSubstitute;         /**< True Black Substitute enabled */
  unsigned char ActiveScreenName[EBD_SCREENNAME_MAXLENGTH];   /**< 1bpp Active Screen name (specified in sw/usr/hqnebd/htm1bpp) */
  int ScreenTableWidth;         /**< Active Screen Table's width */
  int ScreenTableHeight;        /**< Active Screen Table's height */
  unsigned char ScreenFile[EBD_SCREENFILE_MAXLENGTH];   /**< 1bpp Active Screen's file name */
  char ParseComment[EBD_COMMENTPARSER_MAXLENGTH];   /**< Parse Comment value passed by CommentParser */
  int bForceMonoIfNoCMY;        /**< If there is no CMY data treat job as monochrome*/
  int nJobPDL;                  /**< Current job PDL */
  char PDFSpoolDir[EBD_MAX_PDFSPOOLDIR_LENGTH];  /**< HDD PDF Spool directory for swinram */
} OIL_TyEBDDeviceParameters;

/**
 * \brief  Structure to hold device-specific state.
 */
typedef struct
{
  int last_error;      /**< holds last error */
} OIL_Ty_EBDDeviceState;

extern DEVICETYPE Embedded_Device_Type;

extern int ebd_get_int_value(char * szParamName, int * pValue);

void ebddev_InitDevParams();
void ebddev_ClearJobParams();
void ebddev_EndRender(int page);

#endif /* _OIL_EBDDEV_H_ */
