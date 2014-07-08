/** \file
 * \ingroup ccitt
 *
 * $HopeName: COREccitt!src:ccitt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal defines and data structures used for CCITT fax image decompression
 */

#ifndef __CCITT_H__
#define __CCITT_H__


/* ----------------------------------------------------------------------------
   file:                CCITT Fax         author:              Paul Attridge
   creation date:       12-Jun-1991       last modification:   ##-###-####

---------------------------------------------------------------------------- */

#define MaxCodes        64

typedef struct termcode {
  int32         numofbits ;     /* number of significant bits in code */
  int32         codeword ;      /* code to be used */
  int32         codevalue ;     /* value that this represents */
} TERMCODE ;

typedef struct {
  int32 numofwhiteblack[ 2 ] ;
} IMAGETABLE ;

/* access macros */
#define theINumOfBits( val )    ( (val)->numofbits )
#define theNumOfBits( val )     ( (val).numofbits )
#define theICodeWord( val )     ( (val)->codeword )
#define theCodeWord( val )      ( (val).codeword )
#define theICodeValue( val )    ( (val)->codevalue )
#define theCodeValue( val )     ( (val).codevalue )

/* table used to maintain bit ordered codes */
typedef struct sorttable {
  int32 count ;
  TERMCODE *table[ MaxCodes ] ;
} SORTED ;

#define theSortCount( val ) ( (val).count )
#define theSortTable( val ) ( (val).table )
#define theISortCount( val ) ( (val)->count )
#define theISortTable( val ) ( (val)->table )

/* data strucuture formed from filter dictionary, static information */
typedef struct faxdictinfo {
  int32 Uncompressed ;    /* Encode permitted to use uncompressed encoding */
  int32 K ;               /* Coding scheme ; K = 0 Group 3 1D, K < 0 Group 4,
                             K > 0 Group 3 2D */
  int32 EndOfLineFlag ;   /* Encode prefix endofline, decode if true then
                             must be present */
  int32 EncodedByteAlign ;/* Encode fills to nearest byte, decode only works on byte boundary */
  int32 Columns ;         /* Width of destination/source image in pixels */
  int32 Rows ;            /* Height of destination/source image, if
                             0 image is ended by EOB */
  int32 EndOfBlock ;      /* Encode appends EOB, Decode expects EOB */
  uint32 BlackIs1 ;       /* 1 is black, 0 is white */
} FAXINFO ;

/* access macros */
#define theFaxUncompressed( val )       ( (val).Uncompressed )
#define theIFaxUncompressed( val )      ( (val)->Uncompressed )
#define theFaxK( val )                  ( (val).K )
#define theIFaxK( val )                 ( (val)->K )
#define theFaxEndOfLine( val )          ( (val).EndOfLineFlag )
#define theIFaxEndOfLine( val )         ( (val)->EndOfLineFlag )
#define theFaxEncodedByteAlign( val )   ( (val).EncodedByteAlign )
#define theIFaxEncodedByteAlign( val )  ( (val)->EncodedByteAlign )
#define theFaxColumns( val )            ( (val).Columns )
#define theIFaxColumns( val )           ( (val)->Columns )
#define theFaxRows( val )               ( (val).Rows )
#define theIFaxRows( val )              ( (val)->Rows )
#define theFaxEndOfBlock( val )         ( (val).EndOfBlock )
#define theIFaxEndOfBlock( val )        ( (val)->EndOfBlock )
#define theFaxBlackIs1( val )           ( (val).BlackIs1 )
#define theIFaxBlackIs1( val )          ( (val)->BlackIs1 )

#define EDGE_SMALL 8
#define EDGE_LIMIT 4096
#define EDGE_START 1
#define EDGE_END   3
#define EDGE_EXTRA ( EDGE_START + EDGE_END )
#define EDGE_TOTAL ( EDGE_LIMIT + EDGE_EXTRA )

/* used to maintain the state of a faxfilter when running */
typedef struct faxstate {
  FAXINFO *faxinfo ;    /* dictionary of filter settings */
  int32 LineCount ;     /* current K value */
  int32 MaxWidth ;      /* up to date version of Columns in info structure */
  int32 CurHeight ;     /* current number of rows read/written */
  int32 lookaheadcode ; /* last code read at end of line */
  int32 lastbyte ;      /* last byte read/written in datastream */
  int32 lastbits ;      /* bit position in last byte */
  uint32 *refline ;     /* reference line used in 2D encoding */
  int32 *bufedge ;      /* new edge table used in 2D encoding */
  int32 *refedge ;      /* old edge table used in 2D encoding */
  int32 edgesize ;      /* edge size allocated; may be less than Columns. */
  int32 useedge ;       /* use edge table used in 2D encoding */
  int32 checktwodcodes ;/* used to indicate that search should check
                           2D code table */
} FAXSTATE ;

