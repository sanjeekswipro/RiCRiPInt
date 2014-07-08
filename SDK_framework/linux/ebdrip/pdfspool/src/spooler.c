/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWpdfspool!src:spooler.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include "std.h"
#include "mem.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "spooler.h"


/* For this example, spooled PDF job data is stored in a file on the %tmp%
 * device, but it could just as easily be any other PS device, or even a custom
 * device that does not use the PS device interface.
 */
#define SPOOL_DEVICE        ((uint8*)"disktmp")
#define SPOOL_FILE_PREFIX   "PDFspool/"
/* File name is the counter as a 8 byte hex value */
#define SPOOL_FILENAME_LEN  (sizeof(SPOOL_FILE_PREFIX "xXxXxXxX"))

/* Spool file name counter. */
static uint32 next_file;


/* Structure to track spool file */
struct SPOOLER {
  DEVICELIST*           device;       /* Device to write spooled PDF data to. */
  DEVICE_FILEDESCRIPTOR fd;           /* File descriptor for spool file. */
  Hq32x2                pdf_length;   /* Length of PDF data in spool file. */
  Hq32x2                position;     /* Position in PDF data when being read. */
  Hq32x2                spool_length; /* Total length of spooled data. */
  uint8                 file_name[SPOOL_FILENAME_LEN]; /* Spooled data file name */
};


/* Starts a new PDF spooler file.  Returns a pointer to the spooler file, or
 * NULL if unable to create a new one. */
SPOOLER* spooler_new(void)
{
  DEVICELIST* device;
  SPOOLER*    spooler;

  /* Look up PS device to write spooled data to */
  if ( (device = SwFindDevice(SPOOL_DEVICE)) == NULL ) {
    return(NULL);
  }

  spooler = MemAlloc(sizeof(SPOOLER), FALSE, FALSE);
  if ( spooler != NULL ) {
    /* Generate new spool file name */
    swncopyf(spooler->file_name, SPOOL_FILENAME_LEN, (uint8*)"%s%08x", SPOOL_FILE_PREFIX, next_file);
    next_file++;

    /* Open file to spool data to */
    spooler->device = device;
    spooler->fd = (*theIOpenFile(spooler->device))(spooler->device, spooler->file_name,
                                                   (SW_RDWR|SW_CREAT|SW_TRUNC));
    if ( spooler->fd < 0 ) {
      spooler_end(spooler);
      return(NULL);
    }

    /* Reset spooled data lengths */
    Hq32x2FromInt32(&spooler->pdf_length, 0);
    Hq32x2FromInt32(&spooler->position, 0);
    Hq32x2FromInt32(&spooler->spool_length, 0);
  }

  return(spooler);

} /* spooler_new */


/* Finish with the spooled data file. */
void spooler_end(
  SPOOLER*  spooler)
{
  if ( spooler->fd >= 0 ) {
    /* Close and delete the spool file */
    (void)(*theICloseFile(spooler->device))(spooler->device, spooler->fd);
    (*theIDeleteFile(spooler->device))(spooler->device, spooler->file_name);
  }

  MemFree(spooler);

} /* spooler_end */


/* Writes an amount of data to the spool file.  If there was an error writing
 * the data to the spool file then the function returns FALSE.  The amount of
 * spooled data is updated by the amount written. */
HqBool spooler_append(
  void*   spooler_data,
  uint8*  data,
  int32   length)
{
  int32     written;
  SPOOLER*  spooler = spooler_data;
  Hq32x2    temp;

  while ( length > 0 ) {
    written = (*theIWriteFile(spooler->device))(spooler->device, spooler->fd, data, length);
    if ( written < 0 ) {
      return(FALSE);
    }
    Hq32x2FromInt32(&temp, written);
    Hq32x2Add(&spooler->spool_length, &spooler->spool_length, &temp);
    data += written;
    length -= written;
  }

  return(TRUE);

} /* spooler_append */


/* Record the amount of data the PDF parser states is PDF job data.
 */
void spooler_set_pdf_length(
  void*   spooler_data,
  Hq32x2* length)
{
  SPOOLER*  spooler = spooler_data;

  spooler->pdf_length = *length;

} /* spooler_set_pdf_length */


/* Return the amount of PDF job data. */
Hq32x2 spooler_get_pdf_length(
  SPOOLER*  spooler)
{
  return(spooler->pdf_length);

} /* spooler_get_pdf_length */


/* Return the number of extra data read when determining the amount of PDF job
 * data. */
Hq32x2 spooler_get_over_read(
  SPOOLER*  spooler)
{
  Hq32x2  result;

  Hq32x2Subtract(&result, &spooler->spool_length, &spooler->pdf_length);
  return(result);

} /* spooler_get_over_read */


