/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSISIOW.cp		-	Update & activate SIOW window
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSISIOW.cp,v $
Revision 1.2  1999/04/23  08:43:55  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:30:26  neeri
Initial revision

Revision 0.2  1992/12/18  00:00:00  neeri
_DoActivate incorrectly depends on current port

Revision 0.1  1992/04/27  00:00:00  neeri
C++ version

*********************************************************************/

#include "GUSI_P.h"

#include <Events.h>
#include <Windows.h>

static void GUSISIOWActivate(EventRecord * ev);
static void GUSISIOWUpdate(EventRecord * ev);
static void GUSISIOWSusRes(EventRecord * ev);

GUSIEvtHandler	GUSISIOWEvents[]	=	{
	nil,
	
	nil,
	nil,
	nil,
	nil,
	
	nil,
	GUSISIOWUpdate,
	nil,
	GUSISIOWActivate,
	
	nil,
	nil,
	nil,
	nil,
	
	nil,
	nil,
	GUSISIOWSusRes,
	nil,
	
	nil,
	nil,
	nil,
	nil,
	
	nil,
	nil,
	nil,
};

extern "C" void _DoActivate(WindowPtr win, int activate);
extern "C" void _DoUpdate(WindowPtr win);

static void GUSISIOWActivate(EventRecord * ev)
{
	GrafPtr port;
	
	GetPort(&port);
	
	if ((WindowPtr) ev->message)
		SetPort((WindowPtr) ev->message);
		
	_DoActivate((WindowPtr) ev->message, ev->modifiers & activeFlag);
	
	SetPort(port);
}

static void GUSISIOWUpdate(EventRecord * ev)
{
	_DoUpdate((WindowPtr) ev->message);
}

static void GUSISIOWSusRes(EventRecord * ev)
{	GrafPtr port;
	
	GetPort(&port);
	SetPort(FrontWindow());
		
	_DoActivate(FrontWindow(), (int) ev->message & 1);
	
	SetPort(port);
}