/* access macros */
#define theFaxInfo( val )          ( (val).faxinfo )
#define theIFaxInfo( val )         ( (val)->faxinfo )
#define theFaxLineCount( val )     ( (val).LineCount ) /* used in encode only */
#define theIFaxLineCount( val )    ( (val)->LineCount )
#define theFaxMaxWidth( val )      ( (val).MaxWidth )
#define theIFaxMaxWidth( val )     ( (val)->MaxWidth )
#define theFaxCurHeight( val )     ( (val).CurHeight )
#define theIFaxCurHeight( val )    ( (val)->CurHeight )
#define theFaxLookAhead( val )     ( (val).lookaheadcode )
#define theIFaxLookAhead( val )    ( (val)->lookaheadcode )
#define theFaxLastByte( val )      ( (val).lastbyte )
#define theIFaxLastByte( val )     ( (val)->lastbyte )
#define theFaxLastBits( val )      ( (val).lastbits )
#define theIFaxLastBits( val )      ( (val)->lastbits )
#define theFaxRefLine( val )       ( (val).refline )
#define theIFaxRefLine( val )      ( (val)->refline )
#define theFaxBufEdge( val )       ( (val).bufedge )
#define theIFaxBufEdge( val )      ( (val)->bufedge )
#define theFaxRefEdge( val )       ( (val).refedge )
#define theIFaxRefEdge( val )      ( (val)->refedge )
#define theEdgeSize( val )         ( (val).edgesize )
#define theIEdgeSize( val )        ( (val)->edgesize )
#define theUseRefEdge( val )       ( (val).useedge )
#define theIUseRefEdge( val )      ( (val)->useedge )
#define theFaxCheck2DCodes( val )  ( (val).checktwodcodes )
#define theIFaxCheck2DCodes( val ) ( (val)->checktwodcodes )

/*
 * Constants and macros
 */

#define CCITTHASHBITS      8 /* No more than 11 */
#define CCITTHASHSIZE      (1<<CCITTHASHBITS)
#define CCITTHASHMASK      (CCITTHASHSIZE-1)

#define CodeDivider           64
#define MaxCodeSize           13
#define G40DEndCount           2
#define G31DEndCount           6
#define NumOfMakeUpCodes      27
#define MinMakeUp             64
#define MaxMakeUp           1728
#define NumOfExtenders        13
#define MinExtendedMakeUp   1792
#define MaxExtendedMakeUp   2560

#define EolCodeAlignBits                  12

#define NumberCode                      ( 0)
#define EOFCode                         (-1)
#define EolCode                         (-2)
#define PassModeCode                    (-3)
#define HorizontalModeCode              (-4)
#define Extension2DCode                 (-5)
#define Data0Code                       (-6)
#define Data1Code                       (-7)
#define VerticalR3Code                  (-8)
#define VerticalR2Code                  (-9)
#define VerticalR1Code                  (-10)
#define Vertical0Code                   (-11)
#define VerticalL1Code                  (-12)
#define VerticalL2Code                  (-13)
#define VerticalL3Code                  (-14)

#define VerticalCode(_code) ((_code) <= VerticalR3Code )

#if 0
/* For fast switching, we switch on the code normalised.
 * The normalisation is defined as adding the smallest
 * number on which is 15. We do this so that 0 is reserved.
 * Since normal opcodes come through here also, we want
 * them to switch to the HorizontalModeCode case. We do that
 * by having a 15 label there. The 15 is got by anding the top
 * bit with the rest of the number. For runs this will result
 * in 15.
 */

#define SwitchCode( _code ) (((_code) & ((uint32)(_code) >> 31 )) + 15 )
#define CaseCode( _code ) ((_code) + 15 )
#endif

#define PassMode        ( & twod_codetable[ 0 ] )
#define HorizontalMode  ( & twod_codetable[ 1 ] )
#define VerticalR3      ( & twod_codetable[ 2 ] )
#define VerticalR2      ( & twod_codetable[ 3 ] )
#define VerticalR1      ( & twod_codetable[ 4 ] )
#define Vertical0       ( & twod_codetable[ 5 ] )
#define VerticalL1      ( & twod_codetable[ 6 ] )
#define VerticalL2      ( & twod_codetable[ 7 ] )
#define VerticalL3      ( & twod_codetable[ 8 ] )
#define Extension2D     ( & twod_codetable[ 9 ] )

#define IsTerminatingCode( val )        (( val & (~0x3F)) == 0 )

#endif /* protection for multiple inclusion */


/* Log stripped */
