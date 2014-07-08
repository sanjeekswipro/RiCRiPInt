/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltags.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PCLXLTAGS_H__
#define __PCLXLTAGS_H__ (1)

/* Bit flags classifying XL byte tags */
#define XL_TT_WS        (0x01)  /**< Whitespace character */
#define XL_TT_BINDING   (0x02)  /**< Stream header binding tag. */
#define XL_TT_OPERATOR  (0x04)  /**< Operator tag. */
#define XL_TT_DATATYPE  (0x08)  /**< Datatype tag. */
#define XL_TT_ATTRIBUTE (0x10)  /**< Attribute id data type tag. */
#define XL_TT_EMBEDDED  (0x20)  /**< Embedded data length data type tag. */
#define XL_TT_NOTUSED   (0x40)  /**< Unused tag. */
#define XL_TT_RESERVED  (0x80)  /**< Reserved tag. */

/** \brief Byte tag classification table. */
extern
uint8 xl_tag_table[256];

/* XL tag classification macros */
#define is_xlwhitespace(b)  (xl_tag_table[(b)] & XL_TT_WS)
#define is_xlbinding(b)     (xl_tag_table[(b)] & XL_TT_BINDING)
#define is_xloperator(b)    (xl_tag_table[(b)] & XL_TT_OPERATOR)
#define is_xldatatype(b)    (xl_tag_table[(b)] & XL_TT_DATATYPE)
#define is_xlattribute(b)   (xl_tag_table[(b)] & XL_TT_ATTRIBUTE)
#define is_xlembedded(b)    (xl_tag_table[(b)] & XL_TT_EMBEDDED)
#define is_xlnotused(b)     (xl_tag_table[(b)] & XL_TT_NOTUSED)
#define is_xlreserved(b)    (xl_tag_table[(b)] & XL_TT_RESERVED)

/* Convert datatype tags to structure and element type.
 * For reasons unknown datatype structure uses a bit mask while element type is
 * an index.
 */
#define XL_TAG_STRUCT_MASK  (0x38)
#define XL_TAG_TO_STRUCT(t) ((t)&XL_TAG_STRUCT_MASK)
#define XL_TAG_STRUCT_SCALAR (0x00)
#define XL_TAG_STRUCT_ARRAY (0x08)
#define XL_TAG_STRUCT_XY    (0x10)
#define XL_TAG_STRUCT_BOX   (0x20)

#define XL_TAG_TYPE_MASK    (0x7)
#define XL_TAG_TO_TYPE(t)   ((t)&XL_TAG_TYPE_MASK)
#define XL_TAG_TYPE_UBYTE   (0)
#define XL_TAG_TYPE_UINT16  (1)
#define XL_TAG_TYPE_UINT32  (2)
#define XL_TAG_TYPE_SINT16  (3)
#define XL_TAG_TYPE_SINT32  (4)
#define XL_TAG_TYPE_REAL32  (5)

/* Some interesting tag values */
#define XL_TAG_SCALAR_UBYTE   (0xC0 | XL_TAG_STRUCT_SCALAR | XL_TAG_TYPE_UBYTE)
#define XL_TAG_SCALAR_UINT16  (0xC0 | XL_TAG_STRUCT_SCALAR | XL_TAG_TYPE_UINT16)
#define XL_TAG_SCALAR_UINT32  (0xC0 | XL_TAG_STRUCT_SCALAR | XL_TAG_TYPE_UINT32)

#define XL_TAG_ATTRIBUTE_BYTE (0xf8)
#define XL_TAG_ATTRIBUTE_WORD (0xf9)

#define XL_TAG_EMBEDDED_UINT32 (0xfa)
#define XL_TAG_EMBEDDED_BYTE  (0xfb)

/* Embedded data datatypes */
#define XL_DATATYPE_UBYTE   (0)
#define XL_DATATYPE_SBYTE   (1)
#define XL_DATATYPE_UINT16  (2)
#define XL_DATATYPE_SINT16  (3)

#endif /* !__PCLXLTAGS_H__ */

/* Log stripped */

/* EOF */
