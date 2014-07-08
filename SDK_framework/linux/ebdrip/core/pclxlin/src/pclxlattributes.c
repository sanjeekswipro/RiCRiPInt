/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlattributes.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * "C" typedefs and enumerations that represent PCLXL-specific
 * datastream constructs operators, attribute IDs, data-type tags
 * and multi-byte (numeric) values
 */

#include "core.h"
#include "hqmemset.h"

#include "pclxlerrors.h"
#include "pclxlattributes.h"
#include "pclxlcontext.h"
#include "pclxldebug.h"


/* Maximum number of attributes in the set.  No operator takes as many as 8
 * attributes, and since repeated attributes replace prior versions the only way
 * to get more than 8 attributes is to have an illegal combination.
 */
#define MAX_ATTRIBUTES    (8)

/* Maximum attribute id that will be seen.  Testing to date shows that HP
 * LaserJet devices report illegal tag errors for uint16 attribute tag 0xf9.
 */
#define PCLXL_MAX_ATTR_ID (255)

/* XL attributes set.
 *
 * While the XL documentation describes attribute handling as stack like, with a
 * last in first out semantics, it can be modelled as a set with the last
 * occurrence of an attribute replacing any existing occurrence.  The presence
 * of extra attributes can be done by checking the size of the set after
 * removing any that are expected.
 *
 * The attribute set is based on the implementation of an Ullman set which has
 * O(1) time for addition, deletion, retrieval and membership tests.  It does
 * need O(n) storage but is not excessive.
 *
 * The attribute id is used to index into the position array, which contains the
 * index into members containing the pointer to the attribute data, which is in
 * the attributes data.  New members are added to the end.  Deletion is done by
 * either just shrinking the size if the one being deleted is last or swapping
 * the member index with the last one if it is not.
 */
struct PCLXL_ATTRIBUTE_SET {
  uint32          size;                     /* Number of unique attributes seen so far */
  uint8           position[256];            /* Indexed by attribute id to check if in attributes */
  PCLXL_ATTRIBUTE_STRUCT* members[MAX_ATTRIBUTES]; /* Pointer to attribute in set */
  uint32          free;                     /* Bitmask of attributes that can be used */
  uint32          matches;                  /* Number of set members that were matched */
  mm_pool_t       pool;                     /* Memory pool used for attribute data */
  PCLXL_ATTRIBUTE_STRUCT attributes[MAX_ATTRIBUTES]; /* Attributes seen so far */
};

/* Check that the attribute a comes from the attribute set s */
#define ATTR_FROM_ATTRSET(a, s) ((a) == &(s)->attributes[(a)->index])


typedef struct ATTR_META_DATA {
  PCLXL_PROTOCOL_VERSION  version;      /* First version of XL supporting attribute. */
  uint8                   datatypes[4]; /* Datatypes valid for attribute - list ends at 0 or size. */
#if DEBUG_BUILD
  uint8*                  name;         /* Name as a C string. */
#endif /* DEBUG_BUILD */
} ATTR_META_DATA;

/* Macro to specify meta data for debug and non-debug builds */
#if DEBUG_BUILD
#define ATTR_DATA(p, d, n)  { (p), {d}, (uint8*)(n) }
#else /* !DEBUG_BUILD */
#define ATTR_DATA(p, d, n)  { (p), {d} }
#endif /* !DEBUG_BUILD */

/* C89 does not support variadic macros, so one per number of args */
#define ATTR_DATATYPES0()           (0)
#define ATTR_DATATYPES1(a)          (a)
#define ATTR_DATATYPES2(a, b)       (a), (b)
#define ATTR_DATATYPES3(a, b, c)    (a), (b), (c)
#define ATTR_DATATYPES4(a, b, c, d) (a), (b), (c), (d)

/* Unknown attributes never fail a protocol version check based on HP 4700
 * behaviour, so using an any protocol version for them keeps the attribute
 * protocol test simple - they will always pass */
#define ATTR_DATA_UNKNOWN ATTR_DATA(PCLXL_PROTOCOL_VERSION_ANY, ATTR_DATATYPES0(), "<Unknown>")

