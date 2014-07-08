/* $HopeName: SWripiimpl!export:jobcnsmr.h(EBDSDK_P.1) $
 */

/*
 * This module defines an abstract C++ API version of the JobStatusConsumer interface
 */

#ifndef _incl_jobcnsmr
#define _incl_jobcnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"     /* C/C++ RIP API general header */

#ifdef __cplusplus

#include "ripiface.hh"  // RIPInterface generated from IDL, includes HqnTypes
#include "ripcnsmr.h"   // RIPConsumer, (abstract) base class of JobStatusConsumer

#include "hostserv.hh"  // Host

#include "fwstring.h"   // FwTextString

/* ----------------------- Classes ----------------------------------------- */

// definition of abstract class JobStatusConsumer
// clients of RIP API service define an implementation of this class via inheritance

class RIPAPI_JobStatusConsumer : public RIPAPI_RIPConsumer
{
public:
  // abstract definition of job_started callback
  // called when the job has started
  virtual void job_started(RIPInterface::JobID job) = 0;

  // abstract definition of job_start_page callback
  // called when the specified page has finished ripping
  virtual void job_start_page(RIPInterface::JobID job, RIPInterface::PageID page) = 0;

  // abstract definition of job_ripped_page callback
  // called when the specified page has finished ripping
  virtual void job_ripped_page(RIPInterface::JobID job, RIPInterface::PageID page) = 0;

  // abstract definition of job_finished callback
  // called when the job has finished
  virtual void job_finished(RIPInterface::JobID job, int32 fPostScriptError) = 0;

  // abstract definition of input_channels_enabled callback
  // called when the input channels are enabled or disabled
  virtual void input_channels_enabled(int32 enabled) = 0;

  // abstract definition of uni_set_job_name callback
  // called when jobname is set
  virtual void uni_set_job_name
  ( RIPInterface::JobID job, const HqnTypes::UnicodeString & name ) = 0;


  // abstract definition of job_ripped_page_buffer
  // called when RIP writes a page buffer for the job/page combo
  virtual void job_ripped_page_buffer( RIPInterface::JobID job, 
                                       RIPInterface::PageID page,
                                       const HostInterface::File_var & vFile,
                                       const HqnTypes::UnicodeString & deviceName,
                                       int32 xResolution,
                                       int32 yResolution,
                                       int32 imageWidth,
                                       int32 imageHeight,
                                       const HqnTypes::UnicodeString & sepColorName,
                                       int32 rasterDepth ) = 0;

  // abstract definition of job_issued_warning_6
  // called when a warning-level monitor message was generated for the job
  virtual void job_issued_warning( RIPInterface::JobID job ) = 0;

  // abstract definition of job_ripped_byte_map_file_8
  // called when a file output plugin (such as TIFF) produces a page or separation
  virtual void job_ripped_byte_map_file
    (
      RIPInterface::JobID job,
      RIPInterface::PageID page,
      const char *pszFileType,
      const HostInterface::File_var &vFile,
      const HqnTypes::UnicodeString &sepColorName
    ) = 0;

  virtual ~RIPAPI_JobStatusConsumer() {}

}; // class JobStatusConsumer

/* Helper function that uses Host::open_file_2() to convert a
   platform-specific filename text string into a HostInterface::File object
   reference. This functionality is needed by both the
   job_ripped_page_buffer_5 and job_ripped_byte_map_file_8 callbacks.
 
   The pointer returned by this function becomes owned by the caller.

   This can return NIL if there is no SOAR Host Server running locally with
   the RIP.

   Any exception thrown by the open_file_2 call will be thrown by this method.
*/
extern HostInterface::File_ptr getHostServerFileReference( FwTextString pszFileName );

#endif /* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

extern void notifyStartOfJob(int32 nJobNumber);
extern void notifyJobStartPage(int32 nJobNumber, int32 nPageNumber);
extern void notifyJobOutputPage(int32 nJobNumber, int32 nPageNumber);
extern void notifyEndOfJob(int32 nJobNumber, int32 fPostScriptError);
extern void notifyInputChannelsEnabledForJobStatusConsumer(int32 enabled);
extern void notifyUniSetJobName( int32 nJobNumber, FwUniString name );
extern void notifyJobOutputPageBuffer( int32 jobNumber, 
                                       int32 pageNumber, 
                                       FwTextString pageBufferName,
                                       FwTextString deviceName,
                                       int32 xResolution,
                                       int32 yResolution,
                                       int32 imageWidth,
                                       int32 imageHeight,
                                       FwTextString sepColorName,
                                       int32 rasterDepth
                                       );
extern void notifyJobWarning( int32 njobNumber );
extern void notifyJobOutputByteMap
  (
    int32 nJobNumber,
    int32 nPageNumber,
    const char *pszFileType,
    FwTextString fileName,
    FwTextString sepColorName
  );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* _incl_jobcnsmr */

/* jobcnsmr.h */
