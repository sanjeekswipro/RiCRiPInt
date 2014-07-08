/* impl.h.bt: Bit Table Interface
 *
 * $Id: bt.h,v 1.6.11.1.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!bt.h(EBDSDK_P.1) $
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .source: design.mps.bt.  */

#ifndef bt_h
#define bt_h

#include "mpmtypes.h"


/* design.mps.bt.if.size */
extern size_t (BTSize)(Count length);
#define BTSize(n) (((n) + MPS_WORD_WIDTH-1) / MPS_WORD_WIDTH * sizeof(Word))


/* design.mps.bt.if.get */
extern Bool (BTGet)(BT bt, Index index);
#define BTGet(a, i) \
  ((Bool)(((a)[((i) >> MPS_WORD_SHIFT)] \
           >> ((i) & ~((Word)-1 << MPS_WORD_SHIFT))) \
          & (Word)1))

/* design.mps.bt.if.set */
extern void (BTSet)(BT bt, Index index);
#define BTSet(a, i) \
  BEGIN \
    (a)[((i)>>MPS_WORD_SHIFT)] |= (Word)1<<((i)&~((Word)-1<<MPS_WORD_SHIFT)); \
  END

/* design.mps.bt.if.res */
extern void (BTRes)(BT bt, Index index);
#define BTRes(a, i) \
  BEGIN \
    (a)[((i)>>MPS_WORD_SHIFT)] &= \
      ~((Word)1 << ((i) & ~((Word)-1<<MPS_WORD_SHIFT))); \
  END


extern Res BTCreate(BT *btReturn, Arena arena, Count length);
extern void BTDestroy(BT bt, Arena arena, Count length);

extern void BTSetRange(BT bt, Index base, Index limit);
extern Bool BTIsSetRange(BT bt, Index base, Index limit);
extern void BTResRange(BT bt, Index base, Index limit);
extern Bool BTIsResRange(BT bt, Index base, Index limit);

extern Bool BTFindShortResRange(Index *baseReturn, Index *limitReturn,
                                BT bt, Index searchBase, Index searchLimit,
                                Count length);
extern Bool BTFindShortResRangeHigh(Index *baseReturn, Index *limitReturn,
                                    BT bt, Index searchBase, Index searchLimit,
                                    Count length);
extern Bool BTFindLongResRange(Index *baseReturn, Index *limitReturn,
                               BT bt, Index searchBase, Index searchLimit,
                               Count length);
extern Bool BTFindLongResRangeHigh(Index *baseReturn, Index *limitReturn,
                                   BT bt, Index searchBase, Index searchLimit,
                                   Count length);
extern Bool BTFindLongSetRange(Index *baseReturn, Index *limitReturn,
                               BT bt, Index searchBase, Index searchLimit,
                               Count length);

extern Bool BTRangesSame(BT BTx, BT BTy, Index base, Index limit);

extern void BTCopyInvertRange(BT fromBT, BT toBT, Index base, Index limit);
extern void BTCopyRange(BT fromBT, BT toBT, Index base, Index limit);
extern void BTCopyOffsetRange(BT fromBT, BT toBT,
                              Index fromBase, Index fromLimit,
                              Index toBase, Index toLimit);

extern Count BTCountResRange(BT bt, Index base, Index limit);


#endif /* bt_h */
