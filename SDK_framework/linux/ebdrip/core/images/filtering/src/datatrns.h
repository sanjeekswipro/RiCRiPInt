/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:datatrns.h(EBDSDK_P.1) $
 * $Id: src:datatrns.h,v 1.5.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for datatrns.c
 */

#ifndef __DATATRNS_H__
#define __DATATRNS_H__

/* --Public datatypes-- */

/* Data Transaction */
typedef struct DataTransaction_s DataTransaction;

/* --Public methods-- */

/* DataTransaction methods */

extern DataTransaction* dataTransactionNewWithBuffer(uint32 newBufferSize);
extern DataTransaction* dataTransactionNewWithoutBuffer();
extern void dataTransactionDelete(DataTransaction* self);

extern void dataTransactionDuplicate(DataTransaction* self, 
                                     DataTransaction* source);
extern void dataTransactionNewBuffer(DataTransaction* self, 
                                     uint8* newBuffer, 
                                     uint32 newBufferSize, 
                                     uint32 amountAvailable);
extern void dataTransactionClear(DataTransaction* self);
extern void dataTransactionReset(DataTransaction* self);
extern uint8* dataTransactionRead(DataTransaction* self, uint32 amount);
extern void dataTransactionWrite(DataTransaction* self, 
                                 uint8* source, 
                                 uint32 amount);
extern void dataTransactionFakeWrite(DataTransaction* self, uint32 amount);
extern uint32 dataTransactionAvailable(DataTransaction* self);

/* --Description--

DataTransaction allows a buffer of data to be read and written to in stages.
The buffer may be owned by the transaction (newWithBuffer()), in which case
it is created and destroyed with the transaction, or the buffer can be 
external, and be set with newBuffer(). 

Transactions with external buffers are considered invalid before a call to
newBuffer (or when a NULL newBuffer is passed into such a call), and calls to
reset, read, write, etc. will assert errors if called on an invalid 
transaction. getAvailble() however can be called, and will return zero in this
circumstance.

Transactions with internal buffers will assert an error if a call is made to 
newBuffer() or duplicate().

Reads and writes are checked for under and over-flow during debug execution.

duplicate() copies the internal state of the source transaction into the 'self'
transaction. This is only available for non-buffered transactions.

clear() sets each element in the transaction's buffer to zero. This does not 
change the state of the transaction (read/write head etc.), but obviously any 
data in the buffer will be lost.

fakeWrite() increments the write head by the passed amount, but does not copy
any data into the buffer.

The document "Image Filtering Overview.pdf" (currently [July 2001] this 
resides in the ScriptWorks information database in Notes, under 
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
