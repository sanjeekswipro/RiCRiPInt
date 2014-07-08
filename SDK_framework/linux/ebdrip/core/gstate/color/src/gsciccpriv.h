/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsciccpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Graphic-state Private ICC color information
 */

#ifndef __GSCICCPRIV_H__
#define __GSCICCPRIV_H__

#include "gsctoicc.h"
#include "gscicc.h"


/*----------------------------------------------------------------------------*/
/* Taken from icdefs.h */

#ifndef highbytefirst

#define FIX_UINT32(x) (uint32) \
  ((((x) & 0xff000000) >> 24)  \
  |(((x) & 0x00ff0000) >> 8)   \
  |(((x) & 0x0000ff00) << 8)   \
  |(((x) & 0x000000ff) << 24))

#define FIX_UINT16(x) (uint16) \
  ((((x) & 0xff00) >> 8)       \
  |(((x) & 0x00ff) << 8))

#define FIX_S15F16(x) (int32)  \
  ((((x) & 0xff000000) >> 24) \
  |(((x) & 0x00ff0000) >> 8)  \
  |(((x) & 0x0000ff00) << 8)  \
  |(((x) & 0x000000ff) << 24))

#else

#define FIX_UINT32(x) (x)
#define FIX_UINT16(x) (x)
#define FIX_S15F16(x) (x)

#endif

#define icfixsig(sig) \
  sig = FIX_UINT32(sig)

#define icfixu32(val) \
  val = FIX_UINT32(val)

#define icfixs15f16(val) \
  val = FIX_S15F16(val)

#define icfixu16(val) \
  val = FIX_UINT16(val)

/*----------------------------------------------------------------------------*/
#define MAX_CHANNELS 15
#define curv_header_size 12  /* for curveType and parametricCurveType curves */

/* The following give the space taken up by the curves in the icc profile,
 * including up to 3 bytes of padding for the curveType variant.
 */
#define PARA_SPACE( _len_ ) curv_header_size + sizeof(int32) * ( _len_ )
#define CURV_SPACE( _len_ ) curv_header_size + 4 * ((3 + ( _len_ ) * sizeof(uint16)) / 4)

#ifndef ASSERT_BUILD
#define iccbasedInfoAssertions(_pIccBasedInfo) EMPTY_STATEMENT()
#else
void iccbasedInfoAssertions(CLINKiccbased *pIccBasedInfo);
#endif

#define EPSILON 0.0010f /* for whitepoints etc */

#define MD5BUFFLEN 4096 /* buffer size for presentation of data to md5 calc */

#define SCRGB_MIN_VALUE -0.5f /* minimum permitted scRGB input value */
#define SCRGB_MAX_VALUE 7.5f  /* maximum permitted scRGB input value */

/*----------------------------------------------------------------------------*/
/* Following taken from iconvert.c */

/* Definitions */

/* The idea is to look at the collection of tags present in an ICC
   file, and work out quite what kind of profile it is, and hence
   quite what has to be done for various conversion commands.

   Looking at collections of tags... we want to convert what tags are
   present into a bitfield, so we can then try ANDing it with a
   collection of possibles... one of which we hope will produce
   non-zero, meaning we have a profile matching that kind, and can
   then apply the corresponding algorithm.

   The following sequence of bits for the bitfield must be edited in
   step with the array of tag values in Interesting_Tags...
   sorry, but this is C.
*/
#define sigbitAToB0Tag                    0x1
#define sigbitAToB1Tag                    0x2
#define sigbitAToB2Tag                    0x4
#define sigbitBlueColorantTag             0x8
#define sigbitBlueTRCTag                 0x10
#define sigbitBToA0Tag                   0x20
#define sigbitBToA1Tag                   0x40
#define sigbitBToA2Tag                   0x80
#define sigbitGamutTag                  0x100
#define sigbitGrayTRCTag                0x200
#define sigbitGreenColorantTag          0x400
#define sigbitGreenTRCTag               0x800
#define sigbitLuminanceTag             0x1000
#define sigbitMediaBlackPointTag       0x2000
#define sigbitMediaWhitePointTag       0x4000
#define sigbitNamedColorTag            0x8000
#define sigbitPreview0Tag             0x10000
#define sigbitPreview1Tag             0x20000
#define sigbitPreview2Tag             0x40000
#define sigbitProfileDescriptionTag   0x80000
#define sigbitProfileSequenceDescTag 0x100000
#define sigbitRedColorantTag         0x200000
#define sigbitRedTRCTag              0x400000
#define sigbitReferenceResponseTag   0x800000
#define sigbitUcrBgTag              0x1000000
#define sigbitViewingCondDescTag    0x2000000
#define sigbitViewingConditionsTag  0x4000000
#define sigbitColorantTableOutTag   0x8000000
#define sigbitColorantTableTag     0x10000000
#define sigbitWcsProfilesTag       0x20000000

