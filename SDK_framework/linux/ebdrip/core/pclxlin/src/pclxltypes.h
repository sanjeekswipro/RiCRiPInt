/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * "C" typedefs and enumerations that represent PCLXL-specific
 * datastream constructs operators, attribute IDs, data-type tags
 * and multi-byte (numeric) values
 */

#ifndef __PCLXLTYPES_H__
#define __PCLXLTYPES_H__ 1

/*
 * The PCLXL Language suports the construction of page content
 * by providing a set of operators and data (type) tags.
 *
 * When read from a PCLXL data stream these are read as unsigned byte values
 * for which we have a PCLXL_TAG type
 */

typedef uint32 PCLXL_TAG;

/*
 * But we also choose to have various numerations in here that list
 * the complete sets of operators, data types, attribute and data lengths
 * and encodings
 *
 * We assign each enumeration symbol an unsigned 8-bit value such that
 * the value corresponds to the PCLXL unsigned byte-code tag
 *
 * These lists were derived from
 * "PCL XL Feature Reference Protocol Class 2.0" revision 2.2, dated 16-Mar-2000, page 217
 * supplemented by "PCL XL Feature Reference Protocol Class 2.1 Supplement", revision 1.0, dated 09-Aug-2000
 * and by "PCL XL Feature Reference Protocol Class 3.0 Supplement", revision 1.0, dated 08-Jul-2002
 */

enum
{
  /*
   * There are 3 encoding bindings for PCLXL
   * ASCII, High-Byte-First (big-endian)
   * and Low-Byte-First (little-endian)
   *
   * This is indicated by the first byte
   * in the PCLXL byte-code/tag stream
   */

  PCLXL_ENC_Ascii                   = 0x27,
  PCLXL_ENC_BinaryHighByteFirst     = 0x28,
  PCLXL_ENC_BinaryLowByteFirst      = 0x29
};

/*
 * PCLXL Operators
 * As of "PCLXL protocol version 3.0" there are 90 of them.
 */

enum
{
  PCLXL_OP_ArcPath                  = 0x91,
  PCLXL_OP_BeginChar                = 0x52,
  PCLXL_OP_BeginFontHeader          = 0x4f,
  PCLXL_OP_BeginImage               = 0xb0,
  PCLXL_OP_BeginPage                = 0x43,
  PCLXL_OP_BeginRastPattern         = 0xb3,
  PCLXL_OP_BeginScan                = 0xb6,
  PCLXL_OP_BeginSession             = 0x41,
  PCLXL_OP_BeginStream              = 0x5b,
  PCLXL_OP_BeginUserDefinedLineCap  = 0x82,
  PCLXL_OP_BezierPath               = 0x93,
  PCLXL_OP_BezierRelPath            = 0x95,
  PCLXL_OP_Chord                    = 0x96,
  PCLXL_OP_ChordPath                = 0x97,
  PCLXL_OP_CloseDataSource          = 0x49,
  PCLXL_OP_CloseSubPath             = 0x84,
  PCLXL_OP_Comment                  = 0x47,
  PCLXL_OP_Ellipse                  = 0x98,
  PCLXL_OP_EllipsePath              = 0x99,
  PCLXL_OP_EndChar                  = 0x54,
  PCLXL_OP_EndFontHeader            = 0x51,
  PCLXL_OP_EndImage                 = 0xb2,
  PCLXL_OP_EndPage                  = 0x44,
  PCLXL_OP_EndRastPattern           = 0xb5,
  PCLXL_OP_EndScan                  = 0xb8,
  PCLXL_OP_EndSession               = 0x42,
  PCLXL_OP_EndStream                = 0x5d,
  PCLXL_OP_EndUserDefinedLineCaps   = 0x83,
  PCLXL_OP_ExecStream               = 0x5e,
  PCLXL_OP_LinePath                 = 0x9b,
  PCLXL_OP_LineRelPath              = 0x9d,
  PCLXL_OP_NewPath                  = 0x85,
  PCLXL_OP_OpenDataSource           = 0x48,
  PCLXL_OP_PaintPath                = 0x86,
  PCLXL_OP_Passthrough              = 0xbf,
  PCLXL_OP_Pie                      = 0x9e,
  PCLXL_OP_PiePath                  = 0x9f,
  PCLXL_OP_PopGS                    = 0x60,
  PCLXL_OP_PushGS                   = 0x61,
  PCLXL_OP_ReadChar                 = 0x53,
  PCLXL_OP_ReadFontHeader           = 0x50,
  PCLXL_OP_ReadImage                = 0xb1,
  PCLXL_OP_ReadRastPattern          = 0xb4,
  PCLXL_OP_ReadStream               = 0x5c,
  PCLXL_OP_Rectangle                = 0xa0,
  PCLXL_OP_RectanglePath            = 0xa1,
  PCLXL_OP_RemoveFont               = 0x55,
  PCLXL_OP_RemoveStream             = 0x5f,
  PCLXL_OP_RoundRectangle           = 0xa2,
  PCLXL_OP_RoundRectanglePath       = 0xa3,
  PCLXL_OP_ScanLineRel              = 0xb9,
  PCLXL_OP_SetAdaptiveHalftoning    = 0x94,
  PCLXL_OP_SetBrushSource           = 0x63,
  PCLXL_OP_SetCharAttributes        = 0x56,
  PCLXL_OP_SetCharAngle             = 0x64,
  PCLXL_OP_SetCharBoldValue         = 0x7d,
  PCLXL_OP_SetCharScale             = 0x65,
  PCLXL_OP_SetCharShear             = 0x66,
  PCLXL_OP_SetCharSubMode           = 0x81,
  PCLXL_OP_SetClipIntersect         = 0x67,
  PCLXL_OP_SetClipMode              = 0x7f,
  PCLXL_OP_SetClipRectangle         = 0x68,
  PCLXL_OP_SetClipReplace           = 0x62,
  PCLXL_OP_SetClipToPage            = 0x69,
  PCLXL_OP_SetColorSpace            = 0x6a,
  PCLXL_OP_SetColorTrapping         = 0x92,
  PCLXL_OP_SetColorTreatment        = 0x58,
  PCLXL_OP_SetCursor                = 0x6b,
  PCLXL_OP_SetCursorRel             = 0x6c,
  PCLXL_OP_SetDefaultGS             = 0x57,
  PCLXL_OP_SetHalftoneMethod        = 0x6d,
  PCLXL_OP_SetFillMode              = 0x6e,
  PCLXL_OP_SetFont                  = 0x6f,
  PCLXL_OP_SetLineCap               = 0x71,
  PCLXL_OP_SetLineDash              = 0x70,
  PCLXL_OP_SetLineJoin              = 0x72,
  PCLXL_OP_SetMiterLimit            = 0x73,
  PCLXL_OP_SetNeutralAxis           = 0x7e,
  PCLXL_OP_SetPageDefaultCTM        = 0x74,
  PCLXL_OP_SetPageOrigin            = 0x75,
  PCLXL_OP_SetPageRotation          = 0x76,
  PCLXL_OP_SetPageScale             = 0x77,
  PCLXL_OP_SetPathToClip            = 0x80,
  PCLXL_OP_SetPaintTxMode           = 0x78,
  PCLXL_OP_SetPenSource             = 0x79,
  PCLXL_OP_SetPenWidth              = 0x7a,
  PCLXL_OP_SetROP                   = 0x7b,
  PCLXL_OP_SetSourceTxMode          = 0x7c,
  PCLXL_OP_Text                     = 0xa8,
  PCLXL_OP_TextPath                 = 0xa9,
  PCLXL_OP_VendorUnique             = 0x46
};