/* Yer actual attribute meta data. */
static
ATTR_META_DATA attribute[256] = {
  /* 0 */  ATTR_DATA_UNKNOWN,
  /* 1 */  ATTR_DATA_UNKNOWN,
  /* 2 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "PaletteDepth"),
  /* 3 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ColorSpace"),
  /* 4 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "NullBrush"),
  /* 5 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "NullPen"),
  /* 6 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte_Array), "PaletteData"),
  /* 7 */  ATTR_DATA_UNKNOWN,
  /* 8 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_Int16), "PatternSelectID"),
  /* 9 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_Real32), "GrayLevel"),
  /* 10 */  ATTR_DATA_UNKNOWN,
  /* 11 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_Real32_Array), "RGBColor"),
  /* 12 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_Int16_XY), "PatternOrigin"),
  /* 13 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16_XY), "NewDestinationSize"),
  /* 14 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_Real32_Array), "PrimaryArray"),
  /* 15 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "PrimaryDepth"),
  /* 16 */  ATTR_DATA_UNKNOWN,
  /* 17 */  ATTR_DATA_UNKNOWN,
  /* 18 */  ATTR_DATA_UNKNOWN,
  /* 19 */  ATTR_DATA_UNKNOWN,
  /* 20 */  ATTR_DATA_UNKNOWN,
  /* 21 */  ATTR_DATA_UNKNOWN,
  /* 22 */  ATTR_DATA_UNKNOWN,
  /* 23 */  ATTR_DATA_UNKNOWN,
  /* 24 */  ATTR_DATA_UNKNOWN,
  /* 25 */  ATTR_DATA_UNKNOWN,
  /* 26 */  ATTR_DATA_UNKNOWN,
  /* 27 */  ATTR_DATA_UNKNOWN,
  /* 28 */  ATTR_DATA_UNKNOWN,
  /* 29 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_3_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "AllObjectTypes"),
  /* 30 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_3_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "TextObjects"),
  /* 31 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_3_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "VectorObjects"),
  /* 32 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_3_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "RasterObjects"),
  /* 33 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UInt16), "DeviceMatrix"),
  /* 34 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "DitherMatrixDataType"),
  /* 35 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "DitherOrigin"),
  /* 36 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "MediaDestination"),
  /* 37 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UByte_Array), "MediaSize"),
  /* 38 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "MediaSource"),
  /* 39 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte_Array), "MediaType"),
  /* 40 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "Orientation"),
  /* 41 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UInt16, PCLXL_DT_Int16), "PageAngle"),
  /* 42 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "PageOrigin"),
  /* 43 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Real32_XY), "PageScale"),
  /* 44 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ROP3"),
  /* 45 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "TxMode"),
  /* 46 */  ATTR_DATA_UNKNOWN,
  /* 47 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UInt16_XY, PCLXL_DT_Real32_XY), "CustomMediaSize"),
  /* 48 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "CustomMediaSizeUnits"),
  /* 49 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "PageCopies"),
  /* 50 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16_XY), "DitherMatrixSize"),
  /* 51 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "DitherMatrixDepth"),
  /* 52 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "SimplexPageMode"),
  /* 53 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "DuplexPageMode"),
  /* 54 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "DuplexPageSide"),
  /* 55 */  ATTR_DATA_UNKNOWN,
  /* 56 */  ATTR_DATA_UNKNOWN,
  /* 57 */  ATTR_DATA_UNKNOWN,
  /* 58 */  ATTR_DATA_UNKNOWN,
  /* 59 */  ATTR_DATA_UNKNOWN,
  /* 60 */  ATTR_DATA_UNKNOWN,
  /* 61 */  ATTR_DATA_UNKNOWN,
  /* 62 */  ATTR_DATA_UNKNOWN,
  /* 63 */  ATTR_DATA_UNKNOWN,
  /* 64 */  ATTR_DATA_UNKNOWN,
  /* 65 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ArcDirection"),
  /* 66 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_Box, PCLXL_DT_UInt16_Box, PCLXL_DT_Int16_Box), "BoundingBox"),
  /* 67 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte, PCLXL_DT_UInt16, PCLXL_DT_Int16), "DashOffset"),
  /* 68 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY), "EllipseDimension"),
  /* 69 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "EndPoint"),
  /* 70 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "FillMode"),
  /* 71 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "LineCapStyle"),
  /* 72 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "LineJoinStyle"),
  /* 73 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UInt16), "MiterLength"),
  /* 74 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array, PCLXL_DT_Int16_Array), "LineDashStyle"),
  /* 75 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UInt16), "PenWidth"),
  /* 76 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_Int16_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_UByte_XY), "Point"),
  /* 77 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UInt16), "NumberOfPoints"),
  /* 78 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "SolidLine"),
  /* 79 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "StartPoint"),
  /* 80 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "PointType"),
  /* 81 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "ControlPoint1"),
  /* 82 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Int16_XY), "ControlPoint2"),
  /* 83 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ClipRegion"),
  /* 84 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ClipMode"),
  /* 85 */  ATTR_DATA_UNKNOWN,
  /* 86 */  ATTR_DATA_UNKNOWN,
  /* 87 */  ATTR_DATA_UNKNOWN,
  /* 88 */  ATTR_DATA_UNKNOWN,
  /* 89 */  ATTR_DATA_UNKNOWN,
  /* 90 */  ATTR_DATA_UNKNOWN,
  /* 91 */  ATTR_DATA_UNKNOWN,
  /* 92 */  ATTR_DATA_UNKNOWN,
  /* 93 */  ATTR_DATA_UNKNOWN,
  /* 94 */  ATTR_DATA_UNKNOWN,
  /* 95 */  ATTR_DATA_UNKNOWN,
  /* 96 */  ATTR_DATA_UNKNOWN,
  /* 97 */  ATTR_DATA_UNKNOWN,
  /* 98 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ColorDepth"),
  /* 99 */  ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "BlockHeight"),
  /* 100 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ColorMapping"),
  /* 101 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "CompressMode"),
  /* 102 */  ATTR_DATA_UNKNOWN,
  /* 103 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16_XY), "DestinationSize"),
  /* 104 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "PatternPersistence"),
  /* 105 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_Int16), "PatternDefineID"),
  /* 106 */ ATTR_DATA_UNKNOWN,
  /* 107 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "SourceHeight"),
  /* 108 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "SourceWidth"),
  /* 109 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "StartLine"),
  /* 110 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "PadBytesMultiple"),
  /* 111 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES1(PCLXL_DT_UInt32), "BlockByteLength"),
  /* 112 */ ATTR_DATA_UNKNOWN,
  /* 113 */ ATTR_DATA_UNKNOWN,
  /* 114 */ ATTR_DATA_UNKNOWN,
  /* 115 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "NumberOfScanLines"),
  /* 116 */ ATTR_DATA_UNKNOWN,
  /* 117 */ ATTR_DATA_UNKNOWN,
  /* 118 */ ATTR_DATA_UNKNOWN,
  /* 119 */ ATTR_DATA_UNKNOWN,
  /* 120 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ColorTreatment"),
  /* 121 */ ATTR_DATA_UNKNOWN,
  /* 122 */ ATTR_DATA_UNKNOWN,
  /* 123 */ ATTR_DATA_UNKNOWN,
  /* 124 */ ATTR_DATA_UNKNOWN,
  /* 125 */ ATTR_DATA_UNKNOWN,
  /* 126 */ ATTR_DATA_UNKNOWN,
  /* 127 */ ATTR_DATA_UNKNOWN,
  /* 128 */ ATTR_DATA_UNKNOWN,
  /* 129 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array), "CommentData"),
  /* 130 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "DataOrg"),
  /* 131 */ ATTR_DATA_UNKNOWN,
  /* 132 */ ATTR_DATA_UNKNOWN,
  /* 133 */ ATTR_DATA_UNKNOWN,
  /* 134 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "Measure"),
  /* 135 */ ATTR_DATA_UNKNOWN,
  /* 136 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "SourceType"),
  /* 137 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UInt16_XY, PCLXL_DT_Real32_XY), "UnitsPerMeasure"),
  /* 138 */ ATTR_DATA_UNKNOWN,
  /* 139 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array), "StreamName"),
  /* 140 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt32), "StreamDataLength"),
  /* 141 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_3_0, ATTR_DATATYPES1(PCLXL_DT_UByte_Array), "PCLSelectFont"),
  /* 142 */ ATTR_DATA_UNKNOWN,
  /* 143 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "ErrorReport"),
  /* 144 */ ATTR_DATA_UNKNOWN,
  /* 145 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt32), "VUExtension"),
  /* 146 */ ATTR_DATA_UNKNOWN,
  /* 147 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "VUAttr1"),
  /* 148 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "VUAttr2"),
  /* 149 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "VUAttr3"),
  /* 150 */ ATTR_DATA_UNKNOWN,
  /* 151 */ ATTR_DATA_UNKNOWN,
  /* 152 */ ATTR_DATA_UNKNOWN,
  /* 153 */ ATTR_DATA_UNKNOWN,
  /* 154 */ ATTR_DATA_UNKNOWN,
  /* 155 */ ATTR_DATA_UNKNOWN,
  /* 156 */ ATTR_DATA_UNKNOWN,
  /* 157 */ ATTR_DATA_UNKNOWN,
  /* 158 */ ATTR_DATA_UNKNOWN,
  /* 159 */ ATTR_DATA_UNKNOWN,
  /* 160 */ ATTR_DATA_UNKNOWN,
  /* 161 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_Int16, PCLXL_DT_UInt16, PCLXL_DT_Real32), "CharAngle"),
  /* 162 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte, PCLXL_DT_UInt16), "CharCode"),
  /* 163 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UInt16, PCLXL_DT_UInt32), "CharDataSize"),
  /* 164 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Real32_XY), "CharScale"),
  /* 165 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES4(PCLXL_DT_UByte_XY, PCLXL_DT_Int16_XY, PCLXL_DT_UInt16_XY, PCLXL_DT_Real32_XY), "CharShear"),
  /* 166 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte, PCLXL_DT_UInt16, PCLXL_DT_Real32), "CharSize"),
  /* 167 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "FontHeaderLength"),
  /* 168 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array), "FontName"),
  /* 169 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte), "FontFormat"),
  /* 170 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UInt16), "SymbolSet"),
  /* 171 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES2(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array), "TextData"),
  /* 172 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_UByte_Array), "CharSubModeArray"),
  /* 173 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_2_0, ATTR_DATATYPES1(PCLXL_DT_UByte), "WritingMode"),
  /* 174 */ ATTR_DATA_UNKNOWN,
  /* 175 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array, PCLXL_DT_Int16_Array), "XSpacingData"),
  /* 176 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES3(PCLXL_DT_UByte_Array, PCLXL_DT_UInt16_Array, PCLXL_DT_Int16_Array), "YSpacingData"),
  /* 177 */ ATTR_DATA(PCLXL_PROTOCOL_VERSION_1_1, ATTR_DATATYPES1(PCLXL_DT_Real32), "CharBoldValue"),
  /* 178 */ ATTR_DATA_UNKNOWN,
  /* 179 */ ATTR_DATA_UNKNOWN,
  /* 180 */ ATTR_DATA_UNKNOWN,
  /* 181 */ ATTR_DATA_UNKNOWN,
  /* 182 */ ATTR_DATA_UNKNOWN,
  /* 183 */ ATTR_DATA_UNKNOWN,
  /* 184 */ ATTR_DATA_UNKNOWN,
  /* 185 */ ATTR_DATA_UNKNOWN,
  /* 186 */ ATTR_DATA_UNKNOWN,
  /* 187 */ ATTR_DATA_UNKNOWN,
  /* 188 */ ATTR_DATA_UNKNOWN,
  /* 189 */ ATTR_DATA_UNKNOWN,
  /* 190 */ ATTR_DATA_UNKNOWN,
  /* 191 */ ATTR_DATA_UNKNOWN,
  /* 192 */ ATTR_DATA_UNKNOWN,
  /* 193 */ ATTR_DATA_UNKNOWN,
  /* 194 */ ATTR_DATA_UNKNOWN,
  /* 195 */ ATTR_DATA_UNKNOWN,
  /* 196 */ ATTR_DATA_UNKNOWN,
  /* 197 */ ATTR_DATA_UNKNOWN,
  /* 198 */ ATTR_DATA_UNKNOWN,
  /* 199 */ ATTR_DATA_UNKNOWN,
  /* 200 */ ATTR_DATA_UNKNOWN,
  /* 201 */ ATTR_DATA_UNKNOWN,
  /* 202 */ ATTR_DATA_UNKNOWN,
  /* 203 */ ATTR_DATA_UNKNOWN,
  /* 204 */ ATTR_DATA_UNKNOWN,
  /* 205 */ ATTR_DATA_UNKNOWN,
  /* 206 */ ATTR_DATA_UNKNOWN,
  /* 207 */ ATTR_DATA_UNKNOWN,
  /* 208 */ ATTR_DATA_UNKNOWN,
  /* 209 */ ATTR_DATA_UNKNOWN,
  /* 210 */ ATTR_DATA_UNKNOWN,
  /* 211 */ ATTR_DATA_UNKNOWN,
  /* 212 */ ATTR_DATA_UNKNOWN,
  /* 213 */ ATTR_DATA_UNKNOWN,
  /* 214 */ ATTR_DATA_UNKNOWN,
  /* 215 */ ATTR_DATA_UNKNOWN,
  /* 216 */ ATTR_DATA_UNKNOWN,
  /* 217 */ ATTR_DATA_UNKNOWN,
  /* 218 */ ATTR_DATA_UNKNOWN,
  /* 219 */ ATTR_DATA_UNKNOWN,
  /* 220 */ ATTR_DATA_UNKNOWN,
  /* 221 */ ATTR_DATA_UNKNOWN,
  /* 222 */ ATTR_DATA_UNKNOWN,
  /* 223 */ ATTR_DATA_UNKNOWN,
  /* 224 */ ATTR_DATA_UNKNOWN,
  /* 225 */ ATTR_DATA_UNKNOWN,
  /* 226 */ ATTR_DATA_UNKNOWN,
  /* 227 */ ATTR_DATA_UNKNOWN,
  /* 228 */ ATTR_DATA_UNKNOWN,
  /* 229 */ ATTR_DATA_UNKNOWN,
  /* 230 */ ATTR_DATA_UNKNOWN,
  /* 231 */ ATTR_DATA_UNKNOWN,
  /* 232 */ ATTR_DATA_UNKNOWN,
  /* 233 */ ATTR_DATA_UNKNOWN,
  /* 234 */ ATTR_DATA_UNKNOWN,
  /* 235 */ ATTR_DATA_UNKNOWN,
  /* 236 */ ATTR_DATA_UNKNOWN,
  /* 237 */ ATTR_DATA_UNKNOWN,
  /* 238 */ ATTR_DATA_UNKNOWN,
  /* 239 */ ATTR_DATA_UNKNOWN,
  /* 240 */ ATTR_DATA_UNKNOWN,
  /* 241 */ ATTR_DATA_UNKNOWN,
  /* 242 */ ATTR_DATA_UNKNOWN,
  /* 243 */ ATTR_DATA_UNKNOWN,
  /* 244 */ ATTR_DATA_UNKNOWN,
  /* 245 */ ATTR_DATA_UNKNOWN,
  /* 246 */ ATTR_DATA_UNKNOWN,
  /* 247 */ ATTR_DATA_UNKNOWN,
  /* 248 */ ATTR_DATA_UNKNOWN,
  /* 249 */ ATTR_DATA_UNKNOWN,
  /* 250 */ ATTR_DATA_UNKNOWN,
  /* 251 */ ATTR_DATA_UNKNOWN,
  /* 252 */ ATTR_DATA_UNKNOWN,
  /* 253 */ ATTR_DATA_UNKNOWN,
  /* 254 */ ATTR_DATA_UNKNOWN,
  /* 255 */ ATTR_DATA_UNKNOWN
};

