/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:coretypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type definitions common to Core RIP
 *
 * Core types and associated methods.
 */

#ifndef __CORETYPES_H__
#define __CORETYPES_H__

#include "swvalues.h" /* Until SYSTEMVALUE/USERVALUE are moved here */

/**
 * \defgroup types Type definitions common to Core RIP.
 * \ingroup core
 * \{
 */

/*****************************************************************************/
/** \brief Boolean type

    The name of the boolean type used in the core unfortunately conflicts with
    definitions in X11, so we don't typedef the standard definition to Bool. */
typedef HqBool Bool ;

/*****************************************************************************/
/** \brief Device coordinates

    Device coordinates are integer reference positions for pixel centres. The
    possibility is left open to use a fixed-point integer representation, but
    the device coordinate type will always be a signed integral type. */
typedef int32 dcoord ;

#define CAST_DCOORD_TO_INTPTRT(x) ((intptr_t)(x))
#define CAST_INTPTRT_TO_DCOORD(x) (CAST_INTPTRT_TO_INT32(x))

/** \brief Maximum device coordinate */
#define MAXDCOORD ((dcoord)0x7fffffff)
/** \brief Minimum device coordinate */
#define MINDCOORD ((dcoord)0x80000000)

/*****************************************************************************/
/** \brief Spot numbers

    Spot numbers distinguish different halftone screens.  \note Spot
    numbers are used even when rendering to contone, in a trivial way.
 */

typedef int32 SPOTNO;

/** \brief An invalid spot number. */
#define SPOT_NO_INVALID ((SPOTNO)-1)

/** \brief Halftone object types

    Types used for object-based (type 195) halftones.  Actually, the
    repro types, but a separate type name retains the option to change. */
typedef uint8 HTTYPE;

#define HTTYPE_DEFAULT (4u) /* REPRO_N_TYPES, but that's not available */

#define HTTYPE_VALID(type) ((type) <= HTTYPE_DEFAULT)

/*****************************************************************************/
/** \brief Colorant indices

     Colorant indices are zero-based indices, with the negative values used
     as out-of-band special values and error conditions.
 */
typedef int32 COLORANTINDEX;

/** \brief Special values for colorant indices.

    \note COLORANTINDEX_UNKNOWN should have the highest special index! */
#define COLORANTINDEX_SPECIAL(n)  (-((COLORANTINDEX)(n)))
#define COLORANTINDEX_ALL       COLORANTINDEX_SPECIAL(1)
#define COLORANTINDEX_NONE      COLORANTINDEX_SPECIAL(2)
#define COLORANTINDEX_ALPHA     COLORANTINDEX_SPECIAL(3)
#define COLORANTINDEX_UNKNOWN   COLORANTINDEX_SPECIAL(4)
#define COLORANTINDEX_MAX (1023)

/** \brief Macro to check for valid active or special colorant index. */
#define COLORANTINDEX_VALID(ci) \
  ((ci) > COLORANTINDEX_UNKNOWN && (ci) <= COLORANTINDEX_MAX)

/*****************************************************************************/
/** \brief Device-space color values.

    The front end now uses the range 0..0xff00 for colors. Conversion to
    halftone levels is performed at render time. The maximum colorvalue
    0xff00 allows fast conversion to halftone levels; division by 0xff00 is
    optimised by good C compilers to a 32x32:64 multiply by 0x80808081 and a
    shift by 15. This is equivalent to a power-series expansion (see
    ScriptWorks Information\Development\Core Rip\Rational Colour Values),
    which can be performed with 4 shifts and 5 additions. This representation
    also has the nice properties that all 8 and 12-bit sample values are
    exactly representable, there is an exact representation for 0.5, and
    there are several out of band values representable in the same storage
    unit. Conversion to 8 or 10-bit output for contone is also extremely fast
    (one addition, one shift).

    If we decide to use fixed-point colorvalues for wide-gamut rendering,
    this would be likely to change to a signed 16-bit value. With a signed
    16-bit value, we can represent the gamut from -4.0 to +4.0 using a 3.13
    scheme, and still have some out of band values to represent invalid and
    transparent colors.

    If we decide to use floating-point color values for wide-gamut rendering,
    this would change to a float.
 */