/*
 * Many PCLXL operators are preceded by a collection/list of attributes
 * But there are a few operators that expect a chunk of embeded data.
 * In this case the embeded data appears *after* the operation byte-code/tag
 * and this data is prefixed by a length
 * So there are two tags that indicated that what follows
 * is either a single-byte or multi-byte data length
 * which is then followed by that many bytes of data.
 */

enum
{
  PCLXL_DL_DataLength_UInt32        = 0xfa,
  PCLXL_DL_DataLength_UByte         = 0xfb
};

/*
 * PCLXL data stream also allows various types of
 * multi-byte numeric data types.
 *
 * In order to be able to read them correctly each type of
 * data is prefixed by a data type tag as follows.
 */

enum
{
  PCLXL_DT_Real32                   = 0xc5,
  PCLXL_DT_Real32_Array             = 0xcd,
  PCLXL_DT_Real32_Box               = 0xe5,
  PCLXL_DT_Real32_Box_Array         = 0xed,
  PCLXL_DT_Real32_XY                = 0xd5,
  PCLXL_DT_Real32_XY_Array          = 0xdd,

  PCLXL_DT_Int16                    = 0xc3,
  PCLXL_DT_Int16_Array              = 0xcb,
  PCLXL_DT_Int16_Box                = 0xe3,
  PCLXL_DT_Int16_Box_Array          = 0xeb,
  PCLXL_DT_Int16_XY                 = 0xd3,
  PCLXL_DT_Int16_XY_Array           = 0xdb,

  PCLXL_DT_Int32                    = 0xc4,
  PCLXL_DT_Int32_Array              = 0xcc,
  PCLXL_DT_Int32_Box                = 0xe4,
  PCLXL_DT_Int32_Box_Array          = 0xec,
  PCLXL_DT_Int32_XY                 = 0xd4,
  PCLXL_DT_Int32_XY_Array           = 0xdc,

  PCLXL_DT_UByte                    = 0xc0,
  PCLXL_DT_UByte_Array              = 0xc8,
  PCLXL_DT_UByte_Box                = 0xe0,
  PCLXL_DT_UByte_Box_Array          = 0xe8,
  PCLXL_DT_UByte_XY                 = 0xd0,
  PCLXL_DT_UByte_XY_Array           = 0xd8,

  PCLXL_DT_UInt16                   = 0xc1,
  PCLXL_DT_UInt16_Array             = 0xc9,
  PCLXL_DT_UInt16_Box               = 0xe1,
  PCLXL_DT_UInt16_Box_Array         = 0xe9,
  PCLXL_DT_UInt16_XY                = 0xd1,
  PCLXL_DT_UInt16_XY_Array          = 0xd9,

  PCLXL_DT_UInt32                   = 0xc2,
  PCLXL_DT_UInt32_Array             = 0xca,
  PCLXL_DT_UInt32_Box               = 0xe2,
  PCLXL_DT_UInt32_Box_Array         = 0xea,
  PCLXL_DT_UInt32_XY                = 0xd2,
  PCLXL_DT_UInt32_XY_Array          = 0xda
};

/*
 * As mentioned above many of the PCLXL operators
 * can be preceded by a collection/list of "attributes"
 * Each attribute has a unique ID
 * in order to allow an extensible set of more than 256 attributes
 * attribute IDs can either be one or two bytes in length
 * as indicated by a tag that specifies that a
 * 1-byte or 2-byte attribute ID follows
 */

enum
{
  PCLXL_AT_Attribute_UByte          = 0xf8,
  PCLXL_AT_Attribute_UInt16         = 0xf9
};

/*
 * In PCLXL certain characters are given their own
 * special tags so that they can be handled differently
 * from other characters
 */

enum
{
  PCLXL_CHAR_Null                   = 0x00,
  PCLXL_CHAR_HT                     = 0x09,
  PCLXL_CHAR_LF                     = 0x0a,
  PCLXL_CHAR_VT                     = 0x0b,
  PCLXL_CHAR_FF                     = 0x0c,
  PCLXL_CHAR_CR                     = 0x0d,
  PCLXL_CHAR_Space                  = 0x20
};

/*
 * There are a few other specific byte codes
 * that are of significance within a PCLXL byte code stream
 * including the "Escape" character (0x1b) and the semicolon character
 * (0x3b)
 */

enum
{
  PCLXL_CHAR_Escape                 = 0x1b,
  PCLXL_CHAR_Semicolon              = 0x3b
};

/*
 * In PCLXL many/most "operators" take some sort of parameters/arguments.
 * These parameters can be supplied as "attributes"
 * which appear in front of the operators.
 *
 * Each attribute has a unique integer ID
 * and an associated data type and value.
 *
 * This integer ID is typically one byte long
 * but PCLXL allows for an extensible set of attributes
 * by allowing for a 1 or 2 byte attribute ID
 *
 * When reading this from the PCLXL data stream
 * and when storing this in the attribute list beneath a PCLXL_CONTEXT
 * we always store it as an unsigned 16-bit integer value
 *
 * I know that this typically wastes a bit of storage
 * because most, if not all, currently accepted attribute IDs are
 * those numbered 0 to 255, but we will only ever hold a few attributes at a time.
 */

typedef uint32 PCLXL_ATTR_ID;

/*
 * The following enumeration lists
 * the current PCLXL protocol class 3.0 recognised list of attributes
 *
 * And, even if we encounter any 2-byte attribute IDs
 * inside a real-world PCLXL "job", then we almost certainly
 * don't handle it
 */

