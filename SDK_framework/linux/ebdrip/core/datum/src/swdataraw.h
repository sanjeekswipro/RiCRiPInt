/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!src:swdataraw.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Raw datum handler for \c sw_datum.
 */

#ifndef __SWDATARAW_H__
#define __SWDATARAW_H__

#include "swdataapi.h"

/** \brief The raw \c sw_datum API handler.

    This implementation is exposed only so that the sw_data_api_virtual methods
    can call it if there is no owner. Never use it for any other purpose. */
extern const sw_data_api sw_data_api_raw ;


#endif /* __SWDATARAW_H__ */

/* Log stripped */
