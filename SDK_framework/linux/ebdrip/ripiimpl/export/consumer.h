#ifndef __CONSUMER_H__
#define __CONSUMER_H__

#ifdef PRODUCT_HAS_API

/* $HopeName: SWripiimpl!export:consumer.h(EBDSDK_P.1) $
 *
 * Print File Queue Handling
 */


/* ----------------------- Includes ---------------------------------------- */

#include "ripiimpl.h"    /* includes std.h ... */
#include "fwstring.h"    /* FwTextString */

/* ----------------------- Macros ------------------------------------------- */

/* Status values for ripapiNotifyLicenseServerStatus */
#define LSS_LOST_CONTACT                1
#define LSS_REGAINED_CONTACT            2
#define LSS_REGAINED_CONTACT_NO_LICENSE 3
#define LSS_TIMED_OUT                   4

/* ----------------------- Types ------------------------------------------- */

struct CSDial;

/* ----------------------- Functions --------------------------------------- */

/*
 * Job Status consumers
 */


/* corresponds to job_started_1 */
extern void ripapiNotifyStartOfJob(int32 nJobNumber);

/* corresponds to job_finished_1 */
extern void ripapiNotifyEndOfJob(int32 nJobNumber, int32 fPostScriptError);

/* corresponds to job_start_page_7 */
extern void ripapiNotifyJobStartPage(int32 nJobNumber, int32 nPageNumber);

/* corresponds to job_ripped_page_1 */
extern void ripapiNotifyJobOutputPage(int32 nJobNumber, int32 nPageNumber);

/* corresponds to input_channels_enabled_2 */
extern void ripapiNotifyInputChannelsEnabled(int32 enabled);

/* corresponds to license_server_status_changed_3 */
extern void ripapiNotifyLicenseServerStatus(int32 status);

/* corresponds to uni_set_job_name_3 */
extern void ripapiNotifyUniSetJobName( int32 nJobNumber, FwTextString name );

/* corresponds to job_ripped_page_buffer_4 */
extern void ripapiNotifyJobOutputPageBuffer( int32 nJobNumber, 
                                             int32 nPageNumber, 
                                             FwTextString pageBufferName,
                                             FwTextString deviceName,
                                             int32 xResolution,
                                             int32 yResolution,
                                             int32 imageWidth,
                                             int32 imageHeight,
                                             FwTextString sepColorName,
                                             int32 rasterDepth
                                            );

/* corresponds to job_issued_warning_6 */
extern void ripapiNotifyJobWarning( int32 nJobNumber );

/* corresponds to job_ripped_byte_map_file_8 */
extern void ripapiNotifyJobOutputByteMap
  (
    int32 nJobNumber,
    int32 nPageNumber,
    const char *pszFileType,
    FwTextString fileName,
    FwTextString sepColorName
  );

/*
 * Monitor consumers
 */

#define CONSUMER_NOJOBID (-1)
extern void ripapiNotifyMonitor(FwTextString str, int32 jobid);

/*
 * Progress consumers
 */

extern void ripapiNotifyStartProgress(struct CSDial * pCSDial);

extern void ripapiNotifyUpdateProgress(struct CSDial * pCSDial);

extern void ripapiNotifyEndProgress(struct CSDial * pCSDial);

/*
 * Throughput Status consumers
 */

/* corresponds to tp_stat_output_enabled_1 */
extern void ripapiNotifyOutputEnabled( int32 enabled );

/* corresponds to tp_stat_pagebuffers_added_1 */
extern void ripapiNotifyPageBufferAdded
  (
    int32 iQueue,
    int32 pageID,
    int32 nJobNumber
  );

/* corresponds to tp_stat_pagebuffers_removed_1 */
extern void ripapiNotifyPageBufferRemoved
  (
    int32 iQueue,
    int32 reason,
    int32 pageID,
    int32 nJobNumber
  );

/* corresponds to tp_stat_output_error_1 */
extern void ripapiNotifyOutputError
  (
    int32 pageID,
    int32 nJobNumber,
    int32 errorNumber,
    FwTextString errorString,
    int32 disable
  );

/* corresponds to tp_stat_all_pages_output_1 */
extern void ripapiNotifyAllPagesOutput( int32 nJobNumber, int32 nPagesOuput );

#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* protection for multiple inclusion */

/* eof consumer.h */
