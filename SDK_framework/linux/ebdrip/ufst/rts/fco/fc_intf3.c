/* $HopeName: GGEufst5!rts:fco:fc_intf3.c(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2005 Monotype Imaging Inc. All rights reserved.
 */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:fco:fc_intf3.c,v 1.2.10.1.1.1 2013/12/19 11:24:05 rogerb Exp $ */

#define NEW_BIT_ORDER

#include    "cgconfig.h"

#if FCO3      /* conditionalize entire file for Microtype 3 */
#if FCO_RDR  /* conditionalize entire file */

#include    <stdio.h>
#include    <string.h>
#include    <stdlib.h>
#include    <math.h>

#include    "ufstport.h"

#include "shareinc.h"

#if defined(MMDECODE_DEBUG) || ( ( FCO_STANDALONE == 1 ) && defined(MERGER_ON) )
#define     GLOBAL_EXTERN
EXTERN mmdecode_trace_sw;
#include    "mmdebug.h"
#undef      GLOBAL_EXTERN
#endif



#define  NULL_ASSOC       (UB8)0x01  /* 00000001 */
#define  STEM_ASSOC       (UB8)0x00  /* 00000000 */
#define  WDTH_ASSOC       (UB8)0x03  /* 00000011 */
#define  LTRT_BIT       (UB8)0x03
#define  STDM_BIT       (UB8)0x00
#define  WDDM_BIT       (UB8)0x01
#define  FCWD_BIT       (UB8)0x02
#define  PREVIOUS       (UB8)0x00
#define  ADJACENT       (UB8)0x01
#define  PASS_ONE       (UB8)0x01
#define  CORE           (UB8)0x00
#define  EXTENSION      (UB8)0xff 
#define  MAX_ANCHOR_PTS 4
#define  DISCARDED      (SL32)0xfffe

#ifdef MMDECODE_DEBUG
#define COORD_INDEX  1
#define SIBLING      2
#define CHILD        3
#define R_TYPE       4
#define GRID_ALIGN   5
#define ASSOC_SIGN   6
#define DIMSTAT      7
#define DIM_INDEX    8
#define ASSOC_VALUE  9
#define YCLASS_INDEX 10
#define NUMLOOPS     11
#define NUMYXCSEGS   12
#define LOOP         13
#define DISCARD_BIT  14
#define PREV_ADJ     15
#endif


#define STEMBIT    0x20
#define PROJBIT    0x40

#define CONSBIT    0x1
#define FCOWBIT    0x2
#define RTZBIT     0x4

typedef struct
  {
  UL32     yMode, rootMode, coreMode;
  UB8    *yClassDefLo;
  UB8    *yClassDefHi;
  UB8    *TFglobalData;
  UW16     *standardYstem;
  UW16     *standardXstem;
  UB8    *yLineValue;
  UB8    *Ydimensions;  
  UB8    *Xdimensions;  
  UB8    *YconstDims;   
  UB8    *XconstDims;   
  UB8    *YdimType;     
  UB8    *XdimType;     
  UB8    *YRTZflags;    
  UB8    *XRTZflags;
  UB8    *Yfcowflags;
  UB8    *Xfcowflags;
  UB8    *lc_indx;      
  UB8    sd_enable;
  SL32   sd_lo, sd_hi, sd_NA, sd_alignShift;
  UB8    *YXdimensions;
  UB8    *YXdimType;
  UB8    *YXdimProjType;
  UB8    *YXRTZflags;
  UB8    *YXfcowflags;
  UB8    *YXconstDims;
  SL32   yLineLo, yLineHi, yLineLoAligned, yLineHiAligned, sixthClass;
  UB8    fraction;
  SL32             alignShift;
  SL32             dimension, coord, itaCoordValue, localValue;
  SL32             *xItaCrdOU;
  SL32             standStemOU, standWidtOU, standStanOU;
  SL32             FCWstanStemOU, FCWstanWidtOU;
  SL32             italicAngle, *yCoordOU;
  UB8    localComp;
  UB8    loopSta;
  PIXEL_DATA       *pixel;
  SL32             coordValue;
  SL32             baselineAdj;  /* used with yline/yclass compute to prevent ylines rounding together or apart */
  SL32              escape;
  UB8    varEncoding;
  UB8    *modDataPool;
  UB8    *CORstream, *CORsubstream;
  UB8    *EXTstream, *EXTsubstream;
  UB8    altCORgAlign, altCORaSign, altCORdStat;
  UB8    altEXTgAlign, altEXTaSign, altEXTdStat;
  UB8    Cbit, Ebit, Pbit, SCbit, SEbit, Lbit;
  skelNode         **stack;
  UB8 stemBit;
  UB8 fcoBit;
#if USBOUNDBOX				/* Aj 10-04-04 */
  SL32             USstandStemOU, USstandWidtOU;
  SL32             USFCWstanStemOU, USFCWstanWidtOU;
  SL32             *UScoordOU;
  SL32             *USYcoordOU;
  SL32             *USIcoordOU;
  SL32             *min;
  SL32             *max;
  SL32             maxIndex;
#endif
  }        MLOC;

#ifdef MMDECODE_DEBUG
UB8 numOUFILE = 0;
#endif

/*************************************************************************************/
/*
 *  Scales vector (xtr, ytr) and rounds to pixel grid
 */
#if defined (ANSI_DEFS)
void in_VectorScaleGrid( SL32* xtr, SL32* ytr, PIXEL_DATA* xPixel, PIXEL_DATA* yPixel )
#else
void in_VectorScaleGrid( xtr, ytr, xPixel, yPixel )
SL32       *xtr, *ytr;
PIXEL_DATA *xPixel, *yPixel;
#endif
{
    SL32 xlt0 = 0;
    SL32 ylt0 = 0;
    if (*xtr < 0) {
        xlt0 = 1;
        *xtr = -*xtr;
    }
    if (*ytr < 0) {
        ylt0 = 1;
        *ytr = -*ytr;
    }
    *xtr =  ((*xtr << (SL32)xPixel->inBinPlaces) + xPixel->inPrecHaPix)
           / xPixel->inPrecPixel;
    *ytr =  ((*ytr << (SL32)yPixel->inBinPlaces) + yPixel->inPrecHaPix)
           / yPixel->inPrecPixel;
    *xtr <<= (SL32)xPixel->ouBinPlaces;
    *ytr <<= (SL32)yPixel->ouBinPlaces;
    if (xlt0)
        *xtr = -*xtr;
    if (ylt0)
        *ytr = -*ytr;
}


/*************************************************************************************/ 

#define getBit(temp, ptr, item, bitsleft) \
if(bitsleft != 1) {                     \
       item = temp & 1;                   \
	   temp >>= 1;                         \
	   bitsleft -= 1;                     \
}                                     \
else {                                   \
		item = temp;  \
		temp = *ptr++; \
        bitsleft = 8; \
}                                                                                    
	


/*
void getVar(UB8 temp, UB8 *ptr, UB8 item, UB8 bitsleft, UB8 numbits) 
{
if( numbits < bitsleft) {                   
	item = temp & (0xff>>(8-numbits));       
	temp = temp>>numbits;                    
	bitsleft -= numbits;                     
}                                            
else if (bitsleft == numbits) {              
	item = temp & (0xff>>(8-numbits));       
	temp = *ptr++;                           
	bitsleft = 8;                            
}                                            
else {                                       
	item = temp;     
	temp = *ptr++;                           
	item |= (temp & (0xff>>(8-numbits+bitsleft)))<<bitsleft; 
	temp >>= (numbits-bitsleft);              
    bitsleft = 8 - numbits + bitsleft;       
}
}  
*/

#if defined (_WIN32_WCE)
#define getVar(temp, ptr, item, bitsleft, numbits)   \
if( numbits < bitsleft ) {                   \
	item = temp & mask[numbits];       \
	temp = temp>>numbits;                    \
	bitsleft -= numbits;                     \
}                                            \
else if (bitsleft == numbits) {              \
	item = temp;       \
	temp = *ptr++;                           \
	bitsleft = 8;                            \
}                                            \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item = item | ((temp&mask[numbits-bitsleft])<<bitsleft);    \
	temp = temp >> (numbits-bitsleft);      \
    bitsleft = 8 - numbits + bitsleft;       \
}
/*
else {                                       \
	item = temp;                             \
    mask = (255>>(8 - numbits+bitsleft));     \
	temp = *ptr++;                           \
	item = item | ((temp&mask)<<bitsleft);    \
	temp = temp >> ((numbits-bitsleft));      \
    bitsleft = 8 - numbits + bitsleft;       \
}
*/

#define getTri(temp, ptr, item, bitsleft)   \
if( 3 < bitsleft ) {                   \
	item = temp & 0x7;       \
	temp = temp>>3;                    \
	bitsleft -= (UB8)3;                     \
}                                            \
else if (bitsleft == 3) {              \
	item = temp;       \
	temp = *ptr++;                           \
	bitsleft = 8;                            \
}                                            \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item = item | ((temp&mask[3-bitsleft])<<bitsleft);    \
	temp = temp >> ((3-bitsleft));             \
    bitsleft = 5 + bitsleft;       \
}  

#define getByte(temp, ptr, item, bitsleft)   \
if (bitsleft == 8) {              \
	item = temp;       \
	temp = *ptr++;                           \
	}                                        \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item |= (temp & mask[8-bitsleft])<<bitsleft; \
    temp >>= (8-bitsleft);             \
    } 
#else
#define getVar(temp, ptr, item, bitsleft, numbits)   \
if( numbits < bitsleft ) {                   \
	item = temp & (0xff>>(8-numbits));       \
	temp = temp>>numbits;                    \
	bitsleft -= numbits;                     \
}                                            \
else if (bitsleft == numbits) {              \
	item = temp & (0xff>>(8-numbits));       \
	temp = *ptr++;                           \
	bitsleft = 8;                            \
}                                            \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item |= (temp & (0xff>>(8-numbits+bitsleft)))<<bitsleft; \
	temp >>= (numbits-bitsleft);             \
    bitsleft = 8 - numbits + bitsleft;       \
}

