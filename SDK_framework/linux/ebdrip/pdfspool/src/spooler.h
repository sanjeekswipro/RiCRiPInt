/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpdfspool!src:spooler.h(EBDSDK_P.1) $
 */

#ifndef __SPOOLER_H__
#define __SPOOLER_H__  (1)

#include  "std.h"
#include  "swdevice.h"

typedef struct SPOOLER SPOOLER;

/* Create a new spool file */
extern
SPOOLER* spooler_new(void);

/* Release a spool file */
extern
void spooler_end(
  SPOOLER*  spooler);


/* Functions for PDF parser spool interface */
/* Append more data to the spool file */
extern
HqBool spooler_append(
  void*     spooler_data,
  uint8*    data,
  int32     length);

/* Set the amount of spooled data that is the PDF job */
extern
void spooler_set_pdf_length(
  void*     spooler_data,
  Hq32x2*   length);

/* Return the amount of spooled data that is the PDF job */
extern
Hq32x2 spooler_get_pdf_length(
  SPOOLER*  spooler);

/* Return the amount of extra spooled data */
extern
Hq32x2 spooler_get_over_read(
  SPOOLER*  spooler);


/* Functions used by the spooler PS device */
/* Prepare spooled data for reading PDF job data */
extern
HqBool spooler_open_pdf(
  SPOOLER*  spooler);

/* Finish reading the PDF job from the spooled data */
extern
int32 spooler_close_pdf(
  SPOOLER*  spooler);

/* Read PDF job data from the spooled data */
extern
int32 spooler_read_pdf(
  SPOOLER*  spooler,
  uint8*    buff,
  int32     len);

/* Seek on the PDF job data in the spooled data */
extern
int32 spooler_seek_pdf(
  SPOOLER*  spooler,
  Hq32x2*   destn,
  int32     flags);

/* Return number of bytes of PDF job data in the spooled data available */
extern
int32 spooler_bytes_pdf(
  SPOOLER*  spooler,
  Hq32x2*   bytes,
  int32     reason);

/* Reset the spooler for reading the excess spooled data */
extern
HqBool spooler_open_excess(
  SPOOLER*  spooler);

/* Finish reading the excess spooled spooled data */
extern
void spooler_close_excess(
  SPOOLER*  spooler);

/* Read the excess spooled data */
extern
int32 spooler_read_excess(
  SPOOLER*  spooler,
  uint8*    buff,
  int32     len);

/* Return status information on the device holding the spooled data */
extern
int32 spooler_status_device(
  SPOOLER*  spooler,
  DEVSTAT*  devstat);

#endif /* !__SPOOLER_H__ */

/* eof */
