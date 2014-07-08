/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWVECTOR_H__
#define __FWVECTOR_H__

/*
 * $HopeName: HQNframework_os!export:fwvector.h(EBDSDK_P.1) $
 * FrameWork Vector utilities
 *
 * Revision History:
 *
 */

/*
 * The Vector structure is a convenience utility to any application code that needs
 * to have a growable vector of elements where the ultimate number is not known
 * at the point of creating the vector.
 *
 * Features:
 * - An intialization function creates the vector at a specified starting size.
 * - elements in a vector are treated as void *'s.
 * - the application code can add elements to the end of the vector. This vector
 *   will grow automatically to contain the current size of the vector.
 * - Elements can be inserted elsewhere in the vector. Elements above the insertion
 *   point are moved up by one.
 * - Elements can be removed from the vector. Elements above the deletion point are 
 *   moved down one.
 * - The elements can be sorted using qsort and a provided comparison function.
 * - The vector can either be closed or abandoned. Abandoning frees all memory
 *   associated with the vector. Closing allows the application to hang onto
 *   the list of elements beyond the life of the vector.
 *
 * Example of use:
 *    FwVector vector = {FW_VECTOR_NOT_INIT};
 *    FwStrRecord filename;
 *
 *    FwVectorOpen(&vector, 20);
 *    FwStrRecordOpen(&filename);
 *
 *    while ( <get filename from a directory> )
 *    {
 *      FwVectorAddElement(&vector,
 *                        (void *)FwStrDuplicate(FwStrRecordGetBuffer(&filename)));
 *      FwStrRecordShorten(&filename, 0);
 *    }
 *    FwStrRecordAbandon(&filename);
 *    FwVectorQSort(&vector, FwVectorStringCompare);
 *    <do something with the sorted vector>
 *    FwVectorAbandon(&vector, TRUE);
 */

/* ----------------------- Includes ---------------------------------------- */

#include "fwcommon.h"   /* Common */
                        /* Is External */
                        /* No Platform Dependent */


/* ----------------------- Macros ------------------------------------------ */



/* ----------------------- Types ------------------------------------------- */

/*
 * Data structure of a vector
 */
typedef struct FwVector {
  int32   state;
  int32   nCurrentSize;
  int32   nCurrentAllocSize;
  void ** pElements;
} FwVector;

/*
 * The state values
 */

enum {
  FW_VECTOR_NOT_INIT  = 0,
  FW_VECTOR_OPENED,
  FW_VECTOR_CLOSED,
  FW_VECTOR_ABANDONED
};

/* ---------------------- extern functions ------------------------------- */

/*
 * Given a Vector structure, initialize it with a table of size nSize.
 * This vector can have elements added or inserted into it.
 */
extern void FwVectorOpen(FwVector * pVector, int32 nSize);

/*
 * This function is used by application code which now wants
 * to hang onto the table of elements. The state of the vector
 * is set so that it cannot be used again.
 */
extern void ** FwVectorClose(FwVector * pVector);

/*
 * This function is used by application code that wants
 * to free the vector table, and optionally free all the
 * elements in the table. The vector should not be used again.
 */
extern void FwVectorAbandon(FwVector * pVector, int32 fFreeElements);

/*
 * This function resets the vectors state so that it can be
 * re-used for another vector. The vector must have been closed
 * or abandoned.
 */
extern void FwVectorReUse(FwVector *pVector);


/*
 * This function is used to add a element to the end of the vector.
 */
extern void FwVectorAddElement(FwVector * pVector, void * pElement);

/* This function is used to add a element into a particular position
 * in the vector. If the insertion position is beyond the current size
 * the length of the vector is increased and the intervening elements
 * are set to invalid pointers. Elements in the vector above the insertion
 * point are moved up one to make room for the new element.
 */
extern void FwVectorInsertElement(FwVector * pVector, void * pElement, int32 nPosition);

/*
 * This function removes the last element of the vector.
 */
extern void FwVectorPopElement(FwVector * pVector);

/*
 * This function removes an element at a particular point. Elements
 * above the current point are moved down one.
 */
extern void FwVectorRemoveElement(FwVector * pVector, int32 nPosition);

/*
 * This function retrieves the element at a particular position
 */
extern void * FwVectorGetElement(FwVector * pVector, int32 nPosition);


/*
 * A really useful function on vectors - sort. This uses the
 * C runtime "qsort" function to sort the elements in the
 * vector. The compare function declaration has to be done
 * this way to keep the qsort prototype happy.
 */
extern void FwVectorQSort(FwVector * pVector,
                         int (CRT_API *funCompare)(const void *, const void *));

/*
 * This is a useful compare function that takes the two elements
 * as pointers to TextStrings and uses FwStrCompare to
 * determine the ordering.
 */
extern int CRT_API FwVectorStringCompare(const void * ell1, const void * ell2);


#endif /* __FWVECTOR_H__ */