typedef uint16 COLORVALUE;

/** \brief The maximum valid colorant value.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to +32640. */
#define COLORVALUE_MAX              0xff00u

/** \brief The minimum valid colorant value.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to -32640. */
#define COLORVALUE_MIN              0u

/** \brief The color value corresponding to 1.0.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to 8160. */
#define COLORVALUE_ONE              COLORVALUE_MAX

/** \brief The color value corresponding to 0.5.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to 4080.
 */
#define COLORVALUE_HALF             0x7f80u

/** \brief The color value corresponding to 0.0.

    For wide-gamut rendering with 3.13 fixed-point values, this would stay
    as 0. */
#define COLORVALUE_ZERO             0

/** \brief An out-of-band color value indicating a color channel is invalid.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to a value in the range 0x7f81u-0x807fu. */
#define COLORVALUE_INVALID          0xffffu

/** \brief An out-of-band color value indicating a color channel is
    transparent.

    For wide-gamut rendering with 3.13 fixed-point values, this would change
    to a value in the range 0x7f81u-0x807fu. */
#define COLORVALUE_TRANSPARENT      0xfffeu

/** \brief When combining preseparated jobs, the no colorant value for a
    channel. */
#define COLORVALUE_PRESEP_WHITE     COLORVALUE_ONE

/** \brief When combining preseparated jobs, the full colorant value for a
    channel. */
#define COLORVALUE_PRESEP_BLACK     COLORVALUE_ZERO

/** \brief Macros to assert range of colorvalues.

    Do not use this macro for the special out of band values. */
#if defined(ASSERT_BUILD)
#define CAST_TO_COLORVALUE(x) \
   (COLORVALUE)(((uint32)(x) > COLORVALUE_MAX) ? \
      HQFAIL("Overflow while casting to COLORVALUE"), (x) : (x))
#else
#define CAST_TO_COLORVALUE(x) (COLORVALUE)(x)
#endif

/** \brief Normalise a pre-multiplied colourvalue and alpha to colourvalue
    range, yielding a colourvalue result.

    This is a macro so it can be replaced with a power series expansion if
    the compiler does not support the optimisation above. The casts through
    uint32 prevent the ANSI "usual arithmetic conversions" from converting to
    signed ints in the intermediate results.

    For wide-gamut rendering with 3.13 fixed-point values, this would need
    to use signed int32 intermediate results. */
#define COLORVALUE_NORMALISE(_cv, _result) MACRO_START \
  (_result) = CAST_TO_COLORVALUE(((uint32)(_cv) + (uint32)COLORVALUE_HALF) / (uint32)COLORVALUE_ONE) ; \
MACRO_END

/** \brief Multiply a colorvalue by an alpha value.

    For wide-gamut rendering with 3.13 fixed-point values, this would need
    to use signed int32 intermediate results. */
#define COLORVALUE_MULTIPLY(_multiplicand, _multiplier, _result) MACRO_START \
  HQASSERT((uint32)(_multiplicand) <= COLORVALUE_MAX, "Multiplicand is not a colourvalue") ; \
  HQASSERT((uint32)(_multiplier) <= COLORVALUE_ONE, "Multiplier is not a colourvalue") ; \
  COLORVALUE_NORMALISE((uint32)(_multiplicand) * (uint32)(_multiplier), (_result)) ; \
MACRO_END

/** \brief Divide a colourvalue by an alpha value.

    For wide-gamut rendering with 3.13 fixed-point values, this would need to
    use signed int32 intermediate results, and should not clamp to
    COLORVALUE_ONE. */
#define COLORVALUE_DIVIDE(_dividend, _divisor, _result) MACRO_START \
  HQASSERT((uint32)(_dividend) <= COLORVALUE_MAX, "Dividend is not a colourvalue") ; \
  HQASSERT((uint32)(_divisor) <= COLORVALUE_ONE, "Divisor is not a colourvalue") ; \
  if ( ((_divisor) | (_dividend)) == 0 )                                \
    (_result) = 0 ;                                                     \
  else if ( (_divisor) <= (_dividend) )                                 \
    (_result) = COLORVALUE_ONE ;                                        \
  else                                                                  \
    (_result) = CAST_TO_COLORVALUE((uint32)(_dividend) * (uint32)COLORVALUE_ONE / (uint32)(_divisor)) ; \
