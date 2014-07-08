/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxltest.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Contains various test functions
 */

#ifndef __PCLXLTEST_H__
#define __PCLXLTEST_H__ 1

#ifdef DEBUG_BUILD

#include "pclxlcontext.h"
#include "pclxlparsercontext.h"
#include "pclxlgraphicsstatet.h"

Bool pclxl_hello_world(PCLXL_CONTEXT pclxl_context);

Bool pclxl_test_page_1(PCLXL_CONTEXT pclxl_context);

Bool pclxl_dot(PCLXL_CONTEXT pclxl_context,
               PCLXL_SysVal x,
               PCLXL_SysVal y,
               uint8 label_position,
               uint8* label_text);

Bool pclxl_debug_origin(PCLXL_CONTEXT pclxl_context);

Bool pclxl_mm_graph_paper(PCLXL_CONTEXT pclxl_context);

Bool pclxl_debug_elliptical_arc(PCLXL_CONTEXT pclxl_context,
                                PCLXL_SysVal_XY* tl,
                                PCLXL_SysVal_XY* bl,
                                PCLXL_SysVal_XY* br,
                                PCLXL_SysVal_XY* tr,
                                PCLXL_SysVal_XY* c,
                                PCLXL_SysVal_XY* cr,
                                PCLXL_SysVal_XY* ct,
                                PCLXL_SysVal_XY* cl,
                                PCLXL_SysVal_XY* cb,
                                PCLXL_SysVal_XY* p3,
                                PCLXL_SysVal_XY* p4,
                                PCLXL_SysVal_XY* sp,
                                PCLXL_SysVal_XY* ep);

void pclxl_debug_bezier_curve(PCLXL_CONTEXT pclxl_context,
                              PCLXL_SysVal_XY* sp,
                              PCLXL_SysVal_XY* cp1,
                              PCLXL_SysVal_XY* cp2,
                              PCLXL_SysVal_XY* ep);

#else /* DEBUG_BUILD */

extern Bool
pclxl_test(void);

#endif /* DEBUG_BUILD */

#endif /* __PCLXLTEST_H__ */

/******************************************************************************
* Log stripped */
