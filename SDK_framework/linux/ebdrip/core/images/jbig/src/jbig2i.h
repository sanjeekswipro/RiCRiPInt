/** \file
 * \ingroup jbig
 *
 * $HopeName: COREjbig!src:jbig2i.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header file defining the private interface between the core JBIG2
 * implementation and the API layer.
 */

#ifndef __JBIG2I_H__
#define __JBIG2I_H__

/*
 * JBIG2 code reads its input data through an abstracted input stream.
 * Make the data type opaque to all but the lower level functions which
 * need to read the data. This helps to separate the core JBIG2 code 
 * from the underlying buffered stream I/O.
 */
typedef void * J2STREAM;

#if defined(DEBUG_BUILD)

extern int debug_jbig2;

/*
 * Debug levels
 */
#define J2DBG_PARSE  1
#define J2DBG_HUFF   2
#define J2DBG_SYM    4
#define J2DBG_MEM    8
#define J2DBG_TXT   16
#define J2DBG_WARN  32

#define DEBUG_JB2(_x, _y) if ((debug_jbig2 & (_x)) != 0 ) { _y; }
#define JB2ASSERT(_x, _y) if (!(_x)) { jb2fail(_y); }

#else /* !DEBUG_BUILD */

#define DEBUG_JB2(_x, _y) /* Do nothing */
#define JB2ASSERT(_x, _y)    /* Do nothing */

#endif /* DEBUG_BUILD */

/*
 * Basic memory and I/O abstractions implemented by the JBIG2 API layer
 * and used by the core JBIG2 code.
 */
char *jb2malloc(unsigned len, int zero);
void jb2free(char *ptr);
int jb2getc(J2STREAM *f);
int jb2error(J2STREAM *f, char *cp, int n);
void *jb2get_private(J2STREAM *f);
void jb2chars_in_outbuf(J2STREAM *f);
int jb2write(J2STREAM *f, char *ptr, int n);
void jb2countup(J2STREAM *f);
void jb2countdown(J2STREAM *f);
J2STREAM *jb2infile(J2STREAM *f);
int jb2refagg1();
J2STREAM *jb2openMMR (J2STREAM *f, int columns, int rows, int endofblock);
void jb2closeMMR(J2STREAM *f);
void jb2dbg(char *message, ... );
void jb2fail(char *message);
void jb2zero(char *ptr, int bytes);
int jb2cmp(char *p1, char *p2, int bytes);
void jb2copy(char *src, char *dest, int bytes);

/*
 * Private interface implemented by the core JBIG2 code and use by the
 * API layer in proving the public interface.
 */

void *jbig2open(J2STREAM *f);
int jbig2read(J2STREAM *f);
int jbig2close(J2STREAM *f);

#endif /* protection for multiple inclusion */

/*
 * Restarted log with new port of code from Jaws.
 *
* Log stripped */

/* End of jbig2i.h */