#define getTri(temp, ptr, item, bitsleft)   \
if( 3 < bitsleft ) {                   \
	item = temp & (UB8)((UB8)0xff>>(5));       \
	temp = temp>>3;                    \
	bitsleft -= (UB8)3;                     \
}                                            \
else if (bitsleft == 3) {              \
	item = temp & (UB8)(0xff>>(5));       \
	temp = *ptr++;                           \
	bitsleft = 8;                            \
}                                            \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item |= (temp & (0xff>>(5+bitsleft)))<<bitsleft; \
	temp >>= (3-bitsleft);             \
    bitsleft = 5 + bitsleft;       \
}  

#define getByte(temp, ptr, item, bitsleft)   \
if (bitsleft == 8) {              \
	item = temp;       \
	temp = *ptr++;                           \
	}                                        \
else {                                       \
	item = temp;                             \
	temp = *ptr++;                           \
	item |= (temp & (0xff>>(bitsleft)))<<bitsleft; \
	temp >>= (8-bitsleft);             \
    } 
#endif 

#define UPRIGHT(icoord,iparent,xcoord,xparent,ycoord,yparent)             \
	temp = (ycoord - yparent) * italicAngle;  \
	if(temp >= 0)                                 \
      temp = (temp + 16384) >>15;                 \
	else                                          \
	  temp = -((-temp+16384) >> 15);              \
	xcoord = xparent + icoord - iparent - temp;

#define ITALICIZE(xcoord,xparent,icoord,iparent,ycoord,yparent)             \
	temp = (ycoord - yparent) * italicAngle;  \
	if(temp >= 0)                                 \
      temp = (temp + 16384) >>15;                 \
	else                                          \
	  temp = -((-temp+16384) >> 15);              \
	icoord = iparent + xcoord - xparent + temp;
                                          



/*************************************************************************************/
#if defined (_WIN32_WCE)
#define scale(value, pixel) (((value << (pixel->inBinPlaces + pixel->ouBinPlaces))  \
         + (SL32)pixel->inPrecHaPix) / (SL32)pixel->inPrecPixel)
#else            

#if defined( ANSI_DEFS )
SL32 scale (SL32 value, PIXEL_DATA *pixel)
#else
SL32
scale( value, pixel)
SL32 value;
PIXEL_DATA* pixel;
#endif

{
SL32   result;

result =   ((value << (pixel->inBinPlaces + pixel->ouBinPlaces))
         + (SL32)pixel->inPrecHaPix) / (SL32)pixel->inPrecPixel;
return (result);
}
#endif


/*************************************************************************************/


#if defined ( ANSI_DEFS )
static SL32 
pixel_align_fci (MLOC *ml, SL32 value,UB8 RTZ_cond, UB8 CONST_cond,
                         PIXEL_DATA *pixel)
#else
static SL32
pixel_align_fci( ml, value, RTZ_cond, CONST_cond, pixel )
MLOC* ml;
SL32 value;
UB8 RTZ_cond;
UB8 CONST_cond;
PIXEL_DATA* pixel;
#endif

{
SL32   result;

result         = (SL32)((value + pixel->roundTyp[2]) & pixel->ouBinPlMASK);
ml->alignShift = result - value;

if (CONST_cond)
  {
  if ((result == (SL32)pixel->ouFracPixel) || (result == (-((SL32)pixel->ouFracPixel))))
    {
    ml->alignShift = -value;
    return ((SL32)0);
    }
  else
    {
    return (result);
    }
  }

if (result || RTZ_cond)
  {
  return (result);
  }
else
  {
  if (value >= 0)
    {
    ml->alignShift = (SL32)pixel->ouFracPixel - value;
    return (pixel->ouFracPixel);            /* do not allow to round to zero */
    }
  else
    {
    ml->alignShift = -((SL32)pixel->ouFracPixel) - value;
    return (-((SL32)pixel->ouFracPixel));   /* do not allow to round to zero */
    }
  }
}

#define pixel_align_coord(value, pixel) \
  (SL32)((value + pixel->roundTyp[2]) & pixel->ouBinPlMASK)



#if defined ( ANSI_DEFS )
static SL32 
pixel_align_fci_sd (MLOC *ml, SL32 value, PIXEL_DATA *pixel)
#else
static SL32
pixel_align_fci_sd( ml, value, pixel )
MLOC* ml;
SL32 value;

PIXEL_DATA* pixel;
#endif

{
SL32   result;

result         = (SL32)((value + pixel->roundTyp[2]) & pixel->ouBinPlMASK);
ml->alignShift = result - value;

if (result)
  {
  return (result);
  }
else
  {
  if (value >= 0)
    {
    ml->alignShift = (SL32)pixel->ouFracPixel - value;
    return (pixel->ouFracPixel);            /* do not allow to round to zero */
    }
  else
    {
    ml->alignShift = -((SL32)pixel->ouFracPixel) - value;
    return (-((SL32)pixel->ouFracPixel));   /* do not allow to round to zero */
    }
  }
}

/*************************************************************************************/

#define sygn(val,sign) (sign ? -val : val)

/*************************************************************************************/

#define SIBFLAG   0x80
#define CHILDFLAG 0x40
#define RTYPEFLAG 0x20
#define ALIGNFLAG 0x10
#define DIMFLAG   0x08
#define SIGNFLAG  0x04
#define STEMFLAG  0x02
#define NULLFLAG  0x01



#if defined (ANSI_DEFS)
UL32  intelliflator ( FSP
                 
                SW16         *curveSegOff,
                INFLATE       *TFgroup, 
                MODELTYPE     *LBmodel,
                UB8 *CHlocal,
                UB8 CHlocBitPos,
                SL32          *Ysyntell,
                SL32          *Xsyntell,
                SL32          *Isyntell,
				UB8           *parentStack,
				UB8           *parentRtypeStack,
                PIXEL_DATA    *xPixel, 
                PIXEL_DATA    *yPixel, 
                SL32           stanIndx,
                UB8 *yClassPtr, 
                UL32  dimXoff, 
                UL32  dimYoff,
                UB8 *dimTypes,
                SL32           Xescapement,
                SL32           Yescapement
                              )
#else
UL32  intelliflator(
		curveSegOff, TFgroup, LBmodel, CHlocal, CHlocBitPos,
		Ysyntell, Xsyntell, Isyntell, parentStack, parentRtypeStack, xPixel, yPixel, stanIndx,
		yClassPtr, dimXoff, dimYoff, dimTypes,
		Xescapement, Yescapement )
                 
                SW16         *curveSegOff;
                INFLATE       *TFgroup;
                MODELTYPE     *LBmodel;
                UB8 *CHlocal;
                UB8 CHlocBitPos;
                SL32          *Ysyntell;
                SL32          *Xsyntell;
                SL32          *Isyntell;
				UB8           *parentStack;
				UB8           *parentRtypeStack;
                PIXEL_DATA    *xPixel; 
                PIXEL_DATA    *yPixel; 
                SL32           stanIndx;
                UB8 *yClassPtr; 
                UL32  dimXoff; 
                UL32  dimYoff;
                UB8 *dimTypes;
                SL32           Xescapement;
                SL32           Yescapement;
#endif

