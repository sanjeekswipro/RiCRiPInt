/* Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:skinras.h(EBDSDK_P.1) $
 *
 * Header file providing raster description to the Harlequin RIP SDK api
 */

#ifndef __SKINRAS_H__
#define __SKINRAS_H__
/**
 * \file
 * \ingroup skinkit
 * \brief Provides raster description to the Harlequin RIP SDK API.
 */

/* ---------------------------------------------------------------------- */

/**
 * \brief The maximum string length for the \c /PageBufferType parameter in the
 * page device.
 */
#define MAX_PGB_TYPE_LENGTH 32

/**
 * \brief The maximum string length for the \c /OutputTarget parameter in the
 * page device.
 */
#define MAX_OUTPUT_TARGET_LENGTH 32

/**
 * \brief The maximum string length permitted for a print ticket feature.
 */
#define PT_STRING_LENGTH 256

/**
 * \brief The maximum string length permitted for a PDF ID.
 */
#define PDF_ID_LENGTH 33

/**
 * \brief The rip maps colorants onto channels (possibly none to some
   channels, possibly more than one), and it is these that are
   identified by rasterColorant. There are numChannels channels (which
   may not correspond to the number originally set up in PostScript,
   if it permitted the dynamic addition of extra spot colors), and the
   number of colorants is determined by the length of the linked list.

   For example, a four-color dye sublimation printer would normally
   set up to receive four channels, in the order or the ribbon
   interleaving. In composite use, typically each of the four
   colorants corresponding to the ribbon's colors would be mapped to
   the corresponding channel of raster data. However, separations sent
   to such a device would still require a four channel raster, but the
   only colorant would be mapped to the channel corresponding to the
   black colored ribbon. */

typedef struct rasterColorant {

  struct rasterColorant * pNext; /**< these form a linked list */

  int32 nChannel; /**< channels will always be presented in ascending numerical order,
                     but there may be some missing (the channel is blank - has no
                     colorant mapped onto it) or may appear more than once (more
                     than one separations is imposed on the channel) */

  uint8 colorantName [64];  /**< describes the colorant (NOT the channel) */

  float srgbEquivalent [3];  /**< the color of the colorant in sRGB color space */

} RasterColorant;

/* ---------------------------------------------------------------------- */

/** \brief screenData is only used in conjunction with RLE output format */

typedef struct screenData {
  struct screenData *sd_next;        /**< pointer to next screenData structure */

  int32             sd_RLEindex;     /**< RUN_SCREEN index which
                                      * this describes */

  float             sd_frequency;    /**< screen frequency */

  float             sd_angle;        /**< screen angle */

  uint8 *           sd_spotname;     /**< spot function name */

  int32             sd_index;        /**< unique screen index */

  int32             sd_colorant;     /**< the colorant for which this screen applies,
                                        as indexed into the pColorants list in the
                                        page header */
  int32             sd_r1 ;          /** The screen's R1 parameter */
  int32             sd_r2 ;          /** The screen's R2 parameter */
  int32             sd_r3 ;          /** The screen's R3 parameter */
  int32             sd_r4 ;          /** The screen's R4 parameter */
  int32             sd_phasex ;      /** The X phase of the screen */
  int32             sd_phasey ;      /** The Y phase of the screen */
} ScreenData;


/* ---------------------------------------------------------------------- */
/** \brief The main structure describing the raster produced by the RIP */

