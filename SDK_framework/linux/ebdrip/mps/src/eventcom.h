/* impl.h.eventcom -- Event Logging Common Definitions
 *
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * $Id: eventcom.h,v 1.30.1.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!eventcom.h(EBDSDK_P.1) $
 *
 * .sources: mps.design.telemetry
 */

#ifndef eventcom_h
#define eventcom_h

/* #include "eventgen.h" later in the file */
#include "mpmtypes.h" /* for Word */


/* Types for event fields */


typedef size_t EventCode;
typedef Index EventKind;

typedef Byte EventStringLen;

typedef struct {
  EventStringLen len;
  char str[EventStringLengthMAX];
} EventStringStruct;

typedef EventStringStruct *EventString;


/* DebugInfoStruct -- structure for passing debug info to Alloc
 *
 * This must match mps_tag_s in impl.h.mps. */
typedef struct DebugInfoStruct {
  Word location;
  Word class;
} DebugInfoStruct;


/* DebugInfoCheck -- check method for DebugInfo
 *
 * There's nothing to check, infos are even allowed to be NULL. */
#define DebugInfoCheck(info) ((void)info, TRUE)


#define EventNameMAX ((size_t)19)
#define EventCodeMAX ((EventCode)0x006B)


/* eventgen.h is just the automatically generated part of this file */
#include "eventgen.h"


#ifdef EVENT

typedef EventUnion *Event;

#endif


#endif /* eventcom_h */
