/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:objecth.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functional interface definitions for core RIP object subsystem.
 *
 * Handle/forward definitions CORE RIP object subsystem. If you don't need
 * access to the full object definitions or functions, and are just going to
 * pass pointers to these objects around, include this file instead of
 * objects.h.
 */

#ifndef __OBJECTH_H__
#define __OBJECTH_H__

#include "objectt.h"  /* Forward type declarations */

struct core_init_fns ; /* from SWcore */

/** \addtogroup objects */
/** \{ */

/*------------------------- MODULE FUNCTIONS ----------------------------*/

/** Initialise C globals */
void objects_C_globals(struct core_init_fns *fns) ;

/*------------------------- ERROR FUNCTIONS ----------------------------*/

/* Extended error handler functions. */
Bool errorinfo_error_handler(
                                int32 errorno ,
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ,
  /*@notnull@*/ /*@in@*/        const OBJECT *theval ) ;

Bool namedinfo_error_handler(
                                int32 errorno ,
                                int32 nameno ,
  /*@null@*/ /*@in@*/           const OBJECT *theval ) ;

/*------------------------- OBJECT FUNCTIONS ----------------------------*/

/** Comparison function for objects equality. Returns TRUE if objects are equal.
    For objects to be equal:
    - the type of the object must match exactly, so packed arrays are never
      equal to arrays.
    - the access permissions, globalness, PS VM flag, and save level, are all
      ignored in the comparison, but the executable flag is compared for those
      object types that can be meaningfully executed.
    - simple, non-valued objects are compared for type only.
    - simple, valued objects are also compared for value.
    - arrays and dictionaries are recursively compared element by element.
    - other composite objects only compare equal if they are the same object.
    - self-referential arrays and dictionaries will always return FALSE, whether
      the reference is direct or indirect,
    - either or both parameters may be null, in which case FALSE is returned. */
Bool compare_objects(
  /*@in@*/           const OBJECT *o1 ,
  /*@in@*/           const OBJECT *o2 ) ;

/** Sets \c *result to TRUE if objects \c o1 and \c o2 are equal
    according to the rules of the PS \c eq operator. The Boolean
    return value is TRUE if there has been no error, FALSE
    otherwise. */
Bool o1_eq_o2( const OBJECT *o1 , const OBJECT *o2 , Bool *result ) ;

/** Returns a flag indicating whether object \c *o1 is greater than \c
    *o2 according to the PS \c gt operator, more or less. For object
    types that the \c gt operator doesn't support, we simply compare
    object types. */
Bool o1_gt_o2( const OBJECT *o1 , const OBJECT *o2 ) ;

/** Reduce object's access to a lower level */
Bool object_access_reduce(
                          int32 new_acc ,
  /*@notnull@*/ /*@in@*/  register OBJECT *theo ) ;

/** Get the numeric value of an integer, real, or infinity object. This
    routine should only be used where the type of the object has already been
    checked (e.g. after a dictmatch). It will assert if an incorrect object
    type is passed to it. */
SYSTEMVALUE object_numeric_value(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ) ;

/** Get the real value of a numeric object returning a boolean to indicate
    success. The real value has sufficient precision to represent all integer
    values. If the object is non-numeric, return FALSE. */
Bool object_get_numeric(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ,
  /*@notnull@*/ /*@out@*/       SYSTEMVALUE *value ) ;

/** Get several numeric values out of an object array, into a numeric array. */
Bool object_get_numeric_array(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ,
  /*@notnull@*/ /*@out@*/       SYSTEMVALUE *value,
                                int32 len) ;

/** Get the USERVALUE value of a numeric object, returning a boolean to
    indicate success. This MAY LOSE PRECISION when an integer is provided in
    the object. Infinity objects are converted to return the largest possible
    real value. */
Bool object_get_real(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ,
  /*@notnull@*/ /*@out@*/       USERVALUE *value ) ;

/** Get the integer value of a numeric object, returning a boolean to indicate
    success. If the object is non-numeric, is not an integer, is a
    non-integral real number, or is outside of the range of an integer, return
    FALSE. Infinity objects are converted to return MAXINT. If you want the
    integral value of a fractional real, use object_get_numeric() or
    object_numeric_value() and convert to integer. */
Bool object_get_integer(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ,
  /*@notnull@*/ /*@out@*/       int32 *value ) ;

/** Get a bounding box from an array of four numeric values. The bounding box
    is normalised by sorting the higher and lower coordinates. */
