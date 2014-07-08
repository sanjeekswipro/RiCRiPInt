/** \file
 * \ingroup hqbits
 *
 * $HopeName: HQNc-standard!export:hqbitvector.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Macros implementing bit vectors.
 */

#ifndef __HQBITVECTOR_H__
#define __HQBITVECTOR_H__

/** \brief An unsigned type used to implement bit vectors. */
typedef uint32 bitvector_element_t ;

/** \brief Right shift to convert bit indices to element indices.

    The relationship:

      (1 << (BITVECTOR_SHIFT_ELEMENTS - 3)) == sizeof(bitvector_element_t)

    should always hold. */
#define BITVECTOR_SHIFT_ELEMENTS (3 + 2)

/** \brief Bit vector element size in bits. */
#define BITVECTOR_ELEMENT_BITS (1u << BITVECTOR_SHIFT_ELEMENTS)

/** \brief Mask to extract bit index within element from bit index. */
#define BITVECTOR_ELEMENT_MASK (BITVECTOR_ELEMENT_BITS - 1u)

/** \brief Bitvector element containing all zeros. */
#define BITVECTOR_ELEMENT_ZEROS (bitvector_element_t)0

/** \brief Bitvector element containing all ones. */
#define BITVECTOR_ELEMENT_ONES (bitvector_element_t)(~BITVECTOR_ELEMENT_ZEROS)

/** \brief Size of a bitvector containing \c length_ bits, in elements.

    \param length_ Length of the bitvector, in bits.
*/
#define BITVECTOR_SIZE_ELEMENTS(length_) \
  (((length_) + BITVECTOR_ELEMENT_BITS - 1u) >> BITVECTOR_SHIFT_ELEMENTS)

/** \brief Size of a bitvector containing \c length_ bits, in bytes.

    \param length_ Length of the bitvector, in bits.
*/
#define BITVECTOR_SIZE_BYTES(length_) \
  (BITVECTOR_SIZE_ELEMENTS(length_) << 2)

/** \brief Declare a bitvector variable or field.

    \param name_ The name of the bitvector name or field.
    \param size_ A constant expression, giving the number of bits in the vector.

    This macro can be used anywhere that a variable or parameter declaration
    can occur.
*/
#define bitvector_t(name_, size_) \
  bitvector_element_t name_[BITVECTOR_SIZE_ELEMENTS(size_)]


/** \brief Set all of the bits in a bit vector to a value.

    \param vec_   The bitvector to clear.
    \param size_  The number of bits in the vector.
    \param value_ This should be \c BITVECTOR_ELEMENT_ZEROS to clear the
                  vector, \c BITVECTOR_ELEMENT_ONES to set the vector.
*/
#define BITVECTOR_SET_ELEMENTS(vec_, size_, value_) MACRO_START   \
  unsigned int _i_ = BITVECTOR_SIZE_ELEMENTS(size_) ; \
  HQASSERT(_i_ > 0, "Bitvector has no elements") ;    \
  do {                                                \
    (vec_)[--_i_] = (value_) ;                        \
  } while ( _i_ > 0 ) ;                               \
MACRO_END

/** \brief Copy the contents of one bit vector to another, with an optional
    exclusive-or operation.

    \param dest_ The destination bitvector.
    \param src_  The source bitvector.
    \param size_ The number of bits in the vector.
    \param flip_ A boolean, indicating if the destination should be the inverse
                 of the source.
*/
#define BITVECTOR_COPY_FLIP(dest_, src_, size_, flip_) MACRO_START \
  unsigned int _i_ = BITVECTOR_SIZE_ELEMENTS(size_) ; \
  HQASSERT(_i_ > 0, "Bitvector has no elements") ;    \
  do {                                                \
    --_i_ ;                                           \
    (dest_)[_i_] = (src_)[_i_] ^ ((bitvector_element_t)!(flip_) - 1u) ;  \
  } while ( _i_ > 0 ) ;                               \
MACRO_END


/** \brief Clear a single bit in a bit vector.

    \param vec_  The bitvector to modify.
    \param bit_  The bit index to clear.
*/
#define BITVECTOR_CLEAR(vec_, bit_) \
  (vec_)[(bit_) >> BITVECTOR_SHIFT_ELEMENTS] &= ~((bitvector_element_t)1 << (BITVECTOR_ELEMENT_MASK & (bit_)))

/** \brief Set a single bit in a bit vector.

    \param vec_  The bitvector to modify.
    \param bit_  The bit index to set.
*/
#define BITVECTOR_SET(vec_, bit_) \
  (vec_)[(bit_) >> BITVECTOR_SHIFT_ELEMENTS] |= ((bitvector_element_t)1 << (BITVECTOR_ELEMENT_MASK & (bit_)))

