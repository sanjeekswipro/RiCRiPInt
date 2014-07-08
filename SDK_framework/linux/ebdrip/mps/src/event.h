/* impl.h.event -- Event Logging Interface
 *
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * $Id: event.h,v 1.21.1.1.1.1 2013/12/19 11:27:09 anon Exp $
 * $HopeName: MMsrc!event.h(EBDSDK_P.1) $
 *
 * READERSHIP
 *
 * .readership: MPS developers.
 *
 * DESIGN
 *
 * .design: design.mps.telemetry.
 */

#ifndef event_h
#define event_h

#include "eventcom.h"
#include "mpm.h"


extern Res EventSync(void);
extern Res EventInit(void);
extern void EventFinish(void);
extern Word EventControl(Word, Word);
extern Word EventInternString(const char *);
extern Word EventInternGenString(size_t, const char *);
extern void EventLabelAddr(Addr, Word);


#ifdef EVENT


extern void EventAdd(Event, size_t) ;


/* Event Kinds --- see design.mps.telemetry
 *
 * All events are classified as being of one event type.
 * They are small enough to be able to be used as shifts within a word.
 */

#define EventKindArena      ((EventKind)0) /* Per space or arena */
#define EventKindPool       ((EventKind)1) /* Per pool */
#define EventKindTrace      ((EventKind)2) /* Per trace or scan */
#define EventKindSeg        ((EventKind)3) /* Per seg */
#define EventKindRef        ((EventKind)4) /* Per ref or fix */
#define EventKindObject     ((EventKind)5) /* Per alloc or object */
#define EventKindUser       ((EventKind)6) /* User-invoked */

#define EventKindNumber     ((Count)7) /* Number of event kinds */


/* Event type definitions
 *
 * Define various constants for each event type to describe them.
 */

/* Note that enum values can be up to fifteen bits long portably. */
#define RELATION(type, code, always, kind, format) \
  enum { \
    Event##type##Code = code, \
    Event##type##Always = always, \
    Event##type##Kind = EventKind##kind, \
    Event##type##Format = EventFormat##format \
  };

#include "eventdef.h"

#undef RELATION


/* Event writing support */

extern EventUnion EventMould;
extern char *EventNext, *EventLimit;
extern Word EventKindControl;

#define EVENT_BEGIN(type) \
  BEGIN \
    if(BS_IS_MEMBER(EventKindControl, ((Index)Event##type##Kind))) { \
      size_t _length;

#define EVENT_END(type, format, length) \
      AVER(EventFormat##format == Event##type##Format); \
      EventMould.any.codeAndClock = Event##type##Code | (mps_clock() << 8); \
      AVER(EventNext <= EventLimit); \
      _length = size_tAlignUp(length, sizeof(Word)); \
      EventAdd(&EventMould, _length) ; \
    } \
  END


#else /* EVENT not */


#define EventInit()            (ResOK)
#define EventFinish()          NOOP
#define EventControl(r, f)     (UNUSED(r), UNUSED(f), (Word)0)
#define EventInternString(s)   (UNUSED(s), (Word)0)
#define EventInternGenString(l, s) (UNUSED(l), UNUSED(s), (Word)0)
#define EventLabelAddr(a, i)   BEGIN UNUSED(a); UNUSED(i); END


#endif /* EVENT */


#endif /* event_h */
