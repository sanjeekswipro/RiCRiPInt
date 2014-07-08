/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:proto.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Harlequin Generic Prototype Definitions
 */

#ifndef PROTO_H
#define PROTO_H

#ifdef  USE_PROTOTYPES

#define PROTO(params)               params
#define PARAMS(ansi, traditional)   ansi

#else /* !USE_PROTOTYPES */

#define PROTO(params)               ()
#define PARAMS(ansi, traditional)   traditional

#endif  /* !USE_PROTOTYPES */


/* Some platforms do support ansi-protos mixed with trad-definitions.  */
/* Use PROTOMIXED to protect such ansi-protos, so others don't barf.   */

#if defined( USE_PROTOTYPES ) && defined( USE_PROTOTYPES_MIXED )
#define PROTOMIXED(params)               params
#else
#define PROTOMIXED(params)               ()
#endif


#endif  /* PROTO_H */