enum
{
  PCLXL_AT_None                  = 0,
  PCLXL_AT_AllObjectTypes        = 29,
  PCLXL_AT_ArcDirection          = 65,
  PCLXL_AT_BitmapCharScaling     = 36,
  PCLXL_AT_BlockByteLength       = 111,
  PCLXL_AT_BlockHeight           = 99,
  PCLXL_AT_BoundingBox           = 66,
  PCLXL_AT_CharAngle             = 161,
  PCLXL_AT_CharBoldValue         = 177,
  PCLXL_AT_CharCode              = 162,
  PCLXL_AT_CharDataSize          = 163,
  PCLXL_AT_CharScale             = 164,
  PCLXL_AT_CharShear             = 165,
  PCLXL_AT_CharSize              = 166,
  PCLXL_AT_CharSubModeArray      = 172,
  PCLXL_AT_ClipMode              = 84,
  PCLXL_AT_ClipRegion            = 83,
  PCLXL_AT_ColorDepth            = 98,
  PCLXL_AT_ColorMapping          = 100,
  PCLXL_AT_ColorSpace            = 3,
  PCLXL_AT_ColorTreatment        = 120,
  PCLXL_AT_CommentData           = 129,
  PCLXL_AT_CompressMode          = 101,
  PCLXL_AT_ControlPoint1         = 81,
  PCLXL_AT_ControlPoint2         = 82,
  PCLXL_AT_CustomMediaSize       = 47,
  PCLXL_AT_CustomMediaSizeUnits  = 48,
  PCLXL_AT_DashOffset            = 67,
  PCLXL_AT_DataOrg               = 130,
  PCLXL_AT_DestinationSize       = 103,
  PCLXL_AT_DeviceMatrix          = 33,
  PCLXL_AT_DitherMatrixDataType  = 34,
  PCLXL_AT_DitherMatrixDepth     = 51,
  PCLXL_AT_DitherMatrixSize      = 50,
  PCLXL_AT_DitherOrigin          = 35,
  PCLXL_AT_DuplexPageMode        = 53,
  PCLXL_AT_DuplexPageSide        = 54,
  PCLXL_AT_EllipseDimension      = 68,
  PCLXL_AT_EndPoint              = 69,
  PCLXL_AT_ErrorReport           = 143,
  PCLXL_AT_FillMode              = 70,
  PCLXL_AT_FontFormat            = 169,
  PCLXL_AT_FontHeaderLength      = 167,
  PCLXL_AT_FontName              = 168,
  PCLXL_AT_GrayLevel             = 9,
  PCLXL_AT_LineCapStyle          = 71,
  PCLXL_AT_LineDashStyle         = 74,
  PCLXL_AT_LineJoinStyle         = 72,
  PCLXL_AT_Measure               = 134,
  PCLXL_AT_MediaDestination      = 36,
  PCLXL_AT_MediaSize             = 37,
  PCLXL_AT_MediaSource           = 38,
  PCLXL_AT_MediaType             = 39,
  PCLXL_AT_MiterLength           = 73,
  PCLXL_AT_NewDestinationSize    = 13,
  PCLXL_AT_NullBrush             = 4,
  PCLXL_AT_NullPen               = 5,
  PCLXL_AT_NumberOfPoints        = 77,
  PCLXL_AT_NumberOfScanLines     = 115,
  PCLXL_AT_Orientation           = 40,
  PCLXL_AT_PadBytesMultiple      = 110,
  PCLXL_AT_PageAngle             = 41,
  PCLXL_AT_PageCopies            = 49,
  PCLXL_AT_PageOrigin            = 42,
  PCLXL_AT_PageScale             = 43,
  PCLXL_AT_PaletteData           = 6,
  PCLXL_AT_PaletteDepth          = 2,
  PCLXL_AT_PatternDefineID       = 105,
  PCLXL_AT_PatternOrigin         = 12,
  PCLXL_AT_PatternPersistence    = 104,
  PCLXL_AT_PatternSelectID       = 8,
  PCLXL_AT_PCLSelectFont         = 141,
  /*
   * Actually PixelDepth = 98 and PixelEncoding = 100
   * appear to be duplicates of ColorDepth = 98 and ColorMapping = 100
   */
  PCLXL_AT_PixelDepth            = 98,
  PCLXL_AT_PixelEncoding         = 100,
  PCLXL_AT_PenWidth              = 75,
  PCLXL_AT_Point                 = 76,
  PCLXL_AT_PointType             = 80,
  PCLXL_AT_PrimaryArray          = 14,
  PCLXL_AT_PrimaryDepth          = 15,
  PCLXL_AT_RGBColor              = 11,
  PCLXL_AT_ROP3                  = 44,
  PCLXL_AT_RasterObjects         = 32,
  PCLXL_AT_SimplexPageMode       = 52,
  PCLXL_AT_SolidLine             = 78,
  PCLXL_AT_SourceHeight          = 107,
  PCLXL_AT_SourceType            = 136,
  PCLXL_AT_SourceWidth           = 108,
  PCLXL_AT_StartLine             = 109,
  PCLXL_AT_StartPoint            = 79,
  PCLXL_AT_StreamDataLength      = 140,
  PCLXL_AT_StreamName            = 139,
  PCLXL_AT_SymbolSet             = 170,
  PCLXL_AT_TextData              = 171,
  PCLXL_AT_TextObjects           = 30,
  PCLXL_AT_TxMode                = 45,
  PCLXL_AT_UnitsPerMeasure       = 137,
  PCLXL_AT_VectorObjects         = 31,
  PCLXL_AT_VUAttr1               = 147,
  PCLXL_AT_VUAttr2               = 148,
  PCLXL_AT_VUAttr3               = 149,
  PCLXL_AT_VUExtension           = 145,
  PCLXL_AT_WritingMode           = 173,
  PCLXL_AT_XSpacingData          = 175,
  PCLXL_AT_YSpacingData          = 176
};

/*
 * PCLXL numeric types are represented
 * in the core RIP as the following typedefs
 */

typedef uint8       PCLXL_UByte;
typedef uint16      PCLXL_UInt16;
typedef uint32      PCLXL_UInt32;
typedef int16       PCLXL_Int16;
typedef int32       PCLXL_Int32;
typedef float       PCLXL_Real32;

typedef SYSTEMVALUE PCLXL_SysVal;

typedef uint8   PCLXL_PROTOCOL_VERSION;
#define PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(c, r)  (((c) << 4) | (r))
#define PCLXL_UNPACK_PROTOCOL_CLASS(p)                ((p) >> 4)
#define PCLXL_UNPACK_PROTOCOL_REVISION(p)             ((p) & 0x0f)
#define PCLXL_MAX_PROTOCOL_CLASS                      (15)
#define PCLXL_MAX_PROTOCOL_REVISION                   (15)

/* ANY protocol can be used to check against the current stream protocol and pass */
#define PCLXL_PROTOCOL_VERSION_ANY    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(0, 0)
#define PCLXL_PROTOCOL_VERSION_1_0    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(1, 0)
#define PCLXL_PROTOCOL_VERSION_1_1    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(1, 1)
#define PCLXL_PROTOCOL_VERSION_2_0    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(2, 0)
#define PCLXL_PROTOCOL_VERSION_2_1    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(2, 1)
#define PCLXL_PROTOCOL_VERSION_3_0    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(3, 0)
#define PCLXL_PROTOCOL_VERSION_FUTURE PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(4, 0)
/* MAX protocol can be used to check the current attribute/operator protocol against and pass */
#define PCLXL_PROTOCOL_VERSION_MAX    PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(PCLXL_MAX_PROTOCOL_CLASS, PCLXL_MAX_PROTOCOL_REVISION)

