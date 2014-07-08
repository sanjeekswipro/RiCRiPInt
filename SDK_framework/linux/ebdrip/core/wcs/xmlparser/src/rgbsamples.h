/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!xmlparser:src:rgbsamples.h(EBDSDK_P.1) $
 * $Id: xmlparser:src:rgbsamples.h,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ColorDeviceModel callbacks for wcsNonNegRGBSamples
 */

#ifndef __RGBSAMPLES_H__
#define __RGBSAMPLES_H__

#include "xml.h"       /* core XML interface */

Bool wcs_RGBSampleWithMaxOccurs_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      uint32 maxoccurs) ;

Bool wcs_RGB_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs) ;

Bool wcs_RGBCIEXYZ_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs) ;

#endif

/*
* Log stripped */
