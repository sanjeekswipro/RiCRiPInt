/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:lists.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements double and single linked list manipulation functionality via
 * macros.
 *
 * List links are embedded in the structure that is an element of the list
 * rather than have the links in a separate structure with a pointer to the list
 * element.  This means no additional memory management is needed to include a
 * structure in a list.
 *
 * Multiple list links can exist in list element structure allowing the
 * structure to exist in many lists at the same time.
 *
 * Lists are maintained via a list structure that always point to the start and
 * end of the list.  This makes it very easy to implement lists with semantics
 * like FIFO, LIFO, MRU, etc.  Another benefit is quicker list insertion and
 * removal, less so for single linked lists for the usual reason of not having a
 * previous pointer.  This leaves it up to the developer to decide between
 * memory usage and code overhead when deciding which list type to use.
 *
 * The set of types and macros for each list type is similar to each other, the
 * difference being down to the lack of the prev pointer in single link lists.
 * See the macro definition for the arguments required.
 *
 * List and list link types:
 * -# 1-way linked lists -
 *   - sll_list_t list structure.
 *   - sll_link_t list link structure.
 * -# 2-way linked lists -
 *   - dll_list_t list structure.
 *   - dll_link_t list link structure.
 *
 * List structures are abbreviated to sls and dls, link structures are
 * abbreviated to sll and dll.
 *
 * -# Initialisation
 *   - SLL_RESET_LIST()
 *   - SLL_RESET_LINK()
 *   - DLL_RESET_LIST()
 *   - DLL_RESET_LINK()
 * -# Insertion
 *   - SLL_ADD_HEAD()
 *   - SLL_ADD_TAIL()
 *   - SLL_ADD_AFTER()
 *   - DLL_ADD_HEAD()
 *   - DLL_ADD_TAIL()
 *   - DLL_ADD_BEFORE()
 *   - DLL_ADD_AFTER()
 * -# Retrieval
 *   - SLL_GET_HEAD()
 *   - SLL_GET_TAIL()
 *   - SLL_GET_NEXT()
 *   - SLL_GET_NEXT_CIRCULAR()
 *   - DLL_GET_HEAD()
 *   - DLL_GET_TAIL()
 *   - DLL_GET_NEXT()
 *   - DLL_GET_PREV()
 *   - DLL_GET_NEXT_CIRCULAR()
 *   - DLL_GET_PREV_CIRCULAR()
 * -# Removal
 *   - SLL_REMOVE_HEAD()
 *   - SLL_REMOVE_NEXT()
 *   - DLL_REMOVE()
 * -# Joins
 *   - SLL_LIST_APPEND()
 *   - DLL_LIST_APPEND()
 *   - DLL_LINK_SPLICE()
 * -# Tests
 *   - SLL_IN_LIST()
 *   - SLL_LAST_IN_LIST()
 *   - SLL_LIST_IS_EMPTY()
 *   - DLL_IN_LIST()
 *   - DLL_LAST_IN_LIST()
 *   - DLL_FIRST_IN_LIST()
 *   - DLL_LIST_IS_EMPTY()
 *
 * For a structure to be part of a list, a ..._link_t structure is added as a
 * member.  The list is manipulated through a ..._list_t structure declared
 * either as a global or part of another structure.  Before a list can be used
 * the ..._RESET_LIST() macro must be called.  The ..._RESET_LINK() macro clears
 * the list element pointers to allow some assertions to be done on lists status
 * in other macros.  Example code:
 *
 * \code
 * typedef struct MY_ELEMENT {
 *   ...
 *   sll_link_t   list_link;
 *   ...
 * } MY_ELEMENT;
 *
 * sll_list_t element_list;
 *
 * void initialise(..)
 * {
 *   ...
 *   SLL_RESET_LIST(&element_list);
 *   ...
 * }
 *
 * void new_element(...)
 * {
 *   MY_ELEMENT* p_element = malloc(sizeof(MY_ELEMENT));
 *   ...
 *   SLL_RESET_LINK(p_element, list_link);
 *   ...
 * }
 * \endcode
 *
 * Most list manipulations are done relative to an existing list element - add
 * before or after, get next or previous, etc.  Since a single link list only
 * looks forward, insertions and retrievals are for elements after the current
 * one, and also need the main list in order to maintain the tail pointer.
 * There are macros to add list elements at the head or tail of a list without
 * having an existing link.  Example code:
 *
 * \code
 * void insert_element(MY_ELEMENT* p_element);
 * {
 *   MY_ELEMENT* p_existing;
 *   ...
 *   SLL_ADD_AFTER(&element_list, p_existing, p_element, list_link);
 *   ...
 * }
 * \endcode
 *
 * List element removal is done for the current list element for double linked
 * lists, but for relative list elements for single linked lists.  You must
 * already have a pointer to the list element to be removed or you will get a
 * memory leak!  In order to be able to remove the first element of a single
 * linked list there is SLL_REMOVE_HEAD(), for other elements there is
 * SLL_REMOVE_NEXT().  Example code:
 *
 * \code
 * void delete_element(MY_ELEMENT* p_element)
 * {
 *   MY_ELEMENT* p_previous;
 *   ...
 *   if ( p_element == SLL_GET_NEXT(p_previous, MY_ELEMENT, list_link) ) {
 *     SLL_REMOVE_NEXT(p_previous, list_link);
 *   }
 *   ...
 * }
 * \endcode
 *
 * The list join macros allow for quick merging of two existing lists into one,
 * rather than removing from one and adding to the other.  Afterwards, the
 * source list is empty.  Example code:
 *
 * \code
 * sll_list_t free_list;
 * sll_list_t in_use_list;
 *
 * void make_free(void)
 * {
 *   ...
 *   SLL_LIST_APPEND(&free_list, &in_use_list);
 *   ...
 * }
 * \endcode
 *
 * The list tests provide some quick list tests. ..._IN_LIST() does no more than
 * check that the list element link pointers do not point to the element itself
 * (as set by ..._RESET_LINK()), not if an element is in a particular list.
 * Example code:
 *
 * \code
 * void walk_elements(void)
 * {
 *   MY_ELEMENT* p_element;
 *   ...
 *   if ( !LIST_IS_EMPTY(&element_list) ) {
 *     p_element = SLL_GET_HEAD(&element_list, MY_ELEMENT, list_link);
 *     while ( !SLL_LAST_IN_LIST(p_element, list_link) ) {
 *       ...
 *       p_element = SLL_GET_NEXT(p_element, MY_ELEMENT, list_link);
 *     }
 *   }
 *   ...
 * }
 * \endcode
 *
 * The macros are written in terms of themselves where possible to provide more
 * examples of their use.
 *
 * Of course, the big plus is that all the sequence sensitive pointer updates
 * when manipulating list pointers is done for you!
 */

