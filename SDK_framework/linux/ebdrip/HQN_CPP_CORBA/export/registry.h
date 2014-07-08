/* 
 * $HopeName: HQN_CPP_CORBA!export:registry.h(EBDSDK_P.1) $
 * $Id: export:registry.h,v 1.7.4.1.1.1 2013/12/19 11:24:06 anon Exp $
 */

/*
 * This module defines a registry of consumers, which are registered
 * by clients to receive information from a server.
 *
 */

#ifndef __INCL_CONSUMER_REGISTRY__
#define __INCL_CONSUMER_REGISTRY__

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "HqnFeedback.hh"
#include "hqnconsumer.h"   // hqnconsumer base, and queue functionality

/* ----------------------- Definitions ------------------------------------- */

typedef enum
{
    JOB_STATUS_CONSUMER,
    PROGRESS_CONSUMER,
    MONITOR_CONSUMER,
    RIP_STATUS_CONSUMER,
    THROUGHPUT_STATUS_CONSUMER,
    // NOTE!  MAXIMUM_CONSUMER_TYPES MUST ALWAYS BE LAST.  IT'S A COUNT OF THE ENUM MEMBERS
    MAXIMUM_CONSUMER_TYPES 
} ConsumerType;

/* ----------------------- Classes ----------------------------------------- */

// definition of ConsumerUnknownException
// thrown when trying to unregister a consumer which is not known

class ConsumerUnknownException
{
}; // class ConsumerUnknownException

// definition of ConsumerInvalidDataException
// thrown when consumer data is not valid

class ConsumerInvalidDataException
{
}; // class ConsumerInvalidDataException


class ConsumerCompareInterface : public CompareInterface
{

  // CompareInterface is abstract (i.e. contains pure virtual function)

}; // class ConsumerCompareInterface



// ConsumerData is the class for the consumer data the Registry stores.
// Not expected to be subclassed (private constructor prevents it).
class ConsumerData
{
  friend class Registry;

 public:

  // Public access to const members, for simplicity.
  Queue* const queue;
  HqnConsumerProxyBase* const consumer;

  // Anyone can delete one of these
  ~ConsumerData();

  HqnFeedback::ConsumerID get_id() const;

 private:

  HqnFeedback::ConsumerID id;

  // Only friends can construct one of these.
  // Constructor throws API_NoMemoryException if internal allocation fails
  ConsumerData(const HqnSystem::ServerIdentity& identity,
               HqnConsumerProxyBase* hqn_consumer);

}; // class ConsumerData

// Base class for consumer messages. Must be subclassed to use.
class ConsumerMessage
{
 private:
  omni_mutex mutex;
  // note: all access must be via mutex
  int32 reference_count;  // reference count, delete if 0
  
  void dup();
  int32 rel();

 protected:
  ConsumerMessage();

 public:
  virtual ~ConsumerMessage();

  static void duplicate(ConsumerMessage * pCM );
  static void release(ConsumerMessage * pCM );

  virtual void message(HqnConsumerProxyBase *the_consumer) = 0;
    // report to be done for message

}; // class ConsumerMessage




// ConsumerQueueData is used for data that gets packaged up and put
// onto the outgoing ConsumerQueue queue(s). It matches together the
// RIPConsumer and ConsumerMessage
class ConsumerQueueData : public QueueData
{
protected:
  HqnConsumerProxyBase* const consumer;
  ConsumerMessage* const consumer_message;

  friend class ConsumerQueueCompare;       // an implementation class
  friend class ConsumerQueue; // from queue.cpp

public:
  ConsumerQueueData(HqnConsumerProxyBase *the_consumer,
                    ConsumerMessage *consumer_message);
  virtual ~ConsumerQueueData();

  void action();
};



// Registry is a static registry of interested consumers
// It has no knowledge about the differing consumer semantics except the type
class Registry
{
 private:
  
  static void do_unregister(ConsumerCompareInterface & compare);
  // does the work of unregistering a consumer

  // Non-instantiable class, so constructor/destructor are private.
  Registry();
  ~Registry();

 public:

  // need "_me" below because "register" is key word
  // then also use it for unregister for reasons of symmetry

  static HqnFeedback::ConsumerID register_me(int32 type, 
					     const HqnSystem::ServerIdentity & identity,
					     HqnConsumerProxyBase * hqnConsumer);
    // registers consumer interested in (e.g. job status) changes
    // consumer is opaque handle to functions which are called when changes occur
    // throws ConsumerInvalidDataException if consumer NULL
    // throws API_NoMemoryException if memory allocation fails

  static void unregister_me(HqnFeedback::ConsumerID consumer_id);
    // unregisters consumer
    // throws ConsumerUnknownException if consumer not registered
    // throws API_NoMemoryException if memory allocation fails
    // no security, so anyone can (in principle) unregister any consumer

  static void unregister_me(HqnConsumerProxyBase * hqnConsumer);
    // unregisters consumer
    // throws ConsumerUnknownException if consumer not registered
    // throws API_NoMemoryException if memory allocation fails
    // no security, so anyone can (in principle) unregister any consumer

  static HqnConsumerProxyBase* get(HqnFeedback::ConsumerID consumer_id,
                                   int32 type=-1);
    // look-up consumer by ID
    // if type is non-negative, search only that type of consumer, else search all
    // throws ConsumerUnknownException

  static void insert_queue(int32 type, ConsumerMessage *consumer_message);
    // inserts the message into the queue for each registered consumer
    // of the specified type 
    // throws QueueException if insertion into a queue fails

}; // class Registry

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* __INCL_CONSUMER_REGISTRY__ */
