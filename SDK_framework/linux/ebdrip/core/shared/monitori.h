/** \file
 * \ingroup debug
 *
 * $HopeName: SWcore!shared:monitori.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Monitor functions to implement debug output to log file/window.
 */

#ifndef __MONITORI_H__
#define __MONITORI_H__

#include <stdarg.h>
#include "monitor.h"
#include "eventapi.h"
#include "timelineapi.h"

/* ========================================================================== */
/* Note: This would go in swevents.h if it were actually part of the API */

/** Message for SWEVT_MONITOR

    This is used to output text messages to log and/or console, and features
    meta-information such as a Timeline reference and Channel.
 */

enum {
  SWEVT_MONITOR = EVENT_CORE + 300
} ;


/** SWMSG_MONITOR channel:

    Top eight bits are OEM number, so OEMs can generate and parse their own
    monitor and log messages unambiguously.
 */
typedef HqnIdent sw_mon_channel ;
enum {
  MON_CHANNEL_MONITOR = 1, /* glue.c     via %monitor */
  MON_CHANNEL_PROGRESS,    /* corejob.c  via %progress%logfile */
  MON_CHANNEL_HALFTONE,    /* halftone.c via %progress%halfetoneinfo */
  MON_CHANNEL_STDOUT,      /* fileops.c  via %stdout */
  MON_CHANNEL_STDERR       /* errhand    via %stderr */
} ;

/* -------------------------------------------------------------------------- */

/** SWMSG_MONITOR type:

    Top eight bits are message type

    Next eight bits are error number for PS errors, or 0

    Low 16 bits are a unique identifier for this message. eg This allows every
    INVALIDFONT raised by the core to have its source and hence its cause
    documented in the message.

    This number allows messages to be unambiguously recognised without having
    to parse the textual content (which is harder, fragile and subject to
    localisation).
 */
typedef HqnIdent sw_mon_type ;
enum {
  /* Base numbers */
  MON_TYPE_ERROR   = 7<<28,  /* Very important! + error number << 16 + uid */
  MON_TYPE_WARNING = 6<<28,
  MON_TYPE_CONTROL = 5<<28,  /* Job start, etc */
  MON_TYPE_INFO    = 4<<28,
  MON_TYPE_JOB     = 3<<28,  /* Normal importance (PS 'print' for example) */
  MON_TYPE_PROCSET = 2<<28,
  MON_TYPE_COMMENT = 1<<28,  /* Font version number complaints, for example */
  MON_TYPE_DEBUG   = 0<<28,
  /* Mask for importance level */
  MON_TYPE_IMPORTANCE = 7<<28
} ;

enum {
  /* Job status and timing messages (corejob.c) */
  MON_TYPE_JOBSTART = MON_TYPE_CONTROL + 101,
  MON_TYPE_RECOMBINESTART,
  MON_TYPE_RENDERPREPSTART,
  MON_TYPE_TOTALTIME,
  MON_TYPE_JOBEND,
  MON_TYPE_INTERPRETTIME,
  MON_TYPE_HVDSCANTIME,
  MON_TYPE_RECOMBINETIME,
  MON_TYPE_COMPOSITETIME,
  MON_TYPE_RENDERPREPTIME,
  MON_TYPE_PRINTTIME,
  MON_TYPE_SEPPRINTTIME,
  MON_TYPE_PARTIALPAINTTIME
} ;

enum {
  /* Halftone info messages (halftone.c) */
  MON_TYPE_HTGENSEARCH = MON_TYPE_CONTROL + 201,
  MON_TYPE_HTGENSTART,
  MON_TYPE_HTGENEND,
  MON_TYPE_HTUSAGE,
  MON_TYPE_HTTHRESHOLD,
  MON_TYPE_HTSPOT,
  MON_TYPE_HTMODULAR,
  MON_TYPE_HTCOLORANT,

  MON_TYPE_HTUNOPTIMIZED = MON_TYPE_WARNING + 201,
  MON_TYPE_HTINACCURATE
} ;

enum {
  /* TIFF messages (tifreadr.c & tiffclsp.c */
  MON_TYPE_TIFFBADCOLOR = MON_TYPE_WARNING + 301,
  MON_TYPE_TIFFIFDUNALIGNED,
  MON_TYPE_TIFFDIFFERENTBITS,
  MON_TYPE_TIFFBILEVELFLIPPED,
  MON_TYPE_TIFFBILEVELPLANAR,
  MON_TYPE_TIFFCOMPRESSIONFLIPPED,
  MON_TYPE_TIFFDOTRANGE,
  MON_TYPE_TIFFCOMPRESSIONINDEXED,
  MON_TYPE_TIFFRESOLUTIONPARTIAL,
  MON_TYPE_TIFFRESOLUTIONUNIT,
  MON_TYPE_TIFFRESOLUTIONBAD,

  /* TIFF errors */
  MON_TYPE_TIFFSTRIPSTART = MON_TYPE_ERROR + 301,
  MON_TYPE_TIFFSTRIPEND
} ;

/* -------------------------------------------------------------------------- */

typedef struct SWMSG_MONITOR {
  uint8 *         text ;      /** Pointer to text message */
  size_t          length ;    /** The length of the above message, or zero */
  sw_tl_ref       timeline ;  /** The Timeline this message is associated
                                  with or 0 for unattributed messages */
  sw_mon_channel  channel ;   /** Top 8 bits are OEM number */
  sw_mon_type     type ;      /** Identifier for this message - see above */
} SWMSG_MONITOR ;

/* ========================================================================== */

struct SWSTART ; /* from COREinterface */

Bool monitor_swinit(struct SWSTART *params) ;

void monitor_finish(void) ;

/* The guts of the monitor calls, for other implementors [65510] */
void vmonitorf(sw_tl_ref tl, sw_mon_channel channel, sw_mon_type type,
               uint8 * format, va_list vlist) ;

/* And the same with a parameter list */
void emonitorf(sw_tl_ref tl, sw_mon_channel channel, sw_mon_type type,
               uint8 *format, ...) ;

/* ========================================================================== */
/* Log stripped */
#endif /* Protection from multiple inclusion */
