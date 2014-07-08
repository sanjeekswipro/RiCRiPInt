/** \file
 * \brief
 * Platform and configuration definitions for libpng in ScriptWorks environment.
 *
 * $HopeName: HQNlibpng!export:pngusr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005 Global Graphics Software Ltd.  All Rights Reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#ifndef PNGUSR_H
#define PNGUSR_H 1

#ifdef VXWORKS
#include "vxWorks.h"
#endif

/* On some 64 processors it seems that the alignment of the argument
   to setjmp/longjmp *MUST* be 16 byte aligned. See request 61836. */
#define PNG_ALIGN_JMPBUF

/* We don't need PNG in MNG datastreams. */
#define PNG_NO_MNG_FEATURES

/* We don't want standard IO. */
#define PNG_NO_STDIO

/* No need for tIME functions. */
#define PNG_NO_READ_tIME
#define PNG_NO_WRITE_tIME

#endif /* PNGUSR_H */

/*
Log stripped */