/* Prepare spooled data for reading PDF job data */
HqBool spooler_open_pdf(
  SPOOLER*  spooler)
{
  /* Seek back to the start of the spooled data */
  Hq32x2FromInt32(&spooler->position, 0);
  return((*theISeekFile(spooler->device))(spooler->device, spooler->fd, &spooler->position, SW_SET));

} /* spooler_open_pdf */


/* Note that the PDF job data has finished being read */
int32 spooler_close_pdf(
  SPOOLER*  spooler)
{
  UNUSED_PARAM(SPOOLER*, spooler);

  /* Nothing to do here */
  return(0);

} /* spooler_close_pdf */


/* Read PDF job data from spooled data */
int32 spooler_read_pdf(
  SPOOLER*  spooler,
  uint8*    buff,
  int32     len)
{
  int32   bytes;
  Hq32x2  temp;

  /* If the amount requested would extend beyond the end of the PDF data in the
   * spool file then limit it to the length of the PDF data */
  Hq32x2FromInt32(&temp, len);
  Hq32x2Add(&temp, &temp, &spooler->position);
  if ( Hq32x2Compare(&temp, &spooler->pdf_length) > 0 ) {
    Hq32x2Subtract(&temp, &spooler->pdf_length, &spooler->position);
    if ( !Hq32x2ToInt32(&temp, &len) ) {
      return(-1);
    }
  }

  bytes = (*theIReadFile(spooler->device))(spooler->device, spooler->fd, buff, len);

  /* Keep track of the read position in the PDF data */
  if ( bytes > 0 ) {
    Hq32x2FromInt32(&temp, bytes);
    Hq32x2Add(&spooler->position, &spooler->position, &temp);
  }

  return(bytes);

} /* spooler_read_pdf */


/* Do a seek on the PDF portion of spooled data */
int32 spooler_seek_pdf(
  SPOOLER*  spooler,
  Hq32x2*   destn,
  int32     flags)
{
  Hq32x2  zero;
  Hq32x2  temp;

  Hq32x2FromInt32(&zero, 0);

  switch ( flags ) {
  case SW_XTND:
    if ( !Hq32x2IsZero(destn) ) {
      return(FALSE);
    }
    /* Set file position to end of PDF data - need to change seek flags sent to
     * spool device.
     */
    *destn = spooler->position = spooler->pdf_length;
    flags = SW_SET;
    break;

  case SW_SET:
    /* Catch destination less than 0 or beyond end of PDF data */
    if ( (Hq32x2Compare(&zero, destn) > 0) ||
         (Hq32x2Compare(destn, &spooler->pdf_length) > 0) ) {
      return(FALSE);
    }
    spooler->position = *destn;
    break;

  case SW_INCR:
    /* Catch destination less than 0 or beyond end of PDF data */
    Hq32x2Add(&temp, &spooler->position, destn);
    if ( (Hq32x2Compare(&zero, &temp) > 0) ||
         (Hq32x2Compare(&temp, &spooler->pdf_length) > 0) ) {
      return(FALSE);
    }
    spooler->position = temp;
    break;
  }

  return((*theISeekFile(spooler->device))(spooler->device, spooler->fd, destn, flags));

} /* spooler_seek_pdf */


/* Return bytes available for PDF job data */
int32 spooler_bytes_pdf(
  SPOOLER*  spooler,
  Hq32x2*   bytes,
  int32     reason)
{
  switch ( reason ) {
  case SW_BYTES_AVAIL_REL: /* How many bytes are available to read */
    Hq32x2Subtract(bytes, &spooler->pdf_length, &spooler->position);
    break;

  case SW_BYTES_TOTAL_ABS: /* How many bytes in totoal */
    *bytes = spooler->pdf_length;
    break;
  }

  return(TRUE);

} /* spooler_bytes_pdf */


/* Reset the spooler for reading the excess spooled data */
HqBool spooler_open_excess(
  SPOOLER*  spooler)
{
  return(theISeekFile(spooler->device))(spooler->device, spooler->fd, &spooler->pdf_length, SW_SET);

} /* spooler_open_excess */


/* Finish reading the excess spooled spooled data */
void spooler_close_excess(
  SPOOLER*  spooler)
{
  UNUSED_PARAM(SPOOLER*, spooler);

} /* spooler_close_excess */


/* Read the excess spooled data */
int32 spooler_read_excess(
  SPOOLER*  spooler,
  uint8*    buff,
  int32     len)
{
  return((*theIReadFile(spooler->device))(spooler->device, spooler->fd, buff, len));

} /* spooler_read_excess */


/* Return the status of the device containing the spooled data */
int32 spooler_status_device(
  SPOOLER*  spooler,
  DEVSTAT*  devstat)
{
  /* Get the device status of the storage device being used */
  return((*theIStatusDevice(spooler->device))(spooler->device, devstat));

} /* spooler_status_device */

/* eof */
