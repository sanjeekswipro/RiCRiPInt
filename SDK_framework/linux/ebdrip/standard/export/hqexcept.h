/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqexcept.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Harlequin standard code for handling exceptions and signals.
 */

#ifndef __HQEXCEPT_H__ /* { */
#define __HQEXCEPT_H__

#ifdef __cplusplus
extern "C" {
#endif

void HQNCALL HqCatchExceptions(void (HQNCALL *func)(char *));
void HQNCALL HqCStacks(char *title);

#ifdef __cplusplus
}
#endif

#endif /* } __HQEXCEPT_H__ */