#define PCLXL_STREAM_CLASS_NAME "HP-PCL XL"

typedef struct
{
  PCLXL_UByte  x;
  PCLXL_UByte  y;
} PCLXL_UByte_XY;

typedef struct
{
  PCLXL_UInt16 x;
  PCLXL_UInt16 y;
} PCLXL_UInt16_XY;

typedef struct
{
  PCLXL_UInt32 x;
  PCLXL_UInt32 y;
} PCLXL_UInt32_XY;

typedef struct
{
  PCLXL_Int16  x;
  PCLXL_Int16  y;
} PCLXL_Int16_XY;

typedef struct
{
  PCLXL_Int32  x;
  PCLXL_Int32  y;
} PCLXL_Int32_XY;

typedef struct
{
  PCLXL_Real32 x;
  PCLXL_Real32 y;
} PCLXL_Real32_XY;

typedef struct
{
  PCLXL_SysVal x;
  PCLXL_SysVal y;
} PCLXL_SysVal_XY;

typedef struct
{
  PCLXL_UByte  x1;
  PCLXL_UByte  y1;
  PCLXL_UByte  x2;
  PCLXL_UByte  y2;
} PCLXL_UByte_Box;

typedef struct
{
  PCLXL_UInt16 x1;
  PCLXL_UInt16 y1;
  PCLXL_UInt16 x2;
  PCLXL_UInt16 y2;
} PCLXL_UInt16_Box;

typedef struct
{
  PCLXL_UInt32 x1;
  PCLXL_UInt32 y1;
  PCLXL_UInt32 x2;
  PCLXL_UInt32 y2;
} PCLXL_UInt32_Box;

typedef struct
{
  PCLXL_Int16  x1;
  PCLXL_Int16  y1;
  PCLXL_Int16  x2;
  PCLXL_Int16  y2;
} PCLXL_Int16_Box;

typedef struct
{
  PCLXL_Int32  x1;
  PCLXL_Int32  y1;
  PCLXL_Int32  x2;
  PCLXL_Int32  y2;
} PCLXL_Int32_Box;

typedef struct
{
  PCLXL_Real32 x1;
  PCLXL_Real32 y1;
  PCLXL_Real32 x2;
  PCLXL_Real32 y2;
} PCLXL_Real32_Box;

typedef struct
{
  PCLXL_SysVal x1;
  PCLXL_SysVal y1;
  PCLXL_SysVal x2;
  PCLXL_SysVal y2;
} PCLXL_SysVal_Box;

typedef struct
{
  PCLXL_SysVal res_x;
  PCLXL_SysVal res_y;
} PCLXL_UnitsPerMeasure;

/*
 * Some of the above-mentioned/listed attributes
 * can only have values from within restricted enumeration ranges
 *
 * For each of these PCLXL enumeration attribute values
 * we provide a enumeration symbol
 * and a typedef (but not a "typedef enum" because there are known
 * problems with using typedef'd enums under various "C" compilers)
 */

typedef uint8 PCLXL_ENUMERATION;

enum
{
  PCLXL_eClockWise               = 0,
  PCLXL_eCounterClockWise        = 1
};

typedef PCLXL_ENUMERATION PCLXL_ArcDirection;

enum
{
  PCLXL_eNoSubstitution          = 0,
  PCLXL_eVerticalSubstitution    = 1
};

typedef PCLXL_ENUMERATION PCLXL_CharSubModeArray;

enum
{
  PCLXL_eNonZeroWinding          = 0,
  PCLXL_eEvenOdd                 = 1
};

typedef PCLXL_ENUMERATION PCLXL_ClipMode;

enum
{
  PCLXL_eInterior                = 0,
  PCLXL_eExterior                = 1
};

typedef PCLXL_ENUMERATION PCLXL_ClipRegion;

enum
{
  PCLXL_e1Bit                    = 0,
  PCLXL_e4Bit                    = 1,
  PCLXL_e8Bit                    = 2
};

typedef PCLXL_ENUMERATION PCLXL_ColorDepth;

enum
{
  PCLXL_eDirectPixel             = 0,
  PCLXL_eIndexedPixel            = 1
};

typedef PCLXL_ENUMERATION PCLXL_ColorMapping;

enum
{
  PCLXL_eGray                    = 1,
  PCLXL_eRGB                     = 2,
#ifdef DEBUG_BUILD
  PCLXL_eCMYK                    = 3,
#endif
  PCLXL_eSRGB                    = 6
};

typedef PCLXL_ENUMERATION PCLXL_ColorSpace;

enum
{
  PCLXL_eNoTreatment             = 0,
  PCLXL_eScreenMatch             = 1,
  PCLXL_eVivid                   = 2
};

typedef PCLXL_ENUMERATION PCLXL_ColorTreatment;

enum
{
  PCLXL_eTonerBlack               = 0,
  PCLXL_eProcessBlack             = 1
};

typedef PCLXL_ENUMERATION PCLXL_BlackType;

enum
{
  PCLXL_eNoCompression             = 0,
  PCLXL_eRLECompression            = 1,
  PCLXL_eJPEGCompression           = 2,
  PCLXL_eDeltaRowCompression       = 3,
  PCLXL_eJPEGCompressionForPattern = 4 /* Special internal case. */
};

typedef PCLXL_ENUMERATION PCLXL_CompressMode;

enum
{
  PCLXL_eBinaryHighByteFirst     = 0,
  PCLXL_eBinaryLowByteFirst      = 1
};

typedef PCLXL_ENUMERATION PCLXL_DataOrg;

enum
{
  PCLXL_eDefaultDataSource       = 0
};

typedef PCLXL_ENUMERATION PCLXL_DataSource;

enum
{
  PCLXL_eUByte                   = 0,
  PCLXL_eByte                    = 1,
  PCLXL_eUInt16                  = 2,
  PCLXL_eInt16                   = 3
};

typedef PCLXL_ENUMERATION PCLXL_DataTypeSimple;

enum
{
  PCLXL_eDeviceBest              = 0
};

typedef PCLXL_ENUMERATION PCLXL_DitherMatrix;

enum
{
  PCLXL_eHighLPI                 = 0,
  PCLXL_eMediumLPI               = 1,
  PCLXL_eLowLPI                  = 2
};

typedef PCLXL_ENUMERATION PCLXL_HalftoneMethod;

enum
{
  PCLXL_eDisable                 = 0,
  PCLXL_eEnable                  = 1
};

typedef PCLXL_ENUMERATION PCLXL_AdaptiveHalftone;

enum
{
  PCLXL_eDuplexHorizontalBinding  = 0,
  PCLXL_eDuplexVerticalBinding    = 1
};

typedef PCLXL_ENUMERATION PCLXL_DuplexPageMode;

enum
{
  PCLXL_eFrontMediaSide           = 0,
  PCLXL_eBackMediaSide            = 1
};

typedef PCLXL_ENUMERATION PCLXL_DuplexPageSide;