MACRO_END

/** \brief Flip a color from additive to subtractive, or vice versa. */
#define COLORVALUE_FLIP(_cv, _result) MACRO_START \
  HQASSERT((_cv) <= COLORVALUE_MAX, "not a valid colorvalue"); \
  (_result) = CAST_TO_COLORVALUE(COLORVALUE_ONE - (_cv)); \
MACRO_END

/** \brief Multiplicand to enable transformation from fixed point color
    representation back to real value equivalent. */
#define COLORVALUE_INVERSE ((SYSTEMVALUE)(1.0/(SYSTEMVALUE)COLORVALUE_ONE))

/** \brief Convert a 0-1 value float into a fixed point COLORVALUE. */
#define FLOAT_TO_COLORVALUE(value_) \
        ((COLORVALUE)(COLORVALUE_ONE * (value_) + 0.5f))

/** \brief Convert a COLORVALUE to an 0-1 value float. */
#define COLORVALUE_TO_USERVALUE(value_) \
        ((USERVALUE)(COLORVALUE_INVERSE * (value_)))

/** \brief Convert a COLORVALUE to an 0-255 value byte.

    For wide-gamut rendering with 3.13 fixed-point values, the shift and
    addition would change, and the result would need clamping. */
#define COLORVALUE_TO_UINT8(value_) \
        ((uint8)(((value_) + 0x80) >> 8))

/*****************************************************************************/
/** \brief Ranges

    Explicit method of specifying ranges within lists. A length of zero
    indicates an empty range - the origin is irrelevant in this case. */
typedef struct {
  uint32 origin;
  uint32 length;
} Range;

/** \brief Returns a Range with the passed origin and length. */
Range rangeNew(uint32 origin, uint32 length);

/** \brief Returns TRUE if the passed value is within the range. */
Bool rangeContains(Range self, uint32 value);

/** \brief Calculate the upper extent of this range (i.e. origin + length). */
#define rangeTop(range_) ((range_).origin + (range_).length)

/** \brief Test two ranges for equality. */
#define rangeEqual(a_, b_) (((a_).origin == (b_).origin) && \
                            ((a_).length == (b_).length))

/** \brief Return the union of two ranges. */
Range rangeUnion(Range a, Range b);

/** \brief Return the intersection of two ranges. */
Range rangeIntersection(Range a, Range b);


/*****************************************************************************/
/** \brief Sizes (obsolete)

    Width/height container. Please do not add any new uses of this type. */
typedef struct {
  uint32 width;
  uint32 height;
} Size2d;

/** \brief TRUE if either the width or height are zero. */
#define size2dDegenerate(self_) (((self_).width == 0) || ((self_).height == 0))

/** \brief Returns a Size2d with the passed width and height. */
Size2d size2dNew(uint32 width, uint32 height);

/** \brief Initialize width and height to the passed values. */
#define size2dSetup(self_, width_, height_) \
  MACRO_START \
  (self_).width = (width_); \
  (self_).height = (height_); \
  MACRO_END

/*****************************************************************************/
/** Bounding boxes of various flavours.

   BBoxes are ubiquitous, not really belonging to any module; this is a good
   place for them.
*/

/** \brief Floating-point bounding box */
typedef struct {
  SYSTEMVALUE x1, y1, x2, y2 ;
} sbbox_t ;

/** \brief Integer bounding box */
typedef struct {
  int32 x1, y1, x2, y2 ;
} ibbox_t ;

/** \brief Device coordinate bounding box */
typedef struct {
  dcoord x1, y1, x2, y2 ;
} dbbox_t ;

/* Bounding box macros. These are macros so that they can be used on any
   bounding box type. The macros are written using pointers, in case they are
   changed to functions later. Some of the macros have a destination bounding
   box as well as source bounding boxes; in all cases the destination box may
   be the same as one of the source boxes, the compiler will hopefully
   optimise away redundant assignments due to identical locations. */

