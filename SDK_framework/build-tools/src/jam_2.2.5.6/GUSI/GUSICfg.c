/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSICfg.c		-	List of socket types we want
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSICfg.c,v $
Revision 1.2  1999/04/23  08:56:49  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:38:48  neeri
Initial revision

*********************************************************************/

void GUSIwithUnixSockets();
void GUSIwithAppleTalkSockets();
void GUSIwithInternetSockets();
void GUSIwithPPCSockets();
void GUSIwithPAPSockets();

#ifdef GUSI_Everything
#define GUSI_Appletalk
#define GUSI_Internet
#define GUSI_PPC
#define GUSI_PAP
#define GUSI_Unix
#endif

#pragma force_active on

typedef void (*CfgProc)();

static void cfg() {
#ifdef GUSI_Appletalk
	GUSIwithAppleTalkSockets();
#endif
#ifdef GUSI_Internet
	GUSIwithInternetSockets();
#endif
#ifdef GUSI_PPC
	GUSIwithPPCSockets();
#endif
#ifdef GUSI_PAP
	GUSIwithPAPSockets();
#endif
#ifdef GUSI_Unix
	GUSIwithUnixSockets();
#endif
}
