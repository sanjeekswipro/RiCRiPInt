/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWPROG_H__
#define __FWPROG_H__

/*
 * $HopeName: HQNframework_os!export:fwprog.h(EBDSDK_P.1) $
 */

/* ----------------------- Overview ---------------------------------------- */

/*
 * Defines a very simple progress counter class.
 * Progress objects have a number saying how many
 * things are to be done, and one saying how many
 * have been done so far. The message Fw_msg_Progress
 * is intended to increment the count and report
 * on the current operation. 
 */


/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fwstring.h"	/* FwTextString */

#include "fwclass.h"	/* FwObj, FwClassRecord */


/* ----------------------- Messages ---------------------------------------- */


/*
 * Create a progress counter which will count up to "total".
 */
FW_MESSAGE_DECLARE(Fw_msg_Create_Progress)
typedef FwObj *(Fw_msg_Create_Progress_Type) (FwObj *obj, FwClassRecord *pClassRecord, uint32 total);
extern Fw_msg_Create_Progress_Type Fw_msg_Create_Progress;

/*
 * Progress the progress counter by one unit.
 *
 * This needs to be overridden to be useful - the
 * default implementation just counts calls where
 * name is non-NULL, and doesn't tell you anything about it.
 *
 * The return value is "obj" (meaning continue)
 * or NULL (meaning the user cancelled the operation).
 */
FW_MESSAGE_DECLARE(Fw_msg_Progress)
typedef FwObj *(Fw_msg_Progress_Type) (FwObj *obj, FwTextString name);
extern Fw_msg_Progress_Type Fw_msg_Progress;


FW_MESSAGE_DECLARE(Fw_msg_Progress_NameOnly)
typedef FwObj *(Fw_msg_Progress_NameOnly_Type) (FwObj *obj, FwTextString name);
extern Fw_msg_Progress_Type Fw_msg_Progress_NameOnly;



/* ----------------------- Class ---------------------------------------- */

typedef struct FwProgressCounter {
#define FW_PROGRESS_COUNTER_FIELDS \
  FW_OBJECT_FIELDS \
  uint32 sofar; \
  uint32 total; \

  FW_PROGRESS_COUNTER_FIELDS

} FwProgressCounter;

extern FwClassRecord FwProgressCounterClass;

/*
* Log stripped */
#endif
