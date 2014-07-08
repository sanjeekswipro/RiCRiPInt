/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:hpscreen.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Heidelberg spatial screening API
 */

#ifndef __HPSCREEN_H__
#define __HPSCREEN_H__


struct CHALFTONE; /* from chalftone.h */


typedef struct cells {
  int16 xcoord , ycoord ;
  int32 cellxy1 ;
  int32 cellxy2 ;
  int32 freecount ;
} CELLS ;

#define theIXCoord(val)     (val->xcoord)
#define theIYCoord(val)     (val->ycoord)
#define theICellXY1(val)    (val->cellxy1)
#define theICellXY2(val)    (val->cellxy2)
#define theIFreeCount(val)  (val->freecount)


/* ----- Exported functions ----- */
extern void  phasehalftones0(CELLS **cells , int32 number , USERVALUE *uvals );
extern int32 phasehalftones1(CELLS **cells , int32 number , int32 scms );
extern Bool  phasehalftones2(CELLS **cells , int32 number , int32 scms );
extern int32 spatiallyOrderHalftones(CELLS **cells ,
				     int32 number,
				     struct CHALFTONE *tmp_chalftone);
extern void  qsorthalftones(USERVALUE *spotvals ,
			    CELLS **cells ,
			    int32 number );
extern void  qsortthreshold(uint8 *spotvals ,
                            int16 *xcoords ,
                            int16 *ycoords ,
                            int32 number ,
			    int32 pivot1 , int32 pivot2 );

extern void  qsort16threshold(uint16 *spotvals ,
			      int16 *xcoords ,
			      int16 *ycoords ,
			      int32 number ,
			      int32 pivot1 , int32 pivot2 );

#endif /* protection for multiple inclusion */


/* Log stripped */