{ 
MLOC             ml;
SL32             stanStanStem;
INTR    i, j, k;
UB8 *modelptr;
INTR modelbyte,modelbitsleft;
UB8 *altsignptr;
INTR altsignbyte,altsignbitsleft;
UB8 *localptr;
INTR localbyte,localbitsleft;
UB8 *ptr;
INTR item;
INTR haschild, hassibling;
INTR rType,isnotstem, iswidth;
INTR dimStat;
UB8 *stack = parentStack;
INTR numleft;
INTR skelpnt,parent;
UB8 parentrtyp,*rtypstack = parentRtypeStack;
INTR gridAligned,assocSign;
INTR yClassIndex;
INTR loopStart,prev,next;
INTR adjorprv;
INTR rt_indx;
SL32 italicAngle;
INTR tmp;
SL32 localComp;
INTR locbits;
INTR locbase;
#if defined (_WIN32_WCE)
SL32 mask[9] = {0x0,
               0x1,
               0x3,
               0x7,
			   0xf,
			   0x1f,
			   0x3f,
			   0x7f,
			   0xff};
#endif

if     (LBmodel->numYXcSegs >= 128) ml.varEncoding = 8;
else if(LBmodel->numYXcSegs >= 64)  ml.varEncoding = 7; 
else if(LBmodel->numYXcSegs >= 32)  ml.varEncoding = 6; 
else if(LBmodel->numYXcSegs >= 16)  ml.varEncoding = 5; 
else                                ml.varEncoding = 4;


LBmodel->loopEnd = (UB8*)MEMptr(LBmodel->modelDataH);
LBmodel->loopEndOff = 0;

modelptr = LBmodel->loopEnd + LBmodel->numLoops;
modelbyte = *modelptr++;
modelbitsleft = 8;



if(LBmodel->altSignDataH != NIL_MH) {
  altsignptr = (UB8*)MEMptr(LBmodel->altSignDataH);
  altsignbyte = *altsignptr++;
  altsignbitsleft = 8;
 
}
else
  altsignptr = 0;


getBit(modelbyte, modelptr, localComp, modelbitsleft);

if(localComp) {
  localptr = CHlocal;
  localbyte = *localptr++;
  localbitsleft = 8;
}
else {
  localptr = CHlocal;
  localbitsleft = 0;
}


getBit(modelbyte, modelptr, item, modelbitsleft);
if(item)
  ml.varEncoding++;
  


/* load loop ends */
ptr = LBmodel->loopEnd;
for (j=0; j<LBmodel->numLoops-1; j++)
  {
	
  getVar (modelbyte, modelptr, item, modelbitsleft, ml.varEncoding);
  *ptr++ = item;

  
  }
*ptr = LBmodel->numYXcSegs-1;

/* INITIALIZE TREE SKELS */

for (k=0; k<LBmodel->numYXcSegs+MAX_ANCHOR_PTS+16; k++)  {
    Ysyntell[k] = (SL32)INIT_VALU;
    Xsyntell[k] = (SL32)INIT_VALU;
}                  

ml.stack         = (skelNode**)MEMptr(LBmodel->rstackH);
ml.TFglobalData  = (UB8*)MEMptr(TFgroup->globalDataH);

ml.standardYstem = (UW16*)(ml.TFglobalData + TFgroup->standardYstemOff);
ml.standardXstem = (UW16*)(ml.TFglobalData + TFgroup->standardXstemOff);

ml.yLineValue    = ml.TFglobalData + TFgroup->yLineValueOff;
ml.Ydimensions   = ml.TFglobalData + TFgroup->YdimensionsOff + dimYoff;
ml.Xdimensions   = ml.TFglobalData + TFgroup->YdimensionsOff + dimXoff;
ml.YconstDims    = ml.YRTZflags = ml.Yfcowflags = ml.TFglobalData + TFgroup->YdimFlagsOff + dimYoff;
ml.XconstDims    = ml.XRTZflags = ml.Xfcowflags = ml.TFglobalData + TFgroup->YdimFlagsOff + dimXoff;
ml.YdimType      = dimTypes + dimYoff;
ml.XdimType      = dimTypes + dimXoff;

ml.yClassDefLo   = yClassPtr + TFgroup->yClassDefLo;
ml.yClassDefHi   = yClassPtr + TFgroup->yClassDefHi;

ml.YXdimProjType      = dimTypes + dimXoff;


if(LBmodel->italicSwitch)
  italicAngle = TFgroup->italicAngle;
else
  italicAngle = 0;

  parent = -1;
  numleft = 0;
  parentrtyp = 0;

  
  ml.standStemOU     = scale ((((SL32)ml.standardYstem[stanIndx])<<2), yPixel);
  ml.standWidtOU    = scale ((SL32)TFgroup->standardYheight, yPixel);  
  ml.FCWstanStemOU     = scale ((SL32)TFgroup->FCWstanYstem<<2, yPixel);  
  ml.FCWstanWidtOU    = scale ((SL32)TFgroup->FCWstanYwidth, yPixel);

  ml.baselineAdj     = pixel_align_coord(((ml.standWidtOU+2)>>2),yPixel);
  ml.baselineAdj     -= ((ml.standWidtOU+2)>>2);

  stanStanStem       = (SL32)TFgroup->stanStanYstem;

    /* SETUP STANDARD-STANDARD DIMENSION PROCESSING */
/* keb 6/1/06 */
#if USBOUNDBOX
     /* keb fix 7/24/06 */ 
     if (TFgroup->stanFlag == BELOW_STAN && (if_state.isUSBBOX == FALSE))  /* standard-standard  */
#else
  if (TFgroup->stanFlag == BELOW_STAN)
#endif
    {
    ml.standStanOU   = scale (((SL32)stanStanStem<<2), yPixel);
    if (((ml.standStanOU-ml.standStemOU) < ((SL32)yPixel->ouFracPixel<<1)) 
      && ((ml.standStanOU-ml.standStemOU) > -((SL32)yPixel->ouFracPixel<<1)))
      {
      ml.standStemOU = ml.standStanOU;
      }
    }


  /* SETUP STANDARD DIMENSION PROCESSING */

  if (TFgroup->stanFlag <= AT_STAN)
    {
    ml.sd_enable = YES;

    /* half pix in terms of percentage of standard: ml.sd_lo: (((standardStem / 4) - (half pixel)) / standardStem) * 256 */
    /* twice the standard + half pix for NonAligns: ml.sd_NA: ((((standardStem * 2) / 4) - (half pixel)) / standardStem) * 256 */
    if (ml.standStemOU)
      {
      ml.sd_lo = ((ml.standStemOU << 6) - ((SL32)yPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
      ml.sd_hi = ((ml.standStemOU << 6) + ((SL32)yPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
      ml.sd_NA = ((ml.standStemOU << 7) + ((SL32)yPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
      }
    else
      {
      ml.sd_lo = -256;
      ml.sd_hi = 256;
      ml.sd_NA = 256;
      }
    ml.dimension        = pixel_align_fci_sd (&ml,(ml.standStemOU + 2) >> 2,yPixel);
    ml.sd_alignShift    = ml.alignShift;
    }
  else
    {
    ml.sd_enable        = NO;
    }


  Ysyntell[0]        = (ml.standWidtOU + 2) >> 2;       /* left reference */
  Ysyntell[0]        = pixel_align_coord (Ysyntell[0], yPixel);
  Ysyntell[1]        = (ml.standWidtOU + 2) >> 2;       /* right reference */
  Ysyntell[1]        = pixel_align_coord (Ysyntell[1],yPixel);

   
Processyroot:

  getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);
  getBit(modelbyte, modelptr, hassibling, modelbitsleft);
  getBit(modelbyte, modelptr, haschild, modelbitsleft);

  getBit(modelbyte, modelptr, rType, modelbitsleft);
  getByte(modelbyte, modelptr, yClassIndex, modelbitsleft);
  

  if(haschild) {
     numleft++;
    *stack++ = skelpnt;
	*rtypstack++ = rType;
  }

  /*  process root point */

  /* root can be as much as sixthClass above or below yclass limits  */

  ml.yLineLo        = (((SL32)ml.yLineValue[ml.yClassDefLo[yClassIndex]] * ml.standWidtOU) + 128) >> 8;
  ml.yLineLo       += ml.baselineAdj;
  ml.yLineHi        = (((SL32)ml.yLineValue[ml.yClassDefHi[yClassIndex]] * ml.standWidtOU) + 128) >> 8;
  ml.yLineHi       += ml.baselineAdj;

  ml.yLineLoAligned = pixel_align_coord (ml.yLineLo, yPixel);
  ml.yLineHiAligned = pixel_align_coord (ml.yLineHi, yPixel);

 
  if(localComp) {
	getTri (modelbyte, modelptr, locbits, modelbitsleft);
    tmp = 0;    
    if(locbits) {
      locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
	  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
	}
	if(locbits != 8) {
      getByte (modelbyte, modelptr, locbase, modelbitsleft);
	}
	else {
      locbase = 0;
	}

    ml.fraction = locbase + tmp;
	
  }
  else {     
    ml.fraction = (SL32)*localptr;
    localptr++;
  }

  
  if(ml.fraction >= (UB8)224) {
    ml.coord      = (((((SL32)ml.fraction - 224) * (ml.yLineHi - ml.yLineLo)) + 96) / 192);
    ml.coord      = pixel_align_coord (ml.coord, yPixel);
    Ysyntell[skelpnt] = ml.coord + ml.yLineHiAligned;
  }
  else if (ml.fraction <= (UB8)32) {
    ml.coord      = ((((32 - (SL32)ml.fraction) * (ml.yLineHi - ml.yLineLo)) + 96) / 192);
    ml.coord      = (pixel_align_coord (ml.coord, yPixel));
    Ysyntell[skelpnt] = ml.yLineLoAligned - ml.coord;
  }
  else {
    ml.sixthClass = (ml.yLineHiAligned - ml.yLineLoAligned + 3) / 6;
    ml.coord      = ((((SL32)ml.fraction * (ml.yLineHiAligned - ml.yLineLoAligned) * 4) + 384) / 768)
                  + (ml.yLineLoAligned - ml.sixthClass);
    Ysyntell[skelpnt] = pixel_align_coord (ml.coord, yPixel);
  }

  if(hassibling) 
    goto Processyroot;

Processbranch:

  if(numleft == 0)
	goto Yprevadj;
  parent = *--stack;
  parentrtyp = *--rtypstack;
  numleft--;

Processsibling:

  
  getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);
  getBit(modelbyte, modelptr, hassibling, modelbitsleft);
  

  /* process second IP point */
  if(Ysyntell[skelpnt] != INIT_VALU) {
    SL32 localValue;
    SL32 temp;
	  
    if(localComp) {
	  getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
	  tmp = 0;
      if(locbits) {
        locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		getVar(localbyte, localptr, tmp, localbitsleft,locbits);
	  }
      if(locbits != 8) {
        getByte (modelbyte, modelptr, locbase, modelbitsleft);
	  }
	  else {
        locbase = 0;
	  }

      localValue = locbase + tmp;
	}
	else {     
     localValue = (SL32)*localptr;
     localptr++;
	}

    if(localValue == 0x000000ff) {
      localValue = 0x00000100;
    }

	temp = Ysyntell[parent] - Ysyntell[skelpnt];
	if(temp > 0)
	  Ysyntell[skelpnt] = Ysyntell[skelpnt] + (((temp*localValue)+128)>>8);
	else if(temp < 0) /* leave temp=0 value as is */
	  Ysyntell[skelpnt] = Ysyntell[skelpnt] - ((((-temp)*localValue)+128)>>8);
	   
    haschild = 0;
	  goto SecondIPy;
  }

  
  getBit(modelbyte, modelptr, haschild, modelbitsleft);

  getBit(modelbyte, modelptr, rType, modelbitsleft);
  getBit(modelbyte, modelptr, gridAligned, modelbitsleft);
  getBit(modelbyte, modelptr, dimStat, modelbitsleft);
  

  if(haschild) {
    numleft++;
    *stack++ = skelpnt;
    *rtypstack++ = rType;
  }

  rt_indx = 0;
  if(rType == parentrtyp) {
    rt_indx = 2;          /* LATERAL RTYPE */
  }

  if(dimStat) {

    SL32 dimension;
    UB8 dimIndex;

	
    getByte(modelbyte, modelptr, dimIndex, modelbitsleft);
	getBit(modelbyte, modelptr, assocSign, modelbitsleft);
	if(altsignptr) 
	  getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
	

	/* process global dimension */
	if((ml.YdimType[dimIndex] & STEMBIT) == 0) {
      if((ml.Yfcowflags[dimIndex] & FCOWBIT)) {
        dimension  = (((SL32)ml.Ydimensions[dimIndex] * ml.FCWstanStemOU) + 128) >> 8;
      }
      else {
        dimension  = (((SL32)ml.Ydimensions[dimIndex] * ml.standStemOU) + 128) >> 8;
	  }
/* keb 6/1/06 */     
#if USBOUNDBOX
	     /* keb fix 7/24/06 */ 
	     if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE))
#else
	  if((ml.sd_enable && rt_indx != 2))
#endif
	  { 
        if(gridAligned) {
          if(((SL32)ml.Ydimensions[dimIndex] >= ml.sd_lo) && 
	  	     ((SL32)ml.Ydimensions[dimIndex] <= ml.sd_hi) ) {
			if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
              dimension = (ml.standStemOU + 2) >> 2;
			}
		  }
		}
        else {
          if((SL32)ml.Ydimensions[dimIndex] <= ml.sd_NA) {
            if((assocSign && rType) || (!assocSign && !rType)) {
              dimension += ml.sd_alignShift;
			}
            else {
              dimension -= ml.sd_alignShift;
			}
		  } 
		}
	  }
	}
  
    else {                                   /* WDDM_BIT */
      if((ml.Yfcowflags[dimIndex] & FCOWBIT)) {
        dimension  = (((SL32)ml.Ydimensions[dimIndex] * ml.FCWstanWidtOU) + 128) >> 8;
	  }
      else {
        dimension  = (((SL32)ml.Ydimensions[dimIndex] * ml.standWidtOU) + 128) >> 8;
	  }
      if((SL32)ml.Ydimensions[dimIndex] == 255L) {
        dimension = scale (Yescapement, yPixel);
	  }
	}
  
#if BOLD_FCO
    dimension += (yPpixel->roundTyp[rt_indx]-yPpixel->roundTyp[2]);
#endif
/* keb 6/1/06 */
#if USBOUNDBOX
	   /* keb fix 7/24/06 */
       if(gridAligned && (if_state.isUSBBOX == FALSE))
#else
	if(gridAligned)
#endif	
	{ 
      dimension  = pixel_align_fci (&ml, dimension,
                   (UB8)(ml.YRTZflags[dimIndex]&RTZBIT),
				   (UB8)(ml.YconstDims[dimIndex]&CONSBIT),
                   yPixel);
	}
    


    Ysyntell[skelpnt] = Ysyntell[parent] + sygn ((SL32)dimension, assocSign);
  }
  

  else {  /* local dimension */
	  
    getBit(modelbyte, modelptr, isnotstem, modelbitsleft);
	
    if(isnotstem) {
	
		
      getBit(modelbyte, modelptr, iswidth, modelbitsleft);
	  
      if(iswidth) {
	 
        SL32 dimension;
        UB8 localValue;
		
        getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		if(altsignptr) 
	      getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);

		

		/* process width assoc */
        if(localComp) {
	      getTri(modelbyte, modelptr, locbits, modelbitsleft);
        
		  tmp = 0;
          if(locbits) {
            locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		  }
          if(locbits != 8) {
            getByte (modelbyte, modelptr, locbase, modelbitsleft);
		  }
		  else {
            locbase = 0;
		  }

	      
          localValue = locbase + tmp;
		}
	    else {     
         localValue = (SL32)*localptr;
         localptr++;
		}

        if(localValue == 255){
           dimension = scale (Yescapement, yPixel);
		}
        else {
          dimension = (((SL32)(localValue) * (SL32)ml.standWidtOU) + 128) >> 8;
		}
	  

        Ysyntell[skelpnt] = Ysyntell[parent] + sygn ((SL32)dimension, assocSign);
/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
		    if(gridAligned && (if_state.isUSBBOX == FALSE))
#else
		if(gridAligned)
#endif
		{ 
          /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
          Ysyntell[skelpnt] = pixel_align_coord (Ysyntell[skelpnt], yPixel);
		}
	  }
	  else {
	    /* process Null assoc */
	    Ysyntell[skelpnt] = Ysyntell[parent];
	  }
	}

	   
    else { /* stem */
	  SL32 dimension;
	  UB8 localValue;
	  
      getBit(modelbyte, modelptr, assocSign, modelbitsleft);

	  if(altsignptr) 
	    getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
	  
	  /* process stem assoc */
      if(localComp) {
	    getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		tmp = 0;
        if(locbits) {
          locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		}
        if(locbits != 8) {
          getByte (modelbyte, modelptr, locbase, modelbitsleft);
		}
		else {
          locbase = 0;
		}

	    
        localValue = locbase + tmp;
	  }
	  else {     
       localValue = (SL32)*localptr;
       localptr++;
	  }

      dimension = (((SL32)(localValue) * (SL32)ml.standStemOU) + 128) >> 8;
/* keb 6/1/06 */
#if USBOUNDBOX
	   /* keb fix 7/24/06 */
       if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE))
#else
	   if((ml.sd_enable && rt_indx != 2))
#endif
	   { 
        if(gridAligned) {
          if(((SL32)localValue >= ml.sd_lo) && 
	  	     ((SL32)localValue <= ml.sd_hi) ) {
			if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
              dimension = (ml.standStemOU + 2) >> 2;
			}
		  }
		}
        else {
          if((SL32)localValue <= ml.sd_NA) {
            if((assocSign && rType) || (!assocSign && !rType)) {
              dimension += ml.sd_alignShift;
			}
            else {
              dimension -= ml.sd_alignShift;
			}
		  } 
		} 
	  }
	


      Ysyntell[skelpnt] = Ysyntell[parent] + sygn ((SL32)dimension, assocSign);		   
