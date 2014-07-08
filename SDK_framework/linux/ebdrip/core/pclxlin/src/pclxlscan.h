/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlscan.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to read various constructs from a PCLXL stream.
 * If successful they return the number of bytes actually read from the stream
 * If fail for whatever reason they return a negative number
 * If they encounter end-of-file (EOF), but have read one or more bytes beforehand
 * then they return the number of bytes read.
 * If they enounter end-of-file before reading any bytes then they return EOF (-1)
 * If they encounter end-of-file part way through reading a multi-byte value
 * then it is upto the function to decide whether to return EOF (-1) or some other
 * negative value.
 */

#ifndef __PCLXL_SCAN_H__
#define __PCLXL_SCAN_H__ 1

#include "pclxltypes.h"
#include "pclxlparsercontext.h"

int32 pclxl_scan(PCLXL_PARSER_CONTEXT parser_context);

#endif /* __PCLXL_SCAN_H__ */

/******************************************************************************
* Log stripped */