/** \brief Test if a bit in a bit vector is set.

    \param vec_  The bitvector to test.
    \param bit_  The bit index to test.
*/
#define BITVECTOR_IS_SET(vec_, bit_) \
  (((vec_)[(bit_) >> BITVECTOR_SHIFT_ELEMENTS] & ((bitvector_element_t)1 << (BITVECTOR_ELEMENT_MASK & (bit_)))) != 0)

/** \brief Return a mask which can be used to test and isolate a bit in an
   identified element.

   \param element_ The element index being processed.
   \param bit_     The bit index to derive a mask for.

   The return value is a mask that is zero if \c bit is not in \c element, or
   set to the bit mask within \c element if it is.

   This can be used to implement bulk operations in clients, without having
   to test if particular bits that should be treated specially are present in
   each element. */
#define BITVECTOR_ELEMENT_BIT_MASK(element_, bit_) \
  (((element_) == ((bit_) >> BITVECTOR_SHIFT_ELEMENTS)) << ((bit_) & BITVECTOR_ELEMENT_MASK))


/** \brief Iterator structure for walking bit vectors.

    This structure is transparent; the element index and the mask can be used
    by the client to extract and manipulate words from the bitvector
    directly. */
typedef struct bitvector_iterator_t {
  int element ;              /**< Index of element. Note this must be signed. */
  unsigned int bit ;         /**< Index of bit (top bit if element iteration). */
  bitvector_element_t mask ; /**< Mask to extract bit(s) in iterator. */
} bitvector_iterator_t ;


/** \brief Initialise per-bit iteration over a bit vector using an element,
   mask pair.

   \param iterator_ A \c bitvector_iterator_t variable to initialise.
   \param size_    The size of the bit vector.

   This macro is written such that it can be used in a for-loop
   initialisation statement. Note that the iteration goes from the highest to
   lowest bits. The iterator will be initialised correctly for bitvectors of
   size zero or more. The idiom will usually look like this:

   \code
   bitvector_iterator_t iterator ;
   bitvector_t(vector, MAXBITS) ;

   for ( BITVECTOR_ITERATE_BITS(iterator, bits_used) ;
         BITVECTOR_ITERATE_BITS_MORE(iterator) ;
         BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
     if ( (vector[iterator.element] & iterator.mask) != 0 ) {
       ...bit iterator.bit is set...
     }
   }
   \endcode

   \see BITVECTOR_ITERATE_BITS_MORE, BITVECTOR_ITERATE_BITS_NEXT
*/
#define BITVECTOR_ITERATE_BITS(iterator_, size_) \
  (iterator_).bit = (size_) - 1, \
    (iterator_).element = (int)BITVECTOR_SIZE_ELEMENTS(size_) - 1, \
    (iterator_).mask = ((bitvector_element_t)1 << ((iterator_).bit & BITVECTOR_ELEMENT_MASK))

/** \brief Test if per-bit iteration over a bitvector using an element, mask
   pair is complete.

   \param iterator_ The \c bitvector_iterator_t variable to test.

   This macro is written such that it can be used in a for-loop condition
   statement. Note that the iteration goes from the highest to lowest bits.

   \see BITVECTOR_ITERATE_BITS, BITVECTOR_ITERATE_BITS_NEXT
*/
#define BITVECTOR_ITERATE_BITS_MORE(iterator_) ((iterator_).element >= 0)

/** \brief Continue per-bit iteration over a bit vector using an element, mask
   pair.

   \param iterator_ The \c bitvector_iterator_t variable to step.

   This macro is written such that it can be used in a for-loop continuation
   statement. Note that the iteration goes from the highest to lowest bits.

   \see BITVECTOR_ITERATE_BITS, BITVECTOR_ITERATE_BITS_MORE
*/
#define BITVECTOR_ITERATE_BITS_NEXT(iterator_) \
  (iterator_).bit -= 1, \
    (iterator_).element -= (int)((iterator_).mask & 1), \
    (iterator_).mask = ((iterator_).mask >> 1) | ((iterator_).mask << (BITVECTOR_ELEMENT_BITS - 1u))


/** \brief Initialise multi-bit iteration over a bit vector using an element,
   mask pair.

   \param iterator_ A \c bitvector_iterator_t variable to initialise.
   \param size_    The size of the bit vector.
   \param chunk_   The number of bits to iterate at a time. This should be
                   a power of 2.

   This macro is written such that it can be used in a for-loop
   initialisation statement. Note that the iteration goes from the highest to
   lowest bits. The iterator will be initialised correctly for bitvectors of
   size zero or more. The idiom will usually look like this:

   \code
   bitvector_iterator_t iterator ;
   bitvector_t(vector, MAXLEN * 2) ;

   for ( BITVECTOR_ITERATE_BITS_N(iterator, bits_used, 2) ;
         BITVECTOR_ITERATE_BITS_MORE(iterator) ;
         BITVECTOR_ITERATE_BITS_NEXT_N(iterator, 2) ) {
     int chunk = ((vector[iterator.element] & iterator.mask) >> iterator.bit) ;
     ...
   }
   \endcode

   \see BITVECTOR_ITERATE_BITS_MORE, BITVECTOR_ITERATE_BITS_NEXT_N
*/
#define BITVECTOR_ITERATE_BITS_N(iterator_, size_, chunk_) \
  HQASSERT_EXPR(((chunk_) & ((chunk_) - 1)) == 0 && (size_) % (chunk_) == 0, \
    "Iteration chunk or size mismatch", \
    ((iterator_).bit = (size_) - (chunk_), \
     (iterator_).element = (int)BITVECTOR_SIZE_ELEMENTS(size_) - 1, \
     (iterator_).mask = ((bitvector_element_t)((1 << (chunk_)) - 1) << ((iterator_).bit & BITVECTOR_ELEMENT_MASK))))

