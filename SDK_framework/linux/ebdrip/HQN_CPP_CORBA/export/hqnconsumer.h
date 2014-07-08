/*
 * $HopeName: HQN_CPP_CORBA!export:hqnconsumer.h(EBDSDK_P.1) $
 * $Id: export:hqnconsumer.h,v 1.9.2.2.1.1 2013/12/19 11:24:05 anon Exp $
 */

/*
 * This file contains classes which assist in modelling proxy objects
 * for consumer objects registered to a C++ server, modelling
 * "listener" objects (lifetime monitors) for those remote consumers,
 * and and in queueing outgoing messages to those registered consumer
 * objects.
 */

#ifndef __HQNCONSUMER_H__
#define __HQNCONSUMER_H__

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "queue.h"  // generic thread-safe queue
#include "orb_impl.h"   // mutex, CORBA stuff
#include "hqnlist.h"    // List
#include "HqnSystem.hh"
#include "srvmgr_i.h" // ServerProcessListener
#include "HqnFeedback.hh"
/* ----------------------- Classes ----------------------------------------- */

/*
 * ConsumerListener
 */
class ConsumerListener : public ServerProcessListener
{
private:
  const HqnFeedback::ConsumerID consumerID;

  friend class ConsumerListenerCompare;  // implementation class

  ConsumerListener( HqnFeedback::ConsumerID consumerID );

public:
  ~ConsumerListener();

  // Called when server process manager wants this listener to die.
  // No exceptions should be thrown.
  void release(const HqnSystem::ServerInfo_1 & newInfo,
               HqnSystem::ServedByProcess_ptr pServedByProcess);

  // A listener is created for the consumer and returned.
  // Throws CORBA::NO_MEMORY if memory allocation fails.
  static ConsumerListener * create_listener( HqnFeedback::ConsumerID consumerID );

  // returns the listener which was created for the consumer,
  // or 0 if not known.  no exceptions thrown
  static ConsumerListener * unregister_listener( HqnFeedback::ConsumerID consumerID );

}; // class ConsumerListener


/*
 * ConsumerListenerCompare used by ConsumerDeleteData so that
 * corresponding queue elements are deleted
 */
class ConsumerListenerCompare : public CompareInterface
{
private:
  const HqnFeedback::ConsumerID consumerID;

public:
  ConsumerListenerCompare( HqnFeedback::ConsumerID consumerID );

  int32 compare( void * data );
};



/*
 * ConsumerQueue is required in order to implement a version of
 * Queue's 'process' that has specific knowledge of consumers.
 */
class ConsumerQueue : public Queue
{

 private:
  // forbid copy construction and assignment
  ConsumerQueue(const ConsumerQueue &);
  ConsumerQueue & operator=(const ConsumerQueue &);

  QueueData * itemPendingAction;

  int32 failedMessages;

  int32 maxRetries;

  // max number of retries we will do on a message
  static const int32 MAX_RETRIES;
  // minimum number of seconds to sleep after deciding to retry
  static const int32 MINIMUM_RETRY_DELAY;
  // maximum number of seconds to sleep after deciding to retry
  static const int32 MAXIMUM_RETRY_DELAY;

  // max number of consecutive failed messages before we unregister a non-listening consumer
  static const int32 MAX_FAILED_MESSAGES;

  // warn when number of messages in queue reaches this value
  static const uint32 CONSUMERQUEUE_LENGTH_WARN;
  // max number of messages to allow in queue
  static const uint32 CONSUMERQUEUE_LENGTH_LIMIT;

 protected:
  // override of virtual from Queue:
  // sets itemPendingAction to removed item
  virtual QueueData * remove_item();

 public:
  ConsumerQueue() :
    Queue(TRUE, CONSUMERQUEUE_LENGTH_WARN, CONSUMERQUEUE_LENGTH_LIMIT), failedMessages(0), maxRetries(MAX_RETRIES) {}; // consumer queues need a semaphore