/** \brief Get a pointer which can be indexed to access the elements of the
    bbox.

   Any reasonable compiler will ensure that the indexed elements match the
   struct layout. The argument for this case is based upon the following:

   Section 6.5.2.1 says about structs:

   "Within a structure object, the non-bit-field members ...  have addresses
    that increase in the order in which they are declared. A pointer to the a
    structure object, suitably converted, points to its initial member (...)
    and vice-versa. There may therefore be unnamed padding within a structure
    object, but not at its beginning, as necessary to achieve the appropriate
    alignment."

   The "as necessary" can be read as "and only as necessary"; this is typical
   American English usage. This implies that for members of the same type,
   since the first element is aligned appropriately for the pointer
   conversion statement, the other elements would be appropriately aligned
   and need no padding.

   Section 6.1.2.5 defines an array as a "contiguously allocated nonempty set
   of objects with a particular member object type".

   Section 6.5.2.1 gives the same initial member alignment guarantees for
   unions as for structs, for *all* members of the union.

   However, the interpretation given by the ANSI committee indicates that this
   is not guaranteed:

   http://wwwold.dkuug.dk/JTC1/SC22/WG14/www/docs/dr_074.html (part e.)

   It is possible that a 64 bit processor could have a 'natural' alignment of
   4 byte ints on 8 byte boundaries (to save any possible barrel rolling on
   memory read/writes). In this case, the behaviour of the bbox members is
   likely to be the least of our worries. */
#define bbox_as_indexed(ptr_, bb_) MACRO_START \
  (ptr_) = &(bb_)->x1 ; \
  HQASSERT(&(ptr_)[1] == &(bb_)->y1 && \
           &(ptr_)[2] == &(bb_)->x2 && \
           &(ptr_)[3] == &(bb_)->y2, \
           "Bounding box elements are not aligned with indexed elements") ; \
MACRO_END

/** \brief Set the members of a box, bottom-left and top-right by
    convention. */
#define bbox_store(bb_, x1_, y1_, x2_, y2_) MACRO_START \
  (bb_)->x1 = (x1_); \
  (bb_)->y1 = (y1_); \
  (bb_)->x2 = (x2_); \
  (bb_)->y2 = (y2_); \
MACRO_END

/** \brief Set a bounding box to be empty.

   An empty bounding box is one with negative internal area

     (x1 > x2 or y1 > y2).

   We initialise the values to the maximum/minimum values, so that
   unions with a MAXINT/MININT, so that union and intersection work with the
   definitions transparently. If this range is not sufficient for an sbbox,
   use bbox_store() and appropriate values. */
#define bbox_clear(bb_) bbox_store((bb_), MAXINT, MAXINT, MININT, MININT)

/** \brief The inverse of bbox_store: unpack a bounding box into four
    coordinates. */
#define bbox_load(bb_, x1_, y1_, x2_, y2_) MACRO_START \
  (x1_) = (bb_)->x1 ; \
  (y1_) = (bb_)->y1 ; \
  (x2_) = (bb_)->x2 ; \
  (y2_) = (bb_)->y2 ; \
MACRO_END

/** \brief Create the union of two bounding boxes, storing the result in a
    third.

    The destination bounding box may be the same as the first or second bbox.
    This will work fine if either bbox is empty. */
#define bbox_union(bb1_, bb2_, dest_) MACRO_START \
  (dest_)->x1 = min((bb1_)->x1, (bb2_)->x1) ; \
  (dest_)->y1 = min((bb1_)->y1, (bb2_)->y1) ; \
  (dest_)->x2 = max((bb1_)->x2, (bb2_)->x2) ; \
  (dest_)->y2 = max((bb1_)->y2, (bb2_)->y2) ; \
MACRO_END

/** \brief Union with four coordinates, representing an unpacked bounding
    box. */
