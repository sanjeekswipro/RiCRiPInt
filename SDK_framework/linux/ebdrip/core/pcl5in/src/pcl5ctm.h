/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5ctm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for PCL5 current transformation matrix handling.
 */
#ifndef _pcl5ctm_h_
#define _pcl5ctm_h_

#include "matrix.h"
#include "pcl5context.h"

/* Opaque type. */
typedef struct PCL5Ctms PCL5Ctms;

/**
 * Set the passed instance to default values.
 */
void default_ctm(PCL5Ctms* self);

/**
 * Return the current transformation matrix; this includes both orientation and
 * print direction elements.
 */
OMATRIX* ctm_current(PCL5Ctms* self);

/**
 * Return a transformation matrix which excludes the print direction element.
 */
OMATRIX* ctm_orientation(PCL5Ctms* self);

/**
 * Return the base transformation matrix; this excluded both registration
 * offset and orientation elements.
 */
OMATRIX* ctm_base(PCL5Ctms *self);

/**
 * Add a half pixel to the matrix value, before calling gs_setctm.
 * This mimics the snap to grid rules of the reference printer.
 */
Bool pcl_setctm(OMATRIX* ctm, int32 apply_imposition);

/**
 * Set the PS gstate CTM to the PCL5 CTM.
 */
Bool ctm_install(PCL5Ctms *self);

/**
 * Return the DPI in the X direction of the output device.
 */
uint32 ctm_get_device_x_dpi(PCL5Ctms* self);

/**
 * Return the DPI in the Y direction of the output device.
 */
uint32 ctm_get_device_y_dpi(PCL5Ctms* self);

/**
 * Return the DPI of the output device.
 */
uint32 ctm_get_device_dpi(PCL5Ctms *self);

/**
 * Set the base matrix. The base matrix should define the initial PCL5
 * coordinate space, where units are in decipoints, the page orientation is
 * portrait, and the page origin is at the top-left.
 */
void ctm_set_base(PCL5Context* context, OMATRIX* base);

/**
 * Extract the PCL5Ctms structure from the current print enviroment in the
 * passed context.
 */
PCL5Ctms* get_pcl5_ctms(PCL5Context* context);

/**
 * Calculate orientation and print direction matrices from the base matrix.
 */
void ctm_recalculate(PCL5Context* context);

/**
 * Transform the passed coordinates to device space using the current
 * transform (as returned by ctm_current()).
 */
IPOINT ctm_transform(PCL5Context* context, double x, double y);

/**
 * ctm_sanitise_default_matrix is a workaround to remove inaccuracies in the
 * device CTM which build up as a result of the way the CTM is constructed in
 * pagedev.pss and also the /Scaling [ 0.01 0.01 ] entry in oil_psconfig.c.  The
 * construction of the CTM involves casts from doubles to floats for the
 * arguments and also with the use of currentmatrix in pagedev.pss.  This
 * routine recognises that most of the time the matrix values will be resolution
 * / 7200 and therefore the values in the matrix can be snapped accordingly,
 * back to double accuracy.  Without this objects may misalign leading to output
 * problems.
 */
Bool ctm_sanitise_default_matrix(PCL5Context *context);

/**
 * Calculate media orientation from the base CTM,
 * and store it in the PCL5PrintState.
 * It is needed to get patterns the right way
 * round and areafills the right size.
 */
void set_media_orientation(PCL5Context *context);

/**
 * Calculate device dpi and store it in the PCL5PrintState.
 */
void set_device_resolution(PCL5Context *context);

#endif

/* Log stripped */