  // override of virtual from Queue; returns the number of items
  // removed from the queue (which COULD exceed the number
  // successfully processed, note, as unsuccessfully retried items
  // will be thrown away eventually)
  int32 process(int32 multiple = FALSE); // override of virtual

  // as Queue::is_empty, but does not return true until
  // mark_pending_action_complete() called after last remove_item()
  // (can't be const because it locks mutex)
  int32 is_empty_and_actions_complete();

  // non-thread-safe version of above
  int32 dirty_is_empty_and_actions_complete() const;

}; // ConsumerQueue

/*
 * A base class for server-side classes which model, by proxy, remote
 * (client) Consumer objects.  These Consumer objects will be defined
 * in IDL somewhere and implemented and submitted by clients to the
 * C++ server.  See also HqnConsumerManager
 */
class HqnConsumerProxyBase
{
private:
  // Worker thread task, for consumer queue
  static void do_consumer_queue_worker(void * arg);

  // Creates worker thread for consumer queue, and runs it
  static void initialise_consumer_queue_worker(ConsumerQueue* queue);

protected:
  HqnConsumerProxyBase() {}

  static omni_mutex mutex;

public:

  static List queue_identity_list;

  virtual ~HqnConsumerProxyBase() {}

  // returns existing queue for specified identity if latter already
  // known otherwise creates and returns new queue for this identity
  //
  // throws API_NoMemoryException if memory cannot be allocated for new identity
  static ConsumerQueue* get_identity_queue(const HqnSystem::ServerIdentity & identity);

  /* this removes and deletes queue from list of queues returned by
   * get_identity_queue */
  static void remove_queue(ConsumerQueue * queue);


  /* this locks queue list */
  static void lock();


  /* this unlocks queue list */
  static void unlock();

}; /* class HqnConsumerProxyBase */


/*
 * Implements the test for whether a queue is empty.
 */
class QueueEmptyTestInterface : public ProcessInterface
{

public:
  void process( void * data );

  int32 empty;

  QueueEmptyTestInterface() : empty(0) {}

}; /* class QueueEmptyTestInterface */


class HqnConsumerManager
{
 public:

  // Declare virtual destructor, to keep GCC compiler happy.
  virtual ~HqnConsumerManager() { }

  /* provides a shutdown process for queues, to be called when
   * shutting down servers which use consumers.  E.g. in the RIP,
   * coreskinexiterrorf in skinmain.c calls this to allow consumer
   * queues a chance to flush any pending messages before they are
   * trashed.  thus this method doesn't worry about cleanly removing a
   * queue from the list.
   *
   * something in the application will need to define and instantiate
   * one of these.  consider it part of the contract of using
   * HqnConsumerProxyBase.
   *
   * the principle behind this method is to allow pending messages
   * that would be flushed in a short period of time the time to do
   * that flushing.  obviously, we don't want the server waiting
   * around for a long time just because there is a consumer out there
   * that's throwing a CORBA::COMM_FAILURE or TRANSIENT or some such
   * exception.  by testing active queues to see if any have pending
   * messages, waiting a short period of time to give them chance to
   * flush, and then exiting anyway, we give well-behaved, eagerly
   * listening clients the opportunity to receive the messages they
   * are entitled to without slowing server closure time down
   * unnecessarily.
   *
   * originally, this was part of HqnConsumerProxyBase.
   * unfortunately, that tied an "end of life" cleanup requirement for
   * the use of some objects to an actual instance of one of those
   * objects ... which meant that at least one of the consumer objects
   * had to be alive for the queues to be shut down.  this needed to
   * be separated from the lifecycle of consumer objects themselves,
   * since they may all have been unregistered and deleted by the time
   * the application came to shut the queues down.
   */
  virtual void shut_consumer_queues_down() = 0;


}; /* class HqnConsumerManager */

/*
* Log stripped */

#endif /* PRODUCT_HAS_API */
#endif /* __HQNCONSUMER_H__ */
