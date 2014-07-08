/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpscommit.h(EBDSDK_P.1) $
 * $Id: shared:xpscommit.h,v 1.14.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for regsitering xps commit blocks and filter.
 */

#ifndef __XPSCOMMIT_H__
#define __XPSCOMMIT_H__

#include "xml.h"
#include "xpsresblock.h"

#define XPS_COMPLEXPROPERTYMATCH_END { NULL, NULL, NULL, NULL, TRUE }

typedef struct xpsCommitBlock xpsCommitBlock ;

/**
 * Convenience structure to define complex properties.
 */
typedef struct XPS_COMPLEXPROPERTYMATCH {
  xmlGIStr *localname ;
  xmlGIStr *uri ;
  /* Numerous complex properties can alternatively be specified by an
     attribute. This is the attribute name which can be used as an
     alternative to the complex property. We explicitly name the
     attribute in the event that the "." notation does not
     follow. This also allows us to specify complex properties which
     do not have an attribute alternative such as FixedPage.Resources
     etc.. by setting attr_localname abd attr_uri to NULL. */
  xmlGIStr *attr_localname ;
  xmlGIStr *attr_uri ;
  /* Is the complex property optional? If its not, an error will be
     raised. */
  Bool optional ;
} XPS_COMPLEXPROPERTYMATCH ;

/*
static XPS_COMPLEXPROPERTYMATCH complex[] = {
  { XML_INTERN(X_W), XML_INTERN(ns_xyz), NULL, NULL, TRUE},
  { XML_INTERN(X_Y), XML_INTERN(ns_xyz), NULL, NULL, TRUE},
  { XML_INTERN(X_Z), XML_INTERN(ns_xyz), NULL, NULL, FALSE},
  XPS_ENDCOMPLEXPROPERTY_END
};
*/

extern
Bool xps_commit_register(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      XPS_COMPLEXPROPERTYMATCH complex[],
      xmlGStartElementCallback f_commit) ;

extern
void xps_commit_unregister(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      const xmlGIStr *complex_localname,
      const xmlGIStr *complex_uri) ;

extern
Bool xps_commit_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      xmlDocumentContext *xps_ctxt) ;

Bool xps_commit_resource_ref(
      xmlGFilter *filter,
      const xmlGIStr *elementname,
      xpsResourceReference *reference) ;

/* ============================================================================
* Log stripped */
#endif