PCLXL_PROTOCOL_VERSION pclxl_attr_protocol(
  PCLXL_ATTRIBUTE   p_attr)
{
  if ( p_attr->attribute_id < 256 ) {
    return(attribute[p_attr->attribute_id].version);
  }
  return(PCLXL_PROTOCOL_VERSION_ANY);

} /* pclxl_attr_protocol */

#ifdef DEBUG_BUILD

uint8* pclxl_get_attribute_name_by_id(
  PCLXL_ATTR_ID   id)
{
  if ( id < 256 ) {
    return(attribute[id].name);
  }
  return((uint8*)"<Unknown>");
}

uint8* pclxl_get_attribute_name(
  PCLXL_ATTRIBUTE   p_attr)
{
  return(pclxl_get_attribute_name_by_id(p_attr->attribute_id));
}
#endif /* DEBUG_BUILD */

static
void pclxl_empty_attribute(
  mm_pool_t       pool,
  PCLXL_ATTRIBUTE attribute)
{
  uint32 array_byte_size;

  HQASSERT((attribute != NULL),
           "NULL attribute pointer");

  /* Attributes with array values that are larger than the short buffer in the
   * attribute structure need to be freed.  Allocated memory includes extra byte
   * for a terminating NUL */
  array_byte_size = attribute->array_length*attribute->array_element_size + 1;
  if ( PCLXL_IS_ARRAY_TYPE(attribute->data_type) &&
       (array_byte_size > (PCLXL_ATTRIBUTE_BUFFER_SIZE + 1)) ) {
    HQASSERT((attribute->value.v_ubytes != attribute->value_array.v_ubytes),
             "Freeing off part of attribute struct");

    switch ( PCLXL_BASE_DATA_TYPE(attribute->data_type) ) {
    case PCLXL_BASE_DATA_TYPE_UBYTE:
      mm_free(pool, attribute->value.v_ubytes, array_byte_size);
      break;

    case PCLXL_BASE_DATA_TYPE_UINT16:
      mm_free(pool, attribute->value.v_uint16s, array_byte_size);
      break;

    case PCLXL_BASE_DATA_TYPE_UINT32:
      mm_free(pool, attribute->value.v_uint32s, array_byte_size);
      break;

    case PCLXL_BASE_DATA_TYPE_INT16:
      mm_free(pool, attribute->value.v_int16s, array_byte_size);
      break;

    case PCLXL_BASE_DATA_TYPE_INT32:
      mm_free(pool, attribute->value.v_int32s, array_byte_size);
      break;

    case PCLXL_BASE_DATA_TYPE_REAL32:
      mm_free(pool, attribute->value.v_real32s, array_byte_size);
      break;
    }
    attribute->array_length = 0;
    attribute->value.v_ubytes = NULL;
  }
}


