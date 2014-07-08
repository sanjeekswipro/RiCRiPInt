/* $HopeName: SWripiimpl!export:prgcnsmr.h(EBDSDK_P.1) $
 *
 * This module defines an abstract C++ API version of the ProgressConsumer interface
 */

#ifndef _incl_prgcnsmr
#define _incl_prgcnsmr

#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

#include "ripapi.h"     /* C/C++ RIP API general header */
#include "hq32x2.h"     /* 64-bit support */
#include "fwunicod.h"   /* FwUniString */

#ifdef __cplusplus

#include "ripiface.hh"  // RIPInterface IDL-generated header 
#include "ripcnsmr.h"   // RIPConsumer, (abstract) base class of ProgressConsumer

/* ----------------------- Classes ----------------------------------------- */

// definition of abstract class ProgressConsumer
// clients of RIP API service define an implementation of this class via inheritance

class RIPAPI_ProgressConsumer : public RIPAPI_RIPConsumer
{
public:
  // abstract definition of start_progress callback
  // called when the progress of a particular type is started
  virtual void start_progress(RIPInterface::ProgressType type) = 0;

  // the following abstract methods are called to update a particular progress type
  virtual void update_publishing_progress(FwUniString channel_name) = 0;
  virtual void update_bound_progress(RIPInterface::JobID job,
                                     HqU32x2 done,
                                     HqU32x2 total,
                                     FwUniString name) = 0;
  virtual void update_unbound_progress(RIPInterface::JobID job,
                                       HqU32x2 done,
                                       HqU32x2 available,
                                       HqU32x2 total,
                                       FwUniString name) = 0;
  virtual void update_crd_generation_progress(RIPInterface::JobID job,
                                              long percent_done) = 0;
  virtual void update_recombination_progress(RIPInterface::JobID job,
                                             long page,
                                             long percent_done) = 0;
  virtual void update_screening_generation_progress(RIPInterface::JobID job,
                                                    long page,
                                                    long percent_done) = 0;
  virtual void update_trapping_transfer_progress(RIPInterface::JobID job,
                                                 long page,
                                                 long percent_done) = 0;
  virtual void update_trapping_generation_progress(RIPInterface::JobID job,
                                                   long page,
                                                   long percent_done) = 0;
  virtual void update_paint_to_disk_progress(RIPInterface::JobID job,
                                             long page,
                                             long lines_done,
                                             long lines_total) = 0;
  virtual void update_output_progress(RIPInterface::JobID job,
                                      long page,
                                      long lines_ripped,
                                      long lines_printed,
                                      long lines_total) = 0;
  virtual void update_compositing_progress(RIPInterface::JobID job,
                                           long page,
                                           long percent_done) = 0;
  virtual void update_preparing_to_render_progress(RIPInterface::JobID job,
                                                   long page,
                                                   long percent_done) = 0;
  virtual void update_throughput_progress(RIPInterface::JobID job,
                                          long page,
                                          long lines_ripped,
                                          long lines_printed,
                                          long lines_total) = 0;
  virtual void update_PDF_scanning_progress(RIPInterface::JobID job,
                                            long page,
                                            long percent_done) = 0;
  virtual void update_PDF_counting_pages_progress(RIPInterface::JobID job,
                                                  long page,
                                                  long percent_done) = 0;

  // abstract definition of end_progress_1 callback
  // called when the progress of a particular type is finished
  virtual void end_progress(RIPInterface::ProgressType type) = 0;

}; // class ProgressConsumer

#endif /* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

extern void notifyStartProgress(RIPAPI_ProgressType type);
extern void notifyUpdatePublishingProgress(FwUniString channel_name);
extern void notifyUpdateBoundProgress(RIPAPI_JobID jobID,
                                      HqU32x2 * doneAmount,
                                      HqU32x2 * totalAmount,
                                      FwTextString jobName);
extern void notifyUpdateUnboundProgress(RIPAPI_JobID jobID,
                                        HqU32x2 * doneAmount,
                                        HqU32x2 * availableAmount,
                                        HqU32x2 * totalAmount,
                                        FwTextString jobName);
extern void notifyUpdateCRDGenerationProgress(RIPAPI_JobID job,
                                              int32 percent_done);
extern void notifyUpdateRecombinationProgress(RIPAPI_JobID job,
                                              int32 page,
                                              int32 percent_done);
extern void notifyUpdateScreeningGenerationProgress(RIPAPI_JobID job,
                                                    int32 page,
                                                    int32 percent_done);
extern void notifyUpdateTrappingTransferProgress(RIPAPI_JobID job,
                                                 int32 page,
                                                 int32 percent_done);
extern void notifyUpdateTrappingGenerationProgress(RIPAPI_JobID job,
                                                   int32 page,
                                                   int32 percent_done);
extern void notifyUpdatePaintToDiskProgress(RIPAPI_JobID jobID,
                                            int32 page,
                                            int32 lines_done,
                                            int32 lines_total);
extern void notifyUpdateOutputProgress(RIPAPI_JobID jobID,
                                       int32 page,
                                       int32 lines_ripped,
                                       int32 lines_printed,
                                       int32 lines_total);
extern void notifyUpdateCompositingProgress(RIPAPI_JobID job,
                                            int32 page,
                                            int32 percent_done);
extern void notifyUpdatePreparingToRenderProgress(RIPAPI_JobID job,
                                                  int32 page,
                                                  int32 percent_done);
extern void notifyUpdateThroughputProgress(RIPAPI_JobID jobID,
                                           int32 page,
                                           int32 lines_ripped,
                                           int32 lines_printed,
                                           int32 lines_total);
extern void notifyUpdatePDFScanningProgress(RIPAPI_JobID job,
                                            int32 page,
                                            int32 percent_done);
extern void notifyUpdatePDFCountingPagesProgress(RIPAPI_JobID job,
                                                 int32 page,
                                                 int32 percent_done);
extern void notifyEndProgress(RIPAPI_ProgressType type);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* _incl_prgcnsmr */
