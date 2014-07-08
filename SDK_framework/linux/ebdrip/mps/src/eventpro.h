/* impl.h.eventpro: Interface for event processing routines
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * $Id: eventpro.h,v 1.9.1.1.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!eventpro.h(EBDSDK_P.1) $
 */

#ifndef eventpro_h
#define eventpro_h

#include "config.h"
/* override variety setting for EVENT */
#define EVENT

#include "eventcom.h"
#include "mpmtypes.h"


typedef struct EventProcStruct *EventProc;
typedef Res (*EventProcReader)(void *, void *, size_t);


extern EventCode EventName2Code(char *name);
extern char *EventCode2Name(EventCode code);
extern EventCode EventGetCode(Event event);
extern char *EventCode2Format(EventCode code);
extern Bool EventCodeIsValid(EventCode code);

extern Word AddrLabel(EventProc proc, Addr addr);
extern EventString LabelText(EventProc proc, Word label);
extern Word TextLabel(EventProc proc, EventString name) ;

extern Res EventRead(EventCode *codeReturn, mps_clock_t *currentTimeIO,
                     Event *eventReturn, EventProc proc);

extern void EventDestroy(EventProc proc, Event event);

extern Res EventRecord(EventProc proc, Event event, mps_clock_t etime);

extern Res EventProcCreate(EventProc *procReturn, Bool partial,
                           EventProcReader reader, void *readerP);
extern void EventProcDestroy(EventProc proc);


#endif /* eventpro_h */