/* Create and initialise an attribute set in the given memory pool. */
PCLXL_ATTRIBUTE_SET* pclxl_attr_set_create(
  mm_pool_t pool)
{
  PCLXL_ATTRIBUTE_SET* p_attr_set;
  int32 i;

  p_attr_set = mm_alloc(pool, sizeof(*p_attr_set), MM_ALLOC_CLASS_PCLXL_ATTRIBUTE_SET);
  if ( p_attr_set != NULL ) {
    p_attr_set->pool = pool;

    /* Setup up array index for each attribute */
    for ( i = 0; i < MAX_ATTRIBUTES; i++ ) {
      p_attr_set->attributes[i].index = i;
    }

    /* Mark the set empty and all attributes free for use */
    p_attr_set->size = 0;
    p_attr_set->free = BITS_BELOW(MAX_ATTRIBUTES);
  }

  return(p_attr_set);

} /* pclxl_attr_set_create */


/* Free off an attribute set. */
void pclxl_attr_set_destroy(
  PCLXL_ATTRIBUTE_SET*    p_attr_set)
{
  /* Empty any array attributes still present */
  pclxl_attr_set_empty(p_attr_set);

  mm_free(p_attr_set->pool, p_attr_set, sizeof(*p_attr_set));

} /* pclxl_attr_set_destroy */


