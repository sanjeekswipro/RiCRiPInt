/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_job_handler.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file declares the interface to the OIL's job handling functionality
 *
 *  Job handling in the OIL is implemented via a simple linked list of OIL_TyJob structures. 
 *  Functions are provided for creating a job, deleting a job and retrieving a job from the list.
 *  Creating or deleting a job automatically adds it to or removes it from the job list.
 *
 *  Each job is identified by a unique job ID.  This must be supplied in order to delete a job
 *  or retrieve a pointer to a job.
 */

#ifndef _OIL_JOB_HANDLER_H_
#define _OIL_JOB_HANDLER_H_

/*! \brief The linked list which hold the jobs.
*/
typedef struct stJobList{
    OIL_TyJob *    pstJob;                  /**< A pointer to the OIL job held by this record */
    struct stJobList * pNext;               /**< pointer to next OIL job in the list*/
}OIL_TyJobList;

/* job handler interface */
extern OIL_TyJob * CreateOILJob(PMS_TyJob *pms_ptJob, int ePDL);
extern void DeleteOILJob(unsigned int JobID);
extern OIL_TyJob* GetJobByJobID(unsigned int JobID);

#endif /* _OIL_JOB_HANDLER_H_ */
