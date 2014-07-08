/** \file
 * \ingroup calibration
 * $HopeName: SWcalibration!export:calibration.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Calibration setup calculations.
 */

#ifndef __CALIBRATION_H__
#define __CALIBRATION_H__


/* pic */
#include "prodefs.h"  /* PROConversionTable_t */


/* Allowable fp rounding error */
#define CALIB_EPSILON               (1.0E-5)


/* conversion table formula identifiers */
enum {
  CNV_IDENTITY,                 /* identity function */
  CNV_MURRAY_DAVIES             /* Murray-Davies percent dot */
};

/* pair of numbers forming part of a calibration curve */
typedef struct MeasurementPoint {
  float             patchValue;
  float             value;
} MeasurementPoint;

/* definition of a data curve of arbitrary length */
typedef struct MeasurementCurve {
  int32             numPoints;
  MeasurementPoint  *curve;
} MeasurementCurve;

/* pair of numbers forming part of a calibration curve */
typedef struct CalDataPoint {
  float             x;
  float             y;
} CalDataPoint;

/* definition of a data curve of arbitrary length */
typedef struct CalDataCurve {
  int32             numPoints;
  CalDataPoint      *curve;
} CalDataCurve;

/* 'superclass' of CalDataPoint to enable track reordering of points,
 * and keep extra information about missing extrapolate points.
 */
typedef struct {
  CalDataPoint      point;
  int32             fMissing;       /* Missing extrapolate point, ie no point.x */
  int32             prev;           /* Previous supplied point index, -101 => none */
  int32             next;           /* Next supplied point index, -101 => none */
  int32             index;          /* Original index before sorting */
} IndexedDataPoint;

/* Information for SNV <-> MV conversions. */
typedef struct {
  CalDataCurve      data;
  int32             formula;

  /* Murray-Davies %dot parameters */
  double            paperDensity;
  double            maxinkDensity;

  /* White point adjustment requested. */
  int32             fSubtractPaperWhite;

  int32             fRelative;
} ConversionMethod;

extern float calsetMVtoSNV(ConversionMethod *pMethod, float value);
extern float calsetSNVtoMV(ConversionMethod *pMethod, float value);

extern float calsetMVtoRelativeMV(ConversionMethod *pMethod, float value, float patchColor);
extern float calsetRelativeMVtoMV(ConversionMethod *pMethod, float value, float patchColor);

extern float calsetInterpolateValue(CalDataCurve *pCurve, float value);
extern float calsetInterpolateInverseValue(CalDataCurve *pCurve, float value);

extern void calsetGetWhiteInfo( MeasurementCurve *pCalCurve,
                                ConversionMethod *pMethod,
                                float            *pWhitePoint,
                                float            *pWhiteAdjustment );

extern void calsetUpdateCalFromMV( MeasurementCurve  *pCalCurve,
                                   CalDataCurve      *pAppliedCalibration,
                                   ConversionMethod  *pMethod,
                                   float             whiteAdjustment,
                                   int32             fNegativeMedia,
                                   IndexedDataPoint  *pNewCurve );

extern void calsetGetConversionMethodFromTable( PROConversionTable_t  *conversionTable,
                                                double                maxinkDensity,
                                                ConversionMethod      *pMethod ) ;

extern int32 calibration_init(void);
extern void calibration_finish(void);

#endif /* __CALIBRATE_H__ */


/* Log stripped */

/* eof calibration.h */