#define bbox_union_coordinates(bb_, x1_, y1_, x2_, y2_) MACRO_START \
  HQASSERT((x1_) <= (x2_), "X coordinates out of order") ; \
  HQASSERT((y1_) <= (y2_), "Y coordinates out of order") ; \
  if ( (x1_) < (bb_)->x1 ) \
    (bb_)->x1 = (x1_) ; \
  if ( (y1_) < (bb_)->y1 ) \
    (bb_)->y1 = (y1_) ; \
  if ( (x2_) > (bb_)->x2 ) \
    (bb_)->x2 = (x2_) ; \
  if ( (y2_) > (bb_)->y2 ) \
    (bb_)->y2 = (y2_) ; \
MACRO_END

/** \brief Union with a single point (expands the bbox to include the point).

   This only works with non-empty bounding boxes because of the else tests. A
   similar effect can be had by using bbox_union_coordinates() and repeating
   the point if an empty bbox is possible. */
#define bbox_union_point(bb_, x_, y_) MACRO_START \
  HQASSERT(!bbox_is_empty(bb_), "Point expansion fails with empty bbox") ; \
  if ( (x_) < (bb_)->x1 ) \
    (bb_)->x1 = (x_) ; \
  else if ( (x_) > (bb_)->x2 ) \
    (bb_)->x2 = (x_) ; \
  if ( (y_) < (bb_)->y1 ) \
    (bb_)->y1 = (y_) ; \
  else if ( (y_) > (bb_)->y2 ) \
    (bb_)->y2 = (y_) ; \
MACRO_END

/** \brief Create intersection of two bounding boxes, storing the result in a
    third.

    The destination bounding box may be the same as the first or second bbox.
    If either bbox is empty, the result will be empty. */
#define bbox_intersection(bb1_, bb2_, dest_) MACRO_START \
  (dest_)->x1 = max((bb1_)->x1, (bb2_)->x1) ; \
  (dest_)->y1 = max((bb1_)->y1, (bb2_)->y1) ; \
  (dest_)->x2 = min((bb1_)->x2, (bb2_)->x2) ; \
  (dest_)->y2 = min((bb1_)->y2, (bb2_)->y2) ; \
MACRO_END

/** \brief Intersection with four coordinates, representing an unpacked
    bounding box. */
#define bbox_intersection_coordinates(bb_, x1_, y1_, x2_, y2_) MACRO_START \
  HQASSERT((x1_) <= (x2_), "X coordinates out of order") ; \
  HQASSERT((y1_) <= (y2_), "Y coordinates out of order") ; \
  if ( (x1_) > (bb_)->x1 ) \
    (bb_)->x1 = (x1_) ; \
  if ( (y1_) > (bb_)->y1 ) \
    (bb_)->y1 = (y1_) ; \
  if ( (x2_) < (bb_)->x2 ) \
    (bb_)->x2 = (x2_) ; \
  if ( (y2_) < (bb_)->y2 ) \
    (bb_)->y2 = (y2_) ; \
MACRO_END

/** \brief Offset a bounding box in x and y, storing the result in another.

    The destination bounding box may be the same as the first bbox. */
#define bbox_offset(bb_, dx_, dy_, dest_) MACRO_START \
  (dest_)->x1 = (bb_)->x1 + (dx_); \
  (dest_)->y1 = (bb_)->y1 + (dy_); \
  (dest_)->x2 = (bb_)->x2 + (dx_); \
  (dest_)->y2 = (bb_)->y2 + (dy_); \
MACRO_END

/** \brief Offset a bounding box in y, storing the result in another.

    The destination bounding box may be the same as the first bbox. */
#define bbox_offset_y(bb_, dy_, dest_) MACRO_START \
  (dest_)->x1 = (bb_)->x1; \
  (dest_)->y1 = (bb_)->y1 + (dy_); \
  (dest_)->x2 = (bb_)->x2; \
  (dest_)->y2 = (bb_)->y2 + (dy_); \
MACRO_END

/** \brief Clip an x interval to the box (NB: can result in x1_ > x2_). */
#define bbox_clip_x(bb_, x1_, x2_) MACRO_START \
  HQASSERT((x1_) <= (x2_), "X coordinates out of order") ; \
  if ( (x1_) < (bb_)->x1 ) \
    (x1_) = (bb_)->x1; \
  if ( (x2_) > (bb_)->x2 ) \
    (x2_) = (bb_)->x2; \
