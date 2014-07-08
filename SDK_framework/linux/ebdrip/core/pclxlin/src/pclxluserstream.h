/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxluserstream.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCLXL(in) stream interface and stream header information.
 */

#ifndef __PCLXLUSERSTREAM_H__
#define __PCLXLUSERSTREAM_H__ 1

#include "fileio.h"         /* for FILELIST */

#include "pclxlcontext.h"
#include "mm.h"


Bool pclxl_mount_user_defined_stream_device(void) ;

void pclxl_unmount_user_defined_stream_device(void) ;


/**
 * \brief Create a stream cache.
 */
Bool pclxl_stream_cache_create(PCLXLStreamCache **stream_cache, uint32 table_size,
                               mm_pool_t memory_pool) ;

/**
 * \brief Destroy a stream cache.
 */
void pclxl_stream_cache_destroy(PCLXLStreamCache **stream_cache) ;

/**
 * \brief Initialise the PCL XL user defined stream device type,
 * adding it to the list of acceptable device types. This is done at
 * RIP boot time if PCL XL is enabled.
 */
Bool pclxl_user_streams_init(void) ;

extern
void close_stream(
  PCLXL_CONTEXT pclxl_context,
  FILELIST **readstream);

/**
 * \brief Finish the PCL XL user defined stream device type. This is
 * done at RIP shutdown if PCL XL is enabled.
 */
void pclxl_user_streams_finish(void) ;

void pclxl_initialize_streams(void) ;

#endif /* __PCLXLUSERSTREAM_H__ */

/******************************************************************************
* Log stripped */
