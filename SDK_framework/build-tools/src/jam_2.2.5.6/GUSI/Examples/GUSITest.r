/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSITest			-	Testing gear
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSITest.r,v $
Revision 1.2  1999/04/23  08:56:57  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  01:16:51  neeri
Add GU·I resource.

Revision 1.1  1994/02/25  02:48:17  neeri
Initial revision

Revision 0.2  1993/03/03  00:00:00  neeri
define GUSI_PREF_VERSION

Revision 0.1  1992/07/13  00:00:00  neeri
Include GUSI.r

*********************************************************************/

#define __kPrefSize	512
#define __kMinSize	512

#define GUSI_PREF_VERSION '0150'

#include "SIOW.r"
#include "GUSI.r"

include "GUSITest.rsrc";

resource 'GU·I' (GUSIRsrcID) {
	text, mpw, noAutoSpin, useChdir, approxStat, 
	noTCPDaemon, noUDPDaemon, 
	noConsole,
	{};
};