/**
 * \brief Check if attribute with id exists in the attribute set.
 *
 * \param[in] p_attr_set Pointer to attribute set.
 * \param[in] id Attribute id.
 *
 * \return True if the id exists in the attribute set.
 */
static
Bool attr_set_contains_by_id(
  PCLXL_ATTRIBUTE_SET*    p_attr_set,
  PCLXL_ATTR_ID           id)
{
  HQASSERT((p_attr_set != NULL),
           "attr_set_contains_by_id: NULL attribute set pointer");
  HQASSERT((id <= PCLXL_MAX_ATTR_ID),
           "attr_set_contains_by_id: attribute id out of range");

  return((p_attr_set->position[id] < p_attr_set->size) &&
         (p_attr_set->members[p_attr_set->position[id]]->attribute_id == id));

} /* attr_set_contains_by_id */


/**
 * \brief Get attribute pointer for id from attribute set.
 *
 * \param[in] p_attr_set Pointer to attribute set.
 * \param[in] id Attribute id.
 *
 * \return Attribute pointer if id exists in the attribute set, else \c NULL.
 */
static
PCLXL_ATTRIBUTE_STRUCT* attr_set_get_by_id(
  PCLXL_ATTRIBUTE_SET*    p_attr_set,
  PCLXL_ATTR_ID           id)
{
  if ( attr_set_contains_by_id(p_attr_set, id) ) {
    return(p_attr_set->members[p_attr_set->position[id]]);
  }
  return(NULL);

} /* attr_set_get_by_id */


/* Empty the attribute set of any current attributes. */
void pclxl_attr_set_empty(
  PCLXL_ATTRIBUTE_SET*    p_attr_set)
{
  uint32 i;

  HQASSERT((p_attr_set != NULL),
           "pclxl_attr_set_empty: NULL attribute set pointer");

  /* Free off any attribute array data for the attributes used since last clear */
  for ( i = 0; i < p_attr_set->size; i++ ) {
    pclxl_empty_attribute(p_attr_set->pool, p_attr_set->members[i]);
  }

  /* Put the set back to empty and all attributes free to use */
  p_attr_set->size = 0;
  p_attr_set->free = BITS_BELOW(MAX_ATTRIBUTES);

} /* pclxl_attr_set_empty */


/* Get an attribute that is not yet in the attribute set. */
PCLXL_ATTRIBUTE_STRUCT* pclxl_attr_set_get_new(
  PCLXL_ATTRIBUTE_SET*    p_attr_set)
{
  PCLXL_ATTRIBUTE_STRUCT* p_attr = NULL;
  uint32  next;

  HQASSERT((p_attr_set != NULL),
           "pclxl_attr_set_get_new: NULL attribute set pointer");

  /* free is a bitmask of entries in the attributes array that are not currently
   * in use.  Find the lowest set bit in the bitmask and use its index as the
   * index of the attribute to use.
   */
  next = BIT_FIRST_SET(p_attr_set->free);
  if ( next ) {
    p_attr_set->free &= ~next;
    next = BIT_FIRST_SET_INDEX(next);
    HQASSERT((next < MAX_ATTRIBUTES),
             "pclxl_attr_set_get_new: next free attribute index out of range");
    p_attr = &p_attr_set->attributes[next];
  }

  return(p_attr);

} /* pclxl_attr_set_get_new */


