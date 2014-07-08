/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5types.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Base types for PCL5 and HPGL2.
 */
#ifndef _pcl5types_h_
#define _pcl5types_h_

/* From the QL PCL 5e PCL CET test spec:

34.2.1 Decimals
If the command allows numeric passed parameters, send
parameters both with and without decimal values. If the command
expects a whole number, verify that the decimal values are ignored.

It is very likely that QL worked with HP at the time they were
generating the tests and got clarification beyond what the documents
we have say. Much in the same way as we did with the XPS test
suites. */

typedef double PCL5Real ;
typedef int32 PCL5Integer ;

/* PCL5Numeric is used for operator callbacks. */
typedef union PCL5Numeric {
  PCL5Real real ;
  PCL5Integer integer ;
} PCL5Numeric ;


typedef int32 HPGL2Integer ;
typedef double HPGL2Real ;

typedef struct HPGL2Point {
  HPGL2Real x ;
  HPGL2Real y ;
} HPGL2Point ;

typedef struct HPGL2PointList {
  HPGL2Point point ;
  struct HPGL2PointList *next ;
} HPGL2PointList ;

#endif

/* Log stripped */

