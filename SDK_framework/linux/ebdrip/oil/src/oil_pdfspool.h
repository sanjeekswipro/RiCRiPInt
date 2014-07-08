/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pdfspool.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief OIL This header file contains the interfaces for OIL's PDF spooling functionality.
 */

#ifndef _OIL_PDFSPOOL_H_
#define _OIL_PDFSPOOL_H_

#include "pdfspool.h"

extern PDFSPOOL * OIL_PdfSpool_Create( void );
extern void OIL_PdfSpool_Free( PDFSPOOL * pdfspool );

extern int32 OIL_PdfSpool_StoreData( PDFSPOOL * pdfspool );


#endif /* _OIL_PDFSPOOL_H_ */
