#ifndef _HSIEHHASH32_H_
#define _HSIEHHASH32_H_

/** \file
 * \ingroup cstandard
 * \brief
 * Paul Hsieh's SuperFastHash implemented as inline functions for 32-bit
 * input datatypes.
 *
 * $HopeName: HQNc-standard!export:hsiehhash32.h(EBDSDK_P.1) $
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * This header file should ONLY be included by C files that use this hash
 * function. Header files should not include this, if the hash function is
 * not used by a source file that includes it, there will be warnings about
 * unused static functions.
 */

/** \brief Initialise the hash value for Paul Hsieh's super fast hash function.

    \param[out] hashp A pointer to the hash value to initialise.
    \param[in] nvalues The number of 32-bit values that will be used to create
       the hash key.

    To create a hash value, this function is called first, followed by
    \a nvalues iterations of \c hsieh_hash32_step(), and then finally
    \c hsieh_hash32_finish(). All of these functions are inlined in the caller.
*/
static inline void hsieh_hash32_init(/*@notnull@*/ /*@out@*/ uint32 *hashp,
                                     uint32 nvalues)
{
  /* Hash value is initialised to length in bytes. */
  *hashp = nvalues << 2 ;
}

/** \brief Perform one step in Paul Hsieh's super fast hash function, using a
    32-bit input value.

    \param[in,out] hashp A pointer to the hash value to combine the value with.
    This should have been initialised by calling \c hsieh_hash32_init().
    \param[in] value A 32-bit value to merge into the hash value.
 */
static inline void hsieh_hash32_step(/*@notnull@*/ /*@in@*/ /*@out@*/ uint32 *hashp,
                                     uint32 value)
{
  uint32 hash = *hashp, tmp ;

  /* Hsieh hash uses first 16-bit value. We use the low 16 bits of the
     supplied 32-bit value. */
  hash += (uint16)value ;
  /* Hsieh hash uses next 16-bit value shifted left by 11. We use the top 16
     bits of the supplied 32-bit value, shifting them right by 5 to get the
     same result. */
  tmp = ((value & 0xffff0000u) >> 5) ^ hash ;
  hash = (hash << 16) ^ tmp ;
  hash += hash >> 11 ;

  *hashp = hash ;
}

/** \brief Finish the hashing for Paul Hsieh's super fast hash function.
    \param[in,out] hashp A pointer to the hash value to finalise.
    This should have been initialised by calling \c hsieh_hash32_init(), and
    values should have been merged by calling \c hsieh_hash32_step().

    The returned hash value is 32 bits and if a smaller range is required it is
    up to the caller to do either a mod, for general hash table sizes, or a mask
    to size of the hash table minus one, for hash table sizes that are a power
    of two.

     eg.
       hash % size         For general hash sizes (slower).
     or
       hash & (size - 1)   For power of two hash sizes.

    It is recommended to make your hash table size a power of two.
 */
static inline void hsieh_hash32_finish(/*@notnull@*/ /*@in@*/ /*@out@*/ uint32 *hashp)
{
  uint32 hash = *hashp ;

  /* Force avalanching of final bits */
  hash ^= hash << 3 ;
  hash += hash >> 5 ;
  hash ^= hash << 4 ;
  hash += hash >> 17 ;
  hash ^= hash << 25 ;
  hash += hash >> 6 ;

  *hashp = hash;
}

#endif  /* _HSIEHHASH32_H_ */