/* Add an attribute to the attribute set. */
void pclxl_attr_set_add(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTRIBUTE_STRUCT* p_attr)
{
  PCLXL_ATTRIBUTE p_existing;

  HQASSERT((p_attr_set != NULL),
           "pclxl_attr_set_add: NULL attribute set pointer");
  HQASSERT((p_attr != NULL),
           "pclxl_attr_set_add: NULL attribute pointer");
  HQASSERT((p_attr->attribute_id <= PCLXL_MAX_ATTR_ID),
           "pclxl_attr_set_add: attribute id out of range");
  HQASSERT((ATTR_FROM_ATTRSET(p_attr, p_attr_set)),
           "pclxl_attr_set_add: attribute did not come from attribute set");

  p_existing = attr_set_get_by_id(p_attr_set, p_attr->attribute_id);
  if ( p_existing == NULL ) {
    /* New member - add to end of the current set */
    p_attr_set->position[p_attr->attribute_id] = CAST_UNSIGNED_TO_UINT8(p_attr_set->size);
    p_attr_set->members[p_attr_set->size] = p_attr;
    p_attr_set->size++;

  } else { /* Replace existing member */
    p_attr_set->members[p_attr_set->position[p_existing->attribute_id]] = p_attr;

    /* Free off previous attribute and add back to free list */
    pclxl_empty_attribute(p_attr_set->pool, p_existing);
    p_attr_set->free |= BIT(p_existing->index);
  }

} /* pclxl_attr_set_add */


/* Helper macro to get the attribute id to match in the set. */
#define MATCH_ID(m)       (((m)->id) & PCLXL_END_MATCH)
/* Helper macro to check if the attribute is required to be in the set. */
#define MATCH_REQUIRED(m) (((m)->id) & PCLXL_ATTR_REQUIRED)

/**
 * \brief Match an attribute in the attribute set.
 *
 * \param[in] p_attr_set Pointer to the attribute set.
 * \param[in] p_match Pointer to attribute match information.
 *
 * \retval \c PCLXL_MISSING_ATTRIBUTE if the attribute is required and not found
 * in the set.
 * \retval \c PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE if the attribute is in the set
 * but does not have a valid datatype.
 * \retval 0 if the attribute is present and has a valid datatype or it is not
 * present and is not required to be.
 */
static
int32 do_attr_match(
  PCLXL_ATTRIBUTE_SET*    p_attr_set,
  PCLXL_ATTR_MATCH*       p_match)
{
  uint8* valid_datatypes;
  int32 i;

  HQASSERT((p_attr_set != NULL),
           "do_attr_match: NULL attribute set pointer");
  HQASSERT((p_match != NULL),
           "do_attr_match: NULL attribute match pointer");

  /* Check if absent attribute is required */
  p_match->result = attr_set_get_by_id(p_attr_set, MATCH_ID(p_match));
  if ( p_match->result == NULL ) {
    if ( MATCH_REQUIRED(p_match) ) {
      return(PCLXL_MISSING_ATTRIBUTE);
    }
    return(0);
  }
  /* Check attribute data type can be handled */
  valid_datatypes = attribute[MATCH_ID(p_match)].datatypes;
  for ( i = 0; valid_datatypes[i] != 0 && i < 4; i++ ) {
    if ( p_match->result->data_type == valid_datatypes[i] ) {
      p_attr_set->matches++;
      return(0);
    }
  }
  return(PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE);

} /* do_attr_match */


/**
 * \brief Match attributes to the current set.
 *
 * If there are no problems with matching attributes in the set then the number
 * of matched attributes in the set is valid.
 *
 * \param[in] p_attr_set Pointer to the attribute set.
 * \param[in] p_match Pointer to array of attributes to match.
 * \param[in] pclxl_context Pointer to XL interpreter context.
 * \param[in] subsystem XL subsystem to use when reporting an error.
 *
 * \return \c TRUE if attributes are matched without an error, else \c FALSE.
 */
static
Bool attr_set_match(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTR_MATCH*     p_match,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem)
{
  int32 error;

  HQASSERT((p_attr_set != NULL),
           "attr_set_match: NULL attribute set pointer");
  HQASSERT((p_match != NULL),
           "attr_set_match: NULL attribute match pointer");

  /* Start count of matched attributes */
  p_attr_set->matches = 0;

  /* Check for required attributes and datatypes */
  while ( MATCH_ID(p_match) != PCLXL_END_MATCH ) {
    if ( (error = do_attr_match(p_attr_set, p_match)) != 0 ) {
      PCLXL_ERROR_HANDLER(pclxl_context, subsystem, error,
                          ("Attribute %s", pclxl_get_attribute_name_by_id(MATCH_ID(p_match))));
      return(FALSE);
    }
    p_match++;
  }

  return(TRUE);

} /* attr_set_match */


/* Match zero or more attributes in the attribute set. */
Bool pclxl_attr_set_match(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTR_MATCH*     p_match,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem)
{
  if ( !attr_set_match(p_attr_set, p_match, pclxl_context, subsystem) ) {
    return(FALSE);
  }

  /* Check for unmatched attributes */
  if ( p_attr_set->matches < p_attr_set->size ) {
    PCLXL_ERROR_HANDLER(pclxl_context, subsystem, PCLXL_ILLEGAL_ATTRIBUTE,
                        ("Excess attributes: %d", p_attr_set->size));
    return(FALSE);
  }
  return(TRUE);

} /* pclxl_attr_set_match */


