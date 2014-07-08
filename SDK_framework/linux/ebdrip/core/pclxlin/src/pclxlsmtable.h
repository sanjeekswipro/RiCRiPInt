/**
 * \file
 * \ingroup corepclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlsmtable.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Data interface to the PCL XL valid operator sequence state machine.
 *
 * The action and new state are packed into each state table entry.
 *
 * This file has been automatically generated and should NOT be hand edited.
 *
 * This file was generated from operators.sm using COREpcl_pclxl!states:smgen.py(trunk.7)
 */

#ifndef __PCLXLSMTABLE_H__
#define __PCLXLSMTABLE_H__  (1)

/* States */
#define STATE_JOB (0)
#define STATE_SESSION (1)
#define STATE_PAGECONTENT (2)
#define STATE_CHAR1 (3)
#define STATE_CHAR0 (4)
#define STATE_FONTHEADER1 (5)
#define STATE_FONTHEADER0 (6)
#define STATE_READIMAGE1 (7)
#define STATE_READIMAGE0 (8)
#define STATE_RASTPATTERN1 (9)
#define STATE_RASTPATTERN0 (10)
#define STATE_SCANLINE1 (11)
#define STATE_SCANLINE0 (12)
#define STATE_READSTREAM1 (13)
#define STATE_READSTREAM0 (14)
#define STATE_ERROR (15)
#define STATE_END (16)

#define SM_STATE_MASK (0x1f)

/* Actions */
#define ACTION_NOCHANGE (0)
#define ACTION_CHANGE (1)
#define ACTION_PUSH (2)
#define ACTION_POP (3)

#define SM_ACTION_MASK (0x3)
#define SM_ACTION_SHIFT (5)

/* State table entry cracker macros */
#define SM_STATE(s)   ((s)&SM_STATE_MASK)
#define SM_ACTION(s)  (((s)>>SM_ACTION_SHIFT)&SM_ACTION_MASK)

/* Event info */
#define SM_EVENT_OFFSET (65)
#define SM_EVENT_COUNT (127)

/* Transition table */
extern uint8 pclxl_state_table[15][128];

#endif /* !__PCLXLSMTABLE_H__ */

/* EOF */
