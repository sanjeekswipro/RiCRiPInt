/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFinger		-	Finger daemon
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIFinger.r,v $
Revision 1.2  1999/04/23  08:57:02  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:47:15  neeri
Initial revision

*********************************************************************/

#define __kPrefSize	512
#define __kMinSize	512

#define GUSI_PREF_VERSION '0110'

#include "GUSI.r"

resource 'GU·I' (GUSIRsrcID) {
	text, mpw, noAutoSpin, useChdir, approxStat, isTCPDaemon, noUDPDaemon
};

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,				/* we can background; we don't currently, but our sleep value */
								/* guarantees we don't hog the Mac while we are in the background */
	notMultiFinderAware,			/* this says we do our own activate/deactivate; don't fake us out */
	onlyBackground,	/* this is definitely not a background-only application! */
	dontGetFrontClicks,			/* change this is if you want "do first click" behavior like the Finder */
	ignoreChildDiedEvents,		/* essentially, I'm not a debugger (sub-launching) */
	not32BitCompatible,			/* this app should not be run in 32-bit address space */
	isHighLevelEventAware,
	localAndRemoteHLEvents,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	__kPrefSize * 1024,
	__kMinSize * 1024	
};