/* keb 6/1/06 */
#if USBOUNDBOX
	      /* keb fix 7/24/06 */
	      if(gridAligned && (if_state.isUSBBOX == FALSE))
#else
	  if(gridAligned)
#endif
	  { 
        /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
        Ysyntell[skelpnt] = pixel_align_coord (Ysyntell[skelpnt], yPixel);
	  }

	}
  }


SecondIPy:

  if(hassibling)
    goto Processsibling;

  if(numleft)
    goto Processbranch;

Yprevadj:
   /* process Y ADJ/PRV bits */

 
  k = 2;
  loopStart = k;
  prev = 1;
  for (i=0; i<LBmodel->numLoops; i++) {

	if(Ysyntell[k] != INIT_VALU) {
	  prev = k;
	  k++;
	}
	else {
	  if(prev < loopStart) {
		 prev = LBmodel->loopEnd[i];
	     while(Ysyntell[prev] == INIT_VALU) {
	 	   prev--;
		}
	  }
	}
	while (k <= LBmodel->loopEnd[i]) {
	  if(Ysyntell[k] == INIT_VALU) {
    	getBit(modelbyte, modelptr, adjorprv, modelbitsleft);
		if(adjorprv) {
		  SL32 localValue;
		  SL32 temp;

	      next = k+1;
		  if(next > LBmodel->loopEnd[i]) {
            next = loopStart;
		  }
          while((Ysyntell[next] == INIT_VALU))  {
			next++;
            if(next > LBmodel->loopEnd[i]) {
              next = loopStart;
			}
            
		  }

	      /* interpolate between prev and next */
		  if(localComp) {
	   	    getByte(localbyte, localptr, localValue, localbitsleft);
		  }
          else {     
            localValue = (SL32)*localptr;
            localptr++;
		  }
		  if(localValue == 255)
			localValue = 256;
		  temp = Ysyntell[next] - Ysyntell[prev];
		  if(temp > 0)
		    Ysyntell[k] = Ysyntell[prev] + (((localValue*  temp )+128)>>8);
		  else 
		    Ysyntell[k] = Ysyntell[prev] - (((localValue*(-temp))+128)>>8);
		}
	  
	 	else {
	  	  Ysyntell[k] = Ysyntell[prev];
		}
	  }
      prev = k;
	  k++;
	}
	loopStart = LBmodel->loopEnd[i] + 1;
  }

    

  /* done with Y, now do X */

  parent = 0;
  parentrtyp = 1;
  ml.standStemOU     = scale ((((SW16)ml.standardXstem[stanIndx])<<2), xPixel);
  ml.standWidtOU    = scale ((SL32)TFgroup->standardXwidth, xPixel);  
  ml.FCWstanStemOU     = scale ((SL32)TFgroup->FCWstanXstem<<2, xPixel);  
  ml.FCWstanWidtOU    = scale ((SL32)TFgroup->FCWstanXwidth, xPixel);
  stanStanStem       = (SL32)TFgroup->stanStanXstem;

    /* SETUP STANDARD-STANDARD DIMENSION PROCESSING */
/* keb 6/1/06 */
#if USBOUNDBOX
      /* keb fix 7/24/06 */
      if (TFgroup->stanFlag == BELOW_STAN && (if_state.isUSBBOX == FALSE))  /* standard-standard  */
#else
  if (TFgroup->stanFlag == BELOW_STAN)
