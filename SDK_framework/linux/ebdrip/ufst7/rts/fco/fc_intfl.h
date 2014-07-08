
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fc_intfl.h */


/*-----------------------------------------------------------------*/

#ifndef __FC_INTFL__
#define __FC_INTFL__


#define INIT_VALU               (SL32)0x7fffffffL

typedef struct
{
  /* ----------  Intellifont Scaling Intelligence  values  --------------- */

  UL32  inPrecPixel;  /*  precise pixel: 16K <= inPrecPixel < 32K */
  UL32  inPrecHaPix;  /*  precise half pixel                      */
  UL32  inBinPlaces;  /*  bits after the binary point above       */
  UL32           inPrecPixelSav; /*  precise pixel                         */
  UL32           inPrecHaPixSav; /*  precise half pixel                    */
  SW16           inBinPlacesSav; /*  bits after the binary point           */
  UL32           altPrecPix;   /*  alternate pixel scaling for Convergent Fonts */
  SW16           altBinPlaces; /*   "   */

  /* ----------  Scaling to Output Space  -------------------------------- */

  UL32  ouFracPixel;  /*  outputfractional units (power of 2)     */
  SL32           roundTyp[4];  /*  rounding value for 4 "R" types          */
  UL32  ouBinPlaces;  /*  x >> ouBinPlaces is grid line           */
  UL32  ouBinPlMASK;  /*  mask to do grid alignment               */
}  PIXEL_DATA;


#endif	/* __FC_INTFL__ */