MACRO_END

/** \brief Clip a y interval to the box (NB: can result in y1_ > y2_). */
#define bbox_clip_y(bb_, y1_, y2_) MACRO_START \
  HQASSERT((y1_) <= (y2_), "Y coordinates out of order") ; \
  if ( (y1_) < (bb_)->y1 ) \
    (y1_) = (bb_)->y1; \
  if ( (y2_) > (bb_)->y2 ) \
    (y2_) = (bb_)->y2; \
MACRO_END

/**
 * \brief Is the bounding box already in the normal form?
 *
 * A degenerate bounding box is considered to be in normal form.
 */
#define bbox_is_normalised(bb_) \
  ((bb_)->x1 <= (bb_)->x2 && \
   (bb_)->y1 <= (bb_)->y2)

/** \brief Normalise a bounding box so that x1 < x2, putting the result in a
    second bbox.

    The destination bounding box may be the same as the first. The
    subtraction and adding trick to swap coordinates works even if the source
    and destination are the same, and works for both integer and real
    bboxes. */
#define bbox_normalise(bb_, dest_) MACRO_START \
  if ( (bb_)->x1 > (bb_)->x2 ) { \
    (dest_)->x1 = (bb_)->x1 - (bb_)->x2 ; /* x1-x2 */ \
    (dest_)->x2 = (bb_)->x2 + (dest_)->x1 ; /* x2+(x1-x2)=x1 */ \
    (dest_)->x1 = (dest_)->x2 - (dest_)->x1 ; /* x1-(x1-x2)=x2 */ \
  } \
  if ( (bb_)->y1 > (bb_)->y2 ) { \
    (dest_)->y1 = (bb_)->y1 - (bb_)->y2 ; /* y1-y2 */ \
    (dest_)->y2 = (bb_)->y2 + (dest_)->y1 ; /* y2+(y1-y2)=y1 */ \
    (dest_)->y1 = (dest_)->y2 - (dest_)->y1 ; /* y1-(y1-y2)=y2 */ \
  } \
MACRO_END

/** \brief Round bbox values to device coordinates.

   The destination bbox will usually be a \c dbbox_t, but could be an
   \c sbbox_t if desired, and can be the same as the source bbox. */
#define bbox_to_dcoord(bb_, dest_) MACRO_START \
  SC_C2D_INT((dest_)->x1, (bb_)->x1) ; \
  SC_C2D_INT((dest_)->y1, (bb_)->y1) ; \
  SC_C2D_INT((dest_)->x2, (bb_)->x2) ; \
  SC_C2D_INT((dest_)->y2, (bb_)->y2) ; \
MACRO_END

/* BBox predicates */

/** \brief Is the bounding box empty? */
#define bbox_is_empty(bb_) \
  ((bb_)->x1 > (bb_)->x2 || \
   (bb_)->y1 > (bb_)->y2)

/** \brief Does the bounding box have no area?

   (This is a slightly different test to testing for an empty bounding box.) */
#define bbox_is_degenerate(bb_) \
  ((bb_)->x1 == (bb_)->x2 || \
   (bb_)->y1 == (bb_)->y2)

/** \brief Are the bboxes the same? */
#define bbox_equal(bb1_, bb2_) \
  ((bb1_)->x1 == (bb2_)->x1 && \
   (bb1_)->y1 == (bb2_)->y1 && \
   (bb1_)->x2 == (bb2_)->x2 && \
   (bb1_)->y2 == (bb2_)->y2)

/** \brief Is the second bbox contained in the first (including equal
    bboxes)? */
#define bbox_contains(bb1_, bb2_) \
  ((bb1_)->x1 <= (bb2_)->x1 && \
   (bb1_)->y1 <= (bb2_)->y1 && \
   (bb1_)->x2 >= (bb2_)->x2 && \
   (bb1_)->y2 >= (bb2_)->y2)

/** \brief Is the second bbox contained or nearly contained in the first?

   This variant allows an epsilon for X and Y, so that nearly contained boxes
   are detected. */