#endif
    {
    ml.standStanOU   = scale (((SL32)stanStanStem<<2), xPixel);
    if (((ml.standStanOU-ml.standStemOU) < ((SL32)xPixel->ouFracPixel<<1)) 
      && ((ml.standStanOU-ml.standStemOU) > -((SL32)xPixel->ouFracPixel<<1)))
      {
      ml.standStemOU = ml.standStanOU;
      }
    }



  /* SETUP STANDARD DIMENSION PROCESSING */

  if (TFgroup->stanFlag <= AT_STAN)
    {
    ml.sd_enable = YES;

    /* half pix in terms of percentage of standard: ml.sd_lo: (((standardStem / 4) - (half pixel)) / standardStem) * 256 */
    /* twice the standard + half pix for NonAligns: ml.sd_NA: ((((standardStem * 2) / 4) - (half pixel)) / standardStem) * 256 */
    if (ml.standStemOU) {
      ml.sd_lo = ((ml.standStemOU << 6) - ((SL32)xPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
      ml.sd_hi = ((ml.standStemOU << 6) + ((SL32)xPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
      ml.sd_NA = ((ml.standStemOU << 7) + ((SL32)xPixel->ouFracPixel << 7) + (ml.standStemOU >> 1)) / ml.standStemOU;
	}
    else {
      ml.sd_lo = -256;
      ml.sd_hi = 256;
      ml.sd_NA = 256;
	}
    ml.dimension        = pixel_align_fci_sd (&ml, (ml.standStemOU + 2) >> 2, xPixel);
    ml.sd_alignShift    = ml.alignShift;
  }
  else {
    ml.sd_enable        = NO;
  }



  if(italicAngle == 0) {


    /* set up points 0 and 1 */
    Xsyntell[0] = (ml.standWidtOU + 2) >> 2;
    Xsyntell[0] = pixel_align_coord (Xsyntell[0], xPixel);

    Xsyntell[1] = Xsyntell[0] + scale (Xescapement, xPixel);
    Xsyntell[1] = pixel_align_coord (Xsyntell[1], xPixel);
   

Processxassoc:


	
    getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);

    getBit(modelbyte, modelptr, hassibling, modelbitsleft);
	



    /* process second IP point */
    if(Xsyntell[skelpnt] != INIT_VALU) {
      SL32 localValue;
      SL32 temp;
      if(localComp) {
	    getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		tmp = 0;
        if(locbits) {
          locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		}
        if(locbits != 8) {
          getByte (modelbyte, modelptr, locbase, modelbitsleft);
		}
		else {
          locbase = 0;
		}

	    
        localValue = locbase + tmp;
	  }
	  else {     
       localValue = (SL32)*localptr;
       localptr++;
	  }

      if(localValue == 0x000000ff) {
        localValue = 0x00000100;
	  }

	  temp = Xsyntell[parent] - Xsyntell[skelpnt];
	  if(temp > 0)
	    Xsyntell[skelpnt] = Xsyntell[skelpnt] + (((temp*localValue)+128)>>8);
	  else if(temp < 0) /* leave temp=0 value as is */
	    Xsyntell[skelpnt] = Xsyntell[skelpnt] - ((((-temp)*localValue)+128)>>8);
	  haschild = 0;
	  goto SecondIPx;
	}

	
    getBit(modelbyte, modelptr, haschild, modelbitsleft);
    getBit(modelbyte, modelptr, rType, modelbitsleft);
    getBit(modelbyte, modelptr, gridAligned, modelbitsleft);

	rt_indx = 0;
    if(rType == parentrtyp) {
      rt_indx = 2;          /* LATERAL RTYPE */
	}
	


	
    getBit(modelbyte, modelptr, dimStat, modelbitsleft);
	

    if(haschild) {
      numleft++;
      *stack++ = skelpnt;
      *rtypstack++ = rType;
	}


    if(dimStat) {
      SL32 dimension;
      UB8 dimIndex;
	   
	  
      getByte(modelbyte, modelptr, dimIndex, modelbitsleft);
      getBit(modelbyte, modelptr, assocSign, modelbitsleft);

	  if(altsignptr) 
	    getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
	  
      /* process global dimension */
	  if((ml.XdimType[dimIndex] & STEMBIT)== 0) {
        if((ml.Xfcowflags[dimIndex] & FCOWBIT)) {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.FCWstanStemOU) + 128) >> 8;
		}
        else {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.standStemOU) + 128) >> 8;
		}
     
/* keb 6/1/06 */
#if USBOUNDBOX
	     /* keb fix 7/24/06 */
         if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE))
#else
		 if((ml.sd_enable && rt_indx != 2))
#endif
		 { 
          if(gridAligned) {
            if(((SL32)ml.Xdimensions[dimIndex] >= ml.sd_lo) && 
	  	       ((SL32)ml.Xdimensions[dimIndex] <= ml.sd_hi) ) {
			  if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
                dimension = (ml.standStemOU + 2) >> 2;
			  }
			}
		  }
          else {
            if((SL32)ml.Xdimensions[dimIndex] <= ml.sd_NA) {
              if((assocSign && rType) || (!assocSign && !rType)) {
                dimension += ml.sd_alignShift;
			  } 
              else {
                dimension -= ml.sd_alignShift;
			  }
			}  
		  } 
		}
	  } 
  
      else {                                   /* WDDM_BIT */
        if((ml.Xfcowflags[dimIndex] & FCOWBIT)) {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.FCWstanWidtOU) + 128) >> 8;
		} 
        else {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.standWidtOU) + 128) >> 8;
		} 
        if((SL32)ml.Xdimensions[dimIndex] == 255L) {
          dimension = scale (Xescapement, xPixel);
		} 
	  } 
  
#if BOLD_FCO
      dimension += (xPpixel->roundTyp[rt_indx]-xPpixel->roundTyp[2]);
#endif
/* keb 6/1/06 */
#if USBOUNDBOX
	      /* keb fix 7/24/06 */
          if(gridAligned && (if_state.isUSBBOX == FALSE))
#else
	   if(gridAligned)
#endif
	  { 
        dimension  = pixel_align_fci (&ml, dimension,
                     (UB8)(ml.XRTZflags[dimIndex]&RTZBIT),
					 (UB8)(ml.XconstDims[dimIndex]&CONSBIT),
                     xPixel);
	  } 




      Xsyntell[skelpnt] = Xsyntell[parent] + sygn ((SL32)dimension, assocSign);
	
	}

    else {
		
      getBit(modelbyte, modelptr, isnotstem, modelbitsleft);
	  

      if(isnotstem) {
		  
        getBit(modelbyte, modelptr, iswidth, modelbitsleft);
		
        if(iswidth) {
		  SL32 dimension;
		  UB8 localValue;

		  
          getBit(modelbyte, modelptr, assocSign, modelbitsleft);
  		  if(altsignptr) 
	        getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);

		  

	      /* process width assoc */
          if(localComp) {
	        getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
			tmp = 0;
            if(locbits) {
              locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
			}
            if(locbits != 8) {
              getByte (modelbyte, modelptr, locbase, modelbitsleft);
			}
			else {
              locbase = 0;
			}

	        
            localValue = locbase + tmp;
		  }
	      else {     
            localValue = (SL32)*localptr;
            localptr++;
		  }

          if(localValue == 255) {
            dimension = scale (Xescapement, xPixel);
		  }
          else {
            dimension = (((SL32)(localValue) * (SL32)ml.standWidtOU) + 128) >> 8;
		  }
      


          Xsyntell[skelpnt] = Xsyntell[parent] + sygn ((SL32)dimension, assocSign);
 /* keb 6/1/06 */
#if USBOUNDBOX
		      /* keb fix 7/24/06 */
		      if(gridAligned && (if_state.isUSBBOX == FALSE))
#else
		  if(gridAligned)
#endif		  
		  {
          /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
            Xsyntell[skelpnt] = pixel_align_coord (Xsyntell[skelpnt], xPixel);
		  } 

		} 
	    else {
	      /* process Null assoc */
	      Xsyntell[skelpnt] = Xsyntell[parent];
		} 
	  } 
	  else {
	    SL32 dimension;
	    UB8 localValue;
	    
		
	    getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		if(altsignptr) 
	      getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
		

	    /* process stem assoc */
	    if(localComp) {
	      getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		  tmp = 0;
          if(locbits) {
            locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		  }
          if(locbits != 8) {
            getByte (modelbyte, modelptr, locbase, modelbitsleft);
		  }
		  else {
            locbase = 0;
		  }

	      
          localValue = locbase + tmp;
		}
	    else {     
         localValue = (SL32)*localptr;
         localptr++;
		}

        dimension = (((SL32)(localValue) * (SL32)ml.standStemOU) + 128) >> 8;

/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
            if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE))
#else
		if((ml.sd_enable && rt_indx != 2))
#endif
		{ 
          if(gridAligned) {
            if(((SL32)localValue >= ml.sd_lo) && 
	  	       ((SL32)localValue <= ml.sd_hi) ) {
			  if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
                dimension = (ml.standStemOU + 2) >> 2;
			  }
			}
		  }
          else {
            if((SL32)localValue <= ml.sd_NA) {
              if((assocSign && rType) || (!assocSign && !rType)) {
                dimension += ml.sd_alignShift;
			  }
              else {
                dimension -= ml.sd_alignShift;
			  }
			} 
		  }
		} 



	    Xsyntell[skelpnt] = Xsyntell[parent] + sygn ((SL32)dimension, assocSign);

/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
		    if(gridAligned && (if_state.isUSBBOX == FALSE)) 
#else
		if(gridAligned)
#endif
		{ 
        /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
          Xsyntell[skelpnt] = pixel_align_coord (Xsyntell[skelpnt], xPixel);
		} 
	  } 
	} 

