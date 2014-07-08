/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlattributes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * "C" typedefs and enumerations that represent PCLXL-specific
 * datastream constructs operators, attribute IDs, data-type tags and
 * multi-byte (numeric) values
 */

#ifndef __PCLXLATTRIBUTES_H__
#define __PCLXLATTRIBUTES_H__ 1

#include "pclxltypes.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxltypes.h"


/*
 * The byte-code tags used for PCLXL data types have been carefully
 * chosen so that the individual bits each have specific meanings
 *
 * Therefore we have a collection of "accessor" macros below that each
 * take a data type tag and return some information about this data
 * type, including its native PCLXL data type the storage type
 * (i.e. structure field) within the PCLXL_ATTRIBUTE structure and the
 * size (in bytes) of the various data types
 */

/*
 * PCLXL_BASE_DATA_TYPE() returns 0 for ubyte, 1 for uint16, 2 for
 * uint32, 3 for int16, 4 for int32 and 5 for real32 and it ignores
 * whether the data type is a simple value, structured type and/or
 * array
 */
#define PCLXL_BASE_DATA_TYPE(DT) ((DT) & 0x07)

#define PCLXL_BASE_DATA_TYPE_UBYTE  0
#define PCLXL_BASE_DATA_TYPE_UINT16 1
#define PCLXL_BASE_DATA_TYPE_UINT32 2
#define PCLXL_BASE_DATA_TYPE_INT16  3
#define PCLXL_BASE_DATA_TYPE_INT32  4
#define PCLXL_BASE_DATA_TYPE_REAL32 5

/*
 * PCLXL_IS_ARRAY_TYPE() returns true if this is an array type
 * and false if it is not.
 * If it is true, then we will need to obtain the array size
 * by reading an array_length value from the PCLXL stream
 */

#define PCLXL_IS_ARRAY_TYPE(DT) ((DT) & 0x08)

/*
 * We also have a data structure to represent the whole PCLXL
 * attribute which includes the attribute ID, the attribute data type
 * and a union of value fields to hold single instances of each basic
 * PCLXL data type and fields to hold arrays and/or structures of
 * these data types
 */
typedef struct pclxl_attribute_struct
{
  PCLXL_ATTR_ID               attribute_id;         /* Typically read *after* data type value(s) have been read */
  PCLXL_TAG                   data_type;            /* raw/unexpurgated PCLXL data type tag */
  uint32                      index;                /* Attribute set order index */
  uint32                      array_length;         /* only used for PCLXL array  or structured data types */
  uint32                      array_element_size;   /* size (in bytes) of each array element or size of structured data type value */

  union
  {
    /*
     * The following 6 union variants are used for single/simple data
     * values and thus require no additional storage than the
     * PCLXL_ATTRIBUTE struct
     */
    PCLXL_UByte               v_ubyte;
    PCLXL_UInt16              v_uint16;
    PCLXL_UInt32              v_uint32;
    PCLXL_Int16               v_int16;
    PCLXL_Int32               v_int32;
    PCLXL_Real32              v_real32;

    /*
     * The following 6 union variants are used for both structured
     * data values and/or arrays
     *
     * The appropriate one of these is initialized to point at the
     * appropriate amount of dynamically allocated storage and will
     * probably thereafter be accessed via an array and/or structure
     * access function
     */
    PCLXL_UByte*              v_ubytes;
    PCLXL_UInt16*             v_uint16s;
    PCLXL_UInt32*             v_uint32s;
    PCLXL_Int16*              v_int16s;
    PCLXL_Int32*              v_int32s;
    PCLXL_Real32*             v_real32s;
  } value;

  /* Instant structured data type value storage */
  union {
    PCLXL_UByte               v_ubytes[4];
    PCLXL_UInt16              v_uint16[4];
    PCLXL_UInt32              v_uint32[4];
    PCLXL_Int16               v_int16[4];
    PCLXL_Int32               v_int32[4];
    PCLXL_Real32              v_real32[4];
#define PCLXL_ATTRIBUTE_BUFFER_SIZE (16u)
    uint8                     buffer[PCLXL_ATTRIBUTE_BUFFER_SIZE + 1]; /* Allow a terminating NUL for debug */
  } value_array;

} PCLXL_ATTRIBUTE_STRUCT;


/**
 * \brief Create and initialise an attribute set in the given memory pool.
 *
 * Additional storage needed for attribute values will also be created in the
 * given memory pool.
 *
 * \param[in] pool Memory pool to use for attribute set.
 *
 * \return Pointer to attribute set else \c NULL.
 */
extern
PCLXL_ATTRIBUTE_SET* pclxl_attr_set_create(
  mm_pool_t               pool);