enum
{
  PCLXL_eNoReporting              = 0,
  PCLXL_eBackChannel              = 1,
  PCLXL_eErrorPage                = 2,
  PCLXL_eBackChAndErrPage         = 3,
  PCLXL_eNWBackChannel            = 4,
  PCLXL_eNWErrorPage              = 5,
  PCLXL_eNWBackChAndErrPage       = 6
};

typedef PCLXL_ENUMERATION PCLXL_ErrorReport;

typedef PCLXL_ClipRegion PCLXL_FillMode;

enum
{
  PCLXL_eDefaultFontFormat        = 0,
};

typedef PCLXL_ENUMERATION PCLXL_FontFormat;

enum
{
  PCLXL_eButtCap                  = 0,
  PCLXL_eRoundCap                 = 1,
  PCLXL_eSquareCap                = 2,
  PCLXL_eTriangleCap              = 3
};

typedef PCLXL_ENUMERATION PCLXL_LineCap;

enum
{
  PCLXL_eMiterJoin                = 0,
  PCLXL_eRoundJoin                = 1,
  PCLXL_eBevelJoin                = 2,
  PCLXL_eNoJoin                   = 3
};

typedef PCLXL_ENUMERATION PCLXL_LineJoin;

enum
{
  PCLXL_eRoundMiterJoin           = 0,
  PCLXL_eBevelMiterJoin           = 1,
  PCLXL_eNoMiterJoin              = 2
};

typedef PCLXL_ENUMERATION PCLXL_LineJoinMiterJoin;

enum
{
  PCLXL_eInch                     = 0,
  PCLXL_eMillimeter               = 1,
  PCLXL_eTenthsOfAMillimeter      = 2,
  PCLXL_e7200thsOfAnInch          = 3
};

typedef PCLXL_ENUMERATION PCLXL_Measure;

enum
{
  PCLXL_eDefaultDestination       = 0,
  PCLXL_eFaceDownBin              = 1,
  PCLXL_eFaceUpBin                = 2,
  PCLXL_eJobOffsetBin             = 3
};

typedef PCLXL_ENUMERATION PCLXL_MediaDestination;

enum
{
  PCLXL_eDefaultPaper             = 96,
  PCLXL_eLetterPaper              = 0,
  PCLXL_eLegalPaper               = 1,
  PCLXL_eA4Paper                  = 2,
  PCLXL_eExecPaper                = 3,
  PCLXL_eLedgerPaper              = 4,
  PCLXL_eA3Paper                  = 5,
  PCLXL_eCOM10Envelope            = 6,
  PCLXL_eMonarchEnvelope          = 7,
  PCLXL_eC5Envelope               = 8,
  PCLXL_eDLEnvelope               = 9,
  PCLXL_eJB4Paper                 = 10,
  PCLXL_eJB5Paper                 = 11,
  PCLXL_eB5Envelope               = 12,
  PCLXL_eB5Paper                  = 13,
  PCLXL_eJPostcard                = 14,
  PCLXL_eJDoublePostcard          = 15,
  PCLXL_eA5Paper                  = 16,
  PCLXL_eA6Paper                  = 17,
  PCLXL_eJB6Paper                 = 18,
  PCLXL_eJIS8K                    = 19,
  PCLXL_eJIS16K                   = 20,
  PCLXL_eJISExec                  = 21
};

typedef PCLXL_ENUMERATION PCLXL_MediaSize;

enum
{
  PCLXL_eDefaultSource            = 0,
  PCLXL_eAutoSelect               = 1,
  PCLXL_eManualFeed               = 2,
  PCLXL_eMultiPurposeTray         = 3,
  PCLXL_eUpperCassette            = 4,
  PCLXL_eLowerCassette            = 5,
  PCLXL_eEnvelopeTray             = 6,
  PCLXL_eThirdCassette            = 7
};

typedef PCLXL_ENUMERATION PCLXL_MediaSource;

enum
{
  PCLXL_ePortraitOrientation      = 0,
  PCLXL_eLandscapeOrientation     = 1,
  PCLXL_eReversePortrait          = 2,
  PCLXL_eReverseLandscape         = 3,
  PCLXL_eDefaultOrientation       = 4,
};

typedef PCLXL_ENUMERATION PCLXL_Orientation;

enum
{
  PCLXL_eTempPattern              = 0,
  PCLXL_ePagePattern              = 1,
  PCLXL_eSessionPattern           = 2
};

typedef PCLXL_ENUMERATION PCLXL_PatternPersistence;

enum
{
  PCLXL_eSimplexFrontSide         = 0
};

enum {
  /*PCLXL_eDisable  = 0, - defined above and thankfully has the same value */
  PCLXL_eMax      = 1,
  PCLXL_eNormal   = 2,
  PCLXL_eLight    = 3
};

typedef PCLXL_ENUMERATION PCLXL_AllObjectTypes;

/*
 * There are 256 raster operations (ROP3s)
 * whose values are all a compound of 3 sub fields:
 * consisting of 4 bits of "source"
 */

enum
{
  PCLXL_ROP3_0                    = 0,       /* 00000000 */
  PCLXL_ROP3_DPSoon               = 1,       /* 00000001 */
  PCLXL_ROP3_DPSona               = 2,       /* 00000010 */
  PCLXL_ROP3_PSon                 = 3,       /* 00000011 */
  PCLXL_ROP3_SDPona               = 4,       /* 00000100 */
  PCLXL_ROP3_DPon                 = 5,       /* 00000101 */
  PCLXL_ROP3_PDSxnon              = 6,       /* 00000110 */
  PCLXL_ROP3_PDSaon               = 7,       /* 00000111 */
  PCLXL_ROP3_SDPnaa               = 8,       /* 00001000 */
  PCLXL_ROP3_PDSxon               = 9,       /* 00001001 */
  PCLXL_ROP3_DPna                 = 10,      /* 00001010 */
  PCLXL_ROP3_PSDnaon              = 11,      /* 00001011 */
  PCLXL_ROP3_SPna                 = 12,      /* 00001100 */
  PCLXL_ROP3_PDSnaon              = 13,      /* 00001101 */
  PCLXL_ROP3_PDSonon              = 14,      /* 00001110 */
  PCLXL_ROP3_Pn                   = 15,      /* 00001111 */

  PCLXL_ROP3_PDSona               = 16,      /* 00010000 */
  PCLXL_ROP3_DSon                 = 17,      /* 00010001 */
  PCLXL_ROP3_SDPxnon              = 18,      /* 00010010 */
  PCLXL_ROP3_SDPaon               = 19,      /* 00010011 */
  PCLXL_ROP3_DPSxnon              = 20,      /* 00010100 */
  PCLXL_ROP3_DPSaon               = 21,      /* 00010101 */
  PCLXL_ROP3_PSDPSanaxx           = 22,      /* 00010110 */
  PCLXL_ROP3_SSPxDSxaxn           = 23,      /* 00010111 */
  PCLXL_ROP3_SPxPDxa              = 24,      /* 00011000 */
  PCLXL_ROP3_SDPSanaxn            = 25,      /* 00011001 */
  PCLXL_ROP3_PDSPaox              = 26,      /* 00011010 */
  PCLXL_ROP3_SDPSxaxn             = 27,      /* 00011011 */
  PCLXL_ROP3_PSDPaox              = 28,      /* 00011100 */
  PCLXL_ROP3_DSPDxaxn             = 29,      /* 00011101 */
  PCLXL_ROP3_PDSox                = 30,      /* 00011110 */
  PCLXL_ROP3_PDSoan               = 31,      /* 00011111 */