SecondIPx:

    if(hassibling)
      goto Processxassoc;

    if(numleft){
      parent = *--stack;
      parentrtyp = *--rtypstack;
	  numleft--;
	  goto Processxassoc;
	} 

    /* process X PRV/ADJ */


    k = 2;
    loopStart = k;
    prev = 1;
    for (i=0; i<LBmodel->numLoops; i++) {

      if(Xsyntell[k] != INIT_VALU) {
	    prev = k;
	    k++;
	  } 
	  else {
	    if(prev < loopStart) {
		  prev = LBmodel->loopEnd[i];
		  while(Xsyntell[prev] == INIT_VALU) {
	 	    prev--;
		    
		  } 
		} 
	  } 
	
	  while (k <= LBmodel->loopEnd[i]) {
	    if(Xsyntell[k] == INIT_VALU) {
          getBit(modelbyte, modelptr, adjorprv, modelbitsleft);
	      if(adjorprv) {
	        SL32 localValue;
		    SL32 temp;

	        next = k+1;
		    if(next > LBmodel->loopEnd[i])
		      next = loopStart;
            while ((Xsyntell[next] == INIT_VALU))  {
			  next++;
              if(next > LBmodel->loopEnd[i]) {
                next = loopStart;
			  }
           
			} 

		    /* interpolate between prev and next */
			if(localComp) {
	   	      getByte(localbyte, localptr, localValue, localbitsleft);
			}
            else {     
              localValue = (SL32)*localptr;
              localptr++;
			}

		    if(localValue == 255)
			  localValue = 256;
		    temp = Xsyntell[next] - Xsyntell[prev];
		    if(temp > 0)
		      Xsyntell[k] = Xsyntell[prev] + (((localValue*  temp )+128)>>8);
		    else 
		      Xsyntell[k] = Xsyntell[prev] - (((localValue*(-temp))+128)>>8);
		  } 
		
	      else {
	        /* PRV */
	        Xsyntell[k] = Xsyntell[prev];
		  }
		} 
	   prev = k;
	   k++;
	  } 
	 loopStart = LBmodel->loopEnd[i] + 1;
	} 
  }

  else {

	  /* process X for italic characters. Xsyntell contains
	  the italicized points and Isyntell contains the points
	  uprighted. */
	  SL32 temp;  /* Used by UPRIGHT and ITALICIZE */


      /* set up points 0 and 1 */
    Xsyntell[0] = (ml.standWidtOU + 2) >> 2;
    Xsyntell[0] = pixel_align_coord (Xsyntell[0], xPixel);

    Xsyntell[1] = Xsyntell[0] + scale (Xescapement, xPixel);
    Xsyntell[1] = pixel_align_coord (Xsyntell[1], xPixel);
   

	Isyntell[0] = Xsyntell[0];
	Isyntell[1] = Xsyntell[1];



ProcessxassocIt:
    

	
    getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);

    getBit(modelbyte, modelptr, hassibling, modelbitsleft);
	

    /* process second IP point */
    if(Xsyntell[skelpnt] != INIT_VALU) {
      SL32 localValue;
      SL32 temp;

	  if(localComp) {
	    getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		tmp = 0;
        if(locbits) {
          locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		}
        if(locbits != 8) {
          getByte (modelbyte, modelptr, locbase, modelbitsleft);
		}
		else {
          locbase = 0;
		}

	    
        localValue = locbase + tmp;
	  }
	  else {     
       localValue = (SL32)*localptr;
       localptr++;
	  }

      if(localValue == 255) {
        localValue = 256;
	  }

	  temp = Isyntell[parent] - Isyntell[skelpnt];
	  if(temp > 0)
	    Isyntell[skelpnt] = Isyntell[skelpnt] + (((temp*localValue)+128)>>8);
	  else if(temp < 0) /* leave temp=0 value as is */
	    Isyntell[skelpnt] = Isyntell[skelpnt] - ((((-temp)*localValue)+128)>>8);
	  haschild = 0;

	  ITALICIZE(Isyntell[skelpnt],Isyntell[parent],Xsyntell[skelpnt],Xsyntell[parent],Ysyntell[skelpnt],Ysyntell[parent]);
	  goto SecondIPxIt;
	}

	
    getBit(modelbyte, modelptr, haschild, modelbitsleft);
    getBit(modelbyte, modelptr, rType, modelbitsleft);
    getBit(modelbyte, modelptr, gridAligned, modelbitsleft);
	
	rt_indx = 0;
    if(rType == parentrtyp) {
      rt_indx = 2;          /* LATERAL RTYPE */
	}


	
    getBit(modelbyte, modelptr, dimStat, modelbitsleft);
	

    if(haschild) {
      numleft++;
      *stack++ = skelpnt;
      *rtypstack++ = rType;
	}


    if(dimStat) {
      SL32 dimension;
      UB8 dimIndex;
	   
	  
      getByte(modelbyte, modelptr, dimIndex, modelbitsleft);
      getBit(modelbyte, modelptr, assocSign, modelbitsleft);
	  if(altsignptr) 
	    getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
	  
      /* process global dimension */
	  if((ml.XdimType[dimIndex] & STEMBIT)==0) {
        if((ml.Xfcowflags[dimIndex] & FCOWBIT)) {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.FCWstanStemOU) + 128) >> 8;
		}
        else {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.standStemOU) + 128) >> 8;
		}
 /* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
            if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE)) 
#else
		if((ml.sd_enable && rt_indx != 2))
#endif
		{
          if(gridAligned) {
            if(((SL32)ml.Xdimensions[dimIndex] >= ml.sd_lo) && 
	  	       ((SL32)ml.Xdimensions[dimIndex] <= ml.sd_hi) ) {
			  if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
                dimension = (ml.standStemOU + 2) >> 2;
			  }
			}
		  }
          else {
            if((SL32)ml.Xdimensions[dimIndex] <= ml.sd_NA) {
              if((assocSign && rType) || (!assocSign && !rType)) {
                dimension += ml.sd_alignShift;
			  } 
              else {
                dimension -= ml.sd_alignShift;
			  }
			}
		  }
		} 
	  } 
  
      else {                                   /* WDDM_BIT */
        if((ml.Xfcowflags[dimIndex] & FCOWBIT)) {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.FCWstanWidtOU) + 128) >> 8;
		} 
        else {
          dimension  = (((SL32)ml.Xdimensions[dimIndex] * ml.standWidtOU) + 128) >> 8;
		} 
        if((SL32)ml.Xdimensions[dimIndex] == 255L) {
          dimension = scale (Xescapement, xPixel);
		} 
	  } 
  
#if BOLD_FCO
      dimension += (xPpixel->roundTyp[rt_indx]-xPpixel->roundTyp[2]);
#endif



	  

	  temp = (Ysyntell[skelpnt] - Ysyntell[parent])*italicAngle;
	  if(temp >= 0)
		temp = (temp+16384)>>15;
	  else
		temp = -((-temp+16384)>>15);

	  if(ml.YXdimProjType[dimIndex]&PROJBIT) {
        Isyntell[skelpnt] = Isyntell[parent] + sygn ((SL32)dimension, assocSign);
		if(assocSign)
		  dimension -= temp;
		else
		  dimension += temp;
 /* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
		    if(gridAligned && (if_state.isUSBBOX == FALSE)) 
#else
		if(gridAligned)
#endif		
		{
          dimension  = pixel_align_fci (&ml, dimension,
                     (UB8)(ml.XRTZflags[dimIndex]&RTZBIT), 
					 (UB8)(ml.XconstDims[dimIndex]&CONSBIT),
                     xPixel);
		  
		  Isyntell[skelpnt] += sygn (ml.alignShift, assocSign);
		} 
		Xsyntell[skelpnt] = Xsyntell[parent] + sygn((SL32)dimension, assocSign);
	  }
	  else {
		if(assocSign)
		  temp = dimension + temp;
		else
		  temp = dimension - temp;
        Isyntell[skelpnt] = Isyntell[parent] + sygn ((SL32)temp, assocSign);
/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
		    if(gridAligned && (if_state.isUSBBOX == FALSE)) 
#else
		if(gridAligned)
#endif
		{
          dimension  = pixel_align_fci (&ml, dimension,
                     (UB8)(ml.XRTZflags[dimIndex]&RTZBIT),
					 (UB8)(ml.XconstDims[dimIndex]&CONSBIT),
                     xPixel);
		  Isyntell[skelpnt] += sygn (ml.alignShift, assocSign);
		} 
		Xsyntell[skelpnt] = Xsyntell[parent] + sygn ((SL32)dimension, assocSign);
		
	  }
	
	}

    else {
		
      getBit(modelbyte, modelptr, isnotstem, modelbitsleft);
	    

      if(isnotstem) {
		  
          getBit(modelbyte, modelptr, iswidth, modelbitsleft);
		  

        if(iswidth) {
		  SL32 dimension;
		  UB8 localValue;
		  
          getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		  if(altsignptr) 
	        getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
		  

	      /* process width assoc */
		  
          if(localComp) {
	        getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
			tmp = 0;
            if(locbits) {
              locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
			}
            if(locbits != 8) {
              getByte (modelbyte, modelptr, locbase, modelbitsleft);
			}
			else {
              locbase = 0;
			}

	        
            localValue = locbase + tmp;
		  }
	      else {     
            localValue = (SL32)*localptr;
            localptr++;
		  }

          if(localValue == 255) {
            dimension = scale (Xescapement, xPixel);
		  }
          else {
            dimension = (((SL32)(localValue) * (SL32)ml.standWidtOU) + 128) >> 8;
		  }
      

		  

          Isyntell[skelpnt] = Isyntell[parent] + sygn ((SL32)dimension, assocSign);
		  ITALICIZE(Isyntell[skelpnt],Isyntell[parent],Xsyntell[skelpnt],Xsyntell[parent],Ysyntell[skelpnt],Ysyntell[parent]);
		  
/* keb 6/1/06 */
#if USBOUNDBOX
		      /* keb fix 7/24/06 */
		      if(gridAligned && (if_state.isUSBBOX == FALSE)) 
#else
		  if(gridAligned)
#endif		  
		  { 
          /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
			/* adjust Isyntell by the same amount that Xsyntell changed */
            temp = pixel_align_coord (Xsyntell[skelpnt], xPixel);
			Isyntell[skelpnt] += temp - Xsyntell[skelpnt];
			Xsyntell[skelpnt] = temp;
		  } 

		  

		} 
	    else {
	      /* process Null assoc */
	      Isyntell[skelpnt] = Isyntell[parent];
		  ITALICIZE(Isyntell[skelpnt],Isyntell[parent],Xsyntell[skelpnt],Xsyntell[parent],Ysyntell[skelpnt],Ysyntell[parent]);
		  
		} 
	  } 
	  else {
	    SL32 dimension;
	    UB8 localValue;
	    
		
	    getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		if(altsignptr) 
	      getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
		

	    /* process stem assoc */
	    if(localComp) {
	      getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		  tmp = 0;
          if(locbits) {
            locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		  }
          if(locbits != 8) {
            getByte (modelbyte, modelptr, locbase, modelbitsleft);
		  }
		  else {
            locbase = 0;
		  }

	      
          localValue = locbase + tmp;
		}
	    else {     
          localValue = (SL32)*localptr;
          localptr++;
		}

        dimension = (((SL32)(localValue) * (SL32)ml.standStemOU) + 128) >> 8;

/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
            if((ml.sd_enable && rt_indx != 2) && (if_state.isUSBBOX == FALSE)) 
#else		
		if((ml.sd_enable && rt_indx != 2))
#endif
		{ 
          if(gridAligned) {
            if(((SL32)localValue >= ml.sd_lo) && 
	  	       ((SL32)localValue <= ml.sd_hi) ) {
			  if((assocSign && rType) || (!assocSign && !rType)) { /* &&& alter only INTERNAL assocs */
                dimension = (ml.standStemOU + 2) >> 2;
			  }
			}
		  }
          else {
            if((SL32)localValue <= ml.sd_NA) {
              if((assocSign && rType) || (!assocSign && !rType)) {
                dimension += ml.sd_alignShift;
			  }
              else {
                dimension -= ml.sd_alignShift;
			  }
			} 
		  } 
		} 

	    

	    Isyntell[skelpnt] = Isyntell[parent] + sygn ((SL32)dimension, assocSign);
		ITALICIZE(Isyntell[skelpnt],Isyntell[parent],Xsyntell[skelpnt],Xsyntell[parent],Ysyntell[skelpnt],Ysyntell[parent]);

/* keb 6/1/06 */
#if USBOUNDBOX
		    /* keb fix 7/24/06 */
		    if(gridAligned && (if_state.isUSBBOX == FALSE)) 
#else		
		if(gridAligned)
#endif
		{ 
        /* GRID ALIGN THE COORD,NOT THE DIMENSION ! */
          /* adjust Isyntell by the same amount that Xsyntell changed */
          temp = pixel_align_coord (Xsyntell[skelpnt], xPixel);
	      Isyntell[skelpnt] += temp - Xsyntell[skelpnt];
		  Xsyntell[skelpnt] = temp;
		} 

		
	  } 
	} 

