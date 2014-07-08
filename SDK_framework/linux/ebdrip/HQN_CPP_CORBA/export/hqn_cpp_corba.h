#ifndef __HQN_CPP_CORBA_H__
#define __HQN_CPP_CORBA_H__

/* $HopeName: HQN_CPP_CORBA!export:hqn_cpp_corba.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Main include file for HQN_CPP_CORBA
 */

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "std.h"        /* standard includes */

#ifdef __cplusplus

/* ----------------------- Classes ----------------------------------------- */

/*
 * API_NoMemoryException is just a class used to throw memory exceptions
 * this converts to a CORBA::NOMEMORY exception
 */

class API_NoMemoryException
{
};

/*
 * ProcessInterface is abstract class which allows a processing function to be
 * passed to general iteration algorithm, with process being called on each element
 * Originator must define subclass implementing this abstraction
 */

class ProcessInterface
{
public:
  virtual void process(void *data) = 0;

  virtual ~ProcessInterface() {};
}; /* class ProcessInterface */

/* trivial implementation of ProcessInterface, doing no processing */

class NullProcess : public ProcessInterface
{
public:
  void process(void *data) {}
}; /* class NullProcess */

/*
 * CompareInterface is abstract class which allows a comparing function to be
 * passed to general iteration algorithm, with compare being called on each element
 * Originator must define subclass implementing this abstraction
 * This subclass determines the meaning of the return value
 */

class CompareInterface
{
public:
  virtual int32 compare(void *data) = 0;

  virtual ~CompareInterface() {};
}; /* class CompareInterface */

/* trivial implementation of CompareInterface, always returning FALSE for comparison */

class FalseCompare : public CompareInterface
{
public:
  int32 compare(void *data) { return FALSE; }
}; /* class NullCompare */

/* trivial implementation of CompareInterface, always returning TRUE for comparison */

class TrueCompare : public CompareInterface
{
public:
  int32 compare(void *data) { return TRUE; }
}; /* class NullCompare */

#endif /* __cplusplus */

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* __HQN_CPP_CORBA_H__ */

/* eof hqn_cpp_corba.h */