Bool object_get_bbox(
  /*@notnull@*/ /*@in@*/        const OBJECT *object ,
  /*@notnull@*/ /*@out@*/       sbbox_t *bbox ) ;

/** Get an Extended Precision Real from an object known to be a real.
    Returns XPF_STANDARD if it is a simple real, XPF_EXTENDED if it
    is an XPF, or XPF_INVALID if badly formed. This can be used to
    generate Red Book compatible TYPECHECKs where we only allow integer
    or XPF, for example. */
enum {
  XPF_INVALID = 0, /* this must be first */
  XPF_STANDARD,
  XPF_EXTENDED
} ;
int object_get_XPF(const OBJECT *object, SYSTEMVALUE *value) ;

/** Store an Extended Precision Real into an OREAL object. */
void object_store_XPF(OBJECT *object, SYSTEMVALUE value) ;

/** XPF limits */
#define XPF_MINIMUM (16777217.0)
#define XPF_MAXIMUM (1099511562240.0)

/*------------------------- DICTIONARIES ----------------------------*/

/** Callback function type for dictionary walker. */
typedef Bool ( *WALK_DICTIONARY_FN )(
  /*@notnull@*/ /*@in@*/        OBJECT *key ,
  /*@notnull@*/ /*@in@*/        OBJECT *value ,
  /*@null@*/                    void *argBlockPtr ) ;

/** Dictionary walker: calls proc with every key and value in the dictionary.
    If the callback procedure returns FALSE it exits early, returning FALSE
    itself. It is the responsibility of the callback procedure to call the
    error handler, if desired. */
Bool walk_dictionary(
  /*@notnull@*/ /*@in@*/        const OBJECT *dicto,
  /*@notnull@*/                 WALK_DICTIONARY_FN proc ,
  /*@null@*/                    void *argBlockPtr ) ;

/** Does the same thing as walk_dictionary but adds a shim layer which
    pre-sorts the dictionary keys before walking. It should not be
    used if the dictionary is going to change at all during the
    walk. */
Bool walk_dictionary_sorted(
  /*@notnull@*/ /*@in@*/        const OBJECT *dicto,
  /*@notnull@*/                 WALK_DICTIONARY_FN proc ,
  /*@null@*/                    void *argBlockPtr ) ;

/** \brief Allocate a new dictionary from local or glocal memory, as
    appropriate.

    The dictionary is created as a literal, writable dictionary. The
    permissions of the dictionary may be downgraded using
    \c SET_DICT_ACCESS().

    \param[out] thedict The output dictionary object.

    \param lnth The maximum length of the dictionary. The dictionary may be
    extended in Level 2 or 3 PostScript.

    \retval TRUE if the dictionary was constructed.
    \retval FALSE if an allocation error occurred creating the dictionary.
 */
Bool ps_dictionary(/*@notnull@*/ /*@out@*/ OBJECT *thedict, int32 lnth) ;

/** \brief Initialise a newly-allocated dictionary.

    The dictionary is always initialised as a LITERAL, ODICTIONARY object.

    \param[out] thedict The output dictionary object.

    \param lnth The maximum length of the dictionary. The dictionary may be
    extended in Level 2 or 3 PostScript.

    \param tags2 The level of access to be given to the dictionary. This
    should be one of UNLIMITED, READ_ONLY or NO_ACCESS.

    \param[in] dictmem A block of object memory in which to store the dictionary
    contents. This block of memory should be of length:
    \code
    sizeof(OBJECT) * NDICTOBJECTS(lnth)
    \endcode

    \param mark The mark to apply to the dictionary. This includes the
    global allocation mode, the save level, and the VM type flag. This mark
    will *replace* the existing mark in the object memory, so it must be
    compatible with the object slot type of the dictionary memory.
 */
void init_dictionary(
  /*@notnull@*/ /*@out@*/       OBJECT *thedict ,
                                int32 lnth ,
                                uint8 tags2 ,
  /*@notnull@*/ /*@in@*/        OBJECT *dictmem,
                                int8 mark ) ;

/* Dictionary insertion, lookup and deletion. */

/** Bit set of flags controlling the behaviour of insert_hash_with_alloc. */
enum {
  INSERT_HASH_NORMAL = 0,      /**< Normal conditions for insertion. */
  INSERT_HASH_NAMED = 1,       /**< Key MUST be ONAME. */
  INSERT_HASH_DICT_ACCESS = 2, /**< Override dictionary writability. */
  INSERT_HASH_KEY_ACCESS = 4   /**< Override key readability. */
} ;

/**
 * Allocator for PSVM dictionary expansion.
 */