SecondIPxIt:

    if(hassibling)
      goto ProcessxassocIt;

    if(numleft){
      parent = *--stack;
      parentrtyp = *--rtypstack;
	  numleft--;
	  goto ProcessxassocIt;
	} 


    /* process X PRV/ADJ */
    k = 2;
    loopStart = k;
    prev = 1;
    for (i=0; i<LBmodel->numLoops; i++) {

      if(Xsyntell[k] != INIT_VALU) {
	    prev = k;
	    k++;
	  } 
	  else {
	    if(prev < loopStart) {
		  prev = LBmodel->loopEnd[i];
	      while(Xsyntell[prev] == INIT_VALU) {
	 	    prev--;
		    
		  } 
		} 
	  } 
	
	  while (k <= LBmodel->loopEnd[i]) {
	    if(Xsyntell[k] == INIT_VALU) {
          getBit(modelbyte, modelptr, adjorprv, modelbitsleft);
	      if(adjorprv) {
	        SL32 localValue;
		    SL32 temp;

	        next = k+1;
		    if(next > LBmodel->loopEnd[i])
		      next = loopStart;
            while ((Xsyntell[next] == INIT_VALU))  {
			  next++;
              if(next > LBmodel->loopEnd[i]) {
                next = loopStart;
			  }
           
			} 

		    /* interpolate between prev and next */
			if(localComp) {
	   	      getByte(localbyte, localptr, localValue, localbitsleft);
			}
            else {     
              localValue = (SL32)*localptr;
              localptr++;
			}
		    
			if(localValue == 255)
			  localValue = 256;
		    temp = Isyntell[next] - Isyntell[prev];
		    if(temp > 0)
		      Isyntell[k] = Isyntell[prev] + (((localValue*  temp )+128)>>8);
		    else 
		      Isyntell[k] = Isyntell[prev] - (((localValue*(-temp))+128)>>8);
			ITALICIZE(Isyntell[k],Isyntell[prev],Xsyntell[k],Xsyntell[prev],Ysyntell[k],Ysyntell[prev]);
		  } 
		
	      else {
	        /* PRV */
	        Isyntell[k] = Isyntell[prev];
			ITALICIZE(Isyntell[k],Isyntell[prev],Xsyntell[k],Xsyntell[prev],Ysyntell[k],Ysyntell[prev]);
		  }
		} 
	   prev = k;
	   k++;
	  } 
	 loopStart = LBmodel->loopEnd[i] + 1;
	} 
  }
    



                        
#ifdef MMDECODE_DEBUG
for (j=0; j<(LBmodel->numYXcSegs+MAX_ANCHOR_PTS); j++)
  {
  printf("after intelliflator: xy= %2d %7ld %7ld %7ld\n", j, Xsyntell[j], Ysyntell[j], Isyntell[j]);
  }
#endif

if(localComp && localbitsleft == 8)
  localptr--;
*curveSegOff = (UW16)((UL32)localptr - (UL32)CHlocal);  /* setup curve data for putsegsII () */


#ifdef MMDECODE_DEBUG
  if (mmdecode_trace_sw)
    {
    if (numOUFILE == 1)
      {
      fclose (OUFILE);
      }
    numOUFILE--;
    }
#endif

return ((UL32)1);
}




#if 0
/************************************************************************************* */
/*  This is used only by the condensor in the curve compression process to skip over
    the local dimentions to locate the curve descriptions - do not remove -bjg */
    
#if defined (ANSI_DEFS)
UL32  getcurvesegoff ( FSP
                  
                MODELTYPE     *LBmodel,
                UB8 *CHlocal,
                SL32          *Ysyntell,
                SL32          *Xsyntell,
				UB8           *parentStack
                
                              )
#else
UL32  getcurvesegoff(CHlocal, CHlocBitPos,
		Ysyntell, Xsyntell,  coordIndexStack
		 )
                 
                MODELTYPE     *LBmodel;
                UB8 *CHlocal;
                UB8 CHlocBitPos;
                SL32          *Ysyntell;
                SL32          *Xsyntell;
				UB8           *parentStack;
				
        
#endif

{ 
MLOC             ml;
SL32             stanStanStem;
UB8    i, j, k;
UB8 *modelptr;
UB8 modelbyte,modelbitsleft;
UB8 *altsignptr;
UB8 altsignbyte,altsignbitsleft;
UB8 *localptr;
UB8 localbyte,localbitsleft;
UB8 *ptr;
UB8 item;
UB8 haschild, hassibling;
UB8 rType,isnotstem, iswidth;
UB8 dimStat;
UB8 *stack = parentStack;
UB8 numleft;
UB8 skelpnt,parent;
UB8 gridAligned,assocSign;
UB8 yClassIndex;
UB8 loopStart,prev,next;
UB8 adjorprv;
UB8 rt_indx;
SL32 italicAngle;
UB8 tmp;
UB8 localComp;
UB8 locbits;
UB8 locbase;
SL32 curveSegOff;


if     (LBmodel->numYXcSegs >= 128) ml.varEncoding = 8;
else if(LBmodel->numYXcSegs >= 64)  ml.varEncoding = 7; 
else if(LBmodel->numYXcSegs >= 32)  ml.varEncoding = 6; 
else if(LBmodel->numYXcSegs >= 16)  ml.varEncoding = 5; 
else                                ml.varEncoding = 4;


LBmodel->loopEnd = MEMptr(LBmodel->modelDataH);
LBmodel->loopEndOff = 0;

modelptr = LBmodel->loopEnd + LBmodel->numLoops;
modelbyte = *modelptr++;
modelbitsleft = 8;






getBit(modelbyte, modelptr, localComp, modelbitsleft);

if(localComp) {
  localptr = CHlocal;
  localbyte = *localptr++;
  localbitsleft = 8;
}
else {
  localptr = CHlocal;
}


getBit(modelbyte, modelptr, item, modelbitsleft);
if(item)
  ml.varEncoding++;
  


/* load loop ends */
ptr = LBmodel->loopEnd;
for (j=0; j<LBmodel->numLoops-1; j++)
  {
	
  getVar (modelbyte, modelptr, item, modelbitsleft, ml.varEncoding);
  *ptr++ = item;

  
  }
*ptr = LBmodel->numYXcSegs-1;

/* INITIALIZE TREE SKELS */

for (k=0; k<LBmodel->numYXcSegs+MAX_ANCHOR_PTS+16; k++)  {
    Ysyntell[k] = (SL32)INIT_VALU;
    Xsyntell[k] = (SL32)INIT_VALU;
}                  





  parent = -1;
  numleft = 0;
  
  Ysyntell[0]        = 10;
  
  Ysyntell[1]        = 10;

   
Processyroot:

  
  getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);
  getBit(modelbyte, modelptr, hassibling, modelbitsleft);
  getBit(modelbyte, modelptr, haschild, modelbitsleft);

  getBit(modelbyte, modelptr, rType, modelbitsleft);
  getByte(modelbyte, modelptr, yClassIndex, modelbitsleft);
  

  if(haschild) {
     numleft++;
    *stack++ = skelpnt;
	
  }

  /*  process root point */

  if(localComp) {
	getTri (modelbyte, modelptr, locbits, modelbitsleft);
    tmp = 0;    
    if(locbits) {
      locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
	  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
	}
	if(locbits != 8) {
      getByte (modelbyte, modelptr, locbase, modelbitsleft);
	}
	else {
      locbase = 0;
	}

    ml.fraction = locbase + tmp;
	
  }
  else {     
    ml.fraction = (SL32)*localptr;
    localptr++;
  }

  Ysyntell[skelpnt] = 10;
  

  if(hassibling) 
    goto Processyroot;

Processbranch:

  if(numleft == 0)
	goto Yprevadj;
  parent = *--stack;
  numleft--;