  PCLXL_ROP3_DPSnaa               = 32,      /* 00100000 */
  PCLXL_ROP3_SDPxon               = 33,      /* 00100001 */
  PCLXL_ROP3_DSna                 = 34,      /* 00100010 */
  PCLXL_ROP3_SPDnaon              = 35,      /* 00100011 */
  PCLXL_ROP3_SPxDSxa              = 36,      /* 00100100 */
  PCLXL_ROP3_PDSPanaxn            = 37,      /* 00100101 */
  PCLXL_ROP3_SDPSaox              = 38,      /* 00100110 */
  PCLXL_ROP3_SDPSxnox             = 39,      /* 00100111 */
  PCLXL_ROP3_DPSxa                = 40,      /* 00101000 */
  PCLXL_ROP3_PSDPSaoxxn           = 41,      /* 00101001 */
  PCLXL_ROP3_DPSana               = 42,      /* 00101010 */
  PCLXL_ROP3_SSPxPDxaxn           = 43,      /* 00101011 */
  PCLXL_ROP3_SPDSoax              = 44,      /* 00101100 */
  PCLXL_ROP3_PSDnox               = 45,      /* 00101101 */
  PCLXL_ROP3_PSDPxox              = 46,      /* 00101110 */
  PCLXL_ROP3_PSDnoan              = 47,      /* 00101111 */

  PCLXL_ROP3_PSna                 = 48,      /* 00110000 */
  PCLXL_ROP3_SDPnaon              = 49,      /* 00110001 */
  PCLXL_ROP3_SDPSoox              = 50,      /* 00110010 */
  PCLXL_ROP3_Sn                   = 51,      /* 00110011 */
  PCLXL_ROP3_SPDSaox              = 52,      /* 00110100 */
  PCLXL_ROP3_SPDSxnox             = 53,      /* 00110101 */
  PCLXL_ROP3_SDPox                = 54,      /* 00110110 */
  PCLXL_ROP3_SDPoan               = 55,      /* 00110111 */
  PCLXL_ROP3_PSDPoax              = 56,      /* 00111000 */
  PCLXL_ROP3_SPDnox               = 57,      /* 00111001 */
  PCLXL_ROP3_SPDSxox              = 58,      /* 00111010 */
  PCLXL_ROP3_SPDnoan              = 59,      /* 00111011 */
  PCLXL_ROP3_PSx                  = 60,      /* 00111100 */
  PCLXL_ROP3_SPDSonox             = 61,      /* 00111101 */
  PCLXL_ROP3_SPDSnaox             = 62,      /* 00111110 */
  PCLXL_ROP3_PSan                 = 63,      /* 00111111 */

  PCLXL_ROP3_PSDnaa               = 64,      /* 01000000 */
  PCLXL_ROP3_DPSxon               = 65,      /* 01000001 */
  PCLXL_ROP3_SDxPDxa              = 66,      /* 01000010 */
  PCLXL_ROP3_SPDSanaxn            = 67,      /* 01000011 */
  PCLXL_ROP3_SDna                 = 68,      /* 01000100 */
  PCLXL_ROP3_DPSnaon              = 69,      /* 01000101 */
  PCLXL_ROP3_DSPDaox              = 70,      /* 01000110 */
  PCLXL_ROP3_PSDPxaxn             = 71,      /* 01000111 */
  PCLXL_ROP3_SDPxa                = 72,      /* 01001000 */
  PCLXL_ROP3_PDSPDaoxxn           = 73,      /* 01001001 */
  PCLXL_ROP3_DPSDoax              = 74,      /* 01001010 */
  PCLXL_ROP3_PDSnox               = 75,      /* 01001011 */
  PCLXL_ROP3_SDPana               = 76,      /* 01001100 */
  PCLXL_ROP3_SSPxDSxoxn           = 77,      /* 01001101 */
  PCLXL_ROP3_PDSPxox              = 78,      /* 01001110 */
  PCLXL_ROP3_PDSnoan              = 79,      /* 01001111 */

  PCLXL_ROP3_PDna                 = 80,      /* 01010000 */
  PCLXL_ROP3_DSPnaon              = 81,      /* 01010001 */
  PCLXL_ROP3_DPSDaox              = 82,      /* 01010010 */
  PCLXL_ROP3_SPDSxaxn             = 83,      /* 01010011 */
  PCLXL_ROP3_DPSonon              = 84,      /* 01010000 */
  PCLXL_ROP3_Dn                   = 85,      /* 01010001 */
  PCLXL_ROP3_DPSox                = 86,      /* 01010010 */
  PCLXL_ROP3_DPSoan               = 87,      /* 01011011 */
  PCLXL_ROP3_PDSPoax              = 88,      /* 01011000 */
  PCLXL_ROP3_DPSnox               = 89,      /* 01011001 */
  PCLXL_ROP3_DPx                  = 90,      /* 01011010 */
  PCLXL_ROP3_DPSDonox             = 91,      /* 01011011 */
  PCLXL_ROP3_DPSDxox              = 92,      /* 01011100 */
  PCLXL_ROP3_DPSnoan              = 93,      /* 01011101 */
  PCLXL_ROP3_DPSDnaox             = 94,      /* 01011110 */
  PCLXL_ROP3_DPan                 = 95,      /* 01011111 */

  PCLXL_ROP3_PDSxa                = 96,      /* 01100000 */
  PCLXL_ROP3_DSPDSaoxxn           = 97,      /* 01100001 */
  PCLXL_ROP3_DSPDoax              = 98,      /* 01100010 */
  PCLXL_ROP3_SDPnox               = 99,      /* 01100011 */
  PCLXL_ROP3_SDPSoax              = 100,     /* 01100100 */
  PCLXL_ROP3_DSPnox               = 101,     /* 01100101 */
  PCLXL_ROP3_DSx                  = 102,     /* 01100110 */
  PCLXL_ROP3_SDPSonox             = 103,     /* 01100111 */
  PCLXL_ROP3_DSPDSonoxxn          = 104,     /* 01101000 */
  PCLXL_ROP3_PDSxxn               = 105,     /* 01101001 */
  PCLXL_ROP3_DPSax                = 106,     /* 01101010 */
  PCLXL_ROP3_PSDPSoaxxn           = 107,     /* 01101011 */
  PCLXL_ROP3_SDPax                = 108,     /* 01101100 */
  PCLXL_ROP3_PDSPDoaxxn           = 109,     /* 01101101 */
  PCLXL_ROP3_SDPSnoax             = 110,     /* 01101110 */
  PCLXL_ROP3_PDSxnan              = 111,     /* 01101111 */