#define bbox_contains_epsilon(bb1_, bb2_, ex_, ey_) \
  ((bb1_)->x1 - (bb2_)->x1 <= (ex_) && \
   (bb1_)->y1 - (bb2_)->y1 <= (ey_) && \
   (bb2_)->x2 - (bb1_)->x2 <= (ex_) && \
   (bb2_)->y2 - (bb1_)->y2 <= (ey_))

/** \brief Do the bboxes intersect (including degenerate intersections)? */
#define bbox_intersects(bb1_, bb2_) \
  ((bb1_)->x1 <= (bb2_)->x2 && \
   (bb1_)->x2 >= (bb2_)->x1 && \
   (bb1_)->y1 <= (bb2_)->y2 && \
   (bb1_)->y2 >= (bb2_)->y1)

/** \brief Do the bboxes intersect or nearly intersect?

   This variant allows an epsilon for X and Y, so that nearly intersected
   boxes are detected. */
#define bbox_intersects_epsilon(bb1_, bb2_, ex_, ey_) \
  ((bb1_)->x1 - (bb2_)->x2 <= (ex_) && \
   (bb2_)->x1 - (bb1_)->x2 <= (ex_) && \
   (bb1_)->y1 - (bb2_)->y2 <= (ey_) && \
   (bb2_)->y1 - (bb1_)->y2 <= (ey_))

/** \brief Does the bbox contain the given point? */
#define bbox_contains_point(bb_, x_, y_) \
  ((bb_)->x1 <= (x_) && \
   (bb_)->x2 >= (x_) && \
   (bb_)->y1 <= (y_) && \
   (bb_)->y2 >= (y_))

/** \brief Does the bbox intersect the given coordinates? */
#define bbox_intersects_coordinates(bb_, x1_, y1_, x2_, y2_) \
  ((bb_)->x1 <= (x2_) && \
   (bb_)->y1 <= (y2_) && \
   (bb_)->x2 >= (x1_) && \
   (bb_)->y2 >= (y1_))

/** \brief Does the bbox have at least a minimum size? */
#define bbox_has_min_size(bb_, dx_, dy_) \
  ((bb_)->x2 - (bb_)->x1 >= (dx_) && \
   (bb_)->y2 - (bb_)->y1 >= (dy_))

/** \brief Calculate the area of a bbox. */
#define bbox_area(bb_) \
  (((bb_)->x2 - (bb_)->x1 + 1 ) *        \
   ((bb_)->y2 - (bb_)->y1 + 1 ))

/*****************************************************************************/
/** \brief Rectangles.

   Rectangles represent a coordinate and a width and height, rather than the
   two coordinates that bboxes contain. */
typedef struct RECTANGLE {
  SYSTEMVALUE x , y , w , h ;
} RECTANGLE ;

/** \brief Convert a bounding box to a rectangle */
#define bbox_to_rectangle(bb_, rect_) MACRO_START \
  (rect_)->x = (bb_)->x1 ; \
  (rect_)->y = (bb_)->y1 ; \
  (rect_)->w = (bb_)->x2 - (bb_)->x1 ; \
  (rect_)->h = (bb_)->y2 - (bb_)->y1 ; \
MACRO_END

/** \brief Convert a rectangle to a bounding box */
#define rectangle_to_bbox(rect_, bb_) MACRO_START \
  (bb_)->x1 = (rect_)->x ; \
  (bb_)->y1 = (rect_)->y ; \
  (bb_)->x2 = (bb_)->x1 + (rect_)->w ; \
  (bb_)->y2 = (bb_)->y1 + (rect_)->h ; \
MACRO_END

/*****************************************************************************/
/** \brief Points.
 */

typedef struct IPOINT {
  int32 x, y;
} IPOINT;

typedef struct FPOINT {
  SYSTEMVALUE x, y;
} FPOINT;

typedef struct IPOINT IVECTOR;
typedef struct FPOINT FVECTOR;

#define SETXY(_v, _x, _y) MACRO_START \
  (_v).x = _x; \
  (_v).y = _y; \
MACRO_END

#define POINTS_EQUAL(_a, _b) ((_a).x == (_b).x && (_a).y == (_b).y)


/** \} */

#endif

/* Log stripped */