OBJECT * PSmem_alloc_func(int32 size, void * dummy);

/** Insert a value into a dictionary under any key type, passing an
    allocation function for dictionary extension, and flags to determine
    if the permissions are fully checked. */
Bool insert_hash_with_alloc(
  /*@notnull@*/ /*@in@*/        register OBJECT *thed ,
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ,
  /*@notnull@*/ /*@in@*/        const OBJECT *theo ,
                                uint32 flags,
  /*@notnull@*/ /*@only@*/      OBJECT *( *alloc_function )( int32 size ,
                                                             void * params ) ,
  /*@null@*/                    void *alloc_params ) ;

/** \brief Object allocator for dictionaries that should not be extended.

    When used with the (fast_)insert_hash_with_alloc functions, this function
    asserts that it should not have been called, and returns NULL. Use this
    function for dictionaries in C stack memory or other pools that should
    not be extended, or for PSVM dictionary insertion where the dictionary
    is guaranteed to have enough spare slots. */
OBJECT *no_dict_extension(int32 size, void *params) ;

/* Corresponding to hash insertion, the fast_extract_hash_... routines DO NOT
   check access permissions fully, and only take names as keys.
   fast_extract_hash_name takes a system name number (NAME_*) and looks it up
   without disturbing any of the standard objects (nnewobj etc). */

/** Extract a value from a dictionary, using any type as the key. */
/*@null@*/ /*@dependent@*/
OBJECT *extract_hash(
  /*@notnull@*/ /*@in@*/        const OBJECT *thed ,
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ) ;

/** Extract a value from a dictionary, using a name as the key. */
/*@null@*/ /*@dependent@*/
OBJECT *fast_extract_hash(
  /*@notnull@*/ /*@in@*/        const OBJECT *thed ,
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ) ;

/** Extract a value from a dictionary, using a pre-defined name as the key. */
/*@null@*/ /*@dependent@*/
OBJECT *fast_extract_hash_name(
  /*@notnull@*/ /*@in@*/        const OBJECT *thed ,
                                int32 namenum ) ;

/** Remove a key-value pair from a dictionary. */
Bool remove_hash(
  /*@notnull@*/ /*@in@*/        register OBJECT *thed ,
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ,
                                register Bool check_access ) ;

/*------------------------- NAME CACHING ----------------------------*/

/** Lookup a PostScript name, or create a new PostScript name if no such name
    already exists. */
/*@dependent@*/ NAMECACHE *cachename(
  /*@null@*/ /*@observer@*/     const uint8 *nm ,
                                uint32 ln ) ;

/** Lookup a PostScript name, returning NULL if it does not exist. */
/*@dependent@*/ NAMECACHE *lookupname(
  /*@null@*/ /*@observer@*/     const uint8 *nm ,
                                uint32 ln ) ;

/** Lookup an interned string in the name cache, or create a new interned
    string if no such string already exists. The only difference between this
    and cachename is the length of the name it will accept; this function
    takes strings up to 65535 bytes long. */
/*@dependent@*/ NAMECACHE *cachelongname(
  /*@null@*/ /*@observer@*/     const uint8 *nm ,
                                uint32 ln ) ;

/** Lookup an interned string in the name cache, returning NULL if it does
    not exist. The only difference between this and lookupname is the length
    of the name it will accept; this function takes strings up to 65535 bytes
    long. */
/*@dependent@*/ NAMECACHE *lookuplongname(
  /*@null@*/ /*@observer@*/     const uint8 *nm ,
                                uint32 ln ) ;

/** Purge the name cache for a particular save level. */
void purge_ncache( int32 slevel );

void ncache_purge_finalize(
  /*@notnull@*/ /*@in@*/        NAMECACHE *obj ) ;

/** Restore name->dictval ptr, if it points to extension being discarded. */
void ncache_restore_prepare(
  /*@notnull@*/ /*@in@*/    NAMECACHE *name,
  /*@notnull@*/ /*@in@*/    OBJECT* slot,
  /*@notnull@*/ /*@in@*/    OBJECT *base,
  /*@notnull@*/ /*@in@*/    OBJECT *currext,
                            int32 slevel);

/*------------------------- SAVE/RESTORE ----------------------------*/

/** Store object subsystem information for save into the structure provided. */
void objects_save_commit(
  /*@notnull@*/ /*@in@*/        corecontext_t *context,
  /*@notnull@*/ /*@in@*/        OBJECTSAVE *savehere ) ;

/** Check that a restore from the object subsystem information in the
    structure provided is valid. */