  PCLXL_ROP3_PDSana               = 112,     /* 01110000 */
  PCLXL_ROP3_SSDxPDxaxn           = 113,     /* 01110001 */
  PCLXL_ROP3_SDPSxox              = 114,     /* 01110010 */
  PCLXL_ROP3_SDPnoan              = 115,     /* 01110011 */
  PCLXL_ROP3_DSPDxox              = 116,     /* 01110100 */
  PCLXL_ROP3_DSPnoan              = 117,     /* 01110101 */
  PCLXL_ROP3_SDPSnaox             = 118,     /* 01110110 */
  PCLXL_ROP3_DSan                 = 119,     /* 01110111 */
  PCLXL_ROP3_PDSax                = 120,     /* 01111000 */
  PCLXL_ROP3_DSPDSoaxxn           = 121,     /* 01111001 */
  PCLXL_ROP3_DPSDnoax             = 122,     /* 01111010 */
  PCLXL_ROP3_SDPxnan              = 123,     /* 01111111 */
  PCLXL_ROP3_SPDSnoax             = 124,     /* 01111100 */
  PCLXL_ROP3_DPSxnan              = 125,     /* 01111101 */
  PCLXL_ROP3_SPxDSxo              = 126,     /* 01111110 */
  PCLXL_ROP3_DPSaan               = 127,     /* 01111111 */

  PCLXL_ROP3_DPSaa                = 128,     /* 10000000 */
  PCLXL_ROP3_SPxDSxon             = 129,     /* 10000001 */
  PCLXL_ROP3_DPSxna               = 130,     /* 10000010 */
  PCLXL_ROP3_SPDSnoaxn            = 131,     /* 10000011 */
  PCLXL_ROP3_SDPxna               = 132,     /* 10000100 */
  PCLXL_ROP3_PDSPnoaxn            = 133,     /* 10000101 */
  PCLXL_ROP3_DSPDSoaxx            = 134,     /* 10000110 */
  PCLXL_ROP3_PDSaxn               = 135,     /* 10000111 */
  PCLXL_ROP3_DSa                  = 136,     /* 10001000 */
  PCLXL_ROP3_SDPSnaoxn            = 137,     /* 10001001 */
  PCLXL_ROP3_DSPnoa               = 138,     /* 10001010 */
  PCLXL_ROP3_DSPDxoxn             = 139,     /* 10001011 */
  PCLXL_ROP3_SDPnoa               = 140,     /* 10001100 */
  PCLXL_ROP3_SDPSxoxn             = 141,     /* 10001101 */
  PCLXL_ROP3_SSDxPDxax            = 142,     /* 10001110 */
  PCLXL_ROP3_PDSanan              = 143,     /* 10001111 */

  PCLXL_ROP3_PDSxna               = 144,     /* 10010000 */
  PCLXL_ROP3_SDPSnoaxn            = 145,     /* 10010001 */
  PCLXL_ROP3_DPSDPoaxx            = 146,     /* 10010010 */
  PCLXL_ROP3_SPDaxn               = 147,     /* 10010011 */
  PCLXL_ROP3_PSDPSoaxx            = 148,     /* 10010100 */
  PCLXL_ROP3_DPSaxn               = 149,     /* 10010101 */
  PCLXL_ROP3_DPSxx                = 150,     /* 10010110 */
  PCLXL_ROP3_PSDPSonoxx           = 151,     /* 10010111 */
  PCLXL_ROP3_SDPSonoxn            = 152,     /* 10011000 */
  PCLXL_ROP3_DSxn                 = 153,     /* 10011001 */
  PCLXL_ROP3_DPSnax               = 154,     /* 10011010 */
  PCLXL_ROP3_SDPSoaxn             = 155,     /* 10011011 */
  PCLXL_ROP3_SPDnax               = 156,     /* 10011100 */
  PCLXL_ROP3_DSPDoaxn             = 157,     /* 10011101 */
  PCLXL_ROP3_DSPDSaoxx            = 158,     /* 10011110 */
  PCLXL_ROP3_PDSxan               = 159,     /* 10011111 */

  PCLXL_ROP3_DPa                  = 160,     /* 10100000 */
  PCLXL_ROP3_PDSPnaoxn            = 161,     /* 10100001 */
  PCLXL_ROP3_DPSnoa               = 162,     /* 10100010 */
  PCLXL_ROP3_DPSDxoxn             = 163,     /* 10100011 */
  PCLXL_ROP3_PDSPonoxn            = 164,     /* 10100100 */
  PCLXL_ROP3_PDxn                 = 165,     /* 10100101 */
  PCLXL_ROP3_DSPnax               = 166,     /* 10100110 */
  PCLXL_ROP3_PDSPoaxn             = 167,     /* 10100111 */
  PCLXL_ROP3_DPSoa                = 168,     /* 10101000 */
  PCLXL_ROP3_DPSoxn               = 169,     /* 10101001 */
  PCLXL_ROP3_D                    = 170,     /* 10101010 */
  PCLXL_ROP3_DPSono               = 171,     /* 10101011 */
  PCLXL_ROP3_SPDSxax              = 172,     /* 10101100 */
  PCLXL_ROP3_DPSDaoxn             = 173,     /* 10101101 */
  PCLXL_ROP3_DSPnao               = 174,     /* 10101110 */
  PCLXL_ROP3_DPno                 = 175,     /* 10101111 */

  PCLXL_ROP3_PDSnoa               = 176,     /* 10110000 */
  PCLXL_ROP3_PDSPxoxn             = 177,     /* 10110001 */
  PCLXL_ROP3_SSPxDSxox            = 178,     /* 10110010 */
  PCLXL_ROP3_SDPanan              = 179,     /* 10110011 */
  PCLXL_ROP3_PSDnax               = 180,     /* 10110100 */
  PCLXL_ROP3_DPSDoaxn             = 181,     /* 10110101 */
  PCLXL_ROP3_DPSDPaoxx            = 182,     /* 10110110 */
  PCLXL_ROP3_SDPxan               = 183,     /* 10110111 */
  PCLXL_ROP3_PSDPxax              = 184,     /* 10111000 */
  PCLXL_ROP3_DSPDaoxn             = 185,     /* 10111001 */
  PCLXL_ROP3_DPSnao               = 186,     /* 10111010 */
  PCLXL_ROP3_DSno                 = 187,     /* 10111011 */
  PCLXL_ROP3_SPDSanax             = 188,     /* 10111100 */
  PCLXL_ROP3_SDxPDxan             = 189,     /* 10111101 */
  PCLXL_ROP3_DPSxo                = 190,     /* 10111110 */
  PCLXL_ROP3_DPSano               = 191,     /* 10111111 */

