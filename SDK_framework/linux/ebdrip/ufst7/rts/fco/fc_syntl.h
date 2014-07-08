
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fc_syntl.h */


/*-----------------------------------------------------------------*/

#ifndef __FC_SYNTL__
#define __FC_SYNTL__

#define  NUM_YX_SKEL      0xff
#define  NUM_Y_TREES      0xff
#define  NUM_YLINES       0xff
#define  NUM_YCLASS       0xff
#define  NUM_DIMENS       0xff
#define  NUM_LOOPS        0x40
#define  MAX_ANCHOR_PTS   4
#define  ABOVE_STAN       2  /* Standard dimension cut-in limit has not been reached */
#define  AT_STAN          1  /* Standard dimension cut-in limit has been reached */
#define  BELOW_STAN       0  /* Standard-standard dimension cut-in limit has been reached */

/*
The following data description supports an individual character model */

#if FCO3
typedef struct
  {
  MEM_HANDLE     xCORdataH;
  MEM_HANDLE     yCORdataH;
  MEM_HANDLE     yxEXTdataH;
  MEM_HANDLE     xCORsubstH;
  MEM_HANDLE     yCORsubstH;
  MEM_HANDLE     yxEXTsubstH;
  MEM_HANDLE     modelDataH;   /* Handle to MODEL data arrays      */
  MEM_HANDLE     altSignDataH; /* Handle to alt sign data array    */
  MEM_HANDLE     rstackH;      /* Handle to MODEL processing stack */
  UB8  *forest[2];
  UB8  *YXcSegSource;
  UB8  *loopEnd;
  UB8  *potDiscard;
  UB8  Ebit;         /* bit position for beginning of YXcSegSource data */
  UB8  Pbit;         /* bit position for beginning of potDiscard data  */
  UB8  chgrpIndex;   /* FCO wide character group index for stan dims */
  UB8  numLoops;     /* Number of loops this model */
  UB8  numYXcSegs;   /* Number of contour segments in this character description */
  UB8  localCompressSwitch;   /* Indicates whether we use local compression (1) or not (0) */
  SL32 italicSwitch; /* Indicates whether character is italic: 0 no, 1 yes */

  SW16 loopEndOff;   /* List of indices to last segment end point in each loop */
  UL32 altSignOff;   /* offset to the model containing the signs for this model
					     0 if there is no alt signs to use */
  UL32 modelDataOff; /* offset to the model data in the fco */
  UW16 modelDataLen; /* length of the model data in the fco */
  } MODELTYPE;


typedef struct skelNodeLabel     /* definition for each node in skeletal tree */
  {
  UB8         coordIndex;
  UB8         rTypology;
  UB8         gridAlign;
  UB8         assocSign;
  UB8         dimStat;
  UB8         assocValue;
  UB8         dm_indx;
  UB8         yClassIndex;
  UB8         CORorEXT;
  UB8         localbits;
  UB8         localbase;
  struct skelNodeLabel  *parent;
  struct skelNodeLabel  *child;
  struct skelNodeLabel  *sibling;
  }      skelNode;


#endif  /* FCO3 */

#if FCO2
typedef struct
  {
  MEM_HANDLE     xCORdataH;
  MEM_HANDLE     yCORdataH;
  MEM_HANDLE     yxEXTdataH;
  MEM_HANDLE     xCORsubstH;
  MEM_HANDLE     yCORsubstH;
  MEM_HANDLE     yxEXTsubstH;
  MEM_HANDLE     modelDataH;   /* Handle to MODEL data arrays      */
  MEM_HANDLE     rstackH;      /* Handle to MODEL processing stack */
  UB8  *forest[2];
  UB8  *YXcSegSource;
  UB8  *loopEnd;
  UB8  *potDiscard;
  UB8  Ebit;         /* bit position for beginning of YXcSegSource data */
  UB8  Pbit;         /* bit position for beginning of potDiscard data  */
  UB8  chgrpIndex;   /* FCO wide character group index for stan dims */
  UB8  numLoops;     /* Number of loops this model */
  UB8  numYXcSegs;   /* Number of contour segments in this character description */
  UB8  localCompressSwitch;   /* Indicates whether we use local compression (1) or not (0) */
  SL32           italicSwitch; /* Indicates whether character is italic: 0 no, 1 yes */

  SW16          loopEndOff;   /* List of indices to last segment end point in each loop */
  } MODELTYPE;


typedef struct skelNodeLabel     /* definition for each node in skeletal tree */
  {
  UB8         coordIndex;
  UB8         rTypology;
  UB8         gridAlign;
  UB8         assocSign;
  UB8         dimStat;
  UB8         assocValue;
  UB8         dm_indx;
  UB8         yClassIndex;
  UB8         CORorEXT;
  UB8         localbits;
  UB8         localbase;
  struct skelNodeLabel  *parent;
  struct skelNodeLabel  *child;
  struct skelNodeLabel  *sibling;
  }      skelNode;