#ifndef __LISTS_H__
#define __LISTS_H__

#include <stddef.h>

/*
 * _LL_ENTRY() converts a link structure pointer to the containing
 * structure pointer.
 */
/*@notfunction@*/
#define _LL_ENTRY(pll, type, ll)   (type*)((char*)(pll) - offsetof(type, ll))

/* --------------- Double Linked Lists --------------- */

/**
 * \brief Structure for a 2-way linked list entry.
 *
 * The pointers in this structure point to the instance of this structure in the
 * next entry in the list, not to the start of the entry.
 */
typedef struct dll_link_t {
/*@only@*/ /*@notnull@*/
  struct dll_link_t*  pdllNext;   /**< Pointer to the next element link in the list. */
/*@only@*/ /*@notnull@*/
  struct dll_link_t*  pdllPrev;   /**< Pointer to the to the previous element link in the list. */
} dll_link_t;


/**
 * \brief Structure representing the 2-way linked list.
 *
 * By having head and tail link entries, insertion and deletion times are
 * constant as there is no need to do NULL pointer special case handling.
 */
typedef struct dll_list_t {
  dll_link_t  dllHead;            /**< Links to first entry in list. */
  dll_link_t  dllTail;            /**< Links to last entry in list. */
} dll_list_t;

/** Check if a doubly-linked list has been initialised correctly (the head and
 * tail pointers are not NULL, and the sentinels point to the correct places).
 */
/*@notfunction@*/
#define _DLL_LIST_INITIALISED(pdls)   ((pdls) != NULL && \
                                       (pdls)->dllHead.pdllPrev == &(pdls)->dllHead && \
                                       (pdls)->dllHead.pdllNext != NULL && \
                                       (pdls)->dllHead.pdllNext->pdllPrev == &(pdls)->dllHead && \
                                       (pdls)->dllTail.pdllPrev != NULL && \
                                       (pdls)->dllTail.pdllPrev->pdllNext == &(pdls)->dllTail && \
                                       (pdls)->dllTail.pdllNext == &(pdls)->dllTail)

/** Check if the link next or previous pointer is NULL. This is a sign that
 * the entry has not been initialised.
 */
/*@notfunction@*/
#define _DLL_LINK_NULL(pentry, dll)   ((pentry)->dll.pdllNext == NULL || \
                                       (pentry)->dll.pdllPrev == NULL)

/**
 * \brief Reset list entry structure to not be in a list.
 *
 * Before a structure can be added to a list this macro must be used to reset
 * the list link pointers.
 *
 * \param[in] pentry Pointer to structure to have list link reset.
 * \param[in] dll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_RESET_LINK(
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_RESET_LINK(pentry, dll) \
MACRO_START \
  (pentry)->dll.pdllNext = &(pentry)->dll; \
  (pentry)->dll.pdllPrev = &(pentry)->dll; \
MACRO_END

/**
 * \brief Reset list head structure to an empty list.
 *
 * Before any entries can be added to a list this macro must be used to
 * reset the list head and tail pointers.
 *
 * In an empty list the head element's next pointer points to the tail while
 * the previous pointer points to itself, while the tail element's head pointer
 * points to itself and the previous pointer points to the head element.
 *
 * \param[in] pdls Pointer to list structure to reset.
 */
