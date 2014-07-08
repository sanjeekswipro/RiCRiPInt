/* impl.h.splay: SPLAY TREE HEADER
 *
 * $Id: splay.h,v 1.8.11.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!splay.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .source: design.mps.splay
 */

#ifndef splay_h
#define splay_h

#include "mpmtypes.h" /* for Res, etc. */


typedef struct SplayTreeStruct *SplayTree;
typedef struct SplayNodeStruct *SplayNode;
typedef unsigned Compare;
typedef Compare (*SplayCompareMethod)(void *key, SplayNode node);
typedef Bool (*SplayTestNodeMethod)(SplayTree tree, SplayNode node,
                                    void *closureP);
typedef Bool (*SplayTestTreeMethod)(SplayTree tree, SplayNode node,
                                    void *closureP);
typedef void (*SplayUpdateNodeMethod)(SplayTree tree, SplayNode node,
                                      SplayNode leftChild, 
                                      SplayNode rightChild);
typedef Res (*SplayNodeDescribeMethod)(SplayNode node, mps_lib_FILE *stream);
enum {
  CompareLESS = 1,
  CompareEQUAL,
  CompareGREATER
};


typedef struct SplayTreeStruct {
  SplayCompareMethod compare;
  SplayUpdateNodeMethod updateNode;
  SplayNode root;
} SplayTreeStruct;

typedef struct SplayNodeStruct {
  SplayNode left;
  SplayNode right;
} SplayNodeStruct;


extern Bool SplayTreeCheck(SplayTree tree);
extern Bool SplayNodeCheck(SplayNode node);
extern void SplayTreeInit(SplayTree tree, SplayCompareMethod compare,
                          SplayUpdateNodeMethod updateNode);
extern void SplayNodeInit(SplayNode node);
extern void SplayNodeFinish(SplayNode node);
extern void SplayTreeFinish(SplayTree tree);

extern Res SplayTreeInsert(SplayTree tree, SplayNode node, void *key);
extern Res SplayTreeDelete(SplayTree tree, SplayNode node, void *key);

extern Res SplayTreeSearch(SplayNode *nodeReturn,
                           SplayTree tree, void *key );
extern Res SplayTreeNeighbours(SplayNode *leftReturn, 
                               SplayNode *rightReturn,
                               SplayTree tree, void *key);

extern SplayNode SplayTreeFirst(SplayTree tree);
extern SplayNode SplayTreeNext(SplayTree tree, SplayNode oldNode, 
                               void *oldKey);

extern Bool SplayFindFirst(SplayNode *nodeReturn, SplayTree tree,
                           SplayTestNodeMethod testNode,
                           SplayTestTreeMethod testTree,
                           void *closureP);
extern Bool SplayFindLast(SplayNode *nodeReturn, SplayTree tree,
                          SplayTestNodeMethod testNode,
                          SplayTestTreeMethod testTree,
                          void *closureP);

extern void SplayNodeRefresh(SplayTree tree, SplayNode node, void *key);

extern Res SplayTreeDescribe(SplayTree tree, mps_lib_FILE *stream,
                             SplayNodeDescribeMethod nodeDescribe);

extern Bool SplayRoot(SplayNode *nodeReturn, SplayTree tree);


#endif /* splay_h */