/* Match one or more attributes in the attribute set. */
Bool pclxl_attr_set_match_at_least_1(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTR_MATCH*     p_match,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem)
{
  if ( !attr_set_match(p_attr_set, p_match, pclxl_context, subsystem) ) {
    return(FALSE);
  }

  /* Check that we have seen at least one match - must be done before checking
   * for unwanted attributes. */
  if ( p_attr_set->matches == 0 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, subsystem, PCLXL_MISSING_ATTRIBUTE,
                        ("Did not match any attributes when should have"));
    return(FALSE);
  }

  /* Check for unmatched attributes */
  if ( p_attr_set->matches < p_attr_set->size ) {
    PCLXL_ERROR_HANDLER(pclxl_context, subsystem, PCLXL_ILLEGAL_ATTRIBUTE,
                        ("Excess attributes: %d", p_attr_set->size));
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_attr_set_match_at_least_1 */


/* Match an empty attribute set. */
Bool pclxl_attr_set_match_empty(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem)
{
  HQASSERT((p_attr_set != NULL),
           "pclxl_attr_set_match_empty: NULL attribute set pointer");

  if ( p_attr_set->size != 0 ) {
    PCLXL_ERROR_HANDLER(pclxl_context, subsystem, PCLXL_ILLEGAL_ATTRIBUTE,
                        ("Excess attributes: %d", p_attr_set->size));
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_attr_set_match_empty */


/* Get valid enumeration value for an enumerated attribute. */
Bool pclxl_attr_valid_enumeration(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_ENUMERATION*      p_values,
  PCLXL_ENUMERATION*      p_enum)
{
  HQASSERT((p_attr != NULL),
           "pclxl_attr_valid_enumeration: NULL attribute pointer");
  HQASSERT((p_attr->data_type == PCLXL_DT_UByte),
           "pclxl_attr_valid_enumeration: attribute value is not an enumeration");
  HQASSERT((p_values != NULL),
           "pclxl_attr_valid_enumeration: NULL enumeration values pointer");
  HQASSERT((p_enum != NULL),
           "pclxl_attr_valid_enumeration: NULL pointer to returned enumeration value");

  *p_enum = p_attr->value.v_ubyte;
  /* Match attribute byte value against enumeration values */
  while ( *p_values != PCLXL_ENUMERATION_END ) {
    if ( *p_enum == *p_values++ ) {
      return(TRUE);
    }
  }
  return(FALSE);

} /* pclxl_attr_valid_enumeration */


/* Get valid enumeration value for an enumerated attribute. */
Bool pclxl_attr_match_enumeration(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_ENUMERATION*      p_values,
  PCLXL_ENUMERATION*      p_enum,
  PCLXL_CONTEXT           pclxl_context,
  PCLXL_SUBSYSTEM         subsystem)
{
  if ( !pclxl_attr_valid_enumeration(p_attr, p_values, p_enum) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, subsystem, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Illegal (Enum) Attribute Value, Attribute \"%s\", Illegal Value %d (Enumeration)",
                         pclxl_get_attribute_name(p_attr), *p_enum));
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_attr_match_enumeration */


/* Return attribute value as an unsigned 32bit integer. */
uint32 pclxl_attr_get_uint(
  PCLXL_ATTRIBUTE_STRUCT* p_attr)
{
  HQASSERT((p_attr != NULL),
           "pclxl_attr_get_uint: NULL attribute pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte:
    return(p_attr->value.v_ubyte);

  case PCLXL_DT_UInt16:
    return(p_attr->value.v_uint16);

  case PCLXL_DT_UInt32:
    return(p_attr->value.v_uint32);

  default:
    HQFAIL("pclxl_attr_get_uint: unsupported attribute type");
    break;
  }
  return(0);

} /* pclxl_attr_get_uint */


/* Return attribute value as a signed 32bit integer. */
int32 pclxl_attr_get_int(
  PCLXL_ATTRIBUTE_STRUCT* p_attr)
{
  HQASSERT((p_attr != NULL),
           "pclxl_attr_get_int: NULL attribute pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte:
    return(p_attr->value.v_ubyte);

  case PCLXL_DT_UInt16:
    return(p_attr->value.v_uint16);

  case PCLXL_DT_Int16:
    return(p_attr->value.v_int16);

  case PCLXL_DT_Int32:
    return(p_attr->value.v_int32);

  default:
    HQFAIL("pclxl_attr_get_int: unsupported attribute type");
    break;
  }
  return(0);

} /* pclxl_attr_get_int */


/* Return attribute value as a floating point value. */
PCLXL_SysVal pclxl_attr_get_real(
  PCLXL_ATTRIBUTE_STRUCT* p_attr)
{
  HQASSERT((p_attr != NULL),
           "pclxl_attr_get_real: NULL attribute pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte:
    return(p_attr->value.v_ubyte);

  case PCLXL_DT_UInt16:
    return(p_attr->value.v_uint16);

  case PCLXL_DT_Int16:
    return(p_attr->value.v_int16);

  case PCLXL_DT_UInt32:
    return(p_attr->value.v_uint32);

  case PCLXL_DT_Int32:
    return(p_attr->value.v_int32);

  case PCLXL_DT_Real32:
    return(p_attr->value.v_real32);

  default:
    HQFAIL("pclxl_attr_get_real: unsupported attribute type");
    break;
  }
  return(0.0);

} /* pclxl_attr_get_real */


/* Return attribute as an unsigned 32bit integer XY. */
void pclxl_attr_get_uint_xy(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_UInt32_XY*        p_uint_xy)
{
  HQASSERT((p_attr!= NULL),
           "pclxl_attr_get_uint_xy: NULL attribute set pointer");
  HQASSERT((p_uint_xy!= NULL),
           "pclxl_attr_get_uint_xy: NULL returned XY pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte_XY:
    p_uint_xy->x = p_attr->value_array.v_ubytes[0];
    p_uint_xy->y = p_attr->value_array.v_ubytes[1];
    break;

  case PCLXL_DT_UInt16_XY:
    p_uint_xy->x = p_attr->value_array.v_uint16[0];
    p_uint_xy->y = p_attr->value_array.v_uint16[1];
    break;

  case PCLXL_DT_UInt32_XY:
    p_uint_xy->x = p_attr->value_array.v_uint32[0];
    p_uint_xy->y = p_attr->value_array.v_uint32[1];
    break;

  default:
    HQFAIL("pclxl_attr_get_uint_xy: unsupported attribute type");
    break;
  }

} /* pclxl_attr_get_uint_xy */


/* Return attribute as a floating point XY. */
void pclxl_attr_get_real_xy(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_SysVal_XY*        p_real_xy)
{
  HQASSERT((p_attr!= NULL),
           "pclxl_attr_get_real_xy: NULL attribute set pointer");
  HQASSERT((p_real_xy!= NULL),
           "pclxl_attr_get_real_xy: NULL returned XY pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte_XY:
    p_real_xy->x = p_attr->value_array.v_ubytes[0];
    p_real_xy->y = p_attr->value_array.v_ubytes[1];
    break;

  case PCLXL_DT_UInt16_XY:
    p_real_xy->x = p_attr->value_array.v_uint16[0];
    p_real_xy->y = p_attr->value_array.v_uint16[1];
    break;

  case PCLXL_DT_Int16_XY:
    p_real_xy->x = p_attr->value_array.v_int16[0];
    p_real_xy->y = p_attr->value_array.v_int16[1];
    break;

  case PCLXL_DT_UInt32_XY:
    p_real_xy->x = p_attr->value_array.v_uint32[0];
    p_real_xy->y = p_attr->value_array.v_uint32[1];
    break;

  case PCLXL_DT_Int32_XY:
    p_real_xy->x = p_attr->value_array.v_int32[0];
    p_real_xy->y = p_attr->value_array.v_int32[1];
    break;

  case PCLXL_DT_Real32_XY:
    p_real_xy->x = p_attr->value_array.v_real32[0];
    p_real_xy->y = p_attr->value_array.v_real32[1];
    break;

  default:
    HQFAIL("pclxl_attr_get_real_xy: unsupported attribute type");
    break;
  }

} /* pclxl_attr_get_real_xy */


/* Return attribute as a floating point Box. */
void pclxl_attr_get_real_box(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_SysVal_Box*       p_real_box)
{
  HQASSERT((p_attr!= NULL),
           "pclxl_attr_get_real_box: NULL attribute set pointer");
  HQASSERT((p_real_box!= NULL),
           "pclxl_attr_get_real_box: NULL returned box pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte_Box:
    p_real_box->x1 = p_attr->value_array.v_ubytes[0];
    p_real_box->y1 = p_attr->value_array.v_ubytes[1];
    p_real_box->x2 = p_attr->value_array.v_ubytes[2];
    p_real_box->y2 = p_attr->value_array.v_ubytes[3];
    break;

  case PCLXL_DT_UInt16_Box:
    p_real_box->x1 = p_attr->value_array.v_uint16[0];
    p_real_box->y1 = p_attr->value_array.v_uint16[1];
    p_real_box->x2 = p_attr->value_array.v_uint16[2];
    p_real_box->y2 = p_attr->value_array.v_uint16[3];
    break;

  case PCLXL_DT_Int16_Box:
    p_real_box->x1 = p_attr->value_array.v_int16[0];
    p_real_box->y1 = p_attr->value_array.v_int16[1];
    p_real_box->x2 = p_attr->value_array.v_int16[2];
    p_real_box->y2 = p_attr->value_array.v_int16[3];
    break;

  case PCLXL_DT_UInt32_Box:
    p_real_box->x1 = p_attr->value_array.v_uint32[0];
    p_real_box->y1 = p_attr->value_array.v_uint32[1];
    p_real_box->x2 = p_attr->value_array.v_uint32[2];
    p_real_box->y2 = p_attr->value_array.v_uint32[3];
    break;

  case PCLXL_DT_Int32_Box:
    p_real_box->x1 = p_attr->value_array.v_int32[0];
    p_real_box->y1 = p_attr->value_array.v_int32[1];
    p_real_box->x2 = p_attr->value_array.v_int32[2];
    p_real_box->y2 = p_attr->value_array.v_int32[3];
    break;

  case PCLXL_DT_Real32_Box:
    p_real_box->x1 = p_attr->value_array.v_real32[0];
    p_real_box->y1 = p_attr->value_array.v_real32[1];
    p_real_box->x2 = p_attr->value_array.v_real32[2];
    p_real_box->y2 = p_attr->value_array.v_real32[3];
    break;

  default:
    HQFAIL("pclxl_attr_get_real_box: unsupported attribute type");
    break;
  }

} /* pclxl_attr_get_real_box */


void pclxl_attr_get_byte_len(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  uint8**                 pp_bytes,
  uint32*                 p_len)
{
  HQASSERT((p_attr!= NULL),
           "pclxl_attr_get_byte_len: NULL attribute set pointer");
  HQASSERT((pp_bytes != NULL),
           "pclxl_attr_get_byte_len: NULL returned bytes pointer");
  HQASSERT((p_len != NULL),
           "pclxl_attr_get_byte_len: NULL returned len pointer");

  switch ( p_attr->data_type ) {
  case PCLXL_DT_UByte_Array:
    *pp_bytes = p_attr->value.v_ubytes;
    *p_len = p_attr->array_length;
    break;

  case PCLXL_DT_UInt16_Array:
    *pp_bytes = (uint8*)p_attr->value.v_uint16s;
    *p_len = 2*p_attr->array_length;
    break;

  default:
    HQFAIL("pclxl_attr_get_byte_len: unsupported attribute type");
    break;
  }

} /* pclxl_attr_get_byte_len */

/******************************************************************************
* Log stripped */
