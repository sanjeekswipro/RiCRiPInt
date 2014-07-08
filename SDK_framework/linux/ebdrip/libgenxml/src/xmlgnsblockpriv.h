#ifndef __XMLGNSBLOCKPRIV_H__
#define __XMLGNSBLOCKPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgnsblockpriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgnsblockpriv.h,v 1.10.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Interface for tracking XML namespaces.
 *
 * A namespace block represents all the xmlns defintions within a single
 * element. Example:
 * \code
 *
 * <NewElement
 *   xmlns:v1="URI1" -|
 *   xmlns:v2="URI2"  | This namespace block contains 3 entries.
 *   xmlns:v3="URI3" -|
 * />
 *
 * \endcode
 *
 * A namespace block stack represents all the namespaces which are active
 * during XML parsing. When searching for the namespace given a particular
 * prefix, searching is done from the top of the stack to the bottom of the
 * stack. Either a match is found or the bottom of the stack is hit which means
 * that the prefix is invalid.
 *
 * The top of the stack is poped off after the end element token has been
 * processed.
 *
 * Example:
 *
 * \code
 *
 * <NewElement1 xmlns:v1="URI1">        | Namespace block 1
 *   <NewElement2 xmlns:v1="URI2">      | Namespace block 2
 *     <NewElement3>
 *       <NewElement4 xmlns:v2="URI3">  | Namespace block 3 (top of stack)
 *       </NewElement4>                 | Namespace block 3 popped off stack
 *     </NewElement3>
 *   </NewElement2>                     | Namespace block 2 popped off stack
 * </NewElement1>                       | Namespace block 1 popped off stack
 *
 * \endcode
 */

#include "xmlgtype.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NamespaceBlock NamespaceBlock;

typedef struct NamespaceBlockStack NamespaceBlockStack;

/* ============================================================================
 * Namespace block routines.
 */

extern
HqBool namespace_block_create(
      xmlGFilterChain *filter_chain,
      NamespaceBlock **namespaceblock);

extern
void namespace_block_destroy(
      NamespaceBlock **namespaceblock);

extern
HqBool namespace_block_add_namespace(
      NamespaceBlock *namespaceblock,
      const xmlGIStr *prefix,
      const xmlGIStr *uri);

extern
HqBool namespace_block_get_namespace(
      const NamespaceBlock *namespaceblock,
      const xmlGIStr *prefix,
      const xmlGIStr **uri);

/* ============================================================================
 * Namespace block stack routines.
 */
extern
HqBool namespace_stack_create(
      xmlGFilterChain *filter_chain,
      NamespaceBlockStack **namespace_stack);

extern
void namespace_stack_destroy(
      NamespaceBlockStack **namespace_stack);

extern
HqBool namespace_stack_push(
      NamespaceBlockStack *namespace_stack,
      NamespaceBlock *namespace_block,
      uint32 current_element_depth);

extern
void namespace_stack_pop(
      NamespaceBlockStack *namespace_stack,
      uint32 current_element_depth);

extern
NamespaceBlock* namespace_stack_top(
      NamespaceBlockStack *namespace_stack);

extern
HqBool namespace_stack_find(
      const NamespaceBlockStack *namespace_stack,
      const xmlGIStr *prefix,
      const xmlGIStr **uri);

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*! __XMLGNSBLOCKPRIV_H__*/
