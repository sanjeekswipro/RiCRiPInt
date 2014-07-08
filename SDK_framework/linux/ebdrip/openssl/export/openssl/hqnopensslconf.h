/* $HopeName: HQNlibopenssl!export:openssl:hqnopensslconf.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Additional configuration for openssl
 */

#ifndef __HQNOPENSSLCONF_H__
#define __HQNOPENSSLCONF_H__

#include "std.h"

/* Pick up endianness from hqn define highbytefirst
 * and define appropriate openssl symbol.
 * Do this here, and not in a makefile, so as to allow
 * compilation of Mac OS X universal binaries.
 */
#ifdef highbytefirst
#define B_ENDIAN
#else
#define L_ENDIAN
#endif


#endif /* __HQNOPENSSLCONF_H__ */

/* Log stripped */
