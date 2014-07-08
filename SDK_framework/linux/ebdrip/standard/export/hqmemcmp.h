/** \file
 * \ingroup hqmem
 *
 * $HopeName: HQNc-standard!export:hqmemcmp.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Interface to memory compare utility
 */

#ifndef __HQMEMCMP_H__
#define __HQMEMCMP_H__

/**
 * \defgroup hqmem Harlequin standard memory copy and move functions
 * \ingroup cstandard
 * \{
 */

int32 HQNCALL HqMemCmp(const void *s1, int32 ln1,
                       const void *s2, int32 ln2) ;

/** \} */

#endif /* protection for multiple inclusion */

