/* $HopeName: HQN_CPP_CORBA!export:hqnlist.h(EBDSDK_P.1) $
 *
 * This module defines a thread-safe list manipulation
 */

#ifndef _incl_list
#define _incl_list

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include <omnithread.h>    // omni_mutex

#include "hqn_cpp_corba.h" // interface definitions


/* ----------------------- Classes ----------------------------------------- */

// definition of ListNode, which contains one element of data of the list
// the data is of type void * so users have to cast to the appropriate type

class ListNode
{
private:
  void *data;         // the data
  ListNode *next;     // the pointer to the next node
  ListNode & operator=(const ListNode &); // copy assignment forbidden
  ListNode(const ListNode &); // copy construction allowed, but only from List

public:
  ListNode(void *data);
  ~ListNode(); // note: destructor not intended to destroy 'data'

  friend class List;

}; // class ListNode

// definition of ListKeyNotFoundException
// thrown when a key is not found in list

class ListKeyNotFoundException
{
}; // class ListKeyNotFoundException

// definition of thread-safe List

class List
{
private:
  mutable omni_mutex mutex; // mutex used so that list access is thread-safe
  ListNode * head;  // head (first node) of list
  List & operator=(const List &); // copy assignment prohibited
  List(const List &); // copy construction allowed, but defined privately

public:
  List();
  ~List(); // note: destructor not intended to destroy 'head'



  void insert(void *data);
    // inserts specified data into list
    // throws ListMemoryException if memory allocation fails

  void append(void *data);
    // appends specified data to list
    // throws ListMemoryException if memory allocation fails

  int32 is_empty() const;
    // returns TRUE if list is empty, FALSE otherwise

  void destroy(ProcessInterface &destroy_node);
    // destroys list using destroy_node to destroy each node data
    
  void *find_key(CompareInterface &compare_key) const;
    // looks for (first) node matching key (defined by compare_key)
    // returns data for found node
    // (does not delete found node)
    // throws ListKeyNotFoundException if key not found

  void *delete_key(CompareInterface &compare_key);
    // looks for (first) node matching key (defined by compare_key)
    // deletes found node
    // returns data for found node
    // throws ListKeyNotFoundException if key not found

  void traverse(ProcessInterface &process_node);
    // traverses list operating on each node data using process_node

  void traverse_copy(ProcessInterface &process_node);
  // like traverse, but works over a COPY of the list.  
  // -- allows stable modification under iteration 
  // -- use only if process_node is likely to modify the List nodes
  //    during the traversal.  
  //
  // note: the nodes of the copied list are distinct objects from the
  // original but contain the same 'data' pointer, so for the purposes
  // of the operation in process_node they are the same as under a 
  // normal traversal

}; // class List

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // _incl_list

// eof list.h
