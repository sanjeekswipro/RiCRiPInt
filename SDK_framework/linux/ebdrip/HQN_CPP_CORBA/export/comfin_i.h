/* $HopeName: HQN_CPP_CORBA!export:comfin_i.h(EBDSDK_P.1) $
 *
 * This module defines a "mixin" for implementing ComponentFinder
 */

#ifndef _incl_comfin
#define _incl_comfin

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "namingutils.h"

#include "HqnSystem.hh" // IDL-generated from HqnSystem.idl

/* ----------------------- Classes ----------------------------------------- */

class ComponentFinderImpl : public virtual POA_HqnSystem::ComponentFinder
{
  private:
  
  protected:
  ComponentFinderImpl() { }
  virtual ~ComponentFinderImpl() { }
  
  public:
  HqnSystem::ServerModule_ptr find_component_1
    (const char* name,
     HqnSystem::ComponentFinder::SearchType search,
     HqnSystem::ComponentFinder::ResponseCheck check);
  
  CORBA::Long component_finder_impl_version();
  
}; // class ComponentFinderImpl

#endif /* PRODUCT_HAS_API */

/* Log stripped */

#endif // _incl_comfin
