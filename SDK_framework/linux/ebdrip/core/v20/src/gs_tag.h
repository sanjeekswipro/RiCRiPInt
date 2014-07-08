/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gs_tag.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS gstate tagging API.
 */

#ifndef __GS_TAG_H__
#define __GS_TAG_H__

#include "objecth.h"
#include "display.h"

Bool make_gstagstructureobject(DL_STATE *page, OBJECT *dict,
                               GSTAGSTRUCTUREOBJECT **result);

#define TAG_BYTES( state ) ( (state)->gstagstructure == NULL  \
  ? 0 : (state)->gstagstructure->dl_words * sizeof( int32 ) )

#endif /* __GS_TAG_H__ */

/* Log stripped */
