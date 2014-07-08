/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:std_file.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS standard file defines
 */

#ifndef __STD_FILE_H__
#define __STD_FILE_H__

#include "fileioh.h" /* FILELIST */

/* standard files are in the table std_files
 * Do not change this order without changing std_files.c appropriately.
 */

enum {
  INVALIDFILE,
  LINEEDIT,
  STATEMENTEDIT,
  STDIN,
  STDOUT,
  STDERR,
  NUMBER_OF_STANDARD_FILES /* Must be last in enum */
} ;

extern FILELIST *std_files ;

extern Bool stream_echo_flag;

void init_std_files_table( void );

struct core_init_fns ; /* from SWcore */

void std_files_C_globals(struct core_init_fns *fns) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
