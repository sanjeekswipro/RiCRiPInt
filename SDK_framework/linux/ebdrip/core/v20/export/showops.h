/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:showops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS show operators defines
 */

#ifndef __SHOWOPS_H__
#define __SHOWOPS_H__

/* FMapType values (see Reference Book pg 287. */
#define MAP_88		2
#define MAP_ESC		3
#define MAP_17		4
#define MAP_97		5
#define MAP_SUBS	6
#define MAP_DESC	7
#define MAP_SHIFT	8
#define MAP_CMAP	9

/* Structure access macros. */
#define theICharWMode( _v ) ((_v)->wmode)

#define theMetrics2( _v )    ((_v).themetrics2)
#define theWMode( _v )       ((_v).wmode)
#define theWModeNeeded( _v ) ((_v).wmodeneeded)
#define theFMapType( _v )    ((_v).fmaptype)
#define theEscChar( _v )     ((_v).umapthings.mapthings.escchar)
#define theShiftIn( _v )     ((_v).umapthings.mapthings.shiftin)
#define theShiftOut( _v )    ((_v).umapthings.mapthings.shiftout)
#define theSubsBytes( _v )   ((_v).subsbytes)
#define theSubsCount( _v )   ((_v).subscount)
#define theSubsVector( _v )  ((_v).umapthings.subsvector)
#define theFDepVector( _v )  ((_v).fdepvector)
#define thePrefEnc( _v )     ((_v).prefenc)
#define theCDevProc( _v )    ((_v).cdevproc)
#define theCMap( _v )	     ((_v).umapthings.cmap)

extern int32       fid_count;


Bool init_charpath(int32 stroke);
Bool end_charpath(int32 result);

#endif /* protection for multiple inclusion */


/* Log stripped */
