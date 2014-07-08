/** \file gdevstio.h
 * \ingroup PLUGIN_PluginInterface
 * \ingroup interface
 * \brief Plugin API Interface: Dialog definition information.
 *
 * This file contains the structio-related definitions necessary for plugins
 * to specify plugin-specific parameters.
 */

/* $HopeName: SWpic!export:gdevstio.h(EBDSDK_P.1) $
 *
 * Copyright (c) 1993-2009 Global Graphics Software Ltd.  All rights reserved.
 *
 *
 */

#ifndef __GDEVSTIO_H__
#define __GDEVSTIO_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * The DICTSTRUCTION data structure, which contains details of a new Key
 * for use within a plugin. This is not the same as a PostScript Dictionary
 * entry within the RIP.
 *
 * It would be convenient to write a static definition of a DICTSTRUCTION
 * array and then try to copy it to the RIP on request, but this does not work.
 * Doing so would copy a pointer to the string, which would then be in a possibly
 * different address space. The plugin library function PluginLibStioFixup is
 * provided to copy the structure and everything it points to safely.
 *
 * \sa PluginLibStioFixup
 */
typedef struct dictstruction {
  int32 struction_type;         /**< The type of this key, for example STIO_INT. */
  uint8 *struction_title;       /**< For internal RIP use only */
  uint8 *struction_prefix;      /**< For internal RIP use only */
  uint8 *struction_name;        /**< The name of this key. This is a PostScript literal name
                                     and must start with a "/". \sa PluginLibStioFixup */
  int32 struction_offset;       /**< The offset in bytes to the memory containing the values
                                     of the parameter defined by the DICTSTRUCTION. */
  int32 struction_size;         /**< The maximum number of bytes, counting the terminating NUL,
                                     taken up by the parameter in the associated
                                     area of memory. It is currently only used for strings,
                                     since other types have an implicit length. */
  int32 struction_data;         /**< One or more of the SF_x values, for example SF_CONSTANT. */
  int32 struction_min;          /**< For types STIO_INT and STIO_FLOAT, the minimum value of this
                                     key. The GUI RIP will enforce this (but interpreted PostScript
                                     will not). */
  int32 struction_max;          /**< For types STIO_INT and STIO_FLOAT, the maximum value of this
                                     key. The GUI RIP will enforce this (but interpreted PostScript
                                     will not). */
  int32 reserved1;              /**< Unused */
  int32 reserved2;              /**< Unused */
  int32 reserved3;              /**< Unused */
  int32 reserved4;              /**< Unused */
} DICTSTRUCTION;

/**
 * \defgroup dictstructionTypes Defines for struction_type in DICTSTRUCTION.
 * \ingroup PLUGIN_PluginInterface
 * These define the different data types that the DICTSTRUCTION system understands.
 *
 * @{
 */
#define STIO_END           0x0      /**< Not a true type, but marks the end of an array or list of DICTSTRUCTIONs. */
#define STIO_INT           0x1      /**< A 32-bit signed integer */
#define STIO_FLOAT         0x2      /**< A 32-bit floating-point number */
#define STIO_BOOL          0x3      /**< A boolean */
#define STIO_INT_TABLE     0x4      /**< A table (array) of integers. \deprecated because it is incompletely
                                         implemented in the RIP. */
#define STIO_FLOAT_TABLE   0x5      /**< A table (array) of floats. \deprecated because it is incompletely
                                         implemented in the RIP. */
#define STIO_INLINE_STRING 0x8      /**< An (arbirtary-length) string. Beware that strings originating from the
                                         RIP GUI will use Harlequin language system multibyte characters where
                                         necessary. */
/**@}*/

/*
 * The definition below is what we want Stio_Offset to do.
 *  define Stio_Offset(_type_, _elt_) ((int32)(&(((_type_*)0)->_elt_)))
 *
 * But with this definition some compilers generate a warning, and SGI
 * generates an error when compiling with the -ansi flag. So instead,
 * we will use the definition below
*/

/**
 * Return the offset of a scalar entry within a structure from the start of the
 * structure. Use Stio_Offset_Array if the entry is an array.
 */
#define Stio_Offset(_type_, _elt_)       ((int32) (((char *) (&(((_type_*)NULL)->_elt_))) - ((char *) NULL)))

/**
 * Return the offset of an array entry within a structure from the start of the
 * structure. Use Stio_Offset if the entry is not an array.
 */
#define Stio_Offset_Array(_type_, _elt_) ((int32) (((char *) (&((_type_*)NULL)->_elt_[0])) - ((char *) NULL)))


#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef NEVER
/*
* Log stripped */
#endif /* NEVER */
#endif /* ! __GDEVSTIO_H__ */

/* eof gdevstio.h */
