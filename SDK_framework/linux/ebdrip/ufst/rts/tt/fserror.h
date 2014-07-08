/* $HopeName: GGEufst5!rts:tt:fserror.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:tt:fserror.h,v 1.2.10.1.1.1 2013/12/19 11:24:05 rogerb Exp $ */
/* Log stripped */
/* $Date: 2013/12/19 11:24:05 $ */
/*
   File:    FSError.h

 
 

   Copyright: © 1989-1990 by Apple Computer, Inc., all rights reserved.



    Change History (most recent first):

    AGFA changes:

       18-Jan-93  mby   Added new errors - FSE_GET_GLYPH_FAILURE and
                        FSE_NO_CHAR_DATA

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		 <3+>	7/11/91		CKL		Added standard header conditionals
		 <3>	12/20/90	RB		Define INVALID_GLYPH_INDEX error (0x100A) return for
									fs_NewGlyph. [mr]
		 <2>	12/11/90	MR		Add unknown_cmap_format error code. [rb]
		 <4>	 7/13/90	MR		made endif at bottom use a comment
		 <3>	  5/3/90	RB		Changed char to int8 for variable type.   Now it is legal to
									pass in zero as the address of memory when a piece of
		 <2>	 2/27/90	CL		New error code for missing but needed table. (0x1409 )
	   <3.1>	11/14/89	CEL		Now it is legal to pass in zero as the address of memory when a
									piece of the sfnt is requested by the scaler. If this happens
									the scaler will simply exit with an error code !
	   <3.0>	 8/28/89	sjk		Cleanup and one transformation bugfix
	   <2.2>	 8/14/89	sjk		1 point contours now OK
	   <2.1>	  8/8/89	sjk		Improved encryption handling
	   <2.0>	  8/2/89	sjk		Just fixed EASE comment
	   <1.5>	  8/1/89	sjk		Added composites and encryption. Plus some enhancements…
	   <1.4>	 6/13/89	SJK		Comment
	   <1.3>	  6/2/89	CEL		16.16 scaling of metrics, minimum recommended ppem, point size 0
									bug, correct transformed integralized ppem behavior, pretty much
									so
	   <1.2>	 5/26/89	CEL		EASE messed up on “c” comments
	  <•1.1>	 5/26/89	CEL		Integrated the new Font Scaler 1.0 into Spline Fonts
	   <1.0>	 5/25/89	CEL		Integrated 1.0 Font scaler into Bass code for the first time…

	To Do:
*/

#ifndef __FSERROR__
#define __FSERROR__

/************/
/** ERRORS **/
/************/
#define NO_ERR						0x0000

/** EXTERNAL INTERFACE PACKAGE **/
#define ERR_TT_NULL_KEY						3001
#define ERR_TT_NULL_INPUT_PTR				3002
#define ERR_TT_NULL_SFNT_DIR				3003
#define ERR_TT_NULL_OUTPUT_PTR				3004
#define ERR_TT_INVALID_GLYPH_INDEX			3005

/* fnt_execute */
#define ERR_TT_UNDEFINED_INSTRUCTION		3006

/** SFNT DATA ERROR and errors in sfnt.c **/
#define ERR_TT_POINTS_DATA					3007
#define ERR_TT_CONTOUR_DATA					3008
#define ERR_TT_BAD_MAGIC					3009
#define ERR_TT_OUT_OF_RANGE_SUBTABLE		3010
#define ERR_TT_UNKNOWN_COMPOSITE_VERSION	3011
#define ERR_TT_CLIENT_RETURNED_NULL			3012
#define ERR_TT_MISSING_SFNT_TABLE			3013
#define ERR_TT_UNKNOWN_CMAP_FORMAT			3014

#endif  /* __FSERROR__ */

/* -------------------------------------------------------------------
                            END OF "FSError.h"
   ------------------------------------------------------------------- */

