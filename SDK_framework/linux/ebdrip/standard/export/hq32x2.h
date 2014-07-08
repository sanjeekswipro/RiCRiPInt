/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hq32x2.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * FrameWork '64 bit type' Interface
 *
 * Implements a signed 64 bit type, akin to an int64.
 * and an unsigned 64 bit type, akin to a uint64.
 *
 * Getting a 64 bit integer on all our compiler/platform combinations
 * is extremely difficult, and sometimes it's not supported at all. This
 * implementation allows 64 bit integers to be faked where performance is
 * not critical.
 */

#ifndef __HQ32X2_H__
#define __HQ32X2_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "hqncall.h"

/* ----------------------- Macros ------------------------------------------ */

/** \brief Fill in a 64-bit signed integer from a 32-bit signed integer. */
#define Hq32x2FromInt32( p32x2, i32 ) \
MACRO_START \
  ( p32x2 )->high = (( i32 ) < 0) ? -1 : 0; \
  ( p32x2 )->low = (uint32)( i32 ); \
MACRO_END

/** \brief Fill in a 64-bit unsigned integer from a 32-bit signed integer,
    asserting that the 32-bit integer is not negative. */
#define HqU32x2FromInt32( pU32x2, i32 ) \
MACRO_START \
  HQASSERT((i32) >= 0, "HqU32x2FromInt32 out of range"); \
  ( pU32x2 )->high = 0; \
  ( pU32x2 )->low = (int32)( i32 ); \
MACRO_END


/** \brief Fill in a 64-bit signed integer from a 32-bit unsigned integer. */
#define Hq32x2FromUint32( p32x2, ui32 ) \
MACRO_START \
  ( p32x2 )->high = 0; \
  ( p32x2 )->low = ( ui32 ); \
MACRO_END

/** \brief Fill in a 64-bit unsigned integer from a 32-bit unsigned integer. */
#define HqU32x2FromUint32( pU32x2, ui32 ) \
MACRO_START \
  ( pU32x2 )->high = 0; \
  ( pU32x2 )->low = ( ui32 ); \
MACRO_END

/** \brief Static or auto zero-value initialiser for Hq32x2 and HqU32x2. */
#define HQ32X2_INIT_ZERO { 0, 0 }

/** \brief Static or auto maximum-value initialiser for Hq32x2. */
#define HQ32X2_INIT_MAX { MAXUINT32, MAXINT32 }

/** \brief Static or auto minimum-value initialiser for Hq32x2. */
#define HQ32X2_INIT_MIN { 0, MININT32 }

/** \brief Static or auto maximum-value initialiser for HqU32x2. */
#define HQU32X2_INIT_MAX { MAXUINT32, MAXUINT32 }

/** \brief Fill in a 64-bit signed integer with its maximum possible value. */
#define Hq32x2Max( p32x2 ) \
MACRO_START \
  ( p32x2 )->high = MAXINT32; \
  ( p32x2 )->low = MAXUINT32; \
MACRO_END

/** \brief Fill in a 64-bit unsigned integer with its maximum possible
    value. */
#define HqU32x2Max( p32x2 ) \
MACRO_START \
  ( p32x2 )->high = MAXUINT32; \
  ( p32x2 )->low = MAXUINT32; \
MACRO_END

/** \brief A predicate to tell if a 64-bit signed integer's value is zero. */
#define Hq32x2IsZero( p32x2 ) ((p32x2)->high == 0 && (p32x2)->low == 0)

/** \brief A predicate to tell if a 64-bit unsigned integer's value is zero. */
#define HqU32x2IsZero( pU32x2 ) Hq32x2IsZero( pU32x2 )

/** \brief Fill in a 64-bit unsigned integer from a 64-bit signed integer,
    asserting that its value is not negative. */
#define HqU32x2From32x2(pU32x2, p32x2) \
MACRO_START \
  HQASSERT(Hq32x2Sign(p32x2) >= 0, "HqU32x2From32x2 out of range"); \
  (pU32x2)->low = (p32x2)->low; \
  (pU32x2)->high = (uint32)(p32x2)->high; \
MACRO_END

/** \brief Fill in a 64-bit signed integer from a 64-bit unsigned integer,
    asserting that its value is within range. */
