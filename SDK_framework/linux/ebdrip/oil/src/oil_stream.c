/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_stream.c(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief OIL Stream device implementation.
 *
 *  The interface implementations in this file provides an implementation 
 *  of the  \c HqnReadStream interface which can be registered with the Skin.
 */

#include "oil_interface_oil2pms.h"
#include "oil_page_handler.h"
#include "skinkit.h"
#include "streams.h"
#include "streamdev.h"
#include <stdio.h>
#include <string.h> /* for strncpy */

#ifdef USE_UFST5
#define PCLFONTLIST
#endif
#ifdef USE_UFST7
#define PCLFONTLIST
#endif
#ifdef USE_FF
#define PCLFONTLIST
#endif

/* extern variables */
extern OIL_TyConfigurableFeatures g_ConfigurableFeatures;
extern OIL_TyJob *g_pstCurrentJob;
extern OIL_TyError g_JobErrorData;

typedef struct OIL_TyStreamBuff
{
  int cbPreviousRead;

  Hq32x2 cbTotalBytesRead;
  Hq32x2 cbBytesConsumed;

} OIL_TyStreamBuff;

/* static allocations */
static HqnReadStream OIL_Stream;
static OIL_TyStreamBuff stStreamBuff;

/* Forward Declarations */
static int RIPCALL OIL_StreamClose(void *pPrivate);
static int RIPCALL OIL_StreamRead(void *pPrivate, unsigned char * pBuffer, int nSize);
static int RIPCALL OIL_StreamSeek(void *pPrivate, Hq32x2 *pHQ32x2, int nPosition);

static void RIPCALL OIL_StreamFlush(OIL_TyStreamBuff * pStreamBuff);

/**
 * \brief Called by the RIP to close the stream.
 * 
 * The implementation may perform any cleanup actions that are required.
 *
 * \param[in] pPrivate Unused parameter.  The private data pointer of the stream structure.
 *
 * \return Returns a value less than zero to indicate an error in closing
 * the stream, otherwise returns zero.
 */
static int RIPCALL OIL_StreamClose(void *pPrivate)
{
  UNUSED_PARAM( void *, pPrivate );

  GG_SHOW(GG_SHOW_OIL, "OIL_StreamClose()\n");

  if( g_pstCurrentJob->ePDLType == OIL_PDL_PS )
  {
    /* Indicate to the skin that all the job data has now been read.
     * Only needed for PS jobs because their job data is not
     * provided via the console device.
     */
    SwLeProcessingJobEnd();
  }

  g_pstCurrentJob->eJobStatus = OIL_Job_StreamDone;

  return 0;
}

/**
 * \brief Read from the input stream.
 *
 * \param[in] pPrivate The private data pointer from the stream structure. This is 
 * used to force missing/extra data into the stream. For example, 
 * after reading from the PMS to determine the PDL, we need to pass the read
 * data to the RIP for correct operation.
 *
 * \param[in,out]  pBuffer Pointer to a byte buffer to receive the next chunk of input data. 
 * This buffer memory is managed by the RIP. Do not attempt to free this memory, 
 * or to record a pointer to it for future reference.
 *
 * \param[in] nSize Maximum number of bytes that should be copied.
 *
 * \return If the read is successful, the function returns the actual
 * number of bytes copied. A value less than zero indicates an IO error.
 */
static int RIPCALL OIL_StreamRead(void * pPrivate, unsigned char * pBuffer, int nSize)
{
  OIL_TyStreamBuff * pStreamBuff = (OIL_TyStreamBuff *) pPrivate;
  Hq32x2             nBytesConsumed;
  int                nbytes_read;
  Hq32x2             nBytesRead;

  GG_SHOW(GG_SHOW_OIL, "OIL_StreamRead()\n");

  if( g_pstCurrentJob->ePDLType == OIL_PDL_PS )
  {
    /* Indicate to the skin that job data is being read.
     * Only needed for PS jobs because their job data is not
     * provided via the console device.
     */
    SwLeProcessingPs();
  }
/* If the job is a request for a test oage then call the relevant function to create
    internal PDL data for the page 
    Note that PS Test Page returns a complete postscript data stream to create all the pages necessary
              PCL Test Page will return the data for each page seperately to stay within the buffer size */
  if(g_pstCurrentJob->eTestPage != OIL_TESTPAGE_NONE)
  {
    switch( g_pstCurrentJob->eTestPage )
      {
      case OIL_TESTPAGE_CFG:   
        CreateConfigTestPage(pBuffer);
        break ;
      case OIL_TESTPAGE_PS:
        CreatePSTestPage(pBuffer);
        break ;
      case OIL_TESTPAGE_PCL:  
#ifdef PCLFONTLIST
        CreatePCLTestPage(pBuffer);
#undef PCLFONTLIST
#endif
        break ;
      default:
        break;
      }
    nbytes_read = (int)strlen((char *)pBuffer);
    return nbytes_read;
  }
  /* If there is an error and the code is set to 2 we want to print the message */
  if(g_JobErrorData.Code > 1)
  {
    CreateErrorPage(pBuffer);
    nbytes_read = (int)strlen((char *)pBuffer);
    return nbytes_read;
  }

  if( pStreamBuff->cbPreviousRead > 0 )
  {
    /* Tell PMS we've consumed the data we read last time */
    PMS_ConsumeDataStream( pStreamBuff->cbPreviousRead );

    Hq32x2FromInt32( &nBytesConsumed, pStreamBuff->cbPreviousRead );
    Hq32x2Add( &pStreamBuff->cbBytesConsumed, &pStreamBuff->cbBytesConsumed, &nBytesConsumed );
  }

  nbytes_read = PMS_PeekDataStream( pBuffer, nSize );

  Hq32x2FromInt32( &nBytesRead, nbytes_read );
  Hq32x2Add( &pStreamBuff->cbTotalBytesRead, &pStreamBuff->cbTotalBytesRead, &nBytesRead );

  pStreamBuff->cbPreviousRead = nbytes_read;

  return nbytes_read;
}

/**
 * \brief Report availability of bytes in the input stream.
 *
 * This function can be called in either one of two modes, either to 
 * find out whether there are any bytes available in the stream, or 
 * to find out the total size of the stream. The mode is determined
 * by setting the reason parameter.
 * 
 * This implementation does not support retrieving the total size of 
 * a stream, or the actual number of bytes available.  If byte 
 * availability is requested, it will return TRUE to indicate that
 * bytes are available, but will set the number of available bytes to 
 * zero, indicating an unknown number.
 * \param[in]      pPrivate    Unused parameter.
 * \param[out]     pHQ32x2     A signed, 64-bit value to be updated with the bytes
 * available or total size of the stream as appropriate.
 *
 * \param[in]      reason Either <code>STREAM_BYTES_AVAILABLE</code> or
 * <code>STREAM_BYTES_TOTAL</code> to indicate what <code>pBytes</code>
 * should be set to.
 *
 * \return Return FALSE to indicate an error, TRUE if the call succeeds.
 * This implementation will return FALSE if the total size of the 
 * stream is requested.  
 *
 **/
static int RIPCALL OIL_StreamBytes(void * pPrivate, Hq32x2 *pHQ32x2, int reason)
{
  UNUSED_PARAM( void *, pPrivate );

  if( reason == STREAM_BYTES_AVAILABLE )
  {
    /* We indicate that bytes are immediately available, but not
       how many by setting pHQ32x2 to 0. */
    pHQ32x2->high = 0;
    pHQ32x2->low = 0;

    return TRUE;
  }
  else if( reason == STREAM_BYTES_TOTAL )
  {
    /* We don't know the total size of the stream */
    return FALSE;
  }

  return FALSE;
}

/**
 * \brief Perform a seek on the input stream.
 *
 * This stream does not support random access, but a seek operation
 * can still be used to retrieve the current position of the stream
 * pointer, or to flush the stream.
 *
 * The function is called by supplying a base position to start the seek from,
 * and a seek length which is to be interpreted relative to the base position.
 * \arg             To retrieve the current position of the seek pointer, request 
 *                  a zero-length seek from position STREAM_POSITION_CURRENT.
 * \arg             To flush the stream, request a zero-length seek from 
 *                  position STREAM_POSITION_END.
 *
 * \param[in]        pPrivate    Unused parameter.
 * \param[in, out]   pHQ32x2     A signed 64-bit value indicating the target seek length from 
 *                               the base position.  It is updated during the the call to indicate
 *                               the final position of the stream pointer after the seek operation.
 * \param[in]        nPosition   A flag indicating the position in the stream to start seeking from.
 *                               This should be one of \c STREAM_POSITION_START, 
 *                               \c STREAM_POSITION_CURRENT or \c STREAM_POSITION_END.
 * \return           Returns TRUE if the seek operation completed successfully, FALSE otherwise.
 *
 **/
static int RIPCALL OIL_StreamSeek(void * pPrivate, Hq32x2 *pHQ32x2, int nPosition)
{
  if( nPosition == STREAM_POSITION_CURRENT && Hq32x2IsZero( pHQ32x2 ) )
  {
    /* Need to update *pHQ32x2 with the stream's current position */
    OIL_TyStreamBuff * pStreamBuff = (OIL_TyStreamBuff *) pPrivate;

    *pHQ32x2 = pStreamBuff->cbTotalBytesRead;

    return TRUE;
  }
  else if( nPosition == STREAM_POSITION_END && Hq32x2IsZero( pHQ32x2 ) )
  {
    /* Need to flush stream, discarding any remaining input */
    OIL_StreamFlush( (OIL_TyStreamBuff *) pPrivate );

    return TRUE;
  }

  return FALSE;
}

/**
 * \brief Flush the stream.
 *
 * This function marks any data read up to the point of this call as consumed, 
 * then reads and discards any data remaining in the stream.
 * 
 * \param[in]   pStreamBuff     The stream buffer associated with the stream.
 **/
static void RIPCALL OIL_StreamFlush(OIL_TyStreamBuff * pStreamBuff)
{
  Hq32x2        nBytesConsumed;
  int           nbytes_read;
  unsigned char buffer[ OIL_READBUFFER_LEN ];

  if( pStreamBuff->cbPreviousRead > 0 )
  {
    /* Tell PMS we've consumed the data we read last time */
    PMS_ConsumeDataStream( pStreamBuff->cbPreviousRead );

    Hq32x2FromInt32( &nBytesConsumed, pStreamBuff->cbPreviousRead );
    Hq32x2Add( &pStreamBuff->cbBytesConsumed, &pStreamBuff->cbBytesConsumed, &nBytesConsumed );

    pStreamBuff->cbPreviousRead = 0;
  }

  /* Read and discard any remaining input */
  do
  {
    nbytes_read = PMS_PeekDataStream( buffer, sizeof(buffer) );

    if( nbytes_read > 0 )
    {
      PMS_ConsumeDataStream( nbytes_read );
    }
  } while( nbytes_read > 0 );
}

/**
 * \brief Reset the OIL stream.
 *
 * This routine resets OIL's <code>OIL_TyStreamBuff</code> structure.
 **/
void Stream_Reset(void)
{
  GG_SHOW(GG_SHOW_OIL, "Stream_Initialize()\n");

  stStreamBuff.cbPreviousRead = 0;
  Hq32x2FromInt32( &stStreamBuff.cbTotalBytesRead, 0 );
  Hq32x2FromInt32( &stStreamBuff.cbBytesConsumed, 0 );
}

/**
 * \brief Register the OIL stream device.
 *
 * This routine initializes OIL's implementation of \c HqnReadStream
 * and registers it with the Skin.
 **/
int Stream_Register(void)
{
  GG_SHOW(GG_SHOW_OIL, "Stream_Register()\n");

  OIL_Stream.pfClose = OIL_StreamClose;
  OIL_Stream.pfBytes = OIL_StreamBytes;
  OIL_Stream.pfRead = OIL_StreamRead;
  OIL_Stream.pfSeek = OIL_StreamSeek;
  OIL_Stream.pPrivate = &stStreamBuff;

  /* register the /input file input with the pipeline device */
  return (registerStreamReader("pipeline", "/input", &OIL_Stream ));
}

/**
 * \brief Unregister the OIL stream device.
 *
 * This routine unregisters OIL's implementation of \c HqnReadStream.
 **/
void Stream_Unregister(void)
{
  GG_SHOW(GG_SHOW_OIL, "Stream_Unregister()\n");
  UnregisterStreamReaderWriter("pipeline", "/input");
}

/**
 * \brief Called by the progress device when the RIP closes its input file.
 *
 * \param pBytesConsumed The number of bytes which have been consumed by the RIP.
 */
void Stream_SetBytesConsumed( Hq32x2 * pBytesConsumed )
{
  Hq32x2 remainder;

  /* Tell PMS we've consumed the data */
  Hq32x2Subtract( &remainder, pBytesConsumed, &stStreamBuff.cbBytesConsumed );

  if( remainder.low != 0 )
  {
    HQASSERT(remainder.low > 0, "Stream_SetBytesConsumed: RIP's consumed more than we've read");

    PMS_ConsumeDataStream( remainder.low );
  }
}

