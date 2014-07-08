/* Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_ufst.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup PMS
 *  \brief Header file for UFST application functions.
 *
 */

#ifndef _PMS_UFST_H_
#define _PMS_UFST_H_

#define DEF(_this,_as) \
typedef _this _as ;\
typedef unsigned _this u##_as
DEF(char, int8) ;
DEF(short, int16) ;
#if SIZEOF_INT == 4
DEF(int, int32) ;
#else
DEF(long, int32) ;
#endif
#undef DEF

typedef uint32 unsignedint32;

#include "pms.h"      /* size_t */
#include "ufst_hqn.h"

extern PCLchId2PtrFn * g_pmsPCLchId2Ptr;
extern PCLchId2PtrFn * g_pmsPCLglyphId2Ptr;

#if GG_CUSTOM
extern PCLallocFn    * g_pmsPCLalloc;
extern PCLfreeFn     * g_pmsPCLfree;
#endif

#endif /* _PMS_UFST_H_ */