typedef struct rasterDescription {
  double  xResolution;       /**< dpi */

  double  yResolution;       /**< dpi */

  int32  imageNegate;        /**< bool: negate the image. */

  int32  mediaSelect;        /**< The cassette or paper tray to be used (as determined
                                from inputAttributes in setpagedevice). */

  int32  leftMargin;         /**< pixels */

  int32  rightMargin;        /**< pixels */

  int32  topMargin;          /**< pixels */

  int32  bottomMargin;       /**< pixels */

  int32  imageWidth;         /**< pixels */

  int32  imageHeight;        /**< pixels */

  int32  dataWidth;          /**< bits in a scan line (even in multiple bits per pixel
                                case): Divisible by 32. */

  int32  bandHeight;         /**< scanlines in a band. For band interleaved formats
                                this height is height for a single channel.
                             */
  int32  noCopies;           /**< integer: number of copies of this page to
                                be printed (in total). */

  int32  jobNumber;          /**< integer: unique number for this job. */

  int32  pageNumber;         /**< integer: unique number for this page (reset
                                to zero at the start of each job). */

  int32  documentNumber;     /**< integer: unique number for this document. */

  uint8  jobName[512];       /**< null terminated string containing the name of the job */

  int32  runLength;          /**< true if raster is run-length encoded? */

  int32  outputAttributes ;  /**< Which bin we're placing output into as derived from
                                OutputAttributes in setpagedevice */

  int32  screenDataCount;    /**< count of number of screens items in
                              * screenData chain (RLE only) */

  ScreenData *pScreenData;   /**< pointer to head of chain of screenData items (RLE only) */

  int32 insertSheet;         /**< for special sheets which must not be imaged onto,
                                derived from InsertSheet in setpagedevice */

  int32 numChannels;         /**< number of colorant channels. Note that some of these
                                may be blank, for example when separating to a band
                                interleaved device. also where the setup has
                                permitted extra spot colorants there may be more
                                channels than the fixed colorants originally
                                specified. note that for a monochrome device, this is
                                always 1 - it is _not_ the total number of
                                separations. */

  int32 interleavingStyle;   /**< enumeration as below */

  int32 rasterDepth;         /**< Bits per channel, 1/2/4/8/10/12/16. */

  int32 packingUnit;         /**< integer: size of chunks into which data is
                                packed, high-bit first. This will be 8 for
                                contone, 32 or 64 for halftone. */

  RasterColorant * pColorants;
                             /**< see RasterColorant above */

  uint8 colorantFamily [64];
                             /**< the colorant family to which the colorants of this
                                raster belong */
  int32 separation ;         /**< The current separation number */
  int32 nSeparations ;       /**< The number of separations */
  uint8 pageBufferType[ MAX_PGB_TYPE_LENGTH ] ;
                             /**< An indicator of the overall output
                                  methodology, copied from the
                                  /PageBufferType pagedevice key. */
  uint8 outputTarget[MAX_OUTPUT_TARGET_LENGTH];
  uint8 partialPaint ;       /**< 1 when this raster is a partial-paint. */

  uint8  PtfDocumentDuplex[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobDuplex[PT_STRING_LENGTH];      /**< PrintTicket feature */
  uint8  PtfMediaType[PT_STRING_LENGTH];      /**< PrintTicket feature */
  uint8  PtfPageOutputColor[PT_STRING_LENGTH];/**< PrintTicket feature */
  uint32 PtfOutputDeviceBPP;                  /**< PrintTicket feature */
  uint32 PtfOutputDriverBPP;                  /**< PrintTicket feature */
  uint8  PtfOutputQuality[PT_STRING_LENGTH];  /**< PrintTicket feature */
  int32  PtfPageFaceUp;      /**< Boolean to keep track of emulated page duplex */
  int32  PtfPageCopyCount;   /**< Number of copies of each page as specified in the PrintTicket */
  int32  PtfDocumentCopiesAllPages; /**< Number of copies of each document */
  int32  PtfJobCopiesAllDocuments; /**< Number of copies of each job */
  uint8  PtfJobOutputOptimization[PT_STRING_LENGTH];   /**< PrintTicket feature */
  uint8  PtfPagePhotoPrintingIntent[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobHolePunch[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfDocumentHolePunch[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobRollCutAtEndOfJob[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfDocumentRollCut[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobStapleAllDocuments[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfDocumentStaple[PT_STRING_LENGTH]; /**< PrintTicket feature */
  int32  PtfStapleAngle; /**< PrintTicket feature */
  int32  PtfStapleSheetCapacity; /**< PrintTicket feature */
  uint8  PtfPageBorderless[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobDeviceLanguage[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobDeviceLanguage_Level[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobDeviceLanguage_Encoding[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfJobDeviceLanguage_Version[PT_STRING_LENGTH]; /**< PrintTicket feature */
  uint8  PtfOutputBin[PT_STRING_LENGTH]; /**< PrintTicket feature */
  int32  PtfJobCollateAllDocuments; /**< PrintTicket feature */
  int32  PtfDocumentCollate; /**< PrintTicket feature */

  int32 duplex;       /**< bool: Whether the output is to be printed duplex. */
  int32 tumble;       /**< bool: Affects duplex printing. */
  int32 orientation;  /**< integer: Orientation of the page image. */
  int32 collate;      /**< bool: Whether to collate output. */
  int32 outputFaceUp; /**< bool: Page delivery option. */

  uint32 OptimizedPDFUsageCount;              /**< integer: how many times this page is used as a base layer by other pages */
  uint8  OptimizedPDFId[PDF_ID_LENGTH];       /**< empty string, or 32-character hex string unique ID for this page */

  /** When reading or writing, data for this particular band. */
  struct {
    int32 y1, y2 ;    /**< Start, end line of band within entire frame. */
    int32 advance ;   /**< Lines to advance after outputting. */
  } band ;

  int32 separation_id ; /**< Omission-independent separation id */
} RasterDescription;

/* ---------------------------------------------------------------------- */

/* interleavingStyle: */

/** \brief Monochrome interleaving style.
 * Monochrome indicates a single channel, but note that more than one colorant may be
   mapped onto that one channel if separations are imposed. Simple separations are
   separate sheets each of which is interleavingStyle_monochrome, but separations may
   also be made to color devices so this is not the only case to account for. */
#define interleavingStyle_monochrome  1

/** \brief Pixel interleaving style. Pixel interleaved means all colors of one pixel appear in raster memory before any
    colors of the next. This is similar to TIFF PlanarConfiguration = 1, Chunky. */
#define interleavingStyle_pixel       2

/** \brief Band interleaving style.  Band interleaved means that all
   the colors of pixels in a band of consecutive scan lines appear in
   a contiguous chunk of memory, but in that memory each color is
   presented in turn */
#define interleavingStyle_band        3

/** \brief Frame interleaving style.  Frame interleaved means that all
   pixels for one color of the raster appear before any pixels of the
   next color. This is similar to TIFF PlanarConfiguration = 2,
   Planar. */
#define interleavingStyle_frame       4


#endif /* __SKINRAS_H__ */

/* end of skinras.h */