/**
 * \brief Free off an attribute set.
 *
 * \param[in] p_attr_set Pointer to attribute set to free.
 */
extern
void pclxl_attr_set_destroy(
  PCLXL_ATTRIBUTE_SET*    p_attr_set);

/**
 * \brief Empty the attribute set of any current attributes.
 *
 * Any additional memory used by the current attributes will also be freed.
 *
 * \param[in,out] p_attr_set Pointer to the attribute set.
 */
extern
void pclxl_attr_set_empty(
  PCLXL_ATTRIBUTE_SET*    p_attr_set);

/**
 * \brief Get a new attribute that is not yet in the attribute set.
 *
 * \param[in] p_attr_set Pointer to the attribute set.
 *
 * \return Pointer to an new unused attribute, else \c NULL.
 */
extern
PCLXL_ATTRIBUTE_STRUCT* pclxl_attr_set_get_new(
  PCLXL_ATTRIBUTE_SET*    p_attr_set);

/**
 * \brief Add an attribute to the attribute set.
 *
 * The attribute being added to the set must have been obtained by calling
 * pclxl_attr_set_get_new() on the same attribute set.
 *
 * \param[in] p_attr_set Pointer to the attribute set.
 * \param[in] p_attr Pointer to attribute.
 */
extern
void pclxl_attr_set_add(
  PCLXL_ATTRIBUTE_SET*    p_attr_set,
  PCLXL_ATTRIBUTE_STRUCT* p_attr);


/* Flag to OR with attribute id if the match functions should raise an error if
 * the attribute is not present in the attribute set.
 */
#define PCLXL_ATTR_REQUIRED (0x8000)
/* Sentinel attribute id value. */
#define PCLXL_END_MATCH     (0x0fff)
/* Sentinel value for an attribute match array. */
#define PCLXL_MATCH_END     { PCLXL_END_MATCH }

/* Attribute match details. */
typedef struct PCLXL_ATTR_MATCH {
  PCLXL_ATTR_ID           id;           /* Attribute id to look for. */
  PCLXL_ATTRIBUTE_STRUCT* result;       /* Pointer to attribute is id appears in the set. */
} PCLXL_ATTR_MATCH;

/**
 * \brief Match zero or more attributes in the attribute set.
 *
 * Some XL operators require no attributes, or all the attributes are optional.
 * The required attributes of some operators are too complex to model in a
 * simple array but they can be made all optional so that the operator
 * implements its own attribute presence validation.
 *
 * If a required attribute is missing then a missing attribute error is
 * raised.
 *
 * If an attribute data type is not valid than an illegal attribute datatype
 * error is raised.
 *
 * If any attributes in the set are not matched that an illegal attribute
 * combination error will be raised.
 *
 * An attribute set can be matched just once.
 *
 * \param[in] p_attr_set Pointer to attribute set.
 * \param[in] p_match Pointer to attribute match array.
 * \param[in] pclxl_context Pointer to XL interpreter context.
 * \param[in] subsystem XL subsystem to use when reporting an error.
 *
 * \return \c TRUE if all the attributes in the attribute set were matched, else
 * \c FALSE.
 */
extern
Bool pclxl_attr_set_match(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTR_MATCH*     p_match,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem);

/**
 * \brief Match one or more attributes in the attribute set.
 *
 * The required attributes of some operators are too complex to model in a
 * simple array but they can be made optional.  In the case where one or more
 * must exist this function can be used to raise a missing attribute error.
 *
 * If a required attribute is missing or none of the supplied attributes are
 * matched then a missing attribute error is raised.
 *
 * If an attribute data type is not valid than an illegal attribute datatype
 * error is raised.
 *
 * If any attributes in the set are not matched that an illegal attribute
 * combination error will be raised.
 *
 * An attribute set can be matched just once.
 *
 * \param[in] p_attr_set Pointer to attribute set.
 * \param[in] p_match Pointer to attribute match array.
 * \param[in] pclxl_context Pointer to XL interpreter context.
 * \param[in] subsystem XL subsystem to use when reporting an error.
 *
 * \return \c TRUE if all the attributes in the attribute set were matched, else
 * \c FALSE.
 */
extern
Bool pclxl_attr_set_match_at_least_1(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_ATTR_MATCH*     p_match,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem);


/**
 * \brief Match an empty attribute set.
 *
 * \param[in] p_attr_set Pointer to attribute set.
 * \param[in] pclxl_context Pointer to XL interpreter context.
 * \param[in] subsystem XL subsystem to use when reporting an error.
 *
 * \return \c TRUE if the attribute set is empty, else \c FALSE.
 */
extern
Bool pclxl_attr_set_match_empty(
  PCLXL_ATTRIBUTE_SET*  p_attr_set,
  PCLXL_CONTEXT         pclxl_context,
  PCLXL_SUBSYSTEM       subsystem);


