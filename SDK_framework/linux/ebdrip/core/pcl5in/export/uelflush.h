/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!export:uelflush.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for flushing stream up to the next UEL or end of file.
 */

#ifndef __UELFLUSH_H__
#define __UELFLUSH_H__  (1)

#include "objects.h"

/**
 * \brief Flush input stream up to the next UEL or EOF.
 *
 * \param[in] ofile
 * File stream object to flush.
 *
 * \return \c TRUE if successfully flushed input stream, else \c FALSE.
 */
extern
int32 uelflush(
  OBJECT*   ofile);

#endif /* !__UELFLUSH_H__ */

/*
* Log stripped */
/* EOF uelflush.c */
