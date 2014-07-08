/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:stream.h(EBDSDK_P.1) $
 * $Id: export:stream.h,v 1.7.2.1.1.1 2013/12/19 11:25:03 anon Exp $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic stream (encode/decode) methods
 */

#ifndef __STREAM_H__
#define __STREAM_H__

#include "fileioh.h" /* FILELIST */
#include "objects.h" /* OBJECT */

OBJECT *streamLookupDict( OBJECT *theo ) ;
FILELIST *streamLookup( OBJECT *theo ) ;

#endif /* protection for multiple inclusion */

/*
Log stripped */
