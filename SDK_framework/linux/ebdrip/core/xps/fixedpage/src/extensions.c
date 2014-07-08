/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!fixedpage:src:extensions.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of Global Graphics FixedPage markup extensions.
 */

#include "core.h"
#include "namedef_.h"
#include "swctype.h"
#include "swerrors.h"
#include "fileio.h"
#include "hqmemcpy.h"
#include "hqnuri.h"
#include "xml.h"
#include "xpspriv.h"
#include "xpsscan.h"
#include "discardStream.h"

/**
 * Start element callback for the ImmediateDiscard element; reads the target
 * part attribute and discard it.
 */
static Bool immediateDiscardStart(xmlGFilter *filter,
                                  const xmlGIStr *localname,
                                  const xmlGIStr *prefix,
                                  const xmlGIStr *uri,
                                  xmlGAttributes *attributes)
{
  static XML_ATTRIBUTE_MATCH match[] = {
    {XML_INTERN(Target), NULL, NULL, xps_convert_part_reference, NULL},
    XML_ATTRIBUTE_MATCH_END
  };
  xps_partname_t* target = NULL;
  Bool success = FALSE;

  UNUSED_PARAM(const xmlGIStr *, prefix);

  match[0].data = &target;
  if (! xmlg_attributes_match(filter, localname, uri, attributes, match, FALSE))
    return FALSE;

  success = xps_discard_part(target);

  xps_partname_free(&target);

  return success;
}

/**
 * Exported element callbacks.
 */
xpsElementFuncts fixed_page_extension_functions[] =
{
  { XML_INTERN(ImmediateDiscard),
    immediateDiscardStart,
    NULL, /* No end callback. */
    NULL /* No characters callback. */
  }
};

/* Log stripped */