/** \brief Continue multi-bit iteration over a bit vector using an element,
   mask pair.

   \param iterator_ The \c bitvector_iterator_t variable to step.
   \param chunk_   The number of bits to iterate at a time. This should be
                   a power of 2.

   This macro is written such that it can be used in a for-loop continuation
   statement. Note that the iteration goes from the highest to lowest bits.

   \see BITVECTOR_ITERATE_BITS_MORE, BITVECTOR_ITERATE_BITS_N
*/
#define BITVECTOR_ITERATE_BITS_NEXT_N(iterator_, chunk_) \
  HQASSERT_EXPR(((chunk_) & ((chunk_) - 1)) == 0 && (iterator_).bit % (chunk_) == 0, \
    "Iteration chunk or size mismatch", \
    ((iterator_).bit -= (chunk_), \
     (iterator_).element -= (int)((iterator_).mask & 1), \
     (iterator_).mask = (((iterator_).mask >> (chunk_)) | \
                         ((iterator_).mask << (BITVECTOR_ELEMENT_BITS - (chunk_))))))


/** \brief Initialise per-element iteration over a bit vector using an
   element, mask pair.

   \param iterator_ The \c bitvector_iterator_t variable to initialise.
   \param size_    The size of the bit vector.

   This macro is written such that it can be used in a for-loop
   initialisation statement. Note that the iteration goes from the highest to
   lowest element. The iterator will be initialised correctly for bitvectors of
   size zero or more. The iteration idiom will usually look like this:

   \code
   bitvector_iterator_t iterator ;
   bitvector_t(vector, MAXBITS) ;

   for ( BITVECTOR_ITERATE_ELEMENTS(iterator, bits_used) ;
         BITVECTOR_ITERATE_ELEMENTS_MORE(iterator) ;
         BITVECTOR_ITERATE_ELEMENTS_NEXT(iterator) ) {
     if ( (vector[iterator.element] & iterator.mask) == iterator.mask ) {
       ...all bits set...
     } else if ( (vector[iterator.element] & iterator.mask) == 0 ) {
       ...no bits set...
     } ...etc...
   }
   \endcode

   \see BITVECTOR_ITERATE_ELEMENTS_MORE, BITVECTOR_ITERATE_ELEMENTS_NEXT
*/
#define BITVECTOR_ITERATE_ELEMENTS(iterator_, size_) \
  (iterator_).bit = ((size_) - 1), \
    (iterator_).element = (int)BITVECTOR_SIZE_ELEMENTS(size_) - 1, \
    (iterator_).mask = (BITVECTOR_ELEMENT_ONES >> ((BITVECTOR_ELEMENT_BITS - (size_)) & BITVECTOR_ELEMENT_MASK))

/** \brief Test if per-element iteration over a bitvector using an element,
   mask pair is complete.

   \param iterator_ The \c bitvector_iterator_t variable to test.

   The bits index to test. This macro is written such that it can be used in
   a for-loop condition statement. Note that the iteration goes from the
   highest to lowest element.

   \see BITVECTOR_ITERATE_ELEMENTS, BITVECTOR_ITERATE_ELEMENTS_NEXT
*/
#define BITVECTOR_ITERATE_ELEMENTS_MORE(iterator_) ((iterator_).element >= 0)

/** \brief Continue per-element iteration over a bit vector using an element,
   mask pair.

   \param iterator_ The \c bitvector_iterator_t variable to step.

   This macro is written such that it can be used in a for-loop continuation
   statement. Note that the iteration goes from the highest to lowest
   element.

   \see BITVECTOR_ITERATE_ELEMENTS, BITVECTOR_ITERATE_ELEMENTS_MORE
*/
#define BITVECTOR_ITERATE_ELEMENTS_NEXT(iterator_) \
  (iterator_).bit = ((iterator_).bit & ~BITVECTOR_ELEMENT_MASK) - 1, \
  (iterator_).element -= 1, (iterator_).mask = BITVECTOR_ELEMENT_ONES


#endif /* __BITVECTOR_H__ */