void objects_restore_prepare(
  /*@notnull@*/ /*@in@*/        OBJECTSAVE *restorefrom ) ;

/** Restore object subsystem information from the structure provided. */
void objects_restore_commit(
  /*@notnull@*/ /*@out@*/       corecontext_t *context,
  /*@notnull@*/ /*@in@*/        OBJECTSAVE *restorefrom ) ;

/** \brief Initialise a slot as non-PSVM memory.

    \param[out] slot The object slot to initialise.

    \return The \a slot parameter is returned, so this routine can be chained
    with an object store routine.
 */
/*@notnull@*/ OBJECT *object_slot_notvm(/*@notnull@*/ OBJECT *slot) ;

/** Store a null object into a slot. The slot properties are not modified. */
void object_store_null(/*@notnull@*/ OBJECT *slot) ;

/** Store an infinity object into a slot. The slot properties are not
    modified. */
void object_store_infinity(/*@notnull@*/ OBJECT *slot) ;

/** Store a boolean object into a slot. The slot properties are not
    modified. */
void object_store_bool(/*@notnull@*/ OBJECT *slot, Bool value) ;

/** Store a real object into a slot. The slot properties are not modified. */
void object_store_real(/*@notnull@*/ OBJECT *slot, USERVALUE value) ;

/** Store an integer object into a slot. The slot properties are not
    modified. */
void object_store_integer(/*@notnull@*/ OBJECT *slot, int32 value) ;

/** Store a name object into a slot. The slot properties are not modified.*/
void object_store_name(/*@notnull@*/ OBJECT *slot, int32 name_id, uint8 litexec) ;

/** Store a name object into a slot. The slot properties are not modified.*/
void object_store_namecache(OBJECT *slot, NAMECACHE* name, uint8 litexec) ;

/** Store a constant string object into a slot. The slot properties are not
    modified. The string is made READ_ONLY. These strings should not be
    stored permanently in PostScript dictionaries. */
void object_store_string(/*@notnull@*/ OBJECT *slot,
                         uint8 *str, uint16 length,
                         uint8 litexec) ;

/** Store an operator object into a slot. The slot properties are not
    modified.*/
void object_store_operator(/*@notnull@*/ OBJECT *slot, int32 name_id) ;

/** Store an integer, real, or infinity object into a slot, the type
    depending on range and precision of the input. The slot properties are
    not modified. */
void object_store_numeric(/*@notnull@*/ OBJECT *slot, SYSTEMVALUE value) ;


/** Create an array object using the current allocation mode. The array is
    initialised to contain null objects at the current savelevel.

    \param[out] theo Object into which the new array will be put.
    \param arraysize Size of new PS array.
    \return TRUE on success, FALSE on failure. The routine may fail if the
      allocation cannot be made, or the length is too long for an array.
*/
Bool ps_array(OBJECT *theo, int32 arraysize);

/** Create the intermediate long array trampoline to point to the actual array
    for OLONGARRAY and OLONGPACKEDARRAY.

    \param oline     The start of the long array.
    \param length    The length of the long array (must be >PAXPSARRAY).
    \return Ptr to the length OINTEGER of the trampoline, or NULL for VMERROR
*/
OBJECT * extended_array(OBJECT * olist, int32 length);

/** Create a string object using the current allocation mode, and
    initialise it from the supplied data.

    \param[out] theo Object into which the new string will be put.
    \param[in] string Data to initialise the string with. If this is NULL, the
      string is zeroed.
    \param length Length of new PS string.
    \return TRUE on success, FALSE on failure. The routine may fail if the
      allocation cannot be made, or the length is too long for a string.
*/
Bool ps_string(/*@notnull@*/ /*@out@*/ OBJECT *theo,
               /*@null@*/ const uint8 *string, int32 length);

/** Create a longstring object using the current allocation mode, and
    initialise it from the supplied data.

    \param theo Object into which the new longstring will be put.
    \param string Data to initialise the longstring with. If this is NULL, the
      longstring is zeroed.
    \param length Length of new PS string.
    \return TRUE on success, FALSE on failure. The routine may fail if the
      allocation cannot be made.
*/
Bool ps_longstring(/*@notnull@*/ /*@out@*/ OBJECT *theo,
                   /*@null@*/ const uint8 *string, int32 length);

/** Returns a string which will be either a long or a normal length string,
    as required by the passed 'length'.
*/
Bool ps_long_or_normal_string(/*@notnull@*/ /*@out@*/ OBJECT *object,
                              /*@null@*/ const uint8 *string, int32 length);