Processsibling:

  
  getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);
  getBit(modelbyte, modelptr, hassibling, modelbitsleft);
  

  /* process second IP point */
  if(Ysyntell[skelpnt] != INIT_VALU) {
    SL32 localValue;
    SL32 temp;
	  
    if(localComp) {
	  getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
	  tmp = 0;
      if(locbits) {
        locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		getVar(localbyte, localptr, tmp, localbitsleft,locbits);
	  }
      if(locbits != 8) {
        getByte (modelbyte, modelptr, locbase, modelbitsleft);
	  }
	  else {
        locbase = 0;
	  }

      localValue = locbase + tmp;
	}
	else {     
     localValue = (SL32)*localptr;
     localptr++;
	}

    
	
	Ysyntell[skelpnt] = 10;
	   
    haschild = 0;
	  goto SecondIPy;
  }

  
  getBit(modelbyte, modelptr, haschild, modelbitsleft);

  getBit(modelbyte, modelptr, rType, modelbitsleft);
  getBit(modelbyte, modelptr, gridAligned, modelbitsleft);
  getBit(modelbyte, modelptr, dimStat, modelbitsleft);
  


  if(haschild) {
    numleft++;
    *stack++ = skelpnt;
  }

 
  if(dimStat) {

    SL32 dimension;
    UB8 dimIndex;

	
    getByte(modelbyte, modelptr, dimIndex, modelbitsleft);
	getBit(modelbyte, modelptr, assocSign, modelbitsleft);
	 

    Ysyntell[skelpnt] = 10;
  }
  

  else {  /* local dimension */
	  
    getBit(modelbyte, modelptr, isnotstem, modelbitsleft);
	
    if(isnotstem) {
	
		
      getBit(modelbyte, modelptr, iswidth, modelbitsleft);
	  
      if(iswidth) {
	 
        SL32 dimension;
        UB8 localValue;
		
        getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		if(altsignptr) 
	      getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);

		

		/* process width assoc */
        if(localComp) {
	      getTri(modelbyte, modelptr, locbits, modelbitsleft);
        
		  tmp = 0;
          if(locbits) {
            locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		  }
          if(locbits != 8) {
            getByte (modelbyte, modelptr, locbase, modelbitsleft);
		  }
		  else {
            locbase = 0;
		  }

	      
          localValue = locbase + tmp;
		}
	    else {     
         localValue = (SL32)*localptr;
         localptr++;
		}

        

        Ysyntell[skelpnt] = 10;

		
	  }
	  else {
	    /* process Null assoc */
	    Ysyntell[skelpnt] = 10;
	  }
	}

	   
    else { /* stem */
	  SL32 dimension;
	  UB8 localValue;
	  
      getBit(modelbyte, modelptr, assocSign, modelbitsleft);

	  if(altsignptr) 
	    getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
	  
	  /* process stem assoc */
      if(localComp) {
	    getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		tmp = 0;
        if(locbits) {
          locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		}
        if(locbits != 8) {
          getByte (modelbyte, modelptr, locbase, modelbitsleft);
		}
		else {
          locbase = 0;
		}

	    
        localValue = locbase + tmp;
	  }
	  else {     
       localValue = (SL32)*localptr;
       localptr++;
	  }

      Ysyntell[skelpnt] = 10;


	}
  }


SecondIPy:

  if(hassibling)
    goto Processsibling;

  if(numleft)
    goto Processbranch;

Yprevadj:
   /* process Y ADJ/PRV bits */

 
  k = 2;
  loopStart = k;
  prev = 1;
  for (i=0; i<LBmodel->numLoops; i++) {

	if(Ysyntell[k] != INIT_VALU) {
	  prev = k;
	  k++;
	}
	else {
	  if(prev < loopStart) {
		 prev = LBmodel->loopEnd[i];
	     while(Ysyntell[prev] == INIT_VALU) {
	 	   prev--;
		   break;
		}
	  }
	}
	while (k <= LBmodel->loopEnd[i]) {
	  if(Ysyntell[k] == INIT_VALU) {
    	getBit(modelbyte, modelptr, adjorprv, modelbitsleft);
		if(adjorprv) {
		  SL32 localValue;
		  SL32 temp;

	      next = k+1;
		  if(next > LBmodel->loopEnd[i]) {
            next = loopStart;
		  }
          while((Ysyntell[next] == INIT_VALU))  {
			next++;
            if(next > LBmodel->loopEnd[i]) {
              next = loopStart;
			}
            
		  }

	      /* interpolate between prev and next */
		  if(localComp) {
	   	    getByte(localbyte, localptr, localValue, localbitsleft);
		  }
          else {     
            localValue = (SL32)*localptr;
            localptr++;
		  }
		  
		  Ysyntell[k] = 10;
		}
	  
	 	else {
	  	  Ysyntell[k] = 10;
		}
	  }
      prev = k;
	  k++;
	}
	loopStart = LBmodel->loopEnd[i] + 1;
  }

    

  /* done with Y, now do X */

  parent = 0;
  



  


    /* set up points 0 and 1 */
    Xsyntell[0] = 10;
    
    Xsyntell[1] = 10;
    
   

Processxassoc:


	
    getVar (modelbyte, modelptr, skelpnt, modelbitsleft, ml.varEncoding);
    getBit(modelbyte, modelptr, hassibling, modelbitsleft);
	



    /* process second IP point */
    if(Xsyntell[skelpnt] != INIT_VALU) {
      SL32 localValue;
      SL32 temp;
      if(localComp) {
	    getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		tmp = 0;
        if(locbits) {
          locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
		  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		}
        if(locbits != 8) {
          getByte (modelbyte, modelptr, locbase, modelbitsleft);
		}
		else {
          locbase = 0;
		}

	    
        localValue = locbase + tmp;
	  }
	  else {     
       localValue = (SL32)*localptr;
       localptr++;
	  }

      
	  Xsyntell[skelpnt] = 10;
	  haschild = 0;
	  goto SecondIPx;
	}

	
    getBit(modelbyte, modelptr, haschild, modelbitsleft);
    getBit(modelbyte, modelptr, rType, modelbitsleft);
    getBit(modelbyte, modelptr, gridAligned, modelbitsleft);

	
	


	
    getBit(modelbyte, modelptr, dimStat, modelbitsleft);
	

    if(haschild) {
      numleft++;
      *stack++ = skelpnt;
	}


    if(dimStat) {
      SL32 dimension;
      UB8 dimIndex;
	   
	  
      getByte(modelbyte, modelptr, dimIndex, modelbitsleft);
      getBit(modelbyte, modelptr, assocSign, modelbitsleft);

	  Xsyntell[skelpnt] = 10;
	
	}

    else {
		
      getBit(modelbyte, modelptr, isnotstem, modelbitsleft);
	  

      if(isnotstem) {
		  
        getBit(modelbyte, modelptr, iswidth, modelbitsleft);
		
        if(iswidth) {
		  SL32 dimension;
		  UB8 localValue;

		  
          getBit(modelbyte, modelptr, assocSign, modelbitsleft);
  		  if(altsignptr) 
	        getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);

		  

	      /* process width assoc */
          if(localComp) {
	        getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
			tmp = 0;
            if(locbits) {
              locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			  getVar(localbyte, localptr, tmp, localbitsleft,locbits);
			}
            if(locbits != 8) {
              getByte (modelbyte, modelptr, locbase, modelbitsleft);
			}
			else {
              locbase = 0;
			}

	        
            localValue = locbase + tmp;
		  }
	      else {     
            localValue = (SL32)*localptr;
            localptr++;
		  }


          Xsyntell[skelpnt] = 10;
		  

		} 
	    else {
	      /* process Null assoc */
	      Xsyntell[skelpnt] = 10;
		} 
	  } 
	  else {
	    SL32 dimension;
	    UB8 localValue;
	    
		
	    getBit(modelbyte, modelptr, assocSign, modelbitsleft);
		if(altsignptr) 
	      getBit(altsignbyte, altsignptr, assocSign, altsignbitsleft);
		

	    /* process stem assoc */
	    if(localComp) {
	      getTri (modelbyte, modelptr, locbits, modelbitsleft);
        
		  tmp = 0;
          if(locbits) {
            locbits++;       /* 0=>0, 1=>2, 2=>3 etc */
			getVar(localbyte, localptr, tmp, localbitsleft,locbits);
		  }
          if(locbits != 8) {
            getByte (modelbyte, modelptr, locbase, modelbitsleft);
		  }
		  else {
            locbase = 0;
		  }

	      
          localValue = locbase + tmp;
		}
	    else {     
         localValue = (SL32)*localptr;
         localptr++;
		}

        Xsyntell[skelpnt] = 10;


	    
	  } 
	} 

SecondIPx:

    if(hassibling)
      goto Processxassoc;

    if(numleft){
      parent = *--stack;
	  numleft--;
	  goto Processxassoc;
	} 

    /* process X PRV/ADJ */


    k = 2;
    loopStart = k;
    prev = 1;
    for (i=0; i<LBmodel->numLoops; i++) {

      if(Xsyntell[k] != INIT_VALU) {
	    prev = k;
	    k++;
	  } 
	  else {
	    if(prev < loopStart) {
		  prev = LBmodel->loopEnd[i];
	      while(Xsyntell[prev] == INIT_VALU) {
	 	    prev--;
		    break;
		  } 
		} 
	  } 
	
	  while (k <= LBmodel->loopEnd[i]) {
	    if(Xsyntell[k] == INIT_VALU) {
          getBit(modelbyte, modelptr, adjorprv, modelbitsleft);
	      if(adjorprv) {
	        SL32 localValue;
		    SL32 temp;

	        next = k+1;
		    if(next > LBmodel->loopEnd[i])
		      next = loopStart;
            while ((Xsyntell[next] == INIT_VALU))  {
			  next++;
              if(next > LBmodel->loopEnd[i]) {
                next = loopStart;
			  }
           
			} 

		    /* interpolate between prev and next */
			if(localComp) {
	   	      getByte(localbyte, localptr, localValue, localbitsleft);
			}
            else {     
              localValue = (SL32)*localptr;
              localptr++;
			}

		    Xsyntell[k] = 10;
		  } 
		
	      else {
	        /* PRV */
	        Xsyntell[k] = 10;
		  }
		} 
	   prev = k;
	   k++;
	  } 
	 loopStart = LBmodel->loopEnd[i] + 1;
	} 
  


if(localComp && localbitsleft == 8)
  localptr--;
curveSegOff = (UW16)((UL32)localptr - (UL32)CHlocal);  /* setup curve data for putsegsII () */
return curveSegOff;


}

/************************************************************************************* */

#endif /* if 0 for compressor code */


#endif  /* FCO_RDR - conditionalize entire file */
#endif  /* FCO3     - conditionalize entire file for Microtype 3 */

