/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:datatrns.c(EBDSDK_P.1) $
 * $Id: src:datatrns.c,v 1.7.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Allows data to be read and written to a buffer in stages
 */

#include "core.h"
#include "datatrns.h"

#include "swerrors.h"
#include "objnamer.h"
#include "mm.h"
#include "hqmemcpy.h"

/* --Private macros-- */

#define DATATRANSACTION_NAME "Imagefilter data transaction"

/* --Private datatypes-- */

/* Transaction type enumeration */
typedef enum DataTransactionType_s {
  InternalBuffer = 1,
  ExternalBuffer
}DataTransactionType;

/* Data Transaction */
struct DataTransaction_s {
  uint8 *buffer;
  uint32 bufferSize;
  uint32 readHead;
  uint32 writeHead;
  uint32 type;
  OBJECT_NAME_MEMBER
};

/* --Public methods-- */

/* Constructor, creating an internal buffer as the source and target of any
 * transactions. The transaction is reset once the buffer has been allocated.
 * This type of DataTransaction cannot use the newBuffer method
 */
DataTransaction* dataTransactionNewWithBuffer(uint32 newBufferSize)
{
  DataTransaction* self;
    
  HQASSERT(newBufferSize > 0, 
           "dataTransactionNewWithBuffer - Buffer size cannot be zero");

  self = (DataTransaction*) mm_alloc(mm_pool_temp, sizeof(DataTransaction), 
                                     MM_ALLOC_CLASS_DATA_TRANSACTION);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, DATATRANSACTION_NAME);

  /* NULL pointer members */
  self->buffer = NULL;
  self->type = InternalBuffer;
  self->bufferSize = newBufferSize;

  self->buffer = (uint8*) mm_alloc(mm_pool_temp, self->bufferSize, 
                                   MM_ALLOC_CLASS_DATA_TRANSACTION);
  if (self->buffer == NULL) {
    dataTransactionDelete(self);
    (void)error_handler(VMERROR);
    return NULL; /* Allocation failed */
  }
  
  dataTransactionReset(self);

  return self;
}

/* Constructor, without creating an internal buffer. A call to newBuffer must
 * be made before any of the access methods become valid
 */
DataTransaction* dataTransactionNewWithoutBuffer(void)
{
  DataTransaction* self;

  self = (DataTransaction*) mm_alloc(mm_pool_temp, 
                                     sizeof(DataTransaction), 
                                     MM_ALLOC_CLASS_DATA_TRANSACTION);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, DATATRANSACTION_NAME);

  self->type = ExternalBuffer;
  self->buffer = NULL;
    
  return self;
}

/* Destructor. If this DataTransaction has an internal buffer, it will be 
 * deleted 
 */
void dataTransactionDelete(DataTransaction* self)
{
  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  UNNAME_OBJECT(self);
  
  if ((self->type == InternalBuffer) && (self->buffer != NULL)) {
    mm_free(mm_pool_temp, self->buffer, sizeof(uint8) * self->bufferSize);
  }
  mm_free(mm_pool_temp, self, sizeof(DataTransaction));
}

/* Duplicate the "source" transaction into this. Only valid with external 
 * buffer transactions
 */
void dataTransactionDuplicate(DataTransaction* self, DataTransaction* source)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  VERIFY_OBJECT(source, DATATRANSACTION_NAME);
  HQASSERT(self->type != InternalBuffer, 
           "dataTransactionDuplicate - object has internal buffer that cannot "
           "be changed");

  self->buffer = source->buffer;
  self->bufferSize = source->bufferSize;
  self->readHead = source->readHead;
  self->writeHead = source->writeHead;
}

/* Set a new buffer for external buffer type transactions, and resets the 
 * transaction. Overflow and underflow checking is performed on subsequent 
 * reads and writes
 */
void dataTransactionNewBuffer(DataTransaction* self, 
                              uint8* newBuffer, 
                              uint32 newBufferSize, 
                              uint32 amountAvailable)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  HQASSERT(self->type != InternalBuffer, 
           "dataTransactionNewBuffer - object has internal buffer that cannot "
           "be changed");

  self->buffer = newBuffer;
  self->bufferSize = newBufferSize;
  self->writeHead = amountAvailable;
  self->readHead = 0;
}

/* Clear (set to zero) this transaction's buffer. This does not reset the 
 * transaction, buy obviously any data in the buffer will be lost
 */
void dataTransactionClear(DataTransaction* self)
{
  uint32 i;

  VERIFY_OBJECT(self, DATATRANSACTION_NAME);

  if (self->buffer != NULL) {
    for (i = 0; i < self->bufferSize; i ++) {
      self->buffer[i] = 0;
    }
  }
}

/* Reset the transaction; ie. set dataAvailable to zero, and the head to the 
 * start of the buffer 
 */
void dataTransactionReset(DataTransaction* self)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
    
  self->readHead = 0;
  self->writeHead = 0;
}

/* Read data from this transaction 
 */
uint8* dataTransactionRead(DataTransaction* self, uint32 amount)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  HQASSERT(self->buffer != NULL, 
           "dataTransactionRead - object does not have a valid data buffer");
  HQASSERT((self->readHead + amount) <= self->writeHead, 
           "dataTransactionRead - tried to read more data than currently "
           "available");

  self->readHead += amount;
  return &self->buffer[self->readHead - amount];
}

/* Write data to this transaction
 */
void dataTransactionWrite(DataTransaction* self, uint8* source, uint32 amount)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  HQASSERT(self->buffer != NULL, 
           "dataTransactionWrite - object does not have a valid data buffer");
  HQASSERT(source != NULL, 
           "dataTransactionWrite - passed data source is NULL");
  HQASSERT(((self->writeHead + amount) <= self->bufferSize),
           "dataTransactionWrite - tried to write "
           "more data than will fit in buffer");

  if (amount > 0) {
    HqMemCpy(&self->buffer[self->writeHead], source, amount);
    self->writeHead += amount;
  }
}

/* Perform a fake write (increment the write head, but don't copy any data) 
 * to this transaction
 */
void dataTransactionFakeWrite(DataTransaction* self, uint32 amount)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  HQASSERT(self->buffer != NULL, 
           "dataTransactionFakeWrite - object does not have a valid data "
           "buffer");
  HQASSERT(((self->writeHead + amount) <= self->bufferSize),
           "dataTransactionFakeWrite - tried to write "
           "more data than will fit in buffer");

  self->writeHead += amount;
}

/* Get the number of bytes available
 */
uint32 dataTransactionAvailable(DataTransaction* self)
{
  VERIFY_OBJECT(self, DATATRANSACTION_NAME);
  
  if (self->buffer != NULL) {
    return self->writeHead - self->readHead;
  }
  else {
    return 0;
  }
}

/* Log stripped */