#endif  /* FCO2 */
#if FCO1
typedef struct
  {
  UB8  chgrpIndex;   /* FCO wide character group index for stan dims */
  UB8  numLoops;     /* Number of loops this model */
  UB8  italicSwitch; /* Indicates whether character is italic: 0 no, 1 yes */
  UB8  numYroot;     /* Number of Yskeletal trees (numXroot is assumed to be 1) */
  UB8  numYXskel;    /* Number of Y and X skeletal tree nodes  (including interps) */
  UB8  numYXcSegs;   /* Number of contour segments in this character description */
  UB8  numYXcSegSo;  /* Number of contour segment source specifiers in theis character description */
  UB8  numYXdim;     /* Total number of dimension indices used by this character (Y then X) */
  UB8  numLocYXdims; /* Number of local segment end point descriptions for Y and X */
  MEM_HANDLE     modelDataH;   /* Handle to MODEL data arrays */
  SW16          skelListOff;  /* List of Y then X skeletal points in tree processing order */
  SW16          dimIndexOff;  /* List of indices to the Dimension Stack located in Typeface Global Data Area */
  SW16          yclAssIndxOff;/* List of indices to either the yclass or the yline stacks in the Typeface Global Data Area */
  SW16          loopEndOff;   /* List of indices to last segment end point in each loop */
  SW16          numAssocOff;  /* List of number of associations (children) branching from this Y or X skeletal node */
  SW16          alignAttribOff; /* List of grid alignment attributes: 0 = do not align;  1 = do align */
  SW16          assocValueOff;/* List of Standard Association Class Values: 00=NULL; 01=STEM; 10=WDHT; 11=LFSB/RTSB */
  SW16          rTypeYXOff;   /* List of indicators for computing rTypes: YPROC: 0=YL; 1=YC; XPROC: 0=LS; 1=AJ; */
  SW16          dimStatOff;   /* List of dim status for assoc coming into each Y or X skel node (except root): 0 = no dim; 1 = dim */
  SW16          assocSignOff; /* List of signs of the assoc coming into each Y or X skeletal node: 0 = positive; 1 = negative */
  SW16          cSegSourcYXOff; /* List of indicators as to source of data for computing a given contour seg end point */
  } MODELTYPE;
#endif  /* FCO1 */

/*
The following data description supports the Global Data applied to a group of characters (i.e., entire typeface or all lowercase, etc.) */

typedef struct      /* Typeface global data */
  {
  UL32  standardYheight;  /* Standard height for this character group */
  UL32  standardXwidth;   /* Standard width for this character group */
  SL32          italicAngle;      /* Italic angle adjusted for aspect ratio */
  SL32           origItalicAngle;  /* Italic angle of the typeface: 2**15 x tangent */
  UL32  numYline;         /* Number of ylines this character group */
  UL32  numYclass;        /* Number of yclasses */
  UL32  numYconstdim;     /* Number of Y constrained dimensions */
  UL32  numXconstdim;     /* Number of X constrained dimensions */
  UL32  FCWstanYstem;     /* FCO wide standard Y stem */
  UL32  FCWstanXstem;     /* FCO wide standard X stem */
  UL32  FCWstanYwidth;    /* FCO wide standard height */
  UL32  FCWstanXwidth;    /* FCO wide standard width  */
  UL32  stanStanYstem;    /* Standard-standard Y dimension */
  UL32  stanStanXstem;    /* Standard-standard X dimension */
  UL32  stanFlag;         /* flag to indicate state of processing re: standard dimensions */
  UL32  numYXstanStem;    /* Number of Y and X standard stems */
  MEM_HANDLE    globalDataH;      /* Handle to TF global data */
  SW16         standardYstemOff; /* List of Standard Y stem weight for this char group */
  SW16         standardXstemOff; /* List of Standard X stem weight for this char group */
  SW16         yLineValueOff;    /* List of yline values at 127th EM: max = 2X EM */
  SW16         YdimensionsOff;   /* List of Y dimension values as 127th of stan ht/wd (max = 2X) or 63rd of stan stem (max = 4X) */
  SW16         XdimensionsOff;   /* List of X dimension values as 127th of stan ht/wd (max = 2X) or 63rd of stan stem (max = 4X) */
  SW16         YdimFlagsOff;     /* List of flags for dimensions */
  SW16         XdimFlagsOff;     /* 0x01   constrained dim flag */
                                 /* 0x02   FCOWide flag */
                                 /* 0x04   RTZ flag */
/* next two values are offsets into yClassData Table */
  UW16 yClassDefLo;     /* List of indices to yLineValue list for low yline of this yclass */
  UW16 yClassDefHi;     /* List of indices to yLineValue list for high yline of this yclass */
  } INFLATE;  /* SUBGLOBAL; */

#endif	/* __FC_SYNTL__ */