/* XL enumeration attributes are all low valued so this should be a safe
 * sentinel value to use.
 */
#define PCLXL_ENUMERATION_END   (255)

/**
 * \brief Get valid enumeration value for an enumerated attribute.
 *
 * \param[in] p_attr Pointer to enumeration valued attribute.
 * \param[in] p_values Pointer to array of valid enumeration values.
 * \param[in] num_values Number of valid enumeration values.
 * \param[out] p_enum Pointer to returned enumeration value.
 *
 * \return \c TRUE if the attribute value is a valid enumeration, else \c FALSE.
 */
extern
Bool pclxl_attr_valid_enumeration(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_ENUMERATION*      p_values,
  PCLXL_ENUMERATION*      p_enum);

/**
 * \brief Get valid enumeration value for an enumerated attribute.
 *
 * If the attribute value is not valid for the enumeration then an illegal
 * attribute value error is raised.
 *
 * \param[in] p_attr Pointer to enumeration valued attribute.
 * \param[in] p_values Pointer to array of valid enumeration values.
 * \param[in] num_values Number of valid enumeration values.
 * \param[out] p_enum Pointer to returned enumeration value.
 * \param[in] pclxl_context Pointer to XL interpreter context.
 * \param[in] subsystem XL subsystem to use when reporting an error.
 *
 * \return \c TRUE if the attribute value is a valid enumeration, else \c FALSE.
 */
extern
Bool pclxl_attr_match_enumeration(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_ENUMERATION*      p_values,
  PCLXL_ENUMERATION*      p_enum,
  PCLXL_CONTEXT           pclxl_context,
  PCLXL_SUBSYSTEM         subsystem);

/**
 * \brief Return attribute value as an unsigned 32bit integer.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with an unsigned 32bit integer.
 *
 * \param[in] p_attr Pointer to attribute.
 *
 * \return Attribute value as an unsigned 32bit integer.
 */
extern
uint32 pclxl_attr_get_uint(
  PCLXL_ATTRIBUTE_STRUCT* p_attr);

/**
 * \brief Return attribute value as a signed 32bit integer.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with a signed 32bit integer.
 *
 * \param[in] p_attr Pointer to attribute.
 *
 * \return Attribute value as a signed 32bit integer.
 */
extern
int32 pclxl_attr_get_int(
  PCLXL_ATTRIBUTE_STRUCT* p_attr);

/**
 * \brief Return attribute value as a floating point value.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with a floating point value.
 *
 * \param[in] p_attr Pointer to attribute.
 *
 * \return Attribute value as a floating pointer value.
 */
extern
PCLXL_SysVal pclxl_attr_get_real(
  PCLXL_ATTRIBUTE_STRUCT* p_attr);

/**
 * \brief Return attribute as an unsigned 32bit integer XY.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with an unsigned 32bit integer XY.
 *
 * \param[in] p_attr Pointer to attribute.
 * \param[in] p_uint_xy Pointer to unsigned 32bit integer XY.
 */
extern
void pclxl_attr_get_uint_xy(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_UInt32_XY*        p_uint_xy);

/**
 * \brief Return attribute as a floating point XY.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with an floating point XY.
 *
 * \param[in] p_attr Pointer to attribute.
 * \param[in] p_real_xy Pointer to floating point XY.
 */
extern
void pclxl_attr_get_real_xy(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_SysVal_XY*        p_real_xy);

/**
 * \brief Return attribute as a floating point Box.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with an floating point Box.
 *
 * \param[in] p_attr Pointer to attribute.
 * \param[in] p_real_box Pointer to floating point Box.
 */
extern
void pclxl_attr_get_real_box(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  PCLXL_SysVal_Box*       p_real_box);


/**
 * \brief Return attribute as a byte array pointer and length.
 *
 * This function should not be called with an attribute whose datatype is not
 * compatible with 1 or 2 byte character strings.
 *
 * \param[in] p_attr Pointer to attribute.
 * \param[in] pp_bytes Pointer to byte pointer.
 * \param[in] p_len Pointer to length.
 */
extern
void pclxl_attr_get_byte_len(
  PCLXL_ATTRIBUTE_STRUCT* p_attr,
  uint8**                 pp_bytes,
  uint32*                 p_len);

extern
PCLXL_PROTOCOL_VERSION pclxl_attr_protocol(
  PCLXL_ATTRIBUTE   p_attr);


#ifdef DEBUG_BUILD

extern
uint8* pclxl_get_attribute_name(
  PCLXL_ATTRIBUTE   p_attr);

#endif /* DEBUG_BUILD */

#endif /* __PCLXLATTRIBUTES_H__ */

/******************************************************************************
* Log stripped */