#ifdef DOXYGEN_SKIP
void DLL_RESET_LIST(
  dll_list_t* pdls);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_RESET_LIST(pdls) \
MACRO_START \
  (pdls)->dllHead.pdllNext = (pdls)->dllTail.pdllNext = &(pdls)->dllTail; \
  (pdls)->dllHead.pdllPrev = (pdls)->dllTail.pdllPrev = &(pdls)->dllHead; \
MACRO_END

/**
 * \brief Add an entry to the start of a 2-way list.
 *
 * The entry should be initialised or have been removed from a previous list
 * before being prepended to this list.
 *
 * \param[in] pdls Pointer to list structure.
 * \param[in] pentry Pointer to structure to prepend to the list.
 * \param[in] dll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_ADD_HEAD(
  dll_list_t*   pdls,
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_ADD_HEAD(pdls, pentry, dll) \
MACRO_START \
  HQASSERT(_DLL_LIST_INITIALISED(pdls), \
           "DLL_ADD_HEAD: list has not been initialised"); \
  HQASSERT(!_DLL_LINK_NULL(pentry, dll), \
           "DLL_ADD_HEAD: list entry has not been initialised"); \
  HQASSERT(!DLL_IN_LIST(pentry, dll), \
           "DLL_ADD_HEAD: list entry being added already in list"); \
  (pentry)->dll.pdllPrev = &(pdls)->dllHead; \
  (pentry)->dll.pdllNext = (pdls)->dllHead.pdllNext; \
  (pentry)->dll.pdllPrev->pdllNext = \
      (pentry)->dll.pdllNext->pdllPrev = &(pentry)->dll; \
MACRO_END

/**
 * \brief Add an entry to the end of a 2-way list.
 *
 * The entry should be initialised or have been removed from a previous list
 * before being appended to this list.
 *
 * \param[in] pdls Pointer to list structure.
 * \param[in] pentry Pointer to structure to append to the list.
 * \param[in] dll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_ADD_TAIL(
  dll_list_t*   pdls,
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_ADD_TAIL(pdls, pentry, dll) \
MACRO_START \
  HQASSERT(_DLL_LIST_INITIALISED(pdls), \
           "DLL_ADD_TAIL: list has not been initialised"); \
  HQASSERT(!_DLL_LINK_NULL(pentry, dll), \
           "DLL_ADD_TAIL: list entry has not been initialised"); \
  HQASSERT(!DLL_IN_LIST(pentry, dll), \
           "DLL_ADD_TAIL: list entry being added already in list"); \
  (pentry)->dll.pdllPrev = (pdls)->dllTail.pdllPrev; \
  (pentry)->dll.pdllNext = &(pdls)->dllTail; \
  (pentry)->dll.pdllPrev->pdllNext = \
      (pentry)->dll.pdllNext->pdllPrev = &(pentry)->dll; \
MACRO_END

/**
 * \brief Add an new entry before an existing entry in a 2-way list.
 *
 * The new entry should be initialised or have been removed from a previous list
 * before being added to this list.
 *
 * \param[in] pentry Pointer to structure already in the list.
 * \param[in] pentryNew Pointer to structure to add before the existing entry.
 * \param[in] dll Name of structure member linking the structures in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_ADD_BEFORE(
  type*         pentry,
  type*         pentryNew,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_ADD_BEFORE(pentry, pentryNew, dll) \
MACRO_START \
  HQASSERT(!_DLL_LINK_NULL(pentry, dll), \
           "DLL_ADD_BEFORE: existing list entry has not been initialised"); \
  HQASSERT(!_DLL_LINK_NULL(pentryNew, dll), \
           "DLL_ADD_BEFORE: new list entry has not been initialised"); \
  HQASSERT(DLL_IN_LIST(pentry, dll), \
           "DLL_ADD_BEFORE: existing list entry not in list"); \
  HQASSERT(!DLL_IN_LIST(pentryNew, dll), \
           "DLL_ADD_BEFORE: list entry being added already in list"); \
  (pentryNew)->dll.pdllPrev = (pentry)->dll.pdllPrev; \
  (pentryNew)->dll.pdllNext = &(pentry)->dll; \
  (pentryNew)->dll.pdllPrev->pdllNext = \
      (pentryNew)->dll.pdllNext->pdllPrev = &(pentryNew)->dll; \
MACRO_END

/**
 * \brief Add an new entry after an existing entry in a 2-way list.
 *
 * The new entry should be initialised or have been removed from a previous list
 * before being appended to this list.
 *
 * \param[in] pentry Pointer to structure already in the list.
 * \param[in] pentryNew Pointer to structure to add after the existing entry.
 * \param[in] dll Name of structure member linking the structures in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_ADD_AFTER(
  type*         pentry,
  type*         pentryNew,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_ADD_AFTER(pentry, pentryNew, dll) \
MACRO_START \
  HQASSERT(!_DLL_LINK_NULL(pentry, dll), \
           "DLL_ADD_AFTER: existing list entry has not been initialised"); \
  HQASSERT(!_DLL_LINK_NULL(pentryNew, dll), \
           "DLL_ADD_AFTER: new list entry has not been initialised"); \
  HQASSERT(DLL_IN_LIST(pentry, dll), \
           "DLL_ADD_AFTER: existing list entry not in list"); \
  HQASSERT(!DLL_IN_LIST(pentryNew, dll), \
           "DLL_ADD_AFTER: list entry being added already in list"); \
  (pentryNew)->dll.pdllPrev = &(pentry)->dll; \
  (pentryNew)->dll.pdllNext = (pentry)->dll.pdllNext; \
  (pentryNew)->dll.pdllPrev->pdllNext = \
      (pentryNew)->dll.pdllNext->pdllPrev = &(pentryNew)->dll; \
MACRO_END

/**
 * \brief Get the first entry in the 2-way list.
 *
 * \param[in] pdls Pointer to list structure.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the first entry in the list, or \c NULL is the list is
 * empty.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_HEAD(
  dll_list_t*   pdls,
  structure_name type,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_HEAD(pdls, type, dll) \
  HQASSERT_EXPR(_DLL_LIST_INITIALISED(pdls), \
                "DLL_GET_HEAD: list has not been initialised", \
                (!DLL_LIST_IS_EMPTY(pdls) \
                  ? _LL_ENTRY((pdls)->dllHead.pdllNext, type, dll) \
                  : NULL))

/**
 * \brief Get the last entry in the 2-way list.
 *
 * \param[in] pdls Pointer to list structure.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the last entry in the list, or \c NULL is the list is
 * empty.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_TAIL(
  dll_list_t*   pdls,
  structure_name type,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_TAIL(pdls, type, dll) \
  HQASSERT_EXPR(_DLL_LIST_INITIALISED(pdls), \
                "DLL_GET_TAIL: list has not been initialised", \
                (!DLL_LIST_IS_EMPTY(pdls) \
                  ? _LL_ENTRY((pdls)->dllTail.pdllPrev, type, dll) \
                  : NULL))

/**
 * \brief Get the following entry in the 2-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the next entry in the list, or \c NULL if there are no
 * more entries.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_NEXT(
  type*         pentry,
  structure_name type,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_NEXT(pentry, type, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_GET_NEXT: list or entry has not been initialised", \
                (!DLL_LAST_IN_LIST(pentry, dll) \
                  ? _LL_ENTRY((pentry)->dll.pdllNext, type, dll) \
                  : NULL))

/**
 * \brief Get the previous entry in the 2-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the previous entry in the list, or \c NULL if there are
 * no more entries.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_PREV(
  structure_name type,
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_PREV(pentry, type, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_GET_PREV: list or entry has not been initialised", \
                (!DLL_FIRST_IN_LIST(pentry, dll) \
                  ? _LL_ENTRY((pentry)->dll.pdllPrev, type, dll) \
                  : NULL))

/**
 * \brief Get the following entry in the circular 2-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the next entry in the list, wrapping around to the
 * first entry if \a pentry is the last entry.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_NEXT_CIRCULAR(
  type*         pentry,
  structure_name type,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_NEXT_CIRCULAR(pentry, type, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_GET_NEXT_CIRCULAR: list or entry has not been initialised", \
                (!DLL_LAST_IN_LIST(pentry, dll) \
                 ? _LL_ENTRY((pentry)->dll.pdllNext, type, dll)        \
                 : _LL_ENTRY((_LL_ENTRY((pentry)->dll.pdllNext, dll_list_t, dllTail))->dllHead.pdllNext, type, dll)))

/**
 * \brief Get the previous entry in the circular 2-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] dll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the previous entry in the list, wrapping around to the
 * last entry if \a pentry is the first entry.
 */
