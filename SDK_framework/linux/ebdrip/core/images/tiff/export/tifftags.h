/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!export:tifftags.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of Tiff tags
 */


#ifndef __TIFFTAGS_H__
#define __TIFFTAGS_H__ (1)

#include "tifffile.h"   /* tiff_file_t */

/*
 * Compression entry values
 */ 
#define COMPRESS_None      (1)
#define COMPRESS_CCITT     (2)
#define COMPRESS_CCITT_T4  (3)
#define COMPRESS_CCITT_T6  (4)
#define COMPRESS_LZW       (5)
#define COMPRESS_JPEG_OLD  (6)
#define COMPRESS_JPEG      (7)
#define COMPRESS_FLATE     (8)
#define COMPRESS_FLATE_TIFFLIB (32946)
#define COMPRESS_Packbits  (32773)


/*
 * PhotometricInterpretation entry values
 */
#define PMI_WhiteIsZero   (0)
#define PMI_BlackIsZero   (1)
#define PMI_RGB           (2)
#define PMI_Palette       (3)
#define PMI_Transparency  (4)
#define PMI_Separated     (5) /* see InkSets below */
#define PMI_YCbCr         (6)
#define PMI_undefined     (7)
#define PMI_CIE_L_a_b_    (8) 
#define PMI_ICCLab        (9) 

#define PMI_MaxCount      (10) 

/* InkSet values for PMI_Separated */
#define INKSET_CMYK (1)   /* Default */
#define INKSET_NotCMYK (2)

/*
 * Allowed ResolutionUnits
 */
#define RESUNIT_NONE      (1)
#define RESUNIT_INCH      (2)
#define RESUNIT_CM        (3)

/*
 * Allowed values for PlanarConfiguration
 */
#define PLANAR_CONFIG_CHUNKY    (1)
#define PLANAR_CONFIG_PLANAR    (2)


/*
 * Allowed values for FillOrder
 */
#define FILLORDER_MSB_TO_LSB    (1u)
#define FILLORDER_LSB_TO_MSB    (2u)


/*
 * Some default IFD entry values
 */
#define ENTRY_DEF_SAMPLES_PER_PIXEL     (1u)
#define ENTRY_DEF_RESOLUTION_UNITS      RESUNIT_INCH
#define ENTRY_DEF_ORIENTATION           (1u)
#define ENTRY_DEF_COMPRESSION           COMPRESS_None
#define ENTRY_DEF_BITS_PER_SAMPLE       (1u)
#define ENTRY_DEF_COUNT_BITS_PER_SAMPLE (1u)
#define ENTRY_DEF_PREDICTOR             (1u)
#define ENTRY_DEF_T4OPTIONS             (0u)
#define ENTRY_DEF_T6OPTIONS             (0u)
#define ENTRY_DEF_FILLORDER             FILLORDER_MSB_TO_LSB
#define ENTRY_DEF_PLANARCONFIG          PLANAR_CONFIG_CHUNKY
#define ENTRY_DEF_INKSET                INKSET_CMYK
#define ENTRY_DEF_PMI                   PMI_WhiteIsZero


/*
 * Valid values for ExtraSamples
 */
#define EXTRASAMPLES_UNSPECIFIED_DATA   (0)
#define EXTRASAMPLES_ASSOCIATED_ALPHA   (1)
#define EXTRASAMPLES_UNASSOCIATED_ALPHA (2)

#define TIFF_POINTS_INCH      (72.0)
#define TIFF_CM_TO_INCH       (2.54)


/*
 * tiff_tag_name(), tiff_pmi_string(), tiff_compression_string(),
 * tiff_orientation_string(), tiff_resunits_string(), tiff_type_string(),
 * and tiff_planarconfig_string() return a pointer to a string version of
 * the IFD entry value, or the string "UnknownTag" if the value is not 
 * recognised.
 */
extern uint8* tiff_tag_name(
  tiff_short_t  tag);           /* I */

extern uint8* tiff_pmi_string(
  uint32        pmi);           /* I */

extern uint8* tiff_compression_string(
  uint32        compression);       /* I */

extern uint8* tiff_orientation_string(
  uint32        orientation);   /* I */

extern uint8* tiff_resunits_string(
  uint32        res_units);     /* I */

extern uint8* tiff_planarconfig_string(
  uint32        planar_config); /* I */

extern uint8* tiff_extrasamples_string(
  uint32        extra_samples); /* I */

extern uint8* tiff_fillorder_string(
  uint32        fill_order);    /* I */

#endif /* !__TIFFTAGS_H__ */


/* Log stripped */
