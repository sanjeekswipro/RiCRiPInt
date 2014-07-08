/* impl.c.event: EVENT LOGGING
 *
 * $Id: event.c,v 1.19.1.1.1.1 2013/12/19 11:27:05 anon Exp $
 * $HopeName: MMsrc!event.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2003-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .sources: mps.design.event
 *
 * TRANSGRESSIONS (rule.impl.trans)
 *
 * .trans.ref: The reference counting used to destroy the mps_io object
 * isn't right.
 *
 * .trans.log: The log file will be re-created if the lifetimes of
 * arenas don't overlap, but shared if they do.  mps_io_create cannot
 * be called twice, but EventInit avoids this anyway.
 *
 * .trans.ifdef: This file should logically be split into two, event.c
 * (which contains NOOP definitions, for general use) and eventdl.c, which
 * is specific to the logging variety and actually does logging (maybe).
 * Unfortunately, the build system doesn't really cope, and so this file
 * consists of two versions which are conditional on the EVENT symbol.
 */

#include "mpm.h"
#include "event.h"
#include "mpsio.h"

SRCID(event, "$Id: event.c,v 1.19.1.1.1.1 2013/12/19 11:27:05 anon Exp $");


#ifdef EVENT /* .trans.ifdef */


static Bool eventInited = FALSE;
static Res eventError = ResOK;
static mps_io_t eventIO;
static char eventBuffer[EventBufferSIZE];
static Count eventUserCount;
static Serial EventInternSerial;

EventUnion EventMould; /* Used by macros in impl.h.event */
char *EventNext, *EventLimit; /* Used by macros in impl.h.event */
Word EventKindControl; /* Bit set used to control output. */

/* EventFlush -- flush event buffer to the event stream
 *
 * This can't return an error code, because we don't want to complicate
 * users of the EVENT_ macros by error handling.  So it maintains a
 * global error state that can be accessed through EventSync. */

static void EventFlush(void)
{
  AVER(eventInited);

  if (eventError == ResOK) {
    eventError = (Res)mps_io_write(eventIO, (void *)eventBuffer,
                                   EventNext - eventBuffer);
  }
  EventNext = eventBuffer;
}


/* EventAdd -- add an event to the buffer */
void EventAdd(Event ev, size_t length)
{
  AVER(eventInited);

  if (!mps_lib_event_filter(ev, length))
    return ;

  if(length > (size_t)(EventLimit - EventNext))
    EventFlush(); /* @@@@ should pass length */

  /** \todo ajcd 2012-08-02: This is not thread-safe. Does it need to be, or
      are the callers all locked? */
  AVER(length <= (size_t)(EventLimit - EventNext));
  (void)mps_lib_memcpy(EventNext, ev, length);
  EventNext += length ;
}


/* EventSync -- synchronize the event stream with the buffers */

Res EventSync(void)
{
  Res resIO;

  EventFlush();
  resIO = mps_io_flush(eventIO);
  return (eventError != ResOK) ? eventError : resIO;
}


/* EventInit -- start using the event system, initialize if necessary */

Res EventInit(void)
{
  Res res;

  /* Only if this is the first call. */
  if(!eventInited) { /* See .trans.log */
    AVER(EventNext == 0);
    AVER(EventLimit == 0);
    res = (Res)mps_io_create(&eventIO);
    if(res != ResOK) return res;
    EventNext = eventBuffer;
    EventLimit = &eventBuffer[EventBufferSIZE];
    eventUserCount = (Count)1;
    eventError = ResOK;
    eventInited = TRUE;
    EventKindControl = (Word)mps_lib_telemetry_control();
    EventInternSerial = (Serial)1; /* 0 is reserved */
    (void)EventInternString(MPSVersion()); /* emit version */
  } else {
    ++eventUserCount;
  }

  return ResOK;
}


/* EventFinish -- stop using the event system */

void EventFinish(void)
{
  AVER(eventInited);
  AVER(eventUserCount > 0);

  (void)EventSync();

  --eventUserCount;
}


/* EventControl -- Change or read control word
 *
 * Resets the bits specified in resetMask, and flips those in
 * flipMask.  Returns old value.
 *
 * Operations can be implemented as follows:
 *   Set(M)   EventControl(M,M)
 *   Reset(M) EventControl(M,0)
 *   Flip(M)  EventControl(0,M)
 *   Read()   EventControl(0,0)
 */

Word EventControl(Word resetMask, Word flipMask)
{
  Word oldValue = EventKindControl;

  /* EventKindControl = (EventKindControl & ~resetMask) ^ flipMask */
  EventKindControl =
    BS_SYM_DIFF(BS_DIFF(EventKindControl, resetMask), flipMask);

  return oldValue;
}


/* EventInternString -- emit an Intern event on the (null-term) string given */

Word EventInternString(const char *label)
{
  Word id;

  AVER(label != NULL);

  id = (Word)EventInternSerial;
  ++EventInternSerial;
  EVENT_WS(Intern, id, StringLength(label), label);
  return id;
}


/* EventInternGenString -- emit an Intern event on the string given */

Word EventInternGenString(size_t len, const char *label)
{
  Word id;

  AVER(label != NULL);

  id = (Word)EventInternSerial;
  ++EventInternSerial;
  EVENT_WS(Intern, id, len, label);
  return id;
}


/* EventLabelAddr -- emit event to label address with the given id */

void EventLabelAddr(Addr addr, Word id)
{
  AVER((Serial)id < EventInternSerial);

  EVENT_AW(Label, addr, id);
}


#else /* EVENT, not */


Res (EventSync)(void)
{
  return(ResOK);
}


Res (EventInit)(void)
{
  return(ResOK);
}


void (EventFinish)(void)
{
  NOOP;
}


Word (EventControl)(Word resetMask, Word flipMask)
{
  UNUSED(resetMask);
  UNUSED(flipMask);

  return (Word)0;
}


Word (EventInternString)(const char *label)
{
  UNUSED(label);

  return (Word)0;
}


Word (EventInternGenString)(size_t len, const char *label)
{
  UNUSED(len); UNUSED(label);

  return (Word)0;
}


void (EventLabelAddr)(Addr addr, Word id)
{
  UNUSED(addr);
  UNUSED(id);
}


#endif /* EVENT */
