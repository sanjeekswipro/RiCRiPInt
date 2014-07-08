/* Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpfin_ufst5!export:pfinufst5.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup PFIN
 *  \brief Example PFIN module implementation using UFST5
 *
 */

#ifndef __PFINUFST5_H__
#define __PFINUFST5_H__

#include "ufst_hqn.h"

/* Note that the pfin_ufst5_fns* in this call will eventually be published as
 * an RDR, at which point pfin_ufst5_module() will no longer take a parameter.
 */

int RIPCALL pfin_ufst5_module( pfin_ufst5_fns * pFnTable );

int pfin_ufst5_GetPCLFontTotal(void);
char *pfin_ufst5_GetPCLFontSpec(int index, int charper, int height, char *name);
#endif /* __PFINUFST5_H__ */