  PCLXL_ROP3_PSa                  = 192,     /* 11000000 */
  PCLXL_ROP3_SPDSnaoxn            = 193,     /* 11000001 */
  PCLXL_ROP3_SPDSonoxn            = 194,     /* 11000010 */
  PCLXL_ROP3_PSxn                 = 195,     /* 11000011 */
  PCLXL_ROP3_SPDnoa               = 196,     /* 11000100 */
  PCLXL_ROP3_SPDSxoxn             = 197,     /* 11000101 */
  PCLXL_ROP3_SDPnax               = 198,     /* 11000110 */
  PCLXL_ROP3_PSDPoaxn             = 199,     /* 11000111 */
  PCLXL_ROP3_SDPoa                = 200,     /* 11000000 */
  PCLXL_ROP3_SPDoxn               = 201,     /* 11000001 */
  PCLXL_ROP3_DPSDxax              = 202,     /* 11000010 */
  PCLXL_ROP3_SPDSaoxn             = 203,     /* 11000011 */
  PCLXL_ROP3_S                    = 204,     /* 11001100 */
  PCLXL_ROP3_SDPono               = 205,     /* 11001101 */
  PCLXL_ROP3_SDPnao               = 206,     /* 11001110 */
  PCLXL_ROP3_SPno                 = 207,     /* 11001111 */

  PCLXL_ROP3_PSDnoa               = 208,     /* 10010001 */
  PCLXL_ROP3_PSDPxoxn             = 209,     /* 10010000 */
  PCLXL_ROP3_PDSnax               = 210,     /* 10010010 */
  PCLXL_ROP3_SPDSoaxn             = 211,     /* 10010011 */
  PCLXL_ROP3_SSPxPDxax            = 212,     /* 10010100 */
  PCLXL_ROP3_DPSanan              = 213,     /* 10010101 */
  PCLXL_ROP3_PSDPSaoxx            = 214,     /* 10010110 */
  PCLXL_ROP3_DPSxan               = 215,     /* 10010111 */
  PCLXL_ROP3_PDSPxax              = 216,     /* 10011000 */
  PCLXL_ROP3_SDPSaoxn             = 217,     /* 10011001 */
  PCLXL_ROP3_DPSDanax             = 218,     /* 10011010 */
  PCLXL_ROP3_SPxDSxan             = 219,     /* 10011011 */
  PCLXL_ROP3_SPDnao               = 220,     /* 10011100 */
  PCLXL_ROP3_SDno                 = 221,     /* 10011101 */
  PCLXL_ROP3_SDPxo                = 222,     /* 10011110 */
  PCLXL_ROP3_SDPano               = 223,     /* 10011111 */

  PCLXL_ROP3_PDSoa                = 224,     /* 11010000 */
  PCLXL_ROP3_PDSoxn               = 225,     /* 11010001 */
  PCLXL_ROP3_DSPDxax              = 226,     /* 11010010 */
  PCLXL_ROP3_PSDPaoxn             = 227,     /* 11010011 */
  PCLXL_ROP3_SDPSxax              = 228,     /* 11010100 */
  PCLXL_ROP3_PDSPaoxn             = 229,     /* 11010101 */
  PCLXL_ROP3_SDPSanax             = 230,     /* 11010110 */
  PCLXL_ROP3_SPxPDxan             = 231,     /* 11010111 */
  PCLXL_ROP3_SSPxDSxax            = 232,     /* 11011000 */
  PCLXL_ROP3_DSPDSanaxxn          = 233,     /* 11011001 */
  PCLXL_ROP3_DPSao                = 234,     /* 11011010 */
  PCLXL_ROP3_DPSxno               = 235,     /* 11011011 */
  PCLXL_ROP3_SDPao                = 236,     /* 11011100 */
  PCLXL_ROP3_SDPxno               = 237,     /* 11011101 */
  PCLXL_ROP3_DSo                  = 238,     /* 11011110 */
  PCLXL_ROP3_SDPnoo               = 239,     /* 11011111 */

  PCLXL_ROP3_P                    = 240,     /* 11110000 */
  PCLXL_ROP3_PDSono               = 241,     /* 11110001 */
  PCLXL_ROP3_PDSnao               = 242,     /* 11110010 */
  PCLXL_ROP3_PSno                 = 243,     /* 11110011 */
  PCLXL_ROP3_PSDnao               = 244,     /* 11110100 */
  PCLXL_ROP3_PDno                 = 245,     /* 11110101 */
  PCLXL_ROP3_PDSxo                = 246,     /* 11110110 */
  PCLXL_ROP3_PDSano               = 247,     /* 11110111 */
  PCLXL_ROP3_PDSao                = 248,     /* 11111000 */
  PCLXL_ROP3_PDSxno               = 249,     /* 11111001 */
  PCLXL_ROP3_DPo                  = 250,     /* 11111010 */
  PCLXL_ROP3_DPSnoo               = 251,     /* 11111011 */
  PCLXL_ROP3_PSo                  = 252,     /* 11111100 */
  PCLXL_ROP3_PSDnoo               = 253,     /* 11111101 */
  PCLXL_ROP3_DPSoo                = 254,     /* 11111110 */
  PCLXL_ROP3_1                    = 255      /* 11111111 */
};

typedef PCLXL_UByte PCLXL_ROP3;

/*
 * And there are 16 older ROP2 operators
 * for which there are ROP3 equivalents
 */

enum
{
  PCLXL_ROP2_0                    = 1,
  PCLXL_ROP2_DPon                 = 2,
  PCLXL_ROP2_DPna                 = 3,
  PCLXL_ROP2_Pn                   = 4,
  PCLXL_ROP2_PDna                 = 5,
  PCLXL_ROP2_Dn                   = 6,
  PCLXL_ROP2_DPx                  = 7,
  PCLXL_ROP2_DPan                 = 8,
  PCLXL_ROP2_DPa                  = 9,
  PCLXL_ROP2_DPxn                 = 10,
  PCLXL_ROP2_D                    = 11,
  PCLXL_ROP2_DPno                 = 12,
  PCLXL_ROP2_P                    = 13,
  PCLXL_ROP2_DPno_duplicate_of_12 = 14,
  PCLXL_ROP2_DPo                  = 15,
  PCLXL_ROP2_1                    = 16
};

typedef PCLXL_UByte PCLXL_ROP2;

typedef PCLXL_ENUMERATION PCLXL_SimplexPageMode;

enum
{
  PCLXL_eOpaque                   = 0,
  PCLXL_eTransparent              = 1
};

typedef PCLXL_ENUMERATION PCLXL_TxMode;

typedef PCLXL_ClipMode PCLXL_WindingRule;

enum
{
  PCLXL_eHorizontal               = 0,
  PCLXL_eVertical                 = 1
};

typedef PCLXL_ENUMERATION PCLXL_WritingMode;

#endif /* __PCLXLTYPES_H__ */

/******************************************************************************
* Log stripped */