/* Say whether _a has all the bits set required by _b */
#define has_all_bits(_a,_b) (((_a) & (_b)) == (_b))

STATIC icTagSignature Interesting_Tags[] = {
  icSigAToB0Tag,
  icSigAToB1Tag,
  icSigAToB2Tag,
  icSigBlueColorantTag,
  icSigBlueTRCTag,
  icSigBToA0Tag,
  icSigBToA1Tag,
  icSigBToA2Tag,
  icSigGamutTag,
  icSigGrayTRCTag,
  icSigGreenColorantTag,
  icSigGreenTRCTag,
  icSigLuminanceTag,
  icSigMediaBlackPointTag,
  icSigMediaWhitePointTag,
  icSigNamedColorTag,
  icSigPreview0Tag,
  icSigPreview1Tag,
  icSigPreview2Tag,
  icSigProfileDescriptionTag,
  icSigProfileSequenceDescTag,
  icSigRedColorantTag,
  icSigRedTRCTag,
  icSigOutputResponseTag,
  icSigUcrBgTag,
  icSigViewingCondDescTag,
  icSigViewingConditionsTag,
  icSigColorantTableOutTag,
  icSigColorantTableTag,
  icSigWcsProfilesTag,
  (icTagSignature)0
};

/*----------------------------------------------------------------------------*/
/* Taken from icolpriv.h */

typedef struct
{
  icTag               tag;
  icTagTypeSignature  points_at;
} icTags;

/*----------------------------------------------------------------------------*/
/* Taken from icread.h and icread.c */
typedef struct lut_temp_data {
  icSignature             sig;            /* Signature, "mft2" */
  icInt8Number            reserved[4];    /* Reserved, set to 0 */
  icUInt8Number           inputChan;      /* Number of input channels */
  icUInt8Number           outputChan;     /* Number of output channels */
  icUInt8Number           clutPoints;     /* Number of clutTable gridpoints */
  icInt8Number            pad;            /* Padding for byte alignment */
  icS15Fixed16Number      e00;            /* e00 in the 3 * 3 */
  icS15Fixed16Number      e01;            /* e01 in the 3 * 3 */
  icS15Fixed16Number      e02;            /* e02 in the 3 * 3 */
  icS15Fixed16Number      e10;            /* e10 in the 3 * 3 */
  icS15Fixed16Number      e11;            /* e11 in the 3 * 3 */
  icS15Fixed16Number      e12;            /* e12 in the 3 * 3 */
  icS15Fixed16Number      e20;            /* e20 in the 3 * 3 */
  icS15Fixed16Number      e21;            /* e21 in the 3 * 3 */
  icS15Fixed16Number      e22;            /* e22 in the 3 * 3 */
} LUT_TEMP_DATA;

typedef struct lut_16_temp_data {
  icUInt16Number          inputEnt;       /* Number of input table entries */
  icUInt16Number          outputEnt;      /* Number of output table entries */
} LUT_16_TEMP_DATA;

typedef struct lut_AB_BA_temp_data {
  icSignature             sig;            /* Signature, "mft2" */
  icInt8Number            reserved[4];    /* Reserved, set to 0 */
  icUInt8Number           inputChan;      /* Number of input channels */
  icUInt8Number           outputChan;     /* Number of output channels */
  icInt8Number            pad[2];
  icUInt32Number          offsetBcurve;
  icUInt32Number          offsetMatrix;
  icUInt32Number          offsetMcurve;
  icUInt32Number          offsetCLUT;
  icUInt32Number          offsetAcurve;
} LUT_AB_BA_TEMP_DATA;


typedef struct lut_temp_table_header {
  icUInt8Number           count[16];
  icUInt8Number           precision;
  icInt8Number            pad[3];
} LUT_TEMP_TABLE_HEADER;

typedef struct CLUTATOB_HEADER {
  uint8 gridpoints[16];
  uint8 precision;
  uint8 pad[3];
} CLUTATOB_HEADER;

#endif

/* Log stripped */
