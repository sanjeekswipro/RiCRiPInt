/* Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_htm.h(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file contains the interface for all the HTM screening module examples.
 */
#ifndef __OIL_HTM_H__
#define __OIL_HTM_H__

#include "skinkit.h"
#include "swhtm.h"

/** @brief Object categories that can be screened differently.
 */
enum {
  GG_OBJ_GFX = 0,
  GG_OBJ_IMAGE,
  GG_OBJ_TEXT,

  OIL_MAXSCREENOBJECTS
};
typedef int OIL_eTyObjectType;

/** @brief The individual object type to screen table instances.
 *
 * Each instance of this structure contains an object type enumeration and one
 * or more screen table enumerations. One for each colorant supported.
 * The maximum number of colorants is defined by the number of elementes in  the 
 * @c eHtTables array. Each element of @c eHtTables specifies one for the 
 * @c PMS_eScreens enumerations.
 *
 * For example, the 1 bit per pixel screen tables used for CMYK objects that 
 * are in the "image"  category  could be defined as;
 * <pre>
 * OIL_TyScreens t;
 *   t.eObject = GG_OBJ_IMAGE;
 *   t.eHtTables[0] = PMS_SCREEN_1BPP_IMAGE_BLACK;
 *   t.eHtTables[1] = PMS_SCREEN_1BPP_IMAGE_CYAN;
 *   t.eHtTables[2] = PMS_SCREEN_1BPP_IMAGE_MAGENTA;
 *   t.eHtTables[3] = PMS_SCREEN_1BPP_IMAGE_YELLOW;
 * </pre>
 */
typedef struct OIL_stScreens
{
  OIL_eTyObjectType eObject;
  PMS_eScreens eHtTables[4];
}OIL_TyScreens;

/** @brief The dither table structure.
 *
 * The diterMatrix pointer array may contain up to 15 tables of thresholds. Each one
 * represents a different threshold level to set the destination device pixel.
 *
 * For 1 bit per pixel destination, then only the first element in the array points
 * to a table contain the single level threshold values.
 *
 * For 2 bits per pixel destination, the first three elements point to the three
 * threshold tables required to output the four possible levels.
 * 
 * For 4 bits per pixel, all 15 tables are used to output the 16 possible levels.
 *
 * It is conceiveable that any number of output levels can be implemented. 
 * For example, a device capable of six levels would use five threshold tables. 
 * In this case at least three bits per pixel are required to store the six possible 
 * levels per device pixel.
 */
typedef struct {
  unsigned short        uWidth;                /*!< Screen cell width in pixel units */
  unsigned short        uHeight;               /*!< Screen cell height in pixel units */
  const unsigned char   *(*ditherMatrix)[15];  /*!< Threshold array or arrays if multibit */
} DITHERTABLES;

/** @brief The structure this example uses internally for its threshold instances.
 *
 * This simple example module needs nothing more than a reference count and
 * a pointer to the start of the appropriate cell table array.
 */
typedef struct HTI {
  sw_htm_instance *selected ; /*!< Points to the selected instance. */
  DITHERTABLES pHTables[OIL_MAXSCREENOBJECTS];
} HTI ;

/* interface function prototypes */
/* 4-bpp */
sw_htm_api *htm4bpp_getInstance();
void htm4bpp_remapHTtables(OIL_eScreenQuality eImageQuality, OIL_eScreenQuality eGraphicsQuality, OIL_eScreenQuality eTextQuality);
/* 2-bpp */
sw_htm_api *htm2bpp_getInstance();
void htm2bpp_remapHTtables(OIL_eScreenQuality eImageQuality, OIL_eScreenQuality eGraphicsQuality, OIL_eScreenQuality eTextQuality);

#endif /* __OIL_HTM_H__ */