#define Hq32x2FromU32x2( p32x2, pU32x2) \
MACRO_START \
  HQASSERT( (pU32x2)->high <= MAXINT32, "Hq32x2FromU32x2 out of range"); \
  (p32x2)->low = (pU32x2)->low; \
  (p32x2)->high = (int32)(pU32x2)->high; \
MACRO_END


/* ----------------------- Types ------------------------------------------- */

/** \brief Signed 64 bit integer, represented as a 2s complement pair. */
typedef struct Hq32x2 {
  uint32 low;
  int32  high;
} Hq32x2;

/** \brief Unsigned 64 bit integer, represented as a 2s complement pair. */
typedef struct HqU32x2 {
  uint32 low;
  uint32 high;
} HqU32x2;


/* ----------------------- Functions --------------------------------------- */

/** \brief Convert a 64-bit signed integer to a 32-bit signed integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \param[out] pReturn A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of the
    32-bit integer.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    the 32-bit integer. The value stored in \a pReturn is not modified in
    this case.
*/
HqBool HQNCALL Hq32x2ToInt32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2,
                             /*@out@*/ /*@notnull@*/ int32 * pReturn );

/** \brief Convert a 64-bit unsigned integer to a 32-bit signed integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \param[out] pReturn A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of the
    32-bit integer.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    the 32-bit integer. The value stored in \a pReturn is not modified in
    this case.
*/
HqBool HQNCALL HqU32x2ToInt32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2,
                              /*@out@*/ /*@notnull@*/ int32 * pReturn );

/** \brief Convert a 64-bit signed integer to a 32-bit unsigned integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \param[out] pReturn A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of the
    32-bit integer.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    the 32-bit integer. The value stored in \a pReturn is not modified in
    this case.
*/
HqBool HQNCALL Hq32x2ToUint32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2,
                              /*@out@*/ /*@notnull@*/ uint32 * pReturn );

/** \brief Convert a 64-bit unsigned integer to a 32-bit unsigned integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \param[out] pReturn A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of the
    32-bit integer.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    the 32-bit integer. The value stored in \a pReturn is not modified in
    this case.
*/
HqBool HQNCALL HqU32x2ToUint32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2,
                               /*@out@*/ /*@notnull@*/ uint32 * pReturn );


/** \brief Convert a 64-bit signed integer to a 32-bit signed integer,
    limiting to the range of the 32-bit integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \return The value of the 64-bit integer, clamped to the range MININT32 to
    MAXINT32.
*/
int32 HQNCALL Hq32x2BoundToInt32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2);

/** \brief Convert a 64-bit unsigned integer to a 32-bit signed integer,
    limiting to the range of the 32-bit integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \return The value of the 64-bit integer, clamped to the range MININT32 to
    MAXINT32.
*/
int32 HQNCALL HqU32x2BoundToInt32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2);

/** \brief Convert a 64-bit signed integer to a 32-bit unsigned integer,
    limiting to the range of the 32-bit integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \return The value of the 64-bit integer, clamped to the range 0 to
    MAXUINT32.
*/
uint32 HQNCALL Hq32x2BoundToUint32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2);

/** \brief Convert a 64-bit unsigned integer to a 32-bit unsigned integer,
    limiting to the range of the 32-bit integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \return The value of the 64-bit integer, clamped to the range 0 to
    MAXUINT32.
*/
uint32 HQNCALL HqU32x2BoundToUint32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2);


/** \brief Convert a 64-bit signed integer to a 32-bit signed integer,
    limiting to the range of the 32-bit integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \return The value of the 64-bit integer, clamped to the range MININT32 to
    MAXINT32.

    \note This function asserts if the value of the 64-bit integer is clamped
    to the 32-bit range.
*/
int32 HQNCALL Hq32x2AssertToInt32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2);

/** \brief Convert a 64-bit unsigned integer to a 32-bit signed integer,
    limiting to the range of the 32-bit integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \return The value of the 64-bit integer, clamped to the range MININT32 to
    MAXINT32.

    \note This function asserts if the value of the 64-bit integer is clamped
    to the 32-bit range.
*/
int32 HQNCALL HqU32x2AssertToInt32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2);