#ifdef DOXYGEN_SKIP
type* DLL_GET_PREV_CIRCULAR(
  structure_name type,
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_GET_PREV_CIRCULAR(pentry, type, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_GET_PREV_CIRCULAR: list or entry has not been initialised", \
                (!DLL_FIRST_IN_LIST(pentry, dll) \
                 ? _LL_ENTRY((pentry)->dll.pdllPrev, type, dll)        \
                 : _LL_ENTRY((_LL_ENTRY((pentry)->dll.pdllPrev, dll_list_t, dllHead))->dllTail.pdllPrev, type, dll)))

/**
 * \brief Remove an entry from a 2-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] dll Name of structure member linking the structures in the list.
 */
#ifdef DOXYGEN_SKIP
void DLL_REMOVE(
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_REMOVE(pentry, dll) \
MACRO_START \
  HQASSERT(!_DLL_LINK_NULL(pentry, dll), \
           "DLL_REMOVE: list entry has not been initialised"); \
  HQASSERT(DLL_IN_LIST(pentry, dll), \
           "DLL_REMOVE: list entry being removed not in list"); \
  (pentry)->dll.pdllPrev->pdllNext = (pentry)->dll.pdllNext; \
  (pentry)->dll.pdllNext->pdllPrev = (pentry)->dll.pdllPrev; \
  DLL_RESET_LINK(pentry, dll); \
MACRO_END

/**
 * \brief Check if the 2-way list has no entries.
 *
 * \param[in] pdls Pointer to list structure to check.
 *
 * \returns \c TRUE if the list is empty.
 */
#ifdef DOXYGEN_SKIP
Bool DLL_LIST_IS_EMPTY(
  dll_list_t*   pdls);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_LIST_IS_EMPTY(pdls) \
  HQASSERT_EXPR(_DLL_LIST_INITIALISED(pdls), \
                "DLL_LIST_IS_EMPTY: list has not been initialised", \
                ((pdls)->dllHead.pdllNext == &((pdls)->dllTail)))

/**
 * \brief Check if the entry is currently in a 2-way list.
 *
 * \param[in] pentry Pointer to structure to check if it is in a list.
 * \param[in] dll Name of structure member linking the structure in the list.
 *
 * \returns \c TRUE if the entry is in a list.
 */
#ifdef DOXYGEN_SKIP
Bool DLL_IN_LIST(
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_IN_LIST(pentry, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_IN_LIST: list entry has not been initialised", \
                ((pentry)->dll.pdllNext != (pentry)->dll.pdllPrev))

/**
 * \brief Check if the list entry is first in the 2-way list.
 *
 * \param[in] pentry Pointer to structure to check if first in list.
 * \param[in] dll Name of structure member linking the structure in the list.
 *
 * \returns \c TRUE if the entry is first in the list.
 */
#ifdef DOXYGEN_SKIP
Bool DLL_FIRST_IN_LIST(
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_FIRST_IN_LIST(pentry, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll),                           \
                "DLL_FIRST_IN_LIST: list or entry has not been initialised", \
                (pentry)->dll.pdllPrev == (pentry)->dll.pdllPrev->pdllPrev)

/**
 * \brief Check if the list entry is last in the 2-way list.
 *
 * \param[in] pentry Pointer to structure to check if last in list.
 * \param[in] dll Name of structure member linking the structure in the list.
 *
 * \returns \c TRUE if the entry is last in the list.
 */
#ifdef DOXYGEN_SKIP
Bool DLL_LAST_IN_LIST(
  type*         pentry,
  member_name   dll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_LAST_IN_LIST(pentry, dll) \
  HQASSERT_EXPR(!_DLL_LINK_NULL(pentry, dll), \
                "DLL_LAST_IN_LIST: list or entry has not been initialised", \
                (pentry)->dll.pdllNext == (pentry)->dll.pdllNext->pdllNext)

/**
 * \brief Splice together two 2-way list entries.
 *
 * \param[in] pdll1 Pointer to list entry that is before.
 * \param[in] pdll2 Pointer to list entry that is after.
 */
#ifdef DOXYGEN_SKIP
Bool DLL_LINK_SPLICE(
  dll_link_t*   pdll1,
  dll_link_t*   pdll2);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_LINK_SPLICE(pdll1, pdll2) \
MACRO_START \
  HQASSERT((pdll1)->pdllNext != (pdll1)->pdllPrev, \
           "DLL_LINK_SPLICE: first entry not in existing list"); \
  HQASSERT((pdll2)->pdllNext != (pdll2)->pdllPrev, \
           "DLL_LINK_SPLICE: second entry not in existing list"); \
  (pdll1)->pdllNext = (pdll2); \
  (pdll2)->pdllPrev = (pdll1); \
MACRO_END

/**
 * \brief Append contents of one 2-way list to another.
 *
 * The source list is appended to the destination list.  Afterwards, the source
 * list is empty.
 *
 * \param[in] pdlsDest Pointer to list to append to.
 * \param[in] pdlsSrc Pointer to list to be appended.
 */
#ifdef DOXYGEN_SKIP
void DLL_LIST_APPEND(
  dll_list_t*   pdlsDest,
  dll_list_t*   pdlsSrc);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define DLL_LIST_APPEND(pdlsDest, pdlsSrc) \
MACRO_START \
  HQASSERT(_DLL_LIST_INITIALISED(pdlsDest), \
           "DLL_ADD_HEAD: destination list has not been initialised"); \
  HQASSERT(_DLL_LIST_INITIALISED(pdlsSrc), \
           "DLL_ADD_HEAD: source list has not been initialised"); \
  if ( !DLL_LIST_IS_EMPTY(pdlsSrc) ) { \
    /* Splice dest tail to src head, then src tail to dest tail */ \
    DLL_LINK_SPLICE((pdlsDest)->dllTail.pdllPrev, (pdlsSrc)->dllHead.pdllNext); \
    DLL_LINK_SPLICE((pdlsSrc)->dllTail.pdllPrev, &((pdlsDest)->dllTail)); \
    DLL_RESET_LIST(pdlsSrc); \
  } \
MACRO_END

/* --------------- Single Linked Lists --------------- */

/**
 * \brief Structure for a 1-way linked list entry.
 *
 * The pointer in this structure points to the instance of this structure in the
 * next entry in the list, not to the start of the entry.
 */
typedef struct sll_link_t {
/*@only@*/ /*@notnull@*/
  struct sll_link_t*  psllNext;   /**< Pointer to next element link in the list. */
} sll_link_t;

/**
 * \brief Structure representing the 1-way linked list.
 *
 * By having head and tail link entries, insertion and deletion times are
 * constant as there is no need to do NULL pointer special case handling.
 * The tail pointer tracks to the last link in the list for constant update and
 * retrieval - removal is still O(n).
 */
typedef struct sll_list_t {
  sll_link_t  sllHead;            /**< Link to the first entry in the list. */
  sll_link_t  sllTail;            /**< Link to the last entry in the list. */
} sll_list_t;

/** Check if a single-linked list has been initialised correctly (the head and
 * tail pointers are not NULL, and the sentinels point to the correct places).
 */
/*@notfunction@*/
#define _SLL_LIST_INITIALISED(psls)   ((psls) != NULL && \
                                       (psls)->sllHead.psllNext != NULL && \
                                       (psls)->sllTail.psllNext != NULL && \
                                       (psls)->sllTail.psllNext->psllNext == &(psls)->sllTail)

/** Check if the link next pointer is NULL. This is a sign that the entry has
 * not been initialised.
 */
/*@notfunction@*/
#define _SLL_LINK_NULL(pentry, sll)   ((pentry)->sll.psllNext == NULL)

/**
 * \brief Reset list entry structure to not be in a list.
 *
 * \param[in] pentry Pointer to structure appearing in the list.
 * \param[in] sll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void SLL_RESET_LINK(
  structure_t*  pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_RESET_LINK(pentry, sll) \
MACRO_START \
  (pentry)->sll.psllNext = &(pentry)->sll; \
MACRO_END

/**
 * \brief Reset list head structure to an empty list.
 *
 * In an empty list, the head and tail pointers point to the list structure.
 *
 * \param[in] psls Pointer to list head structure.
 */
#ifdef DOXYGEN_SKIP
void SLL_RESET_LIST(
  sll_list_t* psls);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_RESET_LIST(psls) \
MACRO_START \
  (psls)->sllHead.psllNext = &(psls)->sllTail; \
  (psls)->sllTail.psllNext = &(psls)->sllHead; \
MACRO_END

/**
 * \brief Add an entry to the start of a 1-way list.
 *
 * The entry should be initialised or have been removed from a previous list
 * before being prepended to this list.
 *
 * \param[in] psls Pointer to list structure.
 * \param[in] pentry Pointer to structure to prepend to the list.
 * \param[in] sll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void SLL_ADD_HEAD(
  sll_list_t*   psls,
  type*         pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_ADD_HEAD(psls, pentry, sll) \
MACRO_START \
  HQASSERT(_SLL_LIST_INITIALISED(psls), \
           "SLL_ADD_HEAD: list has not been initialised"); \
  HQASSERT(!_SLL_LINK_NULL(pentry, sll), \
           "SLL_ADD_HEAD: list entry has not been initialised"); \
  HQASSERT(!SLL_IN_LIST(pentry, sll), \
           "SLL_ADD_HEAD: list entry being added already in list"); \
  if ( (psls)->sllTail.psllNext == &(psls)->sllHead ) {           \
    (psls)->sllTail.psllNext = &(pentry)->sll; \
  } \
  (pentry)->sll.psllNext = (psls)->sllHead.psllNext; \
  (psls)->sllHead.psllNext = &(pentry)->sll; \
MACRO_END

/**
 * \brief Add an entry to the end of a 1-way list.
 *
 * The entry should be initialised or have been removed from a previous list
 * before being appended to this list.
 *
 * \param[in] psls Pointer to list structure.
 * \param[in] pentry Pointer to structure to append to the list.
 * \param[in] sll Name of structure member linking the structure in the list.
 */
#ifdef DOXYGEN_SKIP
void SLL_ADD_TAIL(
  sll_list_t*   psls,
  type*         pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_ADD_TAIL(psls, pentry, sll) \
MACRO_START \
  HQASSERT(_SLL_LIST_INITIALISED(psls), \
           "SLL_ADD_TAIL: list has not been initialised"); \
  HQASSERT(!_SLL_LINK_NULL(pentry, sll), \
           "SLL_ADD_TAIL: list entry has not been initialised"); \
  HQASSERT(!SLL_IN_LIST(pentry, sll), \
           "SLL_ADD_TAIL: list entry being added already in list"); \
  (psls)->sllTail.psllNext->psllNext = &(pentry)->sll; \
  (psls)->sllTail.psllNext = &(pentry)->sll; \
  (pentry)->sll.psllNext = &(psls)->sllTail; \
MACRO_END

/**
 * \brief Add an new entry after an existing entry in a 1-way list.
 *
 * The new entry should be initialised or have been removed from a previous list
 * before being appended to this list.
 *
 * \param[in] pentry Pointer to structure already in the list.
 * \param[in] pentryNew Pointer to structure to add after the existing entry.
 * \param[in] sll Name of structure member linking the structures in the list.
 */
#ifdef DOXYGEN_SKIP
void SLL_ADD_AFTER(
  type*         pentry,
  type*         pentryNew,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_ADD_AFTER(pentry, pentryNew, sll) \
MACRO_START \
  HQASSERT(!_SLL_LINK_NULL(pentry, sll), \
           "SLL_ADD_AFTER: existing list entry has not been initialised"); \
  HQASSERT(!_SLL_LINK_NULL(pentryNew, sll), \
           "SLL_ADD_AFTER: new list entry has not been initialised"); \
  HQASSERT(SLL_IN_LIST(pentry, sll), \
           "SLL_ADD_AFTER: previous entry is not in list"); \
  HQASSERT(!SLL_IN_LIST(pentryNew, sll), \
           "SLL_ADD_AFTER: new list entry being added already in list"); \
  if ( SLL_LAST_IN_LIST(pentry, sll) ) { \
    (pentry)->sll.psllNext->psllNext = &(pentryNew)->sll; \
  } \
  (pentryNew)->sll.psllNext = (pentry)->sll.psllNext; \
  (pentry)->sll.psllNext = &(pentryNew)->sll; \
MACRO_END

/**
 * \brief Get the first entry in the 1-way list.
 *
 * \param[in] psls Pointer to list structure.
 * \param[in] type Name of structure of list entries.
 * \param[in] sll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the first entry in the list, or \c NULL is the list is
 * empty.
 */
#ifdef DOXYGEN_SKIP
type* SLL_GET_HEAD(
  sll_list_t*   psls,
  structure_name type,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_GET_HEAD(psls, type, sll) \
  HQASSERT_EXPR(_SLL_LIST_INITIALISED(psls), \
                "SLL_GET_HEAD: list has not been initialised", \
                (!SLL_LIST_IS_EMPTY(psls) \
                  ? _LL_ENTRY((psls)->sllHead.psllNext, type, sll) \
                  : NULL))

/**
 * \brief Get the last entry in the 1-way list.
 *
 * \param[in] psls Pointer to list structure.
 * \param[in] type Name of structure of list entries.
 * \param[in] sll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the last entry in the list, or \c NULL is the list is
 * empty.
 */
#ifdef DOXYGEN_SKIP
type* SLL_GET_TAIL(
  sll_list_t*   psls,
  structure_name type,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_GET_TAIL(psls, type, sll) \
  HQASSERT_EXPR(_SLL_LIST_INITIALISED(psls), \
                "SLL_GET_TAIL: list has not been initialised", \
                (!SLL_LIST_IS_EMPTY(psls) \
                  ? _LL_ENTRY((psls)->sllTail.psllNext, type, sll) \
                  : NULL))

/**
 * \brief Get the following entry in the 1-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] sll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the next entry in the list, or \c NULL if there are no
 * more entries.
 */
#ifdef DOXYGEN_SKIP
type* SLL_GET_NEXT(
  type*         pentry,
  structure_name type,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_GET_NEXT(pentry, type, sll) \
  HQASSERT_EXPR(!_SLL_LINK_NULL(pentry, sll), \
                "SLL_GET_NEXT: list or entry has not been initialised", \
                (!SLL_LAST_IN_LIST(pentry, sll) \
                  ? _LL_ENTRY((pentry)->sll.psllNext, type, sll) \
                  : NULL))

/**
 * \brief Get the following entry in a circular 1-way list.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] type Name of structure of list entries.
 * \param[in] sll Name of structure member linking the structures in the list.
 *
 * \returns A pointer to the next entry in the list, wrapping around to the
 * first entry if \a pentry is the last entry.
 */
#ifdef DOXYGEN_SKIP
type* SLL_GET_NEXT_CIRCULAR(
  type*         pentry,
  structure_name type,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_GET_NEXT_CIRCULAR(pentry, type, sll) \
  HQASSERT_EXPR(!_SLL_LINK_NULL(pentry, sll), \
                "SLL_GET_NEXT_CIRCULAR: list or entry has not been initialised", \
                (!SLL_LAST_IN_LIST(pentry, sll)                         \
                 ? _LL_ENTRY((pentry)->sll.psllNext, type, sll)         \
                 : _LL_ENTRY((_LL_ENTRY((pentry)->sll.psllNext, sll_list_t, sllTail))->sllHead.psllNext, type, sll)))

/**
 * \brief Remove the first entry from a 1-way list.
 *
 * To access the head entry after it has been removed use SLL_GET_HEAD() before
 * using this macro.
 *
 * \param[in] psls Pointer to list.
 */
#ifdef DOXYGEN_SKIP
void SLL_REMOVE_HEAD(
  sll_list_t*   psls);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_REMOVE_HEAD(psls) \
MACRO_START \
  sll_link_t* psll; \
  HQASSERT(_SLL_LIST_INITIALISED(psls), \
           "SLL_REMOVE_HEAD: list has not been initialised"); \
  HQASSERT(!SLL_LIST_IS_EMPTY(psls), \
           "SLL_REMOVE_HEAD: trying to remove head of empty list"); \
  psll = (psls)->sllHead.psllNext; \
  if ( (psls)->sllTail.psllNext == psll ) { \
    /* List only has one entry so update tail pointer */ \
    (psls)->sllTail.psllNext = &(psls)->sllHead; \
  } \
  (psls)->sllHead.psllNext = psll->psllNext; \
  psll->psllNext = psll; \
MACRO_END

/**
 * \brief Remove the next entry from a 1-way list.
 *
 * To access the next entry after it has been removed use SLL_GET_NEXT() before
 * using this macro.
 *
 * \param[in] pentry Pointer to existing list entry.
 * \param[in] sll Name of structure member linking the structures in the list.
 */
#ifdef DOXYGEN_SKIP
void SLL_REMOVE_NEXT(
  type*         pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_REMOVE_NEXT(pentry, sll) \
MACRO_START \
  sll_link_t* psllRemove; \
  HQASSERT(!_SLL_LINK_NULL(pentry, sll), \
           "SLL_ADD_AFTER: list entry has not been initialised"); \
  HQASSERT(SLL_IN_LIST(pentry, sll), \
           "SLL_REMOVE_NEXT: existing list entry not in list"); \
  HQASSERT(!SLL_LAST_IN_LIST(pentry, sll), \
           "SLL_REMOVE_NEXT: trying to remove list entry after last in list"); \
  psllRemove = (pentry)->sll.psllNext; \
  if ( psllRemove->psllNext->psllNext == psllRemove ) {               \
    /* Entry being removed is tail - move tail back one */ \
    psllRemove->psllNext->psllNext = &(pentry)->sll;   \
  } \
  (pentry)->sll.psllNext = psllRemove->psllNext; \
  psllRemove->psllNext = psllRemove; \
MACRO_END

/**
 * \brief Check if the 1-way list has no entries.
 *
 * \param[in] psls Pointer to list structure to check.
 *
 * \returns \c TRUE if the list is empty.
 */
#ifdef DOXYGEN_SKIP
Bool SLL_LIST_IS_EMPTY(
  sll_list_t*   psls);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_LIST_IS_EMPTY(psls) \
  HQASSERT_EXPR(_SLL_LIST_INITIALISED(psls), \
                "SLL_LIST_IS_EMPTY: list has not been initialised", \
                (psls)->sllHead.psllNext == &(psls)->sllTail)

/**
 * \brief Check if the entry is currently in a 1-way list.
 *
 * \param[in] pentry Pointer to structure to check if it is in a list.
 * \param[in] sll Name of structure member linking the structure in the list.
 *
 * \returns \c TRUE if the entry is in a list.
 */
#ifdef DOXYGEN_SKIP
Bool SLL_IN_LIST(
  type*         pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_IN_LIST(pentry, sll) \
  HQASSERT_EXPR(!_SLL_LINK_NULL(pentry, sll), \
                "SLL_IN_LIST: list entry has not been initialised", \
                (pentry)->sll.psllNext != &(pentry)->sll)

/**
 * \brief Check if the list entry is last in the 1-way list.
 *
 * \param[in] pentry Pointer to structure to check if last in list.
 * \param[in] sll Name of structure member linking the structure in the list.
 *
 * \returns \c TRUE if the entry is last in the list.
 */
#ifdef DOXYGEN_SKIP
Bool SLL_LAST_IN_LIST(
  type*         pentry,
  member_name   sll);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_LAST_IN_LIST(pentry, sll) \
  HQASSERT_EXPR(!_SLL_LINK_NULL(pentry, sll), \
                "SLL_LAST_IN_LIST: entry has not been initialised", \
                (pentry)->sll.psllNext->psllNext == &(pentry)->sll)

/**
 * \brief Append contents of one 1-way list to another.
 *
 * The source list is appended to the destination list.  Afterwards, the source
 * list is empty.
 *
 * \param[in] pslsDest Pointer to list to append to.
 * \param[in] pslsSrc Pointer to list to be appended.
 */
#ifdef DOXYGEN_SKIP
void SLL_LIST_APPEND(
  sll_list_t*   pslsDest,
  sll_list_t*   pslsSrc);
#endif /* !DOXYGEN_SKIP */
/*@notfunction@*/
#define SLL_LIST_APPEND(pslsDest, pslsSrc) \
MACRO_START \
  HQASSERT(_SLL_LIST_INITIALISED(pslsDest), \
           "SLL_LIST_APPEND: destination list has not been initialised"); \
  HQASSERT(_SLL_LIST_INITIALISED(pslsSrc), \
           "SLL_LIST_APPEND: destination list has not been initialised"); \
  if ( !SLL_LIST_IS_EMPTY(pslsSrc) ) { \
    /* Add src head to dest tail, update dest tail to point to src tail, and update new list tail to point to dest head */ \
    (pslsDest)->sllTail.psllNext->psllNext = (pslsSrc)->sllHead.psllNext; \
    (pslsDest)->sllTail.psllNext = (pslsSrc)->sllTail.psllNext; \
    (pslsDest)->sllTail.psllNext->psllNext = &(pslsDest)->sllTail; \
    SLL_RESET_LIST(pslsSrc); \
  } \
MACRO_END

#endif /* !__LISTS_H__ */


/* Log stripped */
