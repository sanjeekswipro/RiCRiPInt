/*
 * $HopeName: HQN_CPP_CORBA!export:queue.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

/* This module defines a thread-safe queue for use by CORBA
 * applications. Actually, it is more than a queue, having
 * stack-like and priority-queue-like methods, too; rather a hybrid!
 *
 * NOTE: This code uses OmniORB's omnithreads layer.  This means that
 * the RIP and other products that use this queue must always use the
 * Omni thread library, but it ought not to mean that we cannot switch
 * ORB functionality to another ORB in the future.  If it does, we
 * will have to port this code to another thread library.
 */

#ifndef __CORBAQUEUE_H__
#define __CORBAQUEUE_H__

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include <omnithread.h> // omni_mutex, omni_semaphore, etc.
#include "hqn_cpp_corba.h" // basics
#include "fwstring.h"

/* ----------------------- Classes ----------------------------------------- */

// definition of partially abstract class QueueData
class QueueData
{
private:
  const int32 priority;

  omni_semaphore * const semaphore;

  QueueData * next;      // the next node
  QueueData * prev;      // the previous node

  // wait on semaphore if not 0. No C++ exceptions thrown.
  void wait();

  friend class Queue;

public:

  // Constructor. May throw QueueException if resources unavailable.
  QueueData(int32 need_semaphore = FALSE, int32 priority = NORMAL_PRIORITY);

  virtual ~QueueData();

  // Action to be done on removal of data from queue.
  //
  // Implementation should deal with all exceptions that can happen, except
  // possibly those that definitely can only come from a bug.
  virtual void action() = 0;

  // priority values, only used by append
  const static int32 NORMAL_PRIORITY;
  const static int32 HIGH_PRIORITY;

  int32 has_semaphore() const;

  // signal semaphore if not 0. No C++ exceptions thrown.
  void signal();
};


// definition and implementation of QueueException
class QueueException
{
};


// definition of a generic thread-safe Queue class
class Queue
{
 private:
  int32 reference_count;  // reference count (not always used)
  omni_semaphore *semaphore;   // to signal insertions (if not 0)

  int32 valid_state;
    // used to indicate whether thread using this queue should stop

  int32 fail_insert;
    // used to indicate if insert() should fail

  // don't want copy construction or assignment: so declare private
  Queue(const Queue &);
  Queue & operator=(const Queue &);

  void increment_length();

  // warn when number of messages in queue reaches this value
  static const uint32 QUEUE_LENGTH_WARN;
  // max number of messages to allow in queue
  static const uint32 QUEUE_LENGTH_LIMIT;

 protected:

  // Mutex for ensuring single thread at a time can access state. Declared
  // mutable, so locking it in a method does not prevent declaring const.
  mutable omni_mutex mutex;

  QueueData * head;        // head of queue
  QueueData * tail;        // tail of queue

  uint32 current_length;   // current length of queue
  uint32 longest_length;   // longest length queue has reached
  uint32 length_limit;     // length at which to fail queue insertion
  uint32 length_warn;      // length at which to issue a warning about the queue length

  uint32 items_dropped;    // number of items dropped due to queue being full

  // wait on semaphore if not 0. No C++ exceptions thrown.
  void wait();

  // signal semaphore if not 0. No C++ exceptions thrown.
  void signal();

  // removes data from (front of) queue
  virtual QueueData *remove_item();

  void set_state(int32 value); // set valid_state TRUE or FALSE

  virtual void set_fail_insert(int32 value); // set fail_insert TRUE or FALSE
  int32 get_fail_insert();

 public:
  Queue(int32 need_semaphore = FALSE, uint32 len_warn = QUEUE_LENGTH_WARN, uint32 len_limit = QUEUE_LENGTH_LIMIT);
  virtual ~Queue();

  // Inserts given data into queue, for first-in, first-out operation. This is
  // the method to use for traditional "queue" operation.
  //
  // Returns FALSE if queue length would exceed length_limit, in which case
  // data is deleted, otherwise returns TRUE. 
  //
  // This method TAKES NO NOTICE of PRIORITY. Take great care if mixing
  // insert() and append().
  //
  // Throws no C++ exceptions, in any vaguely-reasonable situation.
  //
  // Calls data->wait() after insertion, so will wait if data has semaphore.
  virtual int32 insert(QueueData *data);

  // Appends data to queue after node closest to tail (i.e. front
  // of queue) for which node priority <= data priority. Unless overridden by
  // different priorities, this method gives first-in, last-out operation and
  // is the method to use for traditional "stack" operation.
  //
  // Always adds data to queue, even if length would exceed length_limit.
  //
  // If wait_on_data is TRUE data->wait() is called after data appended.
  //
  // Throws no C++ exceptions, in any vaguely-reasonable situation.
  void append(QueueData *data, int32 wait_on_data = TRUE);

  // Removes data from queue.
  //
  // If multiple is TRUE then removes as much as possible from queue,
  //    otherwise only removes one (or no) entry.
  //
  // Returns number of data entries removed.
  //
  // Data items being removed that do _not_ have a semaphore are deleted
  // on removal. Ones with a semaphore are not deleted on removal.
  //
  // If this operation removes the last data in the queue, then flush() will
  // be called on the data item.
  virtual int32 process(int32 multiple = FALSE);

  // thread-safe determination of whether queue is empty
  int32 is_empty() const;

  // non-thread-safe determination of whether queue is empty
  int32 dirty_is_empty() const;

  // looks for nodes matching key (defined by compare_key)
  // deletes found nodes and release their data
  void delete_key_all(CompareInterface &compare_key);

  // Reference-counting methods. Add one with duplicate(), remove one with
  // release(). This does NOT delete the Queue. But when a previously
  // non-zero reference count returns to zero, set_state(FALSE) is called.
  void duplicate();
  void release();

  int32 have_valid_state() const;

  // Function that may be used as a WorkerThread action, to run a Queue
  // in that thread, until the Queue has invalid state.
  //
  // It is not mandatory to use this method to run a Queue, but it may
  // often be convenient to do so.
  static void doQueue(void* pQueue);
};


/* Log stripped */
#endif /* PRODUCT_HAS_API */

#endif /* __CORBAQUEUE_H__ */
