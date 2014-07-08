/* impl.h.eventrep: Allocation replayer interface
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * $Id: eventrep.h,v 1.3.31.1.1.1 2013/12/19 11:27:06 anon Exp $
 * $HopeName: MMsrc!eventrep.h(EBDSDK_P.1) $
 */

#ifndef eventrep_h
#define eventrep_h

#include "config.h"
/* override variety setting for EVENT */
#define EVENT

#include "eventcom.h"
#include "mpmtypes.h"


extern Res EventRepInit(Bool partial);
extern void EventRepFinish(void);

extern void EventReplay(Event event, Word etime);


#endif /* eventrep_h */
