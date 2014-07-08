/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!export:tiftypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of core Tiff types
 */


#ifndef __TIFTYPES_H__
#define __TIFTYPES_H__ (1)


/*
 * The various TIFF types in internal format.
 */
typedef uint8         tiff_byte_t;
typedef uint8         tiff_ascii_t;       /* ASCII defined to be 7-bit so top bit should be clear! */
typedef uint8         tiff_undefined_t;   /* Contents depends on definition but avoid sign extenstion for now */
typedef  int8         tiff_sbyte_t;
typedef uint16        tiff_short_t;
typedef  int16        tiff_sshort_t;
typedef uint32        tiff_long_t;
typedef  int32        tiff_slong_t;
typedef tiff_long_t   tiff_rational_t[2];
typedef tiff_slong_t  tiff_srational_t[2];
typedef float         tiff_float_t;       /* Mandated to be IEEE !*/
typedef double        tiff_double_t;      /* Mandated to be IEEE !*/


/*
 * Indexes into a RATIONAL for the numerator and denominator
 */
#define RATIONAL_NUMERATOR    (0)
#define RATIONAL_DENOMINATOR  (1)


/*
 * Ids for IFD entry types
 */
#define ENTRY_TYPE_UNSET      ((tiff_short_t)(0))
#define ENTRY_TYPE_FIRST      ((tiff_short_t)(1))

#define ENTRY_TYPE_BYTE       ((tiff_short_t)(1))
#define ENTRY_TYPE_ASCII      ((tiff_short_t)(2))
#define ENTRY_TYPE_SHORT      ((tiff_short_t)(3))
#define ENTRY_TYPE_LONG       ((tiff_short_t)(4))
#define ENTRY_TYPE_RATIONAL   ((tiff_short_t)(5))
#define ENTRY_TYPE_SBYTE      ((tiff_short_t)(6))  /* SBYTE - An 8-bit signed (twos-complement) integer. */
#define ENTRY_TYPE_UNDEFINED  ((tiff_short_t)(7))  /* UNDEFINED - An 8-bit byte that may contain anything, 
                                                    *  depending on the definition of the field. */
#define ENTRY_TYPE_SSHORT     ((tiff_short_t)(8))  /* A 16-bit (2-byte) signed (twos-complement) integer. */
#define ENTRY_TYPE_SLONG      ((tiff_short_t)(9))  /* A 32-bit (4-byte) signed (twos-complement) integer. */
#define ENTRY_TYPE_SRATIONAL  ((tiff_short_t)(10)) /* Two SLONG’s: the first represents the numerator of a
                                                      fraction, the second the denominator.*/
#define ENTRY_TYPE_FLOAT      ((tiff_short_t)(11)) /* Single precision (4-byte) IEEE format. */
#define ENTRY_TYPE_DOUBLE     ((tiff_short_t)(12)) /* Double precision (8-byte) IEEE format. */
#define ENTRY_TYPE_IFD13      ((tiff_short_t)(13)) /* Treat as long */

#define ENTRY_TYPE_LAST       ENTRY_TYPE_IFD13

#define ENTRY_TYPE_NUMBER     (ENTRY_TYPE_LAST + 1) /* Include UNSET */


/*
 * Sizes of each entry type as it appears in a TIFF file in bytes.
 */
#define ENTRY_SIZE_TYPE_UNSET     ((size_t)(0))
#define ENTRY_SIZE_TYPE_BYTE      ((size_t)(1))
#define ENTRY_SIZE_TYPE_ASCII     ((size_t)(1))
#define ENTRY_SIZE_TYPE_SHORT     ((size_t)(2))
#define ENTRY_SIZE_TYPE_LONG      ((size_t)(4))
#define ENTRY_SIZE_TYPE_RATIONAL  ((size_t)(8))
#define ENTRY_SIZE_TYPE_SBYTE     ((size_t)(1))
#define ENTRY_SIZE_TYPE_UNDEFINED ((size_t)(1))
#define ENTRY_SIZE_TYPE_SSHORT     ENTRY_SIZE_TYPE_SHORT
#define ENTRY_SIZE_TYPE_SLONG     ENTRY_SIZE_TYPE_LONG
#define ENTRY_SIZE_TYPE_SRATIONAL ENTRY_SIZE_TYPE_RATIONAL
#define ENTRY_SIZE_TYPE_FLOAT     ((size_t)(4))
#define ENTRY_SIZE_TYPE_DOUBLE    ((size_t)(8))
#define ENTRY_SIZE_TYPE_IFD13     ENTRY_SIZE_TYPE_LONG


#endif /* !__TIFTYPES_H__ */


/* Log stripped */