/** \brief Convert a 64-bit signed integer to a 32-bit unsigned integer,
    limiting to the range of the 32-bit integer.

    \param[in] p32x2 The 64-bit signed integer to convert.

    \return The value of the 64-bit integer, clamped to the range 0 to
    MAXUINT32.

    \note This function asserts if the value of the 64-bit integer is clamped
    to the 32-bit range.
*/
uint32 HQNCALL Hq32x2AssertToUint32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2);

/** \brief Convert a 64-bit unsigned integer to a 32-bit unsigned integer,
    limiting to the range of the 32-bit integer.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \return The value of the 64-bit integer, clamped to the range 0 to
    MAXUINT32.

    \note This function asserts if the value of the 64-bit integer is clamped
    to the 32-bit range.
*/
uint32 HQNCALL HqU32x2AssertToUint32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2);

/*
 * Conversion to and from ptrdiff_t, size_t values
 */

/** \brief Fill in a 64-bit signed integer from the difference between two
    pointers.

    \param[out] p32x2 A pointer to the 64-bit signed integer filled in with the
    pointer difference value.

    \param[in] ptrdiff An integral value, representing the difference between
    two pointers.
*/
void HQNCALL Hq32x2FromPtrdiff_t( /*@out@*/ /*@notnull@*/ Hq32x2 * p32x2, ptrdiff_t ptrdiff ) ;

/** \brief Fill in a 64-bit signed integer from an integral value representing
    the result of the \c sizeof() operator.

    \param[out] p32x2 A pointer to the 64-bit signed integer filled in with the
    pointer difference value.

    \param[in] sizet An integral value, representing the maximum size of an
    object.
*/
void HQNCALL Hq32x2FromSize_t( /*@out@*/ /*@notnull@*/ Hq32x2 * p32x2, size_t sizet ) ;

/** \brief Convert a 64-bit signed integer to an unsigned value sufficient
    to represent the size of an object.

    \param[in] p32x2 The 64-bit unsigned integer to convert.

    \param[out] sizet A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of
    \c size_t.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    \c size_t.
*/
HqBool HQNCALL Hq32x2ToSize_t(/*@out@*/ /*@notnull@*/ const Hq32x2 *p32x2, size_t *sizet) ;

