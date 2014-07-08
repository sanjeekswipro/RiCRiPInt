/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:export:wcsxmlparser.h(EBDSDK_P.1) $
 * $Id: xmlparser:export:wcsxmlparser.h,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Parse a WCS XML profile stream into the appropriate WCS data structures.
 */

#ifndef __WCSXMLPARSER_H__
#define __WCSXMLPARSER_H__

#include "swmemapi.h"
#include "fileio.h"

Bool wcs_parse_cdmp(sw_memory_instance *memstate, FILELIST *cdmp, wcsColorDeviceModel **cdm) ;
Bool wcs_parse_camp(sw_memory_instance *memstate, FILELIST *camp, wcsColorAppearanceModel **cam) ;
Bool wcs_parse_gmmp(sw_memory_instance *memstate, FILELIST *gmmp, wcsGamutMapModel **gmm) ;

void wcs_destroy_cdm(sw_memory_instance *memstate, wcsColorDeviceModel *cdm) ;
void wcs_destroy_cam(sw_memory_instance *memstate, wcsColorAppearanceModel *cam) ;
void wcs_destroy_gmm(sw_memory_instance *memstate, wcsGamutMapModel *gmm) ;

#endif

/*
* Log stripped */
