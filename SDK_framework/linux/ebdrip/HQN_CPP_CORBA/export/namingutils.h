#ifndef __NAMINGUTILS_H__
#define __NAMINGUTILS_H__

/* $HopeName: HQN_CPP_CORBA!export:namingutils.h(EBDSDK_P.1) $
 *
 * Utilities for using name service
 */

#ifdef PRODUCT_HAS_API


/* ----------------------- Includes ---------------------------------------- */

#include "orb_impl.h"   // ORB-dependent code


/* ----------------------- Structures -------------------------------------- */

typedef struct NamingComponent
{
  char * id;
  char * kind;
} NamingComponent;


/* ----------------------- Functions --------------------------------------- */

// find the local root context, this is also the default context for all
// the following functions
// a var ref is returned since the data is callee managed
CosNaming::NamingContext_var & namingUtils_localRootContext();

// caller manages data here
CosNaming::NamingContextExt_ptr namingUtils_localRootContextExt();

// resolve name in given context
extern CORBA::Object_ptr namingUtils_get
(
 int32                        nComponents,
 NamingComponent            * components,
 CosNaming::NamingContext_ptr pContext
 = CosNaming::NamingContext::_nil()
);

// resolve stringified name in given context
extern CORBA::Object_ptr namingUtils_getExt
  (const CosNaming::NamingContextExt::StringName pStringifiedName,
   const CosNaming::NamingContextExt_ptr pContext
   = CosNaming::NamingContextExt::_nil());

// Bind name to specified object in given context
extern void namingUtils_put
(
 CORBA::Object_ptr            pObj,
 int32                        nComponents,
 NamingComponent            * components,
 CosNaming::NamingContext_ptr pContext
 = CosNaming::NamingContext::_nil()
);


/* 
 * Get the root naming context of the SOAR-enabled Naming Service that
 * is the parent of the SOAR-enabled Naming Service which has the root
 * naming context supplied.  If no root naming context is supplied,
 * the root naming context used is the one for the Naming Service
 * running on the local machine.  
 */
extern CosNaming::NamingContext_ptr namingUtils_getParent
(
 CosNaming::NamingContext_ptr pContext
 = CosNaming::NamingContext::_nil()
);

extern CosNaming::NamingContextExt_ptr namingUtils_getParentExt
  (CosNaming::NamingContext_ptr pContext
   = CosNaming::NamingContext::_nil());

/* 
 * Get the object matching the specified NamingComponent in the
 * specified root naming context.  
 *
 * The search starts in the specified root naming context and keeps
 * searching up the tree of linked SOAR-enabled Naming Services until
 * it finds (or fails to find) a match.  The Naming Service must have
 * been configured already.  
 */
extern CORBA::Object_ptr namingUtils_getParentObject
(
 int32                        nComponents,
 NamingComponent            * components,
 CosNaming::NamingContext_ptr pContext
 = CosNaming::NamingContext::_nil()
);

/* 
 * Get the object matching the stringified Name in the
 * specified root naming context.  
 *
 * The search starts in the specified root naming context and keeps
 * searching up the tree of linked SOAR-enabled Naming Services until
 * it finds (or fails to find) a match.  The Naming Service must have
 * been configured already.  
 */
extern CORBA::Object_ptr namingUtils_getParentObjectExt
  (const CosNaming::NamingContextExt::StringName pStringifiedName,
   const CosNaming::NamingContextExt_ptr pContext
   = CosNaming::NamingContextExt::_nil());


#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif // __NAMINGUTILS_H__

// eof naming.h