/** \brief Fill in a 64-bit unsigned integer from an integral value
    representing the result of the \c sizeof() operator.

    \param[out] pU32x2 A pointer to the 64-bit unsigned integer filled in
    with the pointer difference value.

    \param[in] sizet An integral value, representing the maximum size of an
    object.
*/
void HQNCALL HqU32x2FromSize_t( /*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2, size_t sizet ) ;

/** \brief Convert a 64-bit unsigned integer to an unsigned value sufficient
    to represent the size of an object.

    \param[in] pU32x2 The 64-bit unsigned integer to convert.

    \param[out] sizet A pointer to the converted integer.

    \retval TRUE Returned if the 64-bit integer's value was in range of
    \c size_t.

    \retval FALSE Returned if the 64-bit integer's value was out of range of
    \c size_t.
*/
HqBool HQNCALL HqU32x2ToSize_t(/*@out@*/ /*@notnull@*/ const HqU32x2 *pU32x2, size_t *sizet) ;

/*
 * Conversion to and from double values
 */

/** \brief Fill in a 64-bit signed integer from a double-precision floating
    point value. The double is asserted to be in the range of an Hq32x2.

    \param[out] p32x2 A pointer to the 64-bit signed integer filled in with the
    truncated double value.

    \param[in] dbl A double precision floating point value to be truncated and
    converted to a signed 64-bit integral integer.
*/
void HQNCALL Hq32x2FromDouble(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2, double dbl );

/** \brief Fill in a 64-bit unsigned integer from a double-precision floating
    point value. The double is asserted to be in the range of an HqU32x2.

    \param[out] pU32x2 A pointer to the 64-bit unsigned integer filled in
    with the truncated double value.

    \param[in] dbl A double precision floating point value to be truncated
    and converted to a 64-bit integral integer.
*/
void HQNCALL HqU32x2FromDouble(/*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2, double dbl);

/** \brief Convert a 64-bit signed integer to a double-precision floating
    point value. Precision may be silently reduced, depending on the
    magnitude of the 64-bit integral value.

    \param[in] p32x2 A pointer to the 64-bit signed integer to be converted
    to double.

    \return The value stored at \a p32x2, converted to a double precision
    floating point value.
*/
double HQNCALL Hq32x2ToDouble(/*@in@*/ /*@notnull@*/ const Hq32x2 * p32x2 );

/** \brief Convert a 64-bit unsigned integer to a double-precision floating
    point value. Precision may be silently reduced, depending on the
    magnitude of the 64-bit integral value.

    \param[in] pU32x2 A pointer to the 64-bit unsigned integer to be
    converted to double.

    \return The value stored at \a pU32x2, converted to a double precision
    floating point value.
*/
double HQNCALL HqU32x2ToDouble(/*@in@*/ /*@notnull@*/ const HqU32x2 * pU32x2 );

/*
 * Unary functions on a 64 bit value
 */

/** \brief Return the sign of a 64-bit signed integer.

    \param[in] p32x2 A pointer to the 64-bit signed integer to be tested.

    \retval -1 Returned if the value stored at \a p32x2 is negative.

    \retval 0 Returned if the value stored at \a p32x2 is zero.

    \retval 1 Returned if the value stored at \a p32x2 is positive.
*/
int32 HQNCALL Hq32x2Sign(/*@in@*/ /*@notnull@*/ const Hq32x2 * p32x2 );

/** \brief Return the sign of a 64-bit unsigned integer.

    \param[in] pU32x2 A pointer to the 64-bit unsigned integer to be tested.

    \retval 0 Returned if the value stored at \a pU32x2 is zero.

    \retval 1 Returned if the value stored at \a pU32x2 is positive.
*/
uint32 HQNCALL HqU32x2Sign(/*@in@*/ /*@notnull@*/ const HqU32x2 * pU32x2 );


/*
 * Binary functions on two matching 64 bit values
 * Any of p[U]32x2Result, p[U]32x2A, p[U]32x2B may be the same
 */

/** \brief Add two signed 64-bit numbers, storing the sum into a third.

    \param[in] p32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as either or both of the inputs.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] p32x2B A pointer to the second operand.
*/
void HQNCALL Hq32x2Add(/*@out@*/ /*@notnull@*/ Hq32x2 *p32x2Result,
                       /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                       /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2B);

/** \brief Add two unsigned 64-bit numbers, storing the sum into a third.

    \param[in] pU32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as either or both of the inputs.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] pU32x2B A pointer to the second operand.
*/
void HQNCALL HqU32x2Add(/*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2Result,
                        /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                        /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2B);

/* Result = A - B */
/** \brief Subtract two signed 64-bit numbers, storing the difference into a
    third.

    \param[in] p32x2Result A pointer where the difference of the two inputs
    is stored. This pointer may be the same as either or both of the inputs.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] p32x2B A pointer to the second operand.
*/
void HQNCALL Hq32x2Subtract(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2Result,
                            /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                            /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2B);

/** \brief Subtract two unsigned 64-bit numbers, storing the difference into
    a third.

    \param[in] pU32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as either or both of the inputs.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] pU32x2B A pointer to the second operand.
*/
void HQNCALL HqU32x2Subtract(/*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2Result,
                             /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                             /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2B);

/* -1 <=> A < B,
 *  0 <=> A == B
 * +1 <=> A > B
 */
/** \brief Compare two 64-bit signed integers.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] p32x2B A pointer to the second operand.

    \retval -1 Returned if the first operand is less than the second.

    \retval 0 Returned if the operands have the same value.

    \retval 1 Returned if the first operand is greater than the second.

*/
int32 HQNCALL Hq32x2Compare(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                            /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2B);

/** \brief Compare two 64-bit unsigned integers.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] pU32x2B A pointer to the second operand.

    \retval -1 Returned if the first operand is less than the second.

    \retval 0 Returned if the operands have the same value.

    \retval 1 Returned if the first operand is greater than the second.

*/
int32 HQNCALL HqU32x2Compare(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                             /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2B);


/*
 * Binary functions on a 64 bit value and a uint32
 */

/** \brief Add an unsigned 32-bit integer to a signed 64-bit integer, storing
    the sum into a signed 64-bit integer.

    \param[in] p32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as the pointer to the first operand.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be added to the first
    operand.
*/
void HQNCALL Hq32x2AddUint32(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2Result,
                             /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                             uint32 ui32 );

/** \brief Add an unsigned 32-bit integer to an unsigned 64-bit integer,
    storing the sum into an unsigned 64-bit integer.

    \param[in] pU32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as the pointer to the first operand.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be added to the first
    operand.
*/
void HQNCALL HqU32x2AddUint32(/*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2Result,
                              /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                              uint32 ui32 );

/** \brief Subtract an unsigned 32-bit integer from a signed 64-bit integer,
    storing the sum into a signed 64-bit integer.

    \param[in] p32x2Result A pointer where the difference of the two inputs
    is stored. This pointer may be the same as the pointer to the first
    operand.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be subtracted from the first
    operand.
*/
void HQNCALL Hq32x2SubtractUint32(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2Result,
                                  /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                                  uint32 ui32 );

/** \brief Subtract an unsigned 32-bit integer from an unsigned 64-bit
    integer, storing the sum into an unsigned 64-bit integer.

    \param[in] pU32x2Result A pointer where the difference of the two inputs
    is stored. This pointer may be the same as the pointer to the first
    operand.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be subtracted from the first
    operand.
*/
void HQNCALL HqU32x2SubtractUint32(/*@out@*/ /*@notnull@*/ HqU32x2 * pU32x2Result,
                                   /*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                                   uint32 ui32 );

/** \brief Compare an unsigned 32-bit integer to a signed 64-bit integer.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be compared with the first
    operand.

    \retval -1 Returned if the first operand is less than the second.

    \retval 0 Returned if the operands have the same value.

    \retval 1 Returned if the first operand is greater than the second.
*/
int32 HQNCALL Hq32x2CompareUint32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                                  uint32 ui32 );

/** \brief Compare an unsigned 32-bit integer to an unsigned 64-bit integer.

    \param[in] pU32x2A A pointer to the first operand.

    \param[in] ui32 A 32-bit unsigned integer to be compared with the first
    operand.

    \retval -1 Returned if the first operand is less than the second.

    \retval 0 Returned if the operands have the same value.

    \retval 1 Returned if the first operand is greater than the second.
*/
int32 HQNCALL HqU32x2CompareUint32(/*@in@*/ /*@notnull@*/ const HqU32x2 *pU32x2A,
                                   uint32 ui32 );


/*
 * Binary functions on a 64 bit value and a int32
 * The combination of HqU32 and int32 is not implemented yet
 */

/** \brief Add a signed 32-bit integer to a signed 64-bit integer, storing
    the sum into a signed 64-bit integer.

    \param[in] p32x2Result A pointer where the sum of the two inputs is
    stored. This pointer may be the same as the pointer to the first operand.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] i32 A 32-bit signed integer to be added to the first operand.
*/
void HQNCALL Hq32x2AddInt32(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2Result,
                            /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                            int32 i32 );

/** \brief Subtract a signed 32-bit integer from a signed 64-bit integer,
    storing the sum into a signed 64-bit integer.

    \param[in] p32x2Result A pointer where the difference of the two inputs
    is stored. This pointer may be the same as the pointer to the first
    operand.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] i32 A 32-bit signed integer to be subtracted from the first
    operand.
*/
void HQNCALL Hq32x2SubtractInt32(/*@out@*/ /*@notnull@*/ Hq32x2 * p32x2Result,
                                 /*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                                 int32 i32 );

/** \brief Compare a signed 32-bit integer to a signed 64-bit integer.

    \param[in] p32x2A A pointer to the first operand.

    \param[in] i32 A 32-bit signed integer to be compared with the first
    operand.

    \retval -1 Returned if the first operand is less than the second.

    \retval 0 Returned if the operands have the same value.

    \retval 1 Returned if the first operand is greater than the second.
*/
int32 HQNCALL Hq32x2CompareInt32(/*@in@*/ /*@notnull@*/ const Hq32x2 *p32x2A,
                                 int32 i32 );


#ifdef __cplusplus
}
#endif

#endif /* ! __HQ32X2_H__ */