/** Copy the contents of an array into another, checking for saving, local
    into global, etc.

    \param from Array object from which elements will be copied into the output
      object.
    \param to Array object into which elements will be copied.
    \return TRUE on success, FALSE on failure. The routine may fail if the
      source array is longer than the destination array, if there is a global
      into local conflict, or the destination array needs saved and the
      allocation fails.
*/
Bool ps_array_copy(/*@notnull@*/ /*@in@*/ OBJECT *from,
                   /*@notnull@*/ /*@in@*/ OBJECT *to);

/** Extract a string or longstring object from a string or longstring. If
    memory is allocated for a longstring interval, it will share the same
    allocation mode as the original string.

    \param out Object into which a new string or longstring will be put,
      sharing the string memory allocation of the original string.
    \param in String or longstring from which a subsequence of will be
      selected.
    \param start Start of string interval.
    \param length Length of string inerval.
    \return TRUE on success, FALSE on failure. The routine may fail if the
      subsequence is out of range of the original string, or if a longstring
      subsequence is selected and memory for the header cannot be allocated.
*/
Bool ps_string_interval(/*@notnull@*/ /*@out@*/ OBJECT *out,
                        /*@notnull@*/ /*@in@*/ const OBJECT *in,
                        int32 start, int32 length);


/*------------------------- IMPORTED DEFINITIONS ----------------------------*/

/** The extended error handlers require a function implementation provided for
    the following definition. This is provided by another compound (SWv20 in
    the SWcore implementation). */
Bool object_error_info(
  /*@notnull@*/ /*@in@*/        const OBJECT *thekey ,
  /*@notnull@*/ /*@in@*/        const OBJECT *theval ) ;

/** This routine is imported from another compound (SWv20 in the SWcore
    implementation). It returns a TRUE value if dictionary and array access
    permissions are to be ignored. */
Bool object_access_override(
  /*@null@*/ /*@in@*/          const OBJECT *theo ) ;

/** This routine is imported from another compound (SWv20 in the SWcore
    implementation). It returns a TRUE value if dictionary extension is
    permitted by *_insert_hash. */
Bool object_extend_dict(
  /*@null@*/ /*@in@*/          const corecontext_t *context,
  /*@null@*/ /*@in@*/          const OBJECT *theo,
                               Bool PSmem_allocator) ;

/** This routine is imported from another compound (SWv20 in the SWcore
    implementation). It alters the current global/local allocation mode and
    returns the previous allocation mode. */
Bool setglallocmode(corecontext_t *context, Bool glmode) ;

/** \brief Check if a subsequence of objects in an array need saving, because
    they will be modified at this savelevel.

    \param optr Start of a sequence of contiguous objects to check for saving.
    \param size The number of contiguous objects to check for saving.
    \param glmode The global mode of the array object containing the \c optr
      object sequence.
    \param corecontext The corecontext pointer.
    \return TRUE if the function succeeds, FALSE if the function fails (this
      may happen if save memory cannot be allocated).
*/
Bool check_asave(/*@notnull@*/ OBJECT *optr, int32 size, int32 glmode,
                 /*@notnull@*/ corecontext_t *corecontext);

/** \brief Check if an object in an array needs saving because they will be
    modified at this savelevel. A number of neighbouring array slots may also
    be saved if they have not been already. This call is much more efficient
    than check_asave when changing a single slot in a large array.

    \param optr Start of a sequence of contiguous objects.
    \param size The total number of contiguous objects.
    \param index The index of the object that is about to be modified.
    \param glmode The global mode of the array object containing the \c optr
      object sequence.
    \param corecontext The corecontext pointer.
    \return TRUE if the function succeeds, FALSE if the function fails (this
      may happen if save memory cannot be allocated).
*/
Bool check_asave_one(/*@notnull@*/ OBJECT *optr, int32 size, int32 index,
                     int32 glmode, /*@notnull@*/ corecontext_t *corecontext);

/** \brief Check if a dictionary, or dictionary extension needs saving, because
    it will be modified at this savelevel.

    \param optr The dictionary sub-object or extension object (note, this is
      not the top-level dictionary object).
    \param corecontext The corecontext pointer, or NULL if not known.
    \return TRUE if the function succeeds, FALSE if the function fails (this
      may happen if save memory cannot be allocated).
*/
Bool check_dsave(/*@notnull@*/ OBJECT *optr, corecontext_t *corecontext);

/** \} */

/*
Log stripped */
#endif /* Protection from multiple inclusion */
