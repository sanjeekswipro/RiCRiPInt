/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWTREE_H__
#define __FWTREE_H__

/* $HopeName: HQNframework_os!export:fwtree.h(EBDSDK_P.1) $
 * Linked Tree Class definition
 */

/* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

/* see fw.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
                        /* No Platform Dependent */

#include "fwclass.h"    /* FwClassRecord */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/* ----------------------- Types ------------------------------------------- */

/* Predicate function on an object, with data */
typedef int32 (Fw_ObjectPredicate)( FwObj * pObj, void * pvPredicateData );


/* ----------------------- Classes ----------------------------------------- */


/*
 * Linked List Class: A simple class which defines a tree consisting
 * of nodes with parent, sibling and children pointers. It inherits
 * from the Object class.
 */

typedef struct FwTree {
#define FW_TREE_FIELDS \
  FW_OBJECT_FIELDS \
  struct FwTree * pNext; \
  struct FwTree * pParent; \
  struct FwTree * pChildren;

  FW_TREE_FIELDS

} FwTree;

extern FwClassRecord FwTreeClass;


/* ----------------------- Messages and other functions -------------------- */


/* Messages for this class */

/* Fw_msg_CreateAndLink
 * Create a new instance and link it into the tree
 */
FW_MESSAGE_DECLARE( Fw_msg_CreateAndLink )
typedef FwObj * (Fw_msg_CreateAndLink_Type)
(
  FwObj *               pObj,
  FwClassRecord *       pClassRecord,
  FwObj *               pParent,
  FwObj *               pAfterThisItem
);
extern FwObj * Fw_msg_CreateAndLink
 ( FwClassRecord * pClassRecord, FwObj * pParent, FwObj * pAfterThisItem );
#define FW_TREE_INSERT_LAST     (FwObj *)((intptr_t)0x1)
#define FW_TREE_INSERT_FIRST    (FwObj *)((intptr_t)0x3)

/* Fw_msg_GetRoot
 * return root object of tree
 */
FW_MESSAGE_DECLARE( Fw_msg_GetRoot )
typedef FwObj * (Fw_msg_GetRoot_Type)( FwObj * pObj );
extern Fw_msg_GetRoot_Type Fw_msg_GetRoot;

/* Fw_msg_Fw_msg_IsDescendant
 * returns TRUE <=> pObj2 is equal to pObj, or a descendant of pObj
 */
FW_MESSAGE_DECLARE( Fw_msg_IsDescendant )
typedef int32 (Fw_msg_IsDescendant_Type)( FwObj * pObj, FwObj * pObj2 );
extern Fw_msg_IsDescendant_Type Fw_msg_IsDescendant;


/* ObjectPredicate that ignores the object referred to, and always 
 * returns TRUE */
extern Fw_ObjectPredicate FwTree_True;

/* ObjectPredicate that ignores the object referred to, and always 
 * returns FALSE */
extern Fw_ObjectPredicate FwTree_False;

/* ObjectPredicate that ignores the object referred to, and always 
 * returns the value (TRUE or FALSE as an int32) its pvPredicateData 
 * points to */
extern Fw_ObjectPredicate FwTree_Constant;


/* Fw_msg_EnumerateChildren
 * Enumerate immediate children.
 */
FW_MESSAGE_DECLARE( Fw_msg_EnumerateChildren )
typedef FwObj * (Fw_msg_EnumerateChildren_Type)
(
  FwObj *               pObj,           /* Parent to enumerate */
  FwObj *               pStartAfter     /* NULL <=> start at first */
);
extern Fw_msg_EnumerateChildren_Type Fw_msg_EnumerateChildren;

/* Fw_msg_EnumerateChildrenMatching
 * Enumerate immediate children matching predicate.
 */
FW_MESSAGE_DECLARE( Fw_msg_EnumerateChildrenMatching )
typedef FwObj * (Fw_msg_EnumerateChildrenMatching_Type)
(
  FwObj *               pObj,           /* Parent to enumerate */
  FwObj *               pStartAfter,    /* NULL <=> start at first */
  Fw_ObjectPredicate *  pxPredicate,
  void *                pvPredicateData
);
extern Fw_msg_EnumerateChildrenMatching_Type Fw_msg_EnumerateChildrenMatching;


/* Fw_msg_EnumerateDescendants
 * Depth first recursive walk over tree, returns parents before children.
 */
FW_MESSAGE_DECLARE( Fw_msg_EnumerateDescendants )
typedef FwObj * (Fw_msg_EnumerateDescendants_Type)
(
  FwObj *               pObj,           /* Root to enumerate over */
  FwObj *               pStartAfter     /* NULL <=> start at root */
);
extern Fw_msg_EnumerateDescendants_Type Fw_msg_EnumerateDescendants;

/* Fw_msg_EnumerateDescendantsMatching
 * As Fw_msg_EnumerateDescendants but filter to those matching predicate.
 */
FW_MESSAGE_DECLARE( Fw_msg_EnumerateDescendantsMatching )
typedef FwObj * (Fw_msg_EnumerateDescendantsMatching_Type)
(
  FwObj *               pObj,           /* Root to enumerate over */
  FwObj *               pStartAfter,    /* NULL <=> start at root */
  Fw_ObjectPredicate *  pxPredicate,
  void *                pvPredicateData
);
extern Fw_msg_EnumerateDescendantsMatching_Type Fw_msg_EnumerateDescendantsMatching;


/*
 * Fw_msg_AtIndex
 * Returns child with given (zero based) index.
 * Returns NULL if index out of range.
 */
FW_MESSAGE_DECLARE( Fw_msg_AtIndex )
typedef FwObj * (Fw_msg_AtIndex_Type)( FwObj * pObj, uint32 indx );
extern Fw_msg_AtIndex_Type Fw_msg_AtIndex;

/*
 * Fw_msg_IthMatching
 * Returns ith (zero based) child matching predicate.
 * Returns NULL if index out of range.
 */
FW_MESSAGE_DECLARE( Fw_msg_IthMatching )
typedef FwObj * (Fw_msg_IthMatching_Type)
(
  FwObj *               pObj,
  uint32                indx,
  Fw_ObjectPredicate *  pxPredicate,
  void *                pvPredicateData
);
extern Fw_msg_IthMatching_Type Fw_msg_IthMatching;


/*
 * Fw_msg_IndexInParent
 * Returns (zero based) index of pObj among parents children
 */
FW_MESSAGE_DECLARE( Fw_msg_IndexInParent )
typedef uint32 (Fw_msg_IndexInParent_Type)( FwObj * pObj );
extern Fw_msg_IndexInParent_Type Fw_msg_IndexInParent;

/*
 * Fw_msg_PredecessorsMatching
 * Returns number of predecessors matching predicate.
 */
FW_MESSAGE_DECLARE( Fw_msg_PredecessorsMatching )
typedef uint32 (Fw_msg_PredecessorsMatching_Type)
(
  FwObj *               pObj,
  Fw_ObjectPredicate *  pxPredicate,
  void *                pvPredicateData
);
extern Fw_msg_PredecessorsMatching_Type Fw_msg_PredecessorsMatching;


/* psuedo messages */
#define Fw_msg_GetParent( pObj )        ( ((FwTree *)(pObj))->pParent )
#define Fw_msg_GetSibling( pObj )       ( ((FwTree *)(pObj))->pNext )
#define Fw_msg_GetChildren( pObj )      ( ((FwTree *)(pObj))->pChildren )

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __FWTREE_H__ */

/* eof fwtree.h */
