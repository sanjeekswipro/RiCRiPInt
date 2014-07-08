/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pdfspool.c(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup OIL
 *  \brief This file contains the implementations of the OIL PDF spooling functions.
 *
 */

#include "oil_interface_oil2pms.h"
#include "oil_pdfspool.h"
#include "oil_malloc.h"


/**
 * \brief Memory allocation function for creating a new PDF spooler.
 *
 * This function allocates a block of memory of the specified size 
 * from the OIL memory pool.
 * \param[in]   cbSize      The amount of memory to allocate, in bytes.
 * \return      Returns a \c void pointer to the newly allocated memory.
 */
static void * OIL_PdfSpool_MemAllocFn( size_t cbSize )
{
  return OIL_malloc( OILMemoryPoolJob, OIL_MemBlock, cbSize );
}


/**
 * \brief Memory de-allocation function for the PDF spooler.
 *
 * This function frees the specified a block of memory 
 * back to the OIL memory pool.
 * \param[in]   pMem      A pointer to the memory to be freed.
 */
static void OIL_PdfSpool_MemFreeFn( void * pMem )
{
  OIL_free( OILMemoryPoolJob, pMem );
}


/**
 * \brief Create a PDF spooler.
 *
 * \return Returns a pointer to a newly created PDFSPOOL structure, or NULL if there is an error.
 */
PDFSPOOL * OIL_PdfSpool_Create( void )
{
  return pdfspool_new( OIL_PdfSpool_MemAllocFn, OIL_PdfSpool_MemFreeFn );
}


/**
 * \brief Free a PDF spooler
 *
 * \param pdfspool The PDFSPOOL structure, obtained via OIL_PdfSpool_Create(), which is to be freed.
 */
void OIL_PdfSpool_Free( PDFSPOOL * pdfspool )
{
  pdfspool_end( pdfspool );
}


/**
 * \brief Stores job data in a PDF spooler.
 *
 * This function reads data from the PMS data stream and passes it to the PDF
 * spooler until either the end of the PDF file is reached, or some problem is detected in 
 * the incoming PDF data.
 *
 * \param pdfspool A PDFSPOOL object obtained via OIL_PdfSpool_Create.
 *
 * \return Returns TRUE if the whole PDF file is successfully read in and passed to the 
 *  spooler, FALSE otherwise.
 */
int32 OIL_PdfSpool_StoreData( PDFSPOOL * pdfspool )
{
 int32 fSuccess = FALSE;

  int32 status;
  unsigned char buffer[ OIL_READBUFFER_LEN ];
  int32 nBytesRead;

  /* Repeatedly store data in the spooler */
  nBytesRead = PMS_PeekDataStream( buffer, sizeof(buffer) );
  status = pdfspool_pdf( pdfspool, buffer, nBytesRead );

  while( status == SPOOL_MORE )
  {
    PMS_ConsumeDataStream( nBytesRead );
    nBytesRead = PMS_PeekDataStream( buffer, sizeof(buffer) );
    
    if( nBytesRead == 0 )
    {
      break;
    }

    status = pdfspool_pdf( pdfspool, buffer, nBytesRead );
  }

  if( status == SPOOL_MORE && nBytesRead == 0 )
  {
    /* No more data.  Are we at end of PDF? */
    status = pdfspool_eof( pdfspool );
  }

  if( status == SPOOL_EOJ )
  {
    /* Determine how much of the last chunk of data is PDF data
     * and tell PMS that we've consumed it.
     */
    if( nBytesRead > 0 )
    {
      Hq32x2 nBytesExcess = pdfspool_excess_bytes( pdfspool );

      PMS_ConsumeDataStream( nBytesRead - nBytesExcess.low );
    }

    fSuccess = pdfspool_use_pdf( pdfspool );
  }
  else
  {
    /* At end of data but not end of PDF, or some error, or not PDF data at all */
    if( nBytesRead > 0 )
    {
      PMS_ConsumeDataStream( nBytesRead );
    }
  }

  return fSuccess;
}


