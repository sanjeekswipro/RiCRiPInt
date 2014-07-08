/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIINETd.cp	-	Support for internet daemon
Author	:	Matthias Neeracher

	This file was derived from the socket library by 
	
		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>
		  
Language	:	MPW C/C++

$Log: GUSIINETd.cp,v $
Revision 1.2  1999/04/23  08:44:11  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/12/30  20:07:17  neeri
Initial revision

*********************************************************************/

#include "GUSIINET_P.h"
#include "GUSIFile_P.h"

#include <TextUtils.h>

#if !defined(powerc) && !defined(__powerc)
#pragma force_active on
#pragma segment GUSIINET
#endif


/********************* INETdSocketDomain members *********************/

class INETdSocketDomain : public FileSocketDomain {
	static Socket *		inetd;
	static pascal OSErr 	TCPPossession(AppleEvent*, AppleEvent*, long);
	static pascal OSErr 	UDPPossession(AppleEvent*, AppleEvent*, long);
	static pascal OSErr 	GoneFishing(AppleEvent*, AppleEvent*, long);
public:
	INETdSocketDomain()	:	FileSocketDomain(AF_UNSPEC, true, false)	{	}
	
	virtual Boolean Yours(const GUSIFileRef & ref, Request request);
	virtual Socket * open(const GUSIFileRef & ref, int oflag);
};

pascal OSErr 
INETdSocketDomain::TCPPossession(AppleEvent* messagein, AppleEvent* /*reply*/, long /*refIn*/)
{
	AEDesc		streamDesc;
	OSErr			theErr;

	if ((theErr = AEGetParamDesc(messagein, 'STRM', typeLongInteger, &streamDesc)) == noErr) {
		HLock(streamDesc.dataHandle);
		inetd = new TCPSocket(*((StreamPtr*) *(streamDesc.dataHandle)));
		HUnlock(streamDesc.dataHandle);
		
		AEDisposeDesc(&streamDesc);
	}

	return theErr;
}

pascal OSErr 
INETdSocketDomain::UDPPossession(AppleEvent* messagein, AppleEvent* /*reply*/, long /*refIn*/)
{
	AEDesc		streamDesc;
	OSErr			theErr;

	if ((theErr = AEGetParamDesc(messagein, 'STRM', typeLongInteger, &streamDesc)) == noErr) {
		HLock(streamDesc.dataHandle);
		inetd = new UDPSocket(*((StreamPtr*) *(streamDesc.dataHandle)));
		HUnlock(streamDesc.dataHandle);
		
		AEDisposeDesc(&streamDesc);
	}

	return theErr;
}

pascal OSErr 
INETdSocketDomain::GoneFishing(AppleEvent* messagein, AppleEvent* /*reply*/, long /*refIn*/)
{
	inetd = (Socket *) -1;
	
	return noErr;
}

INETdSocketDomain	INETdSockets;

Socket * INETdSocketDomain::inetd	=	(Socket *) nil;

Boolean INETdSocketDomain::Yours(const GUSIFileRef & ref, FileSocketDomain::Request request)
{
	return !ref.spec && (request == willOpen) && 
		equalstring((char *) ref.name, "dev:inetd", false, true);
}

Socket * INETdSocketDomain::open(const GUSIFileRef & ref, int flags)
{
	if (inetd)
		return inetd;
		
#if USESROUTINEDESCRIPTORS
	static RoutineDescriptor uTCPPossession = 
		BUILD_ROUTINE_DESCRIPTOR(uppAEEventHandlerProcInfo, TCPPossession);
	static RoutineDescriptor uUDPPossession = 
		BUILD_ROUTINE_DESCRIPTOR(uppAEEventHandlerProcInfo, UDPPossession);
	static RoutineDescriptor uGoneFishing = 
		BUILD_ROUTINE_DESCRIPTOR(uppAEEventHandlerProcInfo, GoneFishing);
#else
#define uTCPPossession 	TCPPossession
#define uUDPPossession 	UDPPossession
#define uGoneFishing 	GoneFishing
#endif
		
	AEInstallEventHandler('INET', 'TSTR', AEEventHandlerUPP(&uTCPPossession), 0, false);
	AEInstallEventHandler('INET', 'USTR', AEEventHandlerUPP(&uUDPPossession), 0, false);

	AEEventHandlerUPP	quithandler;
	long					quitrefcon;
	Boolean				hadquithandler;
	
	hadquithandler = 
		!AEGetEventHandler('aevt', 'quit', &quithandler, &quitrefcon, false);
	AEInstallEventHandler('aevt', 'quit', AEEventHandlerUPP(&uGoneFishing), 0, false);
		
	SPINP(!inetd, SP_MISC, 0);
	
	if (hadquithandler)
		AEInstallEventHandler('aevt', 'quit', quithandler, quitrefcon, false);
	
	if (inetd != (Socket *) -1)
		return inetd;
		
lose:
	exit(1);
}
