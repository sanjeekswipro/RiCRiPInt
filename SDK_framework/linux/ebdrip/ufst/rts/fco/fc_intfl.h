/* $HopeName: GGEufst5!rts:fco:fc_intfl.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:fco:fc_intfl.h,v 1.3.8.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:03 $ */

/* fc_intfl.h */


/*-----------------------------------------------------------------*/

/* History
 *
 * 18-Mar-94  mby  Changed declaration of intelliflator() to match new
 *                 function definition in "fc_intfl.c".
 * 12-Apr-94  mby  Changed declaration of intelliflator(), putsegs().
 * 28-Jul-94  mby  Move definition of INIT_VALU here from "fc_intfl.c"
 * 09-Sep-94  mby  Changed declarations of intelliflator(), putsegs().
 * 15-Sep-94  dbk  Added LINTARGS for non-ANSI compliance.
 * 27-Oct-94  mby  Added argument to declaration of intelliflator().
 * 15-Nov-94  mby  Added prototype for in_VectorScaleGrid().
 *
 * 21-Dec-94  mby  Split "fc_pixel.h" into "fc_putsg.h", "fc_syntl.h"
 *                 Moved RENDER_TBL, putsegs() and segCounts() declarations
 *                 into "fc_putsg.h"
 * 11-Jan-95  mby  Added two new args to prototype of intelliflator().
 * 08-Feb-95  mby  Added new argument to prototype of intelliflator().
 * 20-Feb-95  tbh  Added new argument (yEscapement) to intelliflator().
 * 24-Feb-95  tbh  Changed "roundTyp[4]" from unsigned long to long.
 * 23-Jan-96  mby  Added new members to PIXEL_DATA structure for Convergent
 *                 Font scaling to 2 different resolutions.
 * 14-Apr-97  mby  Replaced "LINTARGS" with "LINT_ARGS".
 * 03-Sep-97  slg	Fix type mismatches for PTV_OS (long is not same as SL32)
 * 10-Mar-98  slg  Don't use "long" dcls (incorrect if 64-bit platform)
 * 12-Jun-98  slg  Move intelliflator() prototype to shareinc.h
 */

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
